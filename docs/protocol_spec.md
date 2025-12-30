# Teleport Protocol Specification

**Version**: 1.0  
**Protocol Version**: 1

## Overview

Teleport uses a dual-channel architecture:
1. **Control Channel**: TCP connection for metadata, commands, and coordination
2. **Data Channel**: TCP connection(s) for actual file data transfer

## Discovery Protocol

### UDP Broadcast (Port 45454)

Devices announce their presence by broadcasting JSON packets every 1 second.

**Packet Format**:
```json
{
  "v": 1,
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "name": "Desktop-PC",
  "os": "Windows",
  "ip": "192.168.1.100",
  "port": 45455,
  "caps": ["parallel", "resume"]
}
```

| Field | Type | Description |
|-------|------|-------------|
| `v` | int | Protocol version (must be 1) |
| `id` | string | UUID v4, random per app launch (stable per session) |
| `name` | string | User-friendly device name (max 64 chars) |
| `os` | string | Operating system: "Windows", "macOS", "Linux", "Android" |
| `ip` | string | Sender's local IP address |
| `port` | int | Control channel listening port |
| `caps` | array | Capability strings (see below) |

**Capabilities**:
- `parallel`: Supports multiple parallel TCP streams
- `resume`: Supports transfer resume
- `compress`: Supports data compression (future)
- `encrypt`: Supports encryption (future)

**Behavior**:
- Broadcast to subnet broadcast address (e.g., 192.168.1.255)
- Receive timeout: 500ms
- Device TTL: 5 seconds (device expired if no packets received)
- Self-discovery filtering: Ignore packets with our own `id`

## Control Channel Protocol

TCP connection on the sender's advertised `port`. Uses length-prefixed JSON messages.

### Wire Format

```
+------------------+-------------------+
| Length (4 bytes) | JSON Payload      |
| Big-endian       | (UTF-8)           |
+------------------+-------------------+
```

### Message Envelope

```json
{
  "type": "MESSAGE_TYPE",
  "payload": { ... }
}
```

### Message Types

#### HANDSHAKE (Sender → Receiver)

Initiates connection with protocol version and device info.

```json
{
  "type": "HANDSHAKE",
  "payload": {
    "protocol_version": 1,
    "device": {
      "name": "Sender-PC",
      "os": "Windows"
    },
    "session_token": ""
  }
}
```

#### HANDSHAKE_ACK (Receiver → Sender)

Acknowledges handshake with session token.

```json
{
  "type": "HANDSHAKE_ACK",
  "payload": {
    "protocol_version": 1,
    "device": {
      "name": "Receiver-PC",
      "os": "Windows"
    },
    "session_token": "a1b2c3d4e5f6..."
  }
}
```

#### FILE_LIST (Sender → Receiver)

Announces files to be transferred.

```json
{
  "type": "FILE_LIST",
  "payload": {
    "files": [
      {"id": 1, "name": "movie.mp4", "size": 1200000000},
      {"id": 2, "name": "document.pdf", "size": 500000}
    ],
    "total_size": 1200500000
  }
}
```

#### ACCEPT (Receiver → Sender)

Accepts the transfer.

```json
{
  "type": "ACCEPT",
  "payload": {
    "accepted": true,
    "reason": "",
    "data_port": 45456
  }
}
```

#### REJECT (Receiver → Sender)

Rejects the transfer.

```json
{
  "type": "REJECT",
  "payload": {
    "accepted": false,
    "reason": "User declined",
    "data_port": 0
  }
}
```

#### START (Sender → Receiver)

Signals data transfer is beginning.

```json
{
  "type": "START",
  "payload": {}
}
```

#### PROGRESS (Bidirectional)

Progress update.

```json
{
  "type": "PROGRESS",
  "payload": {
    "file_id": 1,
    "bytes_transferred": 600000000,
    "bytes_total": 1200000000,
    "speed_bps": 50000000
  }
}
```

#### PAUSE / RESUME / CANCEL (Bidirectional)

Transfer control commands.

```json
{
  "type": "PAUSE",
  "payload": {
    "action": "pause",
    "file_id": 0
  }
}
```

#### RESUME_REQUEST (Receiver → Sender)

Request retransmission of missing chunks after resume.

```json
{
  "type": "RESUME_REQUEST",
  "payload": {
    "file_id": 1,
    "received_chunks": [0, 1, 2, 5, 6, 7],
    "received_bytes": 14680064
  }
}
```

#### COMPLETE (Sender → Receiver)

Transfer completed.

```json
{
  "type": "COMPLETE",
  "payload": {
    "success": true,
    "message": "Transfer complete",
    "files_transferred": 2,
    "bytes_transferred": 1200500000
  }
}
```

#### ERROR (Bidirectional)

Error occurred.

```json
{
  "type": "ERROR",
  "payload": {
    "code": -8,
    "message": "Failed to open file",
    "fatal": true
  }
}
```

## Data Transfer Protocol

Binary protocol for efficient file data transfer.

### Chunk Format

```
+----------+----------+----------+----------+----------------+
| file_id  | chunk_id | offset   | size     | data           |
| 4 bytes  | 4 bytes  | 4 bytes  | 4 bytes  | N bytes        |
| BE       | BE       | BE       | BE       | raw            |
+----------+----------+----------+----------+----------------+
```

**Header** (16 bytes):
| Field | Bytes | Description |
|-------|-------|-------------|
| file_id | 4 | File identifier from FILE_LIST |
| chunk_id | 4 | Sequential chunk number (0-indexed) |
| offset | 4 | Byte offset within file (for small files) |
| size | 4 | Data size in this chunk |

All integers are **big-endian**.

**Behavior**:
- Default chunk size: 2 MB
- Chunks are sent sequentially for each file
- Receiver reassembles by writing to offset position

### Parallel Streams (Optional)

When both devices support `parallel` capability:
- Establish 4 TCP connections to data port
- Each stream handles non-overlapping chunk ranges
- File is split: Stream 0 gets chunks 0,4,8..., Stream 1 gets 1,5,9..., etc.

### Resume Support

When both devices support `resume` capability:
1. Receiver detects partial file from previous transfer
2. Receiver builds bitmap of received chunks
3. Receiver sends RESUME_REQUEST with received chunk IDs
4. Sender only sends missing chunks

## Connection Flow

```
Sender                              Receiver
  |                                    |
  |-------- Discovery Broadcast ------>|
  |<------- Discovery Response --------|
  |                                    |
  |======= TCP Connection ============>|
  |                                    |
  |-------- HANDSHAKE ---------------->|
  |<------- HANDSHAKE_ACK -------------|
  |                                    |
  |-------- FILE_LIST ---------------->|
  |<------- ACCEPT/REJECT -------------|
  |                                    |
  |-------- START -------------------->|
  |                                    |
  |-------- [chunk data] ------------->|
  |-------- [chunk data] ------------->|
  |        ... (multiple chunks) ...   |
  |                                    |
  |-------- COMPLETE ----------------->|
  |                                    |
  |======= Connection Close ==========>|
```

## Error Codes

| Code | Name | Description |
|------|------|-------------|
| 0 | OK | Success |
| -1 | INVALID_ARGUMENT | Invalid parameter |
| -2 | OUT_OF_MEMORY | Memory allocation failed |
| -3 | SOCKET_CREATE | Failed to create socket |
| -4 | SOCKET_BIND | Failed to bind socket |
| -5 | SOCKET_CONNECT | Failed to connect |
| -6 | SOCKET_SEND | Failed to send data |
| -7 | SOCKET_RECV | Failed to receive data |
| -8 | FILE_OPEN | Failed to open file |
| -9 | FILE_READ | Failed to read file |
| -10 | FILE_WRITE | Failed to write file |
| -11 | PROTOCOL | Protocol error |
| -12 | TIMEOUT | Operation timed out |
| -13 | CANCELLED | Operation cancelled |
| -14 | REJECTED | Transfer rejected |
| -15 | ALREADY_RUNNING | Operation already in progress |
| -16 | NOT_RUNNING | Operation not running |

## Security Considerations

**Phase 1 (Current)**:
- ⚠️ Unencrypted transfers
- Session tokens for connection validation
- Random ephemeral ports
- User must explicitly accept transfers

**Phase 4 (Future)**:
- AES-GCM encryption
- QR code or NFC key exchange
- TLS-like handshake
