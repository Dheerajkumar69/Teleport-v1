/**
 * Teleport WebRTC Signaling Server
 * Handles peer discovery and WebRTC signaling for the web app
 */

const WebSocket = require('ws');
const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = process.env.PORT || 3000;

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
            rooms: rooms.size
        }));
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

// Get peer list for a room
function getPeerList(room, excludePeerId = null) {
    const roomPeers = rooms.get(room);
    if (!roomPeers) return [];

    return Array.from(roomPeers)
        .filter(id => id !== excludePeerId)
        .map(id => {
            const peer = peers.get(id);
            return peer ? { id, name: peer.name } : null;
        })
        .filter(Boolean);
}

wss.on('connection', (ws) => {
    const peerId = generatePeerId();
    console.log(`[${peerId}] Connected`);

    // Send peer their ID
    ws.send(JSON.stringify({ type: 'welcome', peerId }));

    ws.on('message', (data) => {
        let message;
        try {
            message = JSON.parse(data);
        } catch (e) {
            console.error('Invalid JSON:', data);
            return;
        }

        switch (message.type) {
            case 'join': {
                // Join a room
                const { room, name } = message;
                const roomName = room || 'teleport-default';

                // Store peer info
                peers.set(peerId, { ws, name: name || 'Unknown Device', room: roomName });

                // Add to room
                if (!rooms.has(roomName)) {
                    rooms.set(roomName, new Set());
                }
                rooms.get(roomName).add(peerId);

                console.log(`[${peerId}] Joined room "${roomName}" as "${name}"`);

                // Send current peer list
                ws.send(JSON.stringify({
                    type: 'peers',
                    peers: getPeerList(roomName, peerId)
                }));

                // Notify others in room
                broadcastToRoom(roomName, {
                    type: 'peer-joined',
                    peer: { id: peerId, name: name || 'Unknown Device' }
                }, peerId);
                break;
            }

            case 'offer':
            case 'answer':
            case 'ice': {
                // Relay signaling messages to target peer
                const { to } = message;
                if (to && peers.has(to)) {
                    sendToPeer(to, {
                        type: message.type,
                        from: peerId,
                        sdp: message.sdp,
                        candidate: message.candidate
                    });
                }
                break;
            }

            case 'file-request': {
                // Relay file transfer request
                const { to, files } = message;
                if (to && peers.has(to)) {
                    const peer = peers.get(peerId);
                    sendToPeer(to, {
                        type: 'file-request',
                        from: peerId,
                        fromName: peer?.name || 'Unknown',
                        files
                    });
                }
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
