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
// STRUCTURED LOGGER (6C)
// ============================================================================
const LOG_LEVEL = (process.env.LOG_LEVEL || 'info').toLowerCase();
const LEVELS = { error: 0, warn: 1, info: 2, debug: 3 };
const currentLevel = LEVELS[LOG_LEVEL] ?? 2;

function log(level, msg, extra = {}) {
    if ((LEVELS[level] ?? 2) > currentLevel) return;
    const entry = {
        ts: new Date().toISOString(),
        level,
        msg,
        pid: process.pid,
        ...extra,
    };
    const line = JSON.stringify(entry);
    if (level === 'error') process.stderr.write(line + '\n');
    else process.stdout.write(line + '\n');
}

const logger = {
    error: (msg, extra) => log('error', msg, extra),
    warn:  (msg, extra) => log('warn', msg, extra),
    info:  (msg, extra) => log('info', msg, extra),
    debug: (msg, extra) => log('debug', msg, extra),
};

// ============================================================================
// SECURITY CONSTANTS
// ============================================================================
const MAX_CONNECTIONS_PER_MIN = parseInt(process.env.MAX_CONNECTIONS_PER_MIN, 10) || 10;    // Per-IP WS connections per minute
const MAX_WS_MESSAGE_SIZE = 1024 * 1024; // 1 MB hard limit per WS message
const MAX_RELAY_CHUNK_BYTES = 65536;   // 64 KB max decoded relay chunk
const MAX_FILES_PER_TRANSFER = 10000;
const MAX_TOTAL_TRANSFER_BYTES = 100 * 1024 * 1024 * 1024; // 100 GB
const MAX_ROOM_NAME_LENGTH = 128;
const MAX_PEER_NAME_LENGTH = 128;
const MAX_MESSAGES_PER_SECOND = 50; // Per-peer message rate limit
const RATE_LIMIT_WINDOW_MS = 1000;
const TURN_RATE_LIMIT_PER_MIN = 30; // Max TURN credential requests per IP per minute

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
// PER-PEER MESSAGE RATE LIMITING
// ============================================================================
const peerMessageRates = new Map(); // peerId -> { count, windowStart }

function checkPeerRateLimit(peerId) {
    const now = Date.now();
    const record = peerMessageRates.get(peerId);
    if (!record || now - record.windowStart > RATE_LIMIT_WINDOW_MS) {
        peerMessageRates.set(peerId, { count: 1, windowStart: now });
        return true;
    }
    if (record.count >= MAX_MESSAGES_PER_SECOND) return false;
    record.count++;
    return true;
}

// ============================================================================
// TURN ENDPOINT RATE LIMITING
// ============================================================================
const turnEndpointRates = new Map(); // IP -> { count, windowStart }

function checkTurnRateLimit(ip) {
    const now = Date.now();
    const record = turnEndpointRates.get(ip);
    if (!record || now - record.windowStart > 60000) {
        turnEndpointRates.set(ip, { count: 1, windowStart: now });
        return true;
    }
    if (record.count >= TURN_RATE_LIMIT_PER_MIN) return false;
    record.count++;
    return true;
}

// Clean up stale TURN rate-limit entries every 5 minutes
setInterval(() => {
    const cutoff = Date.now() - 120000;
    for (const [ip, record] of turnEndpointRates) {
        if (record.windowStart < cutoff) turnEndpointRates.delete(ip);
    }
}, 300000);

// ============================================================================
// SAFE SEND — wraps ws.send() in try/catch to prevent crash on dead peers
// ============================================================================
function safeSend(ws, message) {
    try {
        if (ws.readyState === WebSocket.OPEN) {
            ws.send(typeof message === 'string' ? message : JSON.stringify(message));
        }
    } catch (e) {
        logger.error('send failed', { error: e.message });
    }
}

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
// 6A: MESSAGE VALIDATION HELPERS
// ============================================================================

/** Validate an SDP object (offer or answer). Must have type and sdp strings. */
function isValidSdp(sdp) {
    if (!sdp || typeof sdp !== 'object') return false;
    if (typeof sdp.type !== 'string' || !['offer', 'answer', 'pranswer', 'rollback'].includes(sdp.type)) return false;
    if (typeof sdp.sdp !== 'string' || sdp.sdp.length === 0 || sdp.sdp.length > 65536) return false;
    return true;
}

/** Validate an RTCIceCandidate object. */
function isValidIceCandidate(candidate) {
    if (!candidate || typeof candidate !== 'object') return false;
    if (typeof candidate.candidate !== 'string' || candidate.candidate.length === 0) return false;
    if (candidate.sdpMid !== undefined && candidate.sdpMid !== null && typeof candidate.sdpMid !== 'string') return false;
    if (candidate.sdpMLineIndex !== undefined && candidate.sdpMLineIndex !== null) {
        if (typeof candidate.sdpMLineIndex !== 'number' || candidate.sdpMLineIndex < 0) return false;
    }
    return true;
}

/** Validate an IPv4 or IPv6 address string. */
function isValidIpAddress(ip) {
    if (typeof ip !== 'string' || ip.length === 0 || ip.length > 45) return false;
    // IPv4
    if (/^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}$/.test(ip)) {
        return ip.split('.').every(o => { const n = Number(o); return n >= 0 && n <= 255; });
    }
    // IPv6 (simplified check)
    if (/^[a-fA-F0-9:]+$/.test(ip) && ip.includes(':')) return true;
    return false;
}

/** Validate a port number (1-65535). */
function isValidPort(port) {
    return typeof port === 'number' && port >= 1 && port <= 65535 && Number.isInteger(port);
}

/** Validate relay-start message fields. */
function validateRelayStart(msg) {
    if (typeof msg.filename !== 'string' || msg.filename.length === 0 || msg.filename.length > 1024) return false;
    if (typeof msg.size !== 'number' || msg.size < 0 || !Number.isFinite(msg.size)) return false;
    if (msg.mimeType !== undefined && typeof msg.mimeType !== 'string') return false;
    return true;
}

/** Validate a file-request files array element. */
function validateFileElement(f) {
    if (!f || typeof f !== 'object') return false;
    if (typeof f.name !== 'string' || f.name.length === 0 || f.name.length > 1024) return false;
    if (typeof f.size !== 'number' || f.size < 0 || !Number.isFinite(f.size)) return false;
    return true;
}

// ============================================================================
// TURN CREDENTIAL CACHE  —  Multi-provider cascade
//
// Priority:
//   1. Twilio Network Traversal Service (TWILIO_ACCOUNT_SID + TWILIO_AUTH_TOKEN)
//      → Short-lived token fetched from Twilio REST API, highest reliability.
//   2. Paid metered.ca  (TURN_USERNAME + TURN_CREDENTIAL env vars)
//      → Static credential, good reliability, low cost.
//   3. openrelay.metered.ca (no extra env vars)
//      → Free public TURN, rate-limited, unreliable under load — last resort.
//
// Cache TTL: 24 h.  Twilio tokens expire after the server-configured TTL;
// we re-fetch them 5 min before expiry by trimming the local cache TTL.
// ============================================================================
const TURN_TTL_MS       = 86400 * 1000; // 24 h cache (for static credentials)
const TWILIO_TOKEN_TTL  = 3600;         // seconds — request 1-hour Twilio tokens
const TWILIO_PREFETCH   = 300 * 1000;   // refresh 5 min before expiry

let cachedTurnCredentials    = null;
let cachedTurnCredentialsAt  = 0;
let cachedTurnExpireMs       = TURN_TTL_MS; // effective TTL for current cache entry

// Fetch a Twilio Network Traversal Service token (async).
// Returns null (and logs) on error so callers gracefully fall back.
async function fetchTwilioTurnToken() {
    const sid  = process.env.TWILIO_ACCOUNT_SID;
    const auth = process.env.TWILIO_AUTH_TOKEN;
    if (!sid || !auth) return null;

    try {
        const https = require('https');
        const creds = Buffer.from(`${sid}:${auth}`).toString('base64');
        const body  = `Ttl=${TWILIO_TOKEN_TTL}`;

        return await new Promise((resolve, reject) => {
            const req = https.request({
                hostname: 'api.twilio.com',
                path:     `/2010-04-01/Accounts/${sid}/Tokens.json`,
                method:   'POST',
                headers: {
                    'Authorization': `Basic ${creds}`,
                    'Content-Type':  'application/x-www-form-urlencoded',
                    'Content-Length': Buffer.byteLength(body)
                }
            }, (res) => {
                let data = '';
                res.on('data', d => { data += d; });
                res.on('end', () => {
                    try {
                        const json = JSON.parse(data);
                        if (json.ice_servers) resolve(json.ice_servers);
                        else { logger.error('Twilio response missing ice_servers', { data }); resolve(null); }
                    } catch (e) { reject(e); }
                });
            });
            req.on('error', reject);
            req.write(body);
            req.end();
        });
    } catch (err) {
        logger.error('Twilio token fetch failed', { error: err.message });
        return null;
    }
}

function getTurnCredentials() {
    const now = Date.now();
    if (cachedTurnCredentials && (now - cachedTurnCredentialsAt) < cachedTurnExpireMs) {
        return cachedTurnCredentials;
    }

    // ── Determine which TURN tier is active ──────────────────────────────────
    const hasTwilio  = !!(process.env.TWILIO_ACCOUNT_SID && process.env.TWILIO_AUTH_TOKEN);
    const hasPaidMetered = !!(process.env.TURN_USERNAME && process.env.TURN_CREDENTIAL);

    // Shared STUN servers (always included)
    const stunServers = [
        { urls: 'stun:stun.l.google.com:19302' },
        { urls: 'stun:stun1.l.google.com:19302' },
        { urls: 'stun:global.stun.twilio.com:3478' },
    ];

    // Free openrelay fallback (always appended as last resort)
    const openRelayServer = {
        urls: [
            'turn:openrelay.metered.ca:80',
            'turn:openrelay.metered.ca:80?transport=tcp',
            'turn:openrelay.metered.ca:443?transport=tcp'
        ],
        username: 'openrelayproject',
        credential: 'openrelayproject'
    };

    let turnProvider = 'openrelay'; // tracking label returned to client

    if (hasTwilio) {
        // Trigger async refresh; this call returns stale/openrelay while fetching.
        // On next request (after PREFETCH ms) the fresh Twilio token will be used.
        fetchTwilioTurnToken().then(iceServers => {
            if (iceServers && iceServers.length > 0) {
                cachedTurnCredentials = {
                    iceServers: [...stunServers, ...iceServers],
                    ttl: TWILIO_TOKEN_TTL,
                    provider: 'twilio'
                };
                cachedTurnCredentialsAt = Date.now();
                // Cache for slightly less than the token TTL so we prefetch ahead
                cachedTurnExpireMs = (TWILIO_TOKEN_TTL * 1000) - TWILIO_PREFETCH;
                logger.info('Twilio token cached', { expiresIn: TWILIO_TOKEN_TTL - 300 });
            }
        }).catch(() => {});

        turnProvider = 'twilio-pending';
    } else if (hasPaidMetered) {
        turnProvider = 'metered-paid';
    } else {
        logger.warn('TURN credentials not configured, falling back to openrelay');
    }

    // Build the ICE server list synchronously (Twilio async result will update cache)
    const iceServers = [
        ...stunServers,
        // Paid metered.ca override (only if configured)
        ...(hasPaidMetered ? [{
            urls: [
                'turn:a.relay.metered.ca:80',
                'turn:a.relay.metered.ca:80?transport=tcp',
                'turn:a.relay.metered.ca:443',
                'turn:a.relay.metered.ca:443?transport=tcp'
            ],
            username:   process.env.TURN_USERNAME,
            credential: process.env.TURN_CREDENTIAL
        }] : [openRelayServer])
    ];

    cachedTurnCredentials   = { iceServers, ttl: 86400, provider: turnProvider };
    cachedTurnCredentialsAt = now;
    cachedTurnExpireMs      = TURN_TTL_MS;
    return cachedTurnCredentials;
}


// ============================================================================
// 6D: CORS — configurable origin whitelist
// ============================================================================
const ALLOWED_ORIGINS = (process.env.ALLOWED_ORIGINS || '')
    .split(',')
    .map(s => s.trim())
    .filter(Boolean);

function getCorsHeaders(req) {
    const origin = req.headers.origin || '';
    if (ALLOWED_ORIGINS.length === 0) {
        // No whitelist configured — allow all (dev mode)
        return { 'Access-Control-Allow-Origin': origin || '*', 'Vary': 'Origin' };
    }
    if (ALLOWED_ORIGINS.includes(origin)) {
        return { 'Access-Control-Allow-Origin': origin, 'Vary': 'Origin' };
    }
    // Origin not in whitelist — deny
    return {};
}

// Create HTTP server with CORS and health check
const server = http.createServer((req, res) => {
    const corsHeaders = getCorsHeaders(req);
    Object.entries(corsHeaders).forEach(([k, v]) => res.setHeader(k, v));
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
        const clientIp = (req.headers['x-forwarded-for'] || '').split(',')[0].trim() || req.socket.remoteAddress || 'unknown';
        if (!checkTurnRateLimit(clientIp)) {
            logger.warn('TURN credential request rate-limited', { ip: clientIp });
            res.writeHead(429, { 'Content-Type': 'application/json' });
            return res.end(JSON.stringify({ error: 'Rate limit exceeded' }));
        }
        res.writeHead(200, { 'Content-Type': 'application/json' });
        return res.end(JSON.stringify(getTurnCredentials()));
    }

    // 404 for other routes
    res.writeHead(404, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ error: 'Not Found' }));
});

// 6F: WebSocket server — clientTracking: false (server tracks peers in its own Map)
const wss = new WebSocket.Server({ server, maxPayload: MAX_WS_MESSAGE_SIZE, clientTracking: false });

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
            if (peer) {
                safeSend(peer.ws, messageStr);
            }
        }
    });
}

// Send to specific peer
function sendToPeer(peerId, message) {
    const peer = peers.get(peerId);
    if (peer) {
        safeSend(peer.ws, JSON.stringify(message));
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
                isLan: peer.isLan || false,
                clientType: peer.clientType || 'unknown'
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
        logger.warn('Rejected connection — rate limit', { ip: clientIp });
        ws.close(1008, 'Rate limit exceeded');
        return;
    }

    const peerId = generatePeerId();
    logger.info('Peer connected', { peerId, ip: clientIp });

    // Send peer their ID
    safeSend(ws, JSON.stringify({ type: 'welcome', peerId }));

    ws.on('message', (data) => {
        // Enforce hard per-message size limit before parsing (6E: defense-in-depth)
        if (data.length > MAX_WS_MESSAGE_SIZE) {
            logger.warn('Message too large', { peerId, bytes: data.length });
            return;
        }

        // Per-peer message rate limiting
        if (!checkPeerRateLimit(peerId)) {
            logger.warn('Rate limited', { peerId });
            return;
        }

        let message;
        try {
            message = JSON.parse(data);
        } catch (e) {
            logger.warn('Invalid JSON', { peerId });
            return;
        }

        switch (message.type) {
            case 'join': {
                // Join a room
                const { room, name, fingerprint, publicKey, clientType } = message;
                const roomName = (room || 'teleport-default').substring(0, MAX_ROOM_NAME_LENGTH);
                const peerName = (name || 'Unknown Device').substring(0, MAX_PEER_NAME_LENGTH);

                // FIX: Remove from old room before adding to new one (prevent double-join leak)
                const existingPeer = peers.get(peerId);
                if (existingPeer && existingPeer.room && rooms.has(existingPeer.room)) {
                    rooms.get(existingPeer.room).delete(peerId);
                    if (rooms.get(existingPeer.room).size === 0) {
                        rooms.delete(existingPeer.room);
                    }
                }

                // Store peer info including crypto identity
                peers.set(peerId, {
                    ws,
                    name: peerName,
                    room: roomName,
                    fingerprint: typeof fingerprint === 'string' ? fingerprint.substring(0, 64) : null,
                    publicKey: typeof publicKey === 'string' ? publicKey.substring(0, 512) : null,
                    ip: clientIp,
                    isLan: false,
                    clientType: clientType || 'unknown'
                });

                // Add to room
                if (!rooms.has(roomName)) {
                    rooms.set(roomName, new Set());
                }
                rooms.get(roomName).add(peerId);

                logger.info('Joined room', { peerId, room: roomName, name: peerName });

                // Send current peer list (includes fingerprint + publicKey)
                safeSend(ws, JSON.stringify({
                    type: 'peers',
                    peers: getPeerList(roomName, peerId)
                }));

                // Notify others in room (include crypto identity so receivers can set up E2E immediately)
                broadcastToRoom(roomName, {
                    type: 'peer-joined',
                    peer: {
                        id: peerId,
                        name: peerName,
                        fingerprint: typeof fingerprint === 'string' ? fingerprint.substring(0, 64) : null,
                        publicKey: typeof publicKey === 'string' ? publicKey.substring(0, 512) : null,
                        clientType: clientType || 'unknown'
                    }
                }, peerId);
                break;
            }

            case 'pong': {
                ws.isAlive = true;
                break;
            }

            case 'offer':
            case 'answer':
            case 'ice': {
                // Relay signaling messages to target peer
                const { to } = message;
                if (!to || !peers.has(to)) break;

                // Validate SDP for offer/answer
                if ((message.type === 'offer' || message.type === 'answer') && !isValidSdp(message.sdp)) {
                    logger.warn('Invalid SDP', { peerId, type: message.type, to });
                    break;
                }
                // Validate ICE candidate
                if (message.type === 'ice' && !isValidIceCandidate(message.candidate)) {
                    logger.warn('Invalid ICE candidate', { peerId, to });
                    break;
                }

                const senderPeer = peers.get(peerId);
                sendToPeer(to, {
                    type: message.type,
                    from: peerId,
                    sdp: message.sdp,
                    candidate: message.candidate,
                    fingerprint: message.fingerprint || senderPeer?.fingerprint || null,
                    publicKey: message.publicKey || senderPeer?.publicKey || null
                });
                break;
            }

            case 'file-request': {
                // Relay file transfer request — validate file count and total size
                const { to, files } = message;

                if (!to || !peers.has(to)) {
                    logger.warn('file-request: target peer not found', { peerId, to });
                    break;
                }

                if (!Array.isArray(files) || files.length === 0) {
                    logger.warn('file-request: no files', { peerId });
                    break;
                }

                // Validate each file element shape
                if (!files.every(validateFileElement)) {
                    logger.warn('file-request: invalid file element shape', { peerId });
                    break;
                }

                // Security: enforce global limits
                if (files.length > MAX_FILES_PER_TRANSFER) {
                    logger.warn('file-request: exceeds max file count', { peerId, count: files.length });
                    break;
                }

                const totalBytes = files.reduce((sum, f) => sum + (typeof f.size === 'number' ? f.size : 0), 0);
                if (totalBytes > MAX_TOTAL_TRANSFER_BYTES) {
                    logger.warn('file-request: exceeds max total size', { peerId, totalBytes });
                    break;
                }

                const senderPeer = peers.get(peerId);
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
                const { to, transferId, filename, size, mimeType, fileIndex, totalFiles, sha256, encrypted } = message;

                if (!to || !peers.has(to)) break;

                // Validate relay-start fields
                if (!validateRelayStart(message)) {
                    logger.warn('relay-start: invalid fields', { peerId, to });
                    break;
                }

                const normalizedSha = isValidSha256Hex(sha256) ? sha256.toLowerCase() : null;
                sendToPeer(to, {
                    type: 'relay-start',
                    from: peerId,
                    transferId,
                    filename,
                    size,
                    mimeType,
                    sha256: normalizedSha,
                    fileIndex,
                    totalFiles,
                    encrypted: !!encrypted,
                });
                break;
            }

            case 'relay-chunk': {
                // Relay a file chunk (base64 encoded for JSON)
                const { to, transferId, data, offset } = message;

                // Validate chunk before forwarding
                if (!to || !peers.has(to)) break;
                if (typeof offset !== 'number' || offset < 0 || !Number.isFinite(offset)) {
                    logger.warn('relay-chunk: invalid offset', { peerId, offset });
                    break;
                }
                if (!isValidBase64(data)) {
                    logger.warn('relay-chunk: payload is not valid base64', { peerId });
                    break;
                }
                const decodedLen = base64DecodedLength(data);
                if (decodedLen > MAX_RELAY_CHUNK_BYTES) {
                    logger.warn('relay-chunk: decoded size exceeds limit', { peerId, decodedLen, limit: MAX_RELAY_CHUNK_BYTES });
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
                const { to, transferId, sha256 } = message;
                if (to && peers.has(to)) {
                    sendToPeer(to, {
                        type: 'relay-end',
                        from: peerId,
                        transferId,
                        sha256: sha256 || null
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

            case 'relay-resume-request': {
                // Desktop → Browser: "I have N bytes already, please resume from here"
                const { to, transferId, resumeOffset } = message;
                if (!to || !peers.has(to)) break;
                if (typeof resumeOffset !== 'number' ||
                    resumeOffset < 0 ||
                    !Number.isFinite(resumeOffset) ||
                    resumeOffset > 10 * 1024 * 1024 * 1024 /* 10 GB sanity cap */) {
                    logger.warn('relay-resume-request: invalid resumeOffset', { peerId, resumeOffset });
                    break;
                }
                sendToPeer(to, {
                    type: 'relay-resume-request',
                    from: peerId,
                    transferId,
                    resumeOffset
                });
                break;
            }

            case 'relay-reconnect-hint': {
                // Desktop → Browser: advisory notice on reconnect that the desktop
                // still has a partial temp file and is ready to receive a resume.
                const { to, transferId, resumeOffset } = message;
                if (!to || !peers.has(to)) break;
                if (typeof resumeOffset !== 'number' ||
                    resumeOffset < 0 ||
                    !Number.isFinite(resumeOffset)) break;
                sendToPeer(to, {
                    type: 'relay-reconnect-hint',
                    from: peerId,
                    transferId,
                    resumeOffset
                });
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

                if (!lanIp || !lanPort || !isValidIpAddress(lanIp) || !isValidPort(lanPort)) {
                    logger.warn('lan-announce: invalid ip or port', { peerId, ip: lanIp, port: lanPort });
                    break;
                }

                peer.lanIp = lanIp;
                peer.lanPort = lanPort;
                peer.isLan = true;

                // Notify room peers that this peer has a LAN address
                broadcastToRoom(peer.room, {
                    type: 'peer-lan-updated',
                    peerId,
                    lanIp,
                    lanPort
                }, peerId);
                break;
            }

            default:
                logger.debug('Unknown message type', { peerId, type: message.type });
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

            // Remove peer and clean up rate limits
            peers.delete(peerId);
            peerMessageRates.delete(peerId);
            logger.info('Peer disconnected', { peerId });
        }
    });

    ws.on('error', (error) => {
        logger.error('WebSocket error', { peerId, error: error.message });
    });
});

// Periodic sweeping of dead connections — iterate our own peers Map
const heartbeatInterval = setInterval(() => {
    for (const [pid, peer] of peers) {
        if (peer.ws && peer.ws.isAlive === false) {
            logger.debug('Terminating dead connection', { peerId: pid });
            peer.ws.terminate();
            continue;
        }
        if (peer.ws) {
            peer.ws.isAlive = false;
            peer.ws.ping();
        }
    }
}, 20000);

wss.on('close', () => {
    clearInterval(heartbeatInterval);
});

server.listen(PORT, () => {
    logger.info('Teleport Signaling Server started', { port: PORT, pid: process.pid });
});

// ============================================================================
// 6B: GRACEFUL SHUTDOWN
// ============================================================================
let isShuttingDown = false;

function gracefulShutdown(signal) {
    if (isShuttingDown) return;
    isShuttingDown = true;
    logger.info('Shutting down', { signal, pid: process.pid });

    // Stop accepting new connections
    server.close(() => {
        logger.info('HTTP server closed');
    });

    // Close all peer WebSocket connections
    for (const [pid, peer] of peers) {
        if (peer.ws) {
            peer.ws.close(1001, 'Server shutting down');
        }
    }

    // Close the WebSocket server
    wss.close(() => {
        logger.info('WebSocket server closed');
    });

    // Clear intervals
    clearInterval(heartbeatInterval);

    // Force exit after 5 seconds if graceful shutdown stalls
    setTimeout(() => {
        logger.warn('Force exit — shutdown timed out');
        process.exit(1);
    }, 5000).unref();
}

process.on('SIGTERM', () => gracefulShutdown('SIGTERM'));
process.on('SIGINT', () => gracefulShutdown('SIGINT'));

// ============================================================================
// TEST EXPORTS — only active when required as a module (not when run directly)
// ============================================================================
if (require.main !== module) {
    module.exports = {
        isValidBase64,
        base64DecodedLength,
        isValidSha256Hex,
        isValidSdp,
        isValidIceCandidate,
        isValidIpAddress,
        isValidPort,
        validateRelayStart,
        validateFileElement,
        checkRateLimit,
        checkPeerRateLimit,
        checkTurnRateLimit,
        logger,
    };
}
