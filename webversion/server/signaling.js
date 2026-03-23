/**
 * Teleport WebRTC Signaling Server
 * Handles peer discovery and WebRTC signaling for the web app
 */

const WebSocket = require('ws');
const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = process.env.PORT || 3000;

// ============================================================================
// SECURITY CONSTANTS
// ============================================================================
const MAX_CONNECTIONS_PER_MIN = 10;    // Per-IP WS connections per minute
const MAX_WS_MESSAGE_SIZE = 1024 * 1024; // 1 MB hard limit per WS message
const MAX_RELAY_CHUNK_BYTES = 65536;   // 64 KB max decoded relay chunk
const MAX_FILES_PER_TRANSFER = 10000;
const MAX_TOTAL_TRANSFER_BYTES = 100 * 1024 * 1024 * 1024; // 100 GB

// ============================================================================
// RATE LIMITING
// ============================================================================
const connectionRates = new Map(); // IP -> { count, windowStart }

function checkRateLimit(ip) {
    const now = Date.now();
    const record = connectionRates.get(ip);
    if (!record || now - record.windowStart > 60000) {
        connectionRates.set(ip, { count: 1, windowStart: now });
        return true;
    }
    if (record.count >= MAX_CONNECTIONS_PER_MIN) return false;
    record.count++;
    return true;
}

// Clean up stale rate-limit entries every 5 minutes
setInterval(() => {
    const cutoff = Date.now() - 60000;
    for (const [ip, record] of connectionRates) {
        if (record.windowStart < cutoff) connectionRates.delete(ip);
    }
}, 300000);

// ============================================================================
// VALIDATION HELPERS
// ============================================================================

/** Returns true when str looks like valid base64 (RFC 4648, padded). */
function isValidBase64(str) {
    if (typeof str !== 'string' || str.length === 0 || str.length % 4 !== 0) return false;
    return /^[A-Za-z0-9+/]*={0,2}$/.test(str);
}

/** Returns the decoded byte-length of a base64 string WITHOUT decoding it. */
function base64DecodedLength(str) {
    if (!str) return 0;
    let padding = 0;
    if (str.endsWith('==')) padding = 2;
    else if (str.endsWith('=')) padding = 1;
    return (str.length * 3) / 4 - padding;
}

/** Returns true when str is a canonical 64-char hex SHA-256 digest. */
function isValidSha256Hex(str) {
    return typeof str === 'string' && /^[a-fA-F0-9]{64}$/.test(str);
}

// ============================================================================
// TURN CREDENTIAL CACHE
// To rotate TURN credentials: set TURN_USERNAME and TURN_CREDENTIAL env vars.
// Falls back to the public openrelay project which never expires.
// ============================================================================
let cachedTurnCredentials = null;

function getTurnCredentials() {
    if (cachedTurnCredentials) return cachedTurnCredentials;
    cachedTurnCredentials = {
        // Public open-relay (never expires, free tier, ~1 Mbps limit)
        iceServers: [
            { urls: 'stun:stun.l.google.com:19302' },
            { urls: 'stun:stun1.l.google.com:19302' },
            { urls: 'stun:stun2.l.google.com:19302' },
            { urls: 'stun:global.stun.twilio.com:3478' },
            {
                urls: [
                    'turn:openrelay.metered.ca:80',
                    'turn:openrelay.metered.ca:80?transport=tcp',
                    'turn:openrelay.metered.ca:443?transport=tcp'
                ],
                username: 'openrelayproject',
                credential: 'openrelayproject'
            },
            // Override with fresh metered.ca credentials via environment variables
            ...(process.env.TURN_USERNAME && process.env.TURN_CREDENTIAL ? [{
                urls: [
                    'turn:a.relay.metered.ca:80',
                    'turn:a.relay.metered.ca:80?transport=tcp',
                    'turn:a.relay.metered.ca:443',
                    'turn:a.relay.metered.ca:443?transport=tcp'
                ],
                username: process.env.TURN_USERNAME,
                credential: process.env.TURN_CREDENTIAL
            }] : [])
        ],
        ttl: 86400
    };
    return cachedTurnCredentials;
}

// Create HTTP server with CORS and health check
const server = http.createServer((req, res) => {
    // CORS headers for all requests
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

    // Handle preflight
    if (req.method === 'OPTIONS') {
        res.writeHead(204);
        return res.end();
    }

    // Health check endpoint for Render
    if (req.url === '/health' || req.url === '/') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        return res.end(JSON.stringify({
            status: 'ok',
            service: 'teleport-signaling',
            peers: peers.size,
            rooms: rooms.size,
            now: Date.now(),
            uptimeSec: Math.floor(process.uptime())
        }));
    }

    // TURN credential endpoint — clients fetch fresh ICE server config here
    if (req.url === '/turn-credentials' && req.method === 'GET') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        return res.end(JSON.stringify(getTurnCredentials()));
    }

    // 404 for other routes
    res.writeHead(404, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ error: 'Not Found' }));
});

// WebSocket server for signaling
const wss = new WebSocket.Server({ server });

// Store connected peers
const peers = new Map(); // peerId -> { ws, name, room }
const rooms = new Map(); // roomName -> Set of peerIds

// Generate unique peer ID
function generatePeerId() {
    return 'peer_' + Math.random().toString(36).substr(2, 9);
}

// Broadcast to all peers in a room except sender
function broadcastToRoom(room, message, excludePeerId = null) {
    const roomPeers = rooms.get(room);
    if (!roomPeers) return;

    const messageStr = JSON.stringify(message);
    roomPeers.forEach(peerId => {
        if (peerId !== excludePeerId) {
            const peer = peers.get(peerId);
            if (peer && peer.ws.readyState === WebSocket.OPEN) {
                peer.ws.send(messageStr);
            }
        }
    });
}

// Send to specific peer
function sendToPeer(peerId, message) {
    const peer = peers.get(peerId);
    if (peer && peer.ws.readyState === WebSocket.OPEN) {
        peer.ws.send(JSON.stringify(message));
    }
}

// Get peer list for a room (includes fingerprint and publicKey for E2E encryption)
function getPeerList(room, excludePeerId = null) {
    const roomPeers = rooms.get(room);
    if (!roomPeers) return [];

    return Array.from(roomPeers)
        .filter(id => id !== excludePeerId)
        .map(id => {
            const peer = peers.get(id);
            if (!peer) return null;
            return {
                id,
                name: peer.name,
                fingerprint: peer.fingerprint || null,
                publicKey: peer.publicKey || null,
                isLan: peer.isLan || false
            };
        })
        .filter(Boolean);
}

wss.on('connection', (ws, req) => {
    // Extract client IP (respects X-Forwarded-For from Render/proxy)
    const clientIp =
        (req.headers['x-forwarded-for'] || '').split(',')[0].trim() ||
        req.socket.remoteAddress ||
        'unknown';

    // Rate limiting — hard-reject if IP exceeds 10 new connections/min
    if (!checkRateLimit(clientIp)) {
        console.warn(`[RateLimit] Rejected connection from ${clientIp}`);
        ws.close(1008, 'Rate limit exceeded');
        return;
    }

    const peerId = generatePeerId();
    console.log(`[${peerId}] Connected from ${clientIp}`);

    // Send peer their ID
    ws.send(JSON.stringify({ type: 'welcome', peerId }));

    ws.on('message', (data) => {
        // Enforce hard per-message size limit before parsing
        if (data.length > MAX_WS_MESSAGE_SIZE) {
            console.warn(`[${peerId}] Message too large (${data.length} bytes), dropping`);
            return;
        }

        let message;
        try {
            message = JSON.parse(data);
        } catch (e) {
            console.error(`[${peerId}] Invalid JSON, dropping message`);
            return;
        }

        switch (message.type) {
            case 'join': {
                // Join a room
                const { room, name, fingerprint, publicKey } = message;
                const roomName = room || 'teleport-default';

                // Store peer info including crypto identity
                peers.set(peerId, {
                    ws,
                    name: name || 'Unknown Device',
                    room: roomName,
                    fingerprint: typeof fingerprint === 'string' ? fingerprint.substring(0, 64) : null,
                    publicKey: typeof publicKey === 'string' ? publicKey.substring(0, 512) : null,
                    ip: clientIp,
                    isLan: false
                });

                // Add to room
                if (!rooms.has(roomName)) {
                    rooms.set(roomName, new Set());
                }
                rooms.get(roomName).add(peerId);

                console.log(`[${peerId}] Joined room "${roomName}" as "${name}"`);

                // Send current peer list (includes fingerprint + publicKey)
                ws.send(JSON.stringify({
                    type: 'peers',
                    peers: getPeerList(roomName, peerId)
                }));

                // Notify others in room (include crypto identity so receivers can set up E2E immediately)
                broadcastToRoom(roomName, {
                    type: 'peer-joined',
                    peer: {
                        id: peerId,
                        name: name || 'Unknown Device',
                        fingerprint: typeof fingerprint === 'string' ? fingerprint.substring(0, 64) : null,
                        publicKey: typeof publicKey === 'string' ? publicKey.substring(0, 512) : null
                    }
                }, peerId);
                break;
            }

            case 'offer':
            case 'answer':
            case 'ice': {
                // Relay signaling messages to target peer
                // We include sender's fingerprint and publicKey so the receiver can
                // establish E2E encryption without an extra round-trip.
                const { to } = message;
                if (to && peers.has(to)) {
                    const senderPeer = peers.get(peerId);
                    sendToPeer(to, {
                        type: message.type,
                        from: peerId,
                        sdp: message.sdp,
                        candidate: message.candidate,
                        fingerprint: message.fingerprint || senderPeer?.fingerprint || null,
                        publicKey: message.publicKey || senderPeer?.publicKey || null
                    });
                }
                break;
            }

            case 'file-request': {
                // Relay file transfer request — validate file count and total size
                const { to, files } = message;
                console.log(`[${peerId}] File request to ${to}, files:`, files?.length);

                if (!to || !peers.has(to)) {
                    console.warn(`[${peerId}] file-request: target peer ${to} not found`);
                    break;
                }

                if (!Array.isArray(files) || files.length === 0) {
                    console.warn(`[${peerId}] file-request: no files`);
                    break;
                }

                // Security: enforce global limits
                if (files.length > MAX_FILES_PER_TRANSFER) {
                    console.warn(`[${peerId}] file-request: exceeds max file count (${files.length})`);
                    break;
                }

                const totalBytes = files.reduce((sum, f) => sum + (typeof f.size === 'number' ? f.size : 0), 0);
                if (totalBytes > MAX_TOTAL_TRANSFER_BYTES) {
                    console.warn(`[${peerId}] file-request: exceeds max total size (${totalBytes})`);
                    break;
                }

                const senderPeer = peers.get(peerId);
                console.log(`[${peerId}] Relaying file request to ${to}`);
                sendToPeer(to, {
                    type: 'file-request',
                    from: peerId,
                    fromName: senderPeer?.name || 'Unknown',
                    fingerprint: senderPeer?.fingerprint || null,
                    files
                });
                break;
            }

            case 'file-response': {
                // Relay accept/reject
                const { to, accepted } = message;
                if (to && peers.has(to)) {
                    sendToPeer(to, {
                        type: 'file-response',
                        from: peerId,
                        accepted
                    });
                }
                break;
            }

            // ============ SERVER RELAY MODE ============
            // Used when WebRTC P2P fails due to NAT/firewall

            case 'relay-start': {
                // Start a relay transfer
                const { to, transferId, filename, size, mimeType, fileIndex, totalFiles, sha256 } = message;
                const normalizedSha = isValidSha256Hex(sha256) ? sha256.toLowerCase() : null;
                console.log(`[${peerId}] Starting relay transfer to ${to}: ${filename}`);
                if (to && peers.has(to)) {
                    sendToPeer(to, {
                        type: 'relay-start',
                        from: peerId,
                        transferId,
                        filename,
                        size,
                        mimeType,
                        sha256: normalizedSha,
                        fileIndex,
                        totalFiles
                    });
                }
                break;
            }

            case 'relay-chunk': {
                // Relay a file chunk (base64 encoded for JSON)
                const { to, transferId, data, offset } = message;

                // Validate chunk before forwarding
                if (!to || !peers.has(to)) break;
                if (typeof offset !== 'number' || offset < 0 || !Number.isFinite(offset)) {
                    console.warn(`[${peerId}] relay-chunk: invalid offset (${offset})`);
                    break;
                }
                if (!isValidBase64(data)) {
                    console.warn(`[${peerId}] relay-chunk: payload is not valid base64`);
                    break;
                }
                const decodedLen = base64DecodedLength(data);
                if (decodedLen > MAX_RELAY_CHUNK_BYTES) {
                    console.warn(`[${peerId}] relay-chunk: decoded size ${decodedLen} exceeds limit ${MAX_RELAY_CHUNK_BYTES}`);
                    break;
                }

                sendToPeer(to, {
                    type: 'relay-chunk',
                    from: peerId,
                    transferId,
                    data,
                    offset
                });
                break;
            }

            case 'relay-end': {
                // End relay transfer
                const { to, transferId } = message;
                console.log(`[${peerId}] Ending relay transfer to ${to}`);
                if (to && peers.has(to)) {
                    sendToPeer(to, {
                        type: 'relay-end',
                        from: peerId,
                        transferId
                    });
                }
                break;
            }

            case 'relay-cancel': {
                // Cancel relay transfer
                const { to, transferId, reason } = message;
                if (to && peers.has(to)) {
                    sendToPeer(to, {
                        type: 'relay-cancel',
                        from: peerId,
                        transferId,
                        reason: typeof reason === 'string' ? reason : null
                    });
                }
                break;
            }

            case 'relay-verified': {
                // Relay receiver integrity acknowledgement back to sender
                const { to, transferId, ok, reason, sha256 } = message;
                const normalizedSha = isValidSha256Hex(sha256) ? sha256.toLowerCase() : null;

                if (to && peers.has(to)) {
                    sendToPeer(to, {
                        type: 'relay-verified',
                        from: peerId,
                        transferId,
                        ok: !!ok,
                        reason: typeof reason === 'string' ? reason : '',
                        sha256: normalizedSha
                    });
                }
                break;
            }

            // lan-announce: desktop peers advertise their local TCP endpoint
            // so web clients can distinguish LAN vs. WAN peers.
            case 'lan-announce': {
                const peer = peers.get(peerId);
                if (!peer) break;

                const lanIp = typeof message.ip === 'string' ? message.ip : null;
                const lanPort = typeof message.port === 'number' ? message.port : null;

                if (lanIp && lanPort) {
                    peer.lanIp = lanIp;
                    peer.lanPort = lanPort;
                    peer.isLan = true;
                    console.log(`[${peerId}] LAN announce: ${lanIp}:${lanPort}`);

                    // Notify room peers that this peer has a LAN address
                    broadcastToRoom(peer.room, {
                        type: 'peer-lan-updated',
                        peerId,
                        lanIp,
                        lanPort
                    }, peerId);
                }
                break;
            }

            default:
                console.log(`[${peerId}] Unknown message type:`, message.type);
        }
    });

    ws.on('close', () => {
        const peer = peers.get(peerId);
        if (peer) {
            const room = peer.room;

            // Remove from room
            if (rooms.has(room)) {
                rooms.get(room).delete(peerId);
                if (rooms.get(room).size === 0) {
                    rooms.delete(room);
                }
            }

            // Notify others
            broadcastToRoom(room, {
                type: 'peer-left',
                peerId
            });

            // Remove peer
            peers.delete(peerId);
            console.log(`[${peerId}] Disconnected`);
        }
    });

    ws.on('error', (error) => {
        console.error(`[${peerId}] WebSocket error:`, error.message);
    });
});

server.listen(PORT, () => {
    console.log(`
╔════════════════════════════════════════════════╗
║     Teleport Signaling Server Running          ║
╠════════════════════════════════════════════════╣
║  HTTP:      http://localhost:${PORT}              ║
║  WebSocket: ws://localhost:${PORT}                ║
╚════════════════════════════════════════════════╝
    `);
});
