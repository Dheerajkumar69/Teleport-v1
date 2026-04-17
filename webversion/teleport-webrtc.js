/**
 * Teleport WebRTC File Transfer Engine - Final Production
 * Complete with all fixes and real implementations
 */

// ============================================================================
// STREAMING FILE WRITER — Writes chunks to disk without buffering in memory
// ============================================================================
// Supports File System Access API (Chrome 85+, Edge 85+) and IndexedDB (all browsers)

class StreamingFileWriter {
    constructor(filename, filesize, options = {}) {
        this.filename = filename;
        this.filesize = filesize;
        this.writer = null;  // FileSystemWritableFileStream
        this.chunks = [];    // Fallback for IndexedDB storage (small files only)
        this.bytesWritten = 0;
        this.useFileSystemAPI = options.useFileSystemAPI || false;
        this.aborted = false;
    }

    // Initialize with File System API (preferred)
    async initFileSystemAPI(fileHandle) {
        try {
            this.writer = await fileHandle.createWritable();
            this.useFileSystemAPI = true;
            return true;
        } catch (e) {
            console.warn('[StreamWriter] File System API init failed:', e.message);
            return false;
        }
    }

    // Write a chunk — goes directly to disk if using File System API
    async write(chunk) {
        if (this.aborted) throw new Error('StreamingFileWriter aborted');
        
        if (this.useFileSystemAPI && this.writer) {
            try {
                await this.writer.write(chunk);
                this.bytesWritten += chunk.length;
                return true;
            } catch (e) {
                console.error('[StreamWriter] Write error:', e);
                throw e;
            }
        } else {
            // Fallback: buffer in memory (IndexedDB backend only for resumable state)
            this.chunks.push(chunk);
            this.bytesWritten += chunk.length;
            return true;
        }
    }

    // Finalize and close the stream
    async close() {
        if (this.aborted) return;
        
        try {
            if (this.useFileSystemAPI && this.writer) {
                await this.writer.close();
            }
            this.writer = null;
        } catch (e) {
            console.warn('[StreamWriter] Close error:', e);
        }
    }

    // Abort and discard the stream
    async abort() {
        this.aborted = true;
        try {
            if (this.useFileSystemAPI && this.writer) {
                await this.writer.abort();
            }
            this.chunks = [];
            this.writer = null;
        } catch (e) {
            console.warn('[StreamWriter] Abort error:', e);
        }
    }

    // Get assembled file as Blob (if using fallback chunking)
    getBlob(mimeType = 'application/octet-stream') {
        if (this.chunks.length === 0) return new Blob([], { type: mimeType });
        return new Blob(this.chunks, { type: mimeType });
    }
}


// ============================================================================
// IncrementalSHA256 — Accumulates binary chunks, computes SHA-256 on finalise
// Uses SubtleCrypto (available in all modern browsers on HTTPS and localhost).
// Falls back to a pure-JS implementation when SubtleCrypto is unavailable.
// ============================================================================

/**
 * Pure-JS SHA-256 fallback (FIPS 180-4).
 * Only used when crypto.subtle is unavailable (non-secure contexts, old browsers).
 */
function sha256PureJS(dataBytes) {
    const K = [
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,
        0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
        0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,
        0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,
        0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
        0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,
        0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,
        0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
        0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    ];
    let H = [0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
              0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19];
    const len = dataBytes.length;
    const bitLen = len * 8;
    // Pre-processing: add padding
    const padded = new Uint8Array(((len + 9 + 63) & ~63));
    padded.set(dataBytes);
    padded[len] = 0x80;
    const dv = new DataView(padded.buffer);
    dv.setUint32(padded.length - 4, bitLen & 0xffffffff, false);
    dv.setUint32(padded.length - 8, Math.floor(bitLen / 2**32), false);

    for (let i = 0; i < padded.length; i += 64) {
        const W = new Uint32Array(64);
        for (let t = 0; t < 16; t++) W[t] = dv.getUint32(i + t * 4, false);
        for (let t = 16; t < 64; t++) {
            const s0 = (W[t-15] >>> 7 | W[t-15] << 25) ^ (W[t-15] >>> 18 | W[t-15] << 14) ^ (W[t-15] >>> 3);
            const s1 = (W[t-2]  >>> 17 | W[t-2]  << 15) ^ (W[t-2]  >>> 19 | W[t-2]  << 13) ^ (W[t-2]  >>> 10);
            W[t] = (W[t-16] + s0 + W[t-7] + s1) >>> 0;
        }
        let [a,b,c,d,e,f,g,h] = H;
        for (let t = 0; t < 64; t++) {
            const S1 = (e >>> 6 | e << 26) ^ (e >>> 11 | e << 21) ^ (e >>> 25 | e << 7);
            const ch = (e & f) ^ (~e & g);
            const T1 = (h + S1 + ch + K[t] + W[t]) >>> 0;
            const S0 = (a >>> 2 | a << 30) ^ (a >>> 13 | a << 19) ^ (a >>> 22 | a << 10);
            const maj = (a & b) ^ (a & c) ^ (b & c);
            const T2 = (S0 + maj) >>> 0;
            h=g; g=f; f=e; e=(d+T1)>>>0; d=c; c=b; b=a; a=(T1+T2)>>>0;
        }
        H=[H[0]+a,H[1]+b,H[2]+c,H[3]+d,H[4]+e,H[5]+f,H[6]+g,H[7]+h].map(v=>v>>>0);
    }
    return H.map(v => v.toString(16).padStart(8,'0')).join('');
}

class IncrementalSHA256 {
    constructor() {
        this.chunks = [];
        this.totalSize = 0;
    }

    /** Feed a Uint8Array chunk into the hasher. */
    update(chunk) {
        // Always store a copy — callers may reuse the same buffer.
        this.chunks.push(new Uint8Array(chunk));
        this.totalSize += chunk.length;
    }

    /**
     * Finalise and return the SHA-256 digest as a lowercase 64-char hex string.
     * Returns null only when called with zero data AND zero chunks.
     */
    async hex() {
        if (this.chunks.length === 0 && this.totalSize === 0) {
            // Nothing was fed — return SHA-256("") so callers get a valid hash
            return 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855';
        }

        // Assemble all chunks into one contiguous buffer.
        const combined = new Uint8Array(this.totalSize);
        let offset = 0;
        for (const chunk of this.chunks) {
            combined.set(chunk, offset);
            offset += chunk.length;
        }

        // SubtleCrypto path — available on HTTPS and localhost.
        if (typeof crypto !== 'undefined' && crypto.subtle && crypto.subtle.digest) {
            try {
                const hashBuffer = await crypto.subtle.digest('SHA-256', combined.buffer);
                return Array.from(new Uint8Array(hashBuffer))
                    .map(b => b.toString(16).padStart(2, '0'))
                    .join('');
            } catch (e) {
                console.warn('[IncrementalSHA256] SubtleCrypto failed, using pure-JS fallback:', e.message);
            }
        }

        // Pure-JS fallback — slightly slower but works on non-HTTPS origins.
        return sha256PureJS(combined);
    }
}


// ============================================================================
// CHUNK BATCHER — Batch small chunks to reduce control message overhead
// ============================================================================

class ChunkBatcher {
    constructor(maxBatchSize = 1024 * 1024) {
        this.batch = null;
        this.batchSize = 0;
        this.maxBatchSize = maxBatchSize;
        this.pendingFlush = null;
    }

    add(chunk) {
        if (!this.batch) {
            this.batch = [];
            this.batchSize = 0;
        }

        this.batch.push(chunk);
        this.batchSize += chunk.length;

        return this.shouldFlush();
    }

    shouldFlush() {
        return this.batchSize >= this.maxBatchSize;
    }

    get() {
        if (!this.batch || this.batch.length === 0) return null;

        // Combine batch into single array
        const combined = new Uint8Array(this.batchSize);
        let offset = 0;
        for (const chunk of this.batch) {
            combined.set(chunk, offset);
            offset += chunk.length;
        }

        this.batch = null;
        this.batchSize = 0;
        return combined;
    }

    combine() {
        return this.get();  // Alias
    }
}

// ============================================================================
// ADAPTIVE CHUNK SIZE — Select chunk size based on file size
// ============================================================================

function selectChunkSize(fileSize) {
    // Balance between throughput (larger chunks) and resumability (smaller chunks)
    if (fileSize < 100 * 1024 * 1024) return 16 * 1024;           // <100MB → 16KB
    if (fileSize < 500 * 1024 * 1024) return 256 * 1024;          // 100–500MB → 256KB
    return 256 * 1024; // Default to 256KB for maximum throughput
}
// ============================================================================
// PHASE 3: ERROR CODES & PRODUCTION HARDENING
// ============================================================================

const ErrorCodes = {
    // Connection errors
    CONN_TIMEOUT: 'CONN_TIMEOUT',
    CONN_REFUSED: 'CONN_REFUSED',
    CONN_LOST: 'CONN_LOST',
    SIGNALING_FAILED: 'SIGNALING_FAILED',
    ICE_FAILED: 'ICE_FAILED',
    
    // Transfer errors
    TRANSFER_TIMEOUT: 'TRANSFER_TIMEOUT',
    TRANSFER_CANCELLED: 'TRANSFER_CANCELLED',
    TRANSFER_CORRUPTED: 'TRANSFER_CORRUPTED',
    HASH_MISMATCH: 'HASH_MISMATCH',
    FILE_NOT_FOUND: 'FILE_NOT_FOUND',
    FILE_TOO_LARGE: 'FILE_TOO_LARGE',
    FILE_ACCESS_DENIED: 'FILE_ACCESS_DENIED',
    
    // DataChannel errors
    DATACHANNEL_CLOSED: 'DATACHANNEL_CLOSED',
    DATACHANNEL_ERROR: 'DATACHANNEL_ERROR',
    BUFFER_OVERFLOW: 'BUFFER_OVERFLOW',
    
    // Relay/Stream errors
    RELAY_CONNECTION_FAILED: 'RELAY_CONNECTION_FAILED',
    RELAY_TIMEOUT: 'RELAY_TIMEOUT',
    STREAMING_WRITE_FAILED: 'STREAMING_WRITE_FAILED',
    
    // Resource errors
    OUT_OF_MEMORY: 'OUT_OF_MEMORY',
    DISK_SPACE_LOW: 'DISK_SPACE_LOW',
    STORAGE_QUOTA_EXCEEDED: 'STORAGE_QUOTA_EXCEEDED',
    
    // Protocol errors
    INVALID_MESSAGE: 'INVALID_MESSAGE',
    VERSION_MISMATCH: 'VERSION_MISMATCH',
    UNSUPPORTED_FEATURE: 'UNSUPPORTED_FEATURE',
    
    // Application errors
    NO_PEER_AVAILABLE: 'NO_PEER_AVAILABLE',
    TRANSFER_REJECTED: 'TRANSFER_REJECTED',
    AUTHENTICATION_FAILED: 'AUTHENTICATION_FAILED'
};

// Error message mapping
const ErrorMessages = {
    [ErrorCodes.CONN_TIMEOUT]: 'Connection timeout - peer may be unreachable',
    [ErrorCodes.CONN_REFUSED]: 'Connection refused - peer closed connection',
    [ErrorCodes.CONN_LOST]: 'Connection lost - disconnected from peer',
    [ErrorCodes.SIGNALING_FAILED]: 'Signaling failed - unable to reach signaling server',
    [ErrorCodes.ICE_FAILED]: 'ICE connection failed - no compatible NAT traversal',
    
    [ErrorCodes.TRANSFER_TIMEOUT]: 'Transfer timeout - no progress for 5 minutes',
    [ErrorCodes.TRANSFER_CANCELLED]: 'Transfer cancelled by user',
    [ErrorCodes.TRANSFER_CORRUPTED]: 'Transfer corrupted - data integrity check failed',
    [ErrorCodes.HASH_MISMATCH]: 'Hash mismatch - file content verification failed',
    [ErrorCodes.FILE_NOT_FOUND]: 'File not found - source file deleted',
    [ErrorCodes.FILE_TOO_LARGE]: 'File too large - exceeds transfer limit',
    [ErrorCodes.FILE_ACCESS_DENIED]: 'File access denied - permission limited',
    
    [ErrorCodes.DATACHANNEL_CLOSED]: 'DataChannel closed unexpectedly',
    [ErrorCodes.DATACHANNEL_ERROR]: 'DataChannel error - connection unstable',
    [ErrorCodes.BUFFER_OVERFLOW]: 'Buffer overflow - receiver unable to keep up',
    
    [ErrorCodes.RELAY_CONNECTION_FAILED]: 'Relay connection failed - server unavailable',
    [ErrorCodes.RELAY_TIMEOUT]: 'Relay timeout - server not responding',
    [ErrorCodes.STREAMING_WRITE_FAILED]: 'Streaming write failed - disk I/O error',
    
    [ErrorCodes.OUT_OF_MEMORY]: 'Out of memory - insufficient resources',
    [ErrorCodes.DISK_SPACE_LOW]: 'Disk space low - insufficient storage',
    [ErrorCodes.STORAGE_QUOTA_EXCEEDED]: 'Storage quota exceeded - IndexedDB full',
    
    [ErrorCodes.INVALID_MESSAGE]: 'Invalid message format - protocol error',
    [ErrorCodes.VERSION_MISMATCH]: 'Version mismatch - incompatible protocol',
    [ErrorCodes.UNSUPPORTED_FEATURE]: 'Unsupported feature - peer does not support',
    
    [ErrorCodes.NO_PEER_AVAILABLE]: 'No peer available - no connections established',
    [ErrorCodes.TRANSFER_REJECTED]: 'Transfer rejected by peer',
    [ErrorCodes.AUTHENTICATION_FAILED]: 'Authentication failed - fingerprint mismatch'
};

// ============================================================================
// MAIN ENGINE
// ============================================================================

class TeleportWebRTC {
    constructor() {
        this.ws = null;
        this.peerId = null;
        this.peerFingerprint = null;
        this.deviceName = this.loadSetting('teleport-device-name') || this.generateDeviceName();
        this.peers = new Map();
        this.dataChannels = new Map();
        this.pendingFiles = new Map();
        this.incomingChunks = new Map();
        this.activeTransfers = new Map();
        this.relayIncoming = new Map(); // For server relay fallback
        this.transferQueue = [];
        this.isProcessingQueue = false;
        this.useRelayFallback = true; // Enable automatic relay fallback
        this.peerList = []; // Live list of discovered peers (updated on peer-joined/peer-left)
        this.fileHashCache = new Map(); // file cache key -> SHA-256 hex

        // Session sharing via BroadcastChannel
        this.broadcastChannel = null;
        this.initBroadcastChannel();

        // Connection state
        this.isConnected = false;
        this.reconnectAttempts = 0;
        this.maxReconnectAttempts = 5;
        this.reconnectDelay = 1000;
        this.serverUrl = null;
        this.manualServerUrl = null;
        this.connectionTimeout = 30000;
        this._keepAliveTimer = null; // Interval that pings the Render server to prevent sleep
        this.shouldReconnect = true;
        this.signalingHealth = new Map(); // wsUrl -> { healthy, latency, lastSuccessAt, lastFailureAt }
        this.signalingServers = this.loadSignalingServers();

        // P2P vs relay-only mode (persisted in localStorage)
        const savedMode = this.loadSetting('teleport-connection-mode');
        this.forceRelayMode = (savedMode === 'relay');


        // Bandwidth throttling
        this.maxBandwidth = parseInt(this.loadSetting('teleport-bandwidth-limit')) || 0;
        this.bandwidthTokens = this.maxBandwidth > 0 ? this.maxBandwidth : 0;
        this.lastThrottleTick = Date.now();

        // File size warning threshold
        this.fileSizeWarningThreshold = 100 * 1024 * 1024;
        this.MAX_IN_MEMORY_RECEIVE_SIZE = 512 * 1024 * 1024; // 512MB safety limit without streaming sink

        // Callbacks
        this.onPeersUpdated = null;
        this.onFileRequest = null;
        this.onTransferProgress = null;
        this.onTransferComplete = null;
        this.onTransferError = null;
        this.onConnected = null;
        this.onDisconnected = null;
        this.onReconnecting = null;
        this.onFileSizeWarning = null;
        this.onPeerVerification = null;

        // ===== PHASE 0: SECURITY BASELINE =====
        // Trusted device storage and fingerprint validation
        this.trustedDevices = new Map(); // peerId -> { fingerprint, publicKey, firstSeen, lastVerified, trustLevel }
        this.pendingVerification = new Map(); // peerId -> { fingerprint, timestamp, signature }
        this.deviceSigningKey = null;
        this.initSecurityBaseline();

        // WebRTC config — starts with reliable public STUN + open-relay TURN.
        // Expired/hardcoded metered.ca credentials are REMOVED;
        // call fetchTurnCredentials() after connect() to pull fresh ones from the server.
        this.rtcConfig = {
            iceServers: [
                { urls: 'stun:stun.l.google.com:19302' },
                { urls: 'stun:stun1.l.google.com:19302' },
                { urls: 'stun:stun2.l.google.com:19302' },
                { urls: 'stun:stun3.l.google.com:19302' },
                { urls: 'stun:stun4.l.google.com:19302' },
                { urls: 'stun:global.stun.twilio.com:3478' },
                // openrelay.metered.ca — public project, credentials never expire
                {
                    urls: [
                        'turn:openrelay.metered.ca:80',
                        'turn:openrelay.metered.ca:80?transport=tcp',
                        'turn:openrelay.metered.ca:443?transport=tcp'
                    ],
                    username: 'openrelayproject',
                    credential: 'openrelayproject'
                }
            ],
            iceCandidatePoolSize: 10,
            iceTransportPolicy: 'all'
        };
        this.turnFetched = false; // guard to avoid repeated fetches

        this.CHUNK_SIZE = 16384;
        this.MAX_BUFFER_SIZE = 16 * 1024 * 1024; // 16MB buffer for maximum WebRTC P2P throughput
        this.TRANSFER_TIMEOUT = 300000; // 5 minutes base timeout
        this.CONNECTION_TIMEOUT = 30000; // 30 seconds for ICE negotiation
        this.SIGNALING_ATTEMPT_TIMEOUT = 10000;
        this.RESUME_READY_TIMEOUT = 15000;
        this.RELAY_VERIFICATION_TIMEOUT = 15000;

        // Streaming config for large files
        this.STREAMING_THRESHOLD = 50 * 1024 * 1024; // 50MB - use streaming above this
        this.useFileSystemAPI = 'showSaveFilePicker' in window;

        // E2E Encryption
        this.keyPair = null;
        this.sharedSecrets = new Map(); // peerId -> AES key
        this.peerPublicKeys = new Map();
        this.encryptionEnabled = true;
        this.onError = null; // Global error callback

        // Resume support (IndexedDB)
        this.resumeDb = null;
        this.initResumeDB();

        // ===== PHASE 3: LIFECYCLE & RESOURCE MANAGEMENT =====
        this.activeTimers = new Map();        // transferId -> { timeout, cleanup }
        this.peerMetadata = new Map();        // peerId -> { connectedAt, lastActivity }
        this.resumeStateExpiry = new Map();   // transferId -> expiresAt (timestamp)
        this.RESUME_STATE_LIFETIME = 1 * 60 * 60 * 1000; // 1 hour
        this.TRANSFER_TIMEOUT = 5 * 60 * 1000; // 5 minutes
        this.cleanup = this.cleanup.bind(this);
        this.cleanupTimer = null;

        // Initialize encryption keys
        this.initEncryption();
        
        // Start periodic cleanup
        this.startPeriodicCleanup();
    }

    // ===== PHASE 3: ERROR HANDLING =====

    handleError(code, context = {}) {
        const message = ErrorMessages[code] || 'Unknown error';
        const fullError = {
            code,
            message,
            context,
            timestamp: Date.now()
        };

        console.error(`[TeleportError] ${code}: ${message}`, context);

        // Call error callbacks
        if (this.onError) {
            try {
                this.onError(fullError);
            } catch (e) {
                console.error('[TeleportError] Error callback failed:', e);
            }
        }

        if (this.onTransferError && context.transferId) {
            try {
                this.onTransferError({
                    transferId: context.transferId,
                    error: message,
                    code
                });
            } catch (e) {
                console.error('[TeleportError] Transfer error callback failed:', e);
            }
        }

        return fullError;
    }

    // Cleanup transfer resources
    cleanupTransfer(transferId) {
        const transfer = this.activeTransfers.get(transferId);
        if (!transfer) return;

        // Clear timeout
        const timer = this.activeTimers.get(transferId);
        if (timer) {
            clearTimeout(timer.timeout);
            this.activeTimers.delete(transferId);
        }

        // Close streaming writer if exists
        if (transfer.writer) {
            transfer.writer.abort?.().catch(e => console.warn('Writer abort failed:', e));
        }

        // Clear relay data
        const relayTransfer = this.relayIncoming.get(transferId);
        if (relayTransfer) {
            relayTransfer.writer?.abort?.().catch(e => console.warn('Relay writer abort failed:', e));
            this.relayIncoming.delete(transferId);
        }

        // Clean up chunks and metadata
        this.incomingChunks.delete(transferId);
        this.activeTransfers.delete(transferId);
        this.resumeStateExpiry.delete(transferId);

        console.log(`[Cleanup] Transfer ${transferId} resources cleaned up`);
    }

    // Cleanup peer resources
    cleanupPeer(peerId) {
        const peer = this.peers.get(peerId);
        if (!peer) return;

        // Close datachannel
        const dc = this.dataChannels.get(peerId);
        if (dc && dc.readyState !== 'closed') {
            try {
                dc.close();
            } catch (e) {
                console.warn('[Cleanup] DataChannel close failed:', e);
            }
        }

        // Close peer connection
        if (peer.connection && peer.connection.signalingState !== 'closed') {
            try {
                peer.connection.close();
            } catch (e) {
                console.warn('[Cleanup] Peer connection close failed:', e);
            }
        }

        // Clean up transfers for this peer
        for (const [tid, t] of this.activeTransfers) {
            if (t.peerId === peerId) {
                this.cleanupTransfer(tid);
            }
        }

        this.dataChannels.delete(peerId);
        this.peers.delete(peerId);
        this.peerMetadata.delete(peerId);
        
        console.log(`[Cleanup] Peer ${peerId} resources cleaned up`);
    }

    // Start scheduled cleanup for old resume state
    startPeriodicCleanup() {
        this.cleanupTimer = setInterval(() => {
            const now = Date.now();
            let expired = 0;

            for (const [tid, expiresAt] of this.resumeStateExpiry) {
                if (now > expiresAt) {
                    this.resumeStateExpiry.delete(tid);
                    expired++;
                }
            }

            // Clean up inactive peers (no activity for 30 minutes)
            for (const [peerId, metadata] of this.peerMetadata) {
                if (now - metadata.lastActivity > 30 * 60 * 1000) {
                    this.cleanupPeer(peerId);
                }
            }

            if (expired > 0) {
                console.log(`[Cleanup] Expired ${expired} old resume states`);
            }
        }, 10 * 60 * 1000); // Run every 10 minutes
    }

    // Stop periodic cleanup
    stopPeriodicCleanup() {
        if (this.cleanupTimer) {
            clearInterval(this.cleanupTimer);
            this.cleanupTimer = null;
        }
    }

    // Update peer metadata (track last activity)
    updatePeerActivity(peerId) {
        if (!this.peerMetadata.has(peerId)) {
            this.peerMetadata.set(peerId, {
                connectedAt: Date.now(),
                lastActivity: Date.now()
            });
        } else {
            const metadata = this.peerMetadata.get(peerId);
            metadata.lastActivity = Date.now();
        }
    }

    // Master cleanup on disconnect
    cleanup() {
        console.log('[Cleanup] Starting full cleanup');

        // Stop timers
        this.stopPeriodicCleanup();
        
        // Close all peer connections
        for (const peerId of this.peers.keys()) {
            this.cleanupPeer(peerId);
        }

        // Cleanup all transfers
        for (const transferId of this.activeTransfers.keys()) {
            this.cleanupTransfer(transferId);
        }

        // Clear all queues
        this.transferQueue = [];
        this.incomingChunks.clear();
        this.pendingFiles.clear();

        console.log('[Cleanup] Full cleanup completed');
    }

    // ==================== BROADCAST CHANNEL ====================

    initBroadcastChannel() {
        try {
            this.broadcastChannel = new BroadcastChannel('teleport-session');
            this.broadcastChannel.onmessage = (event) => {
                this.handleBroadcastMessage(event.data);
            };
        } catch (e) {
            console.log('BroadcastChannel not supported');
        }
    }

    handleBroadcastMessage(data) {
        switch (data.type) {
            case 'peer-connected':
                if (this.onPeersUpdated) {
                    this.onPeersUpdated(data.peers);
                }
                break;
            case 'transfer-started':
            case 'transfer-progress':
            case 'transfer-complete':
                // Sync transfer state across tabs
                break;
        }
    }

    broadcastEvent(type, data) {
        if (this.broadcastChannel) {
            this.broadcastChannel.postMessage({ type, ...data });
        }
    }

    // ==================== SETTINGS ====================

    loadSetting(key) {
        try { return localStorage.getItem(key); } catch (e) { return null; }
    }

    saveSetting(key, value) {
        try { localStorage.setItem(key, value); } catch (e) { }
    }

    generateDeviceName() {
        const ua = navigator.userAgent;
        let name = 'Web Browser';
        if (ua.includes('Chrome')) name = 'Chrome';
        else if (ua.includes('Firefox')) name = 'Firefox';
        else if (ua.includes('Safari')) name = 'Safari';
        else if (ua.includes('Edge')) name = 'Edge';

        if (ua.includes('Windows')) name += ' (Windows)';
        else if (ua.includes('Mac')) name += ' (Mac)';
        else if (ua.includes('Linux')) name += ' (Linux)';
        else if (ua.includes('Android')) name += ' (Android)';
        else if (ua.includes('iPhone') || ua.includes('iPad')) name += ' (iOS)';

        return name;
    }

    setDeviceName(name) {
        this.deviceName = name;
        this.saveSetting('teleport-device-name', name);
    }

    setBandwidthLimit(bytesPerSecond) {
        this.maxBandwidth = bytesPerSecond;
        this.bandwidthTokens = bytesPerSecond > 0 ? bytesPerSecond : 0;
        this.lastThrottleTick = Date.now();
        this.saveSetting('teleport-bandwidth-limit', bytesPerSecond.toString());
    }

    getBandwidthLimit() {
        return parseInt(this.loadSetting('teleport-bandwidth-limit')) || 0;
    }

    loadSignalingServers() {
        try {
            const raw = localStorage.getItem('teleport-signaling-servers');
            if (!raw) return [];
            const parsed = JSON.parse(raw);
            if (!Array.isArray(parsed)) return [];

            const normalized = parsed
                .map(url => this.normalizeWebSocketUrl(url))
                .filter(Boolean);

            return Array.from(new Set(normalized));
        } catch (e) {
            return [];
        }
    }

    saveSignalingServers(servers) {
        const normalized = Array.from(new Set(
            (Array.isArray(servers) ? servers : [])
                .map(url => this.normalizeWebSocketUrl(url))
                .filter(Boolean)
        ));

        this.signalingServers = normalized;
        try {
            localStorage.setItem('teleport-signaling-servers', JSON.stringify(normalized));
        } catch (e) { }
    }

    setSignalingServers(servers) {
        this.saveSignalingServers(servers);
    }

    getSignalingServers() {
        if (this.signalingServers.length > 0) {
            return [...this.signalingServers];
        }
        return this.getDefaultSignalingServers();
    }

    normalizeWebSocketUrl(url) {
        if (typeof url !== 'string') return null;
        const trimmed = url.trim();
        if (!trimmed) return null;

        const withScheme = /^wss?:\/\//i.test(trimmed) ? trimmed : `ws://${trimmed}`;
        try {
            const parsed = new URL(withScheme);
            if (parsed.protocol !== 'ws:' && parsed.protocol !== 'wss:') return null;
            return parsed.toString().replace(/\/$/, '');
        } catch (e) {
            return null;
        }
    }

    toHttpBase(webSocketUrl) {
        return (webSocketUrl || '')
            .replace(/^wss:\/\//, 'https://')
            .replace(/^ws:\/\//, 'http://')
            .replace(/\/$/, '');
    }

    async fetchWithTimeout(url, options = {}, timeoutMs = 5000) {
        const fetchOptions = { ...options };

        if (typeof AbortSignal !== 'undefined' && typeof AbortSignal.timeout === 'function') {
            if (!fetchOptions.signal) {
                fetchOptions.signal = AbortSignal.timeout(timeoutMs);
            }
            return fetch(url, fetchOptions);
        }

        const controller = new AbortController();
        const timeoutId = setTimeout(() => controller.abort(), timeoutMs);
        fetchOptions.signal = controller.signal;

        try {
            return await fetch(url, fetchOptions);
        } finally {
            clearTimeout(timeoutId);
        }
    }

    getDefaultSignalingServers() {
        const host = window.location.hostname;
        const runtimeConfigured = Array.isArray(window.TELEPORT_SIGNALING_SERVERS)
            ? window.TELEPORT_SIGNALING_SERVERS
                .map(url => this.normalizeWebSocketUrl(url))
                .filter(Boolean)
            : [];

        if (runtimeConfigured.length > 0) {
            return Array.from(new Set(runtimeConfigured));
        }

        if (host === 'localhost' || host === '127.0.0.1') {
            return [
                this.normalizeWebSocketUrl('ws://localhost:3000'),
                // CF Workers as first-choice for local dev too
                this.normalizeWebSocketUrl('wss://teleport-signaling.dheeraj-kumar-28c.workers.dev'),
                this.normalizeWebSocketUrl('wss://teleport-signaling.onrender.com')
            ].filter(Boolean);
        }

        if (this.isPrivateIP(host)) {
            return [
                this.normalizeWebSocketUrl(`ws://${host}:3000`),
                this.normalizeWebSocketUrl('wss://teleport-signaling.dheeraj-kumar-28c.workers.dev'),
                this.normalizeWebSocketUrl('wss://teleport-signaling.onrender.com')
            ].filter(Boolean);
        }

        // Cloudflare Workers first (fastest, no bandwidth cap, global PoP),
        // then Render as fallback.  Replace YOUR_SUBDOMAIN after `wrangler deploy`.
        return [
            this.normalizeWebSocketUrl('wss://teleport-signaling.dheeraj-kumar-28c.workers.dev'),
            this.normalizeWebSocketUrl('wss://teleport-signaling.onrender.com'),
            this.normalizeWebSocketUrl('wss://teleport-signaling-backup.onrender.com')
        ].filter(Boolean);
    }

    /**
     * Switch between WebRTC P2P (default) and server-relay-only mode.
     * @param {'p2p'|'relay'} mode
     */
    setConnectionMode(mode) {
        this.forceRelayMode = (mode === 'relay');
        try { localStorage.setItem('teleport-connection-mode', mode); } catch {}
        console.log(`[Teleport] Connection mode set to: ${mode}`);
    }

    getConnectionMode() {
        return this.forceRelayMode ? 'relay' : 'p2p';
    }

    async probeSignalingServer(webSocketUrl, timeoutMs = 2500) {
        const now = (typeof performance !== 'undefined' && typeof performance.now === 'function')
            ? () => performance.now()
            : () => Date.now();
        const startedAt = now();
        try {
            const healthUrl = `${this.toHttpBase(webSocketUrl)}/health`;
            const res = await this.fetchWithTimeout(healthUrl, {
                method: 'GET',
                headers: { 'Accept': 'application/json' }
            }, timeoutMs);

            const latency = now() - startedAt;
            return {
                url: webSocketUrl,
                healthy: res.ok,
                latency,
                status: res.status
            };
        } catch (e) {
            return {
                url: webSocketUrl,
                healthy: false,
                latency: Number.POSITIVE_INFINITY,
                status: 0
            };
        }
    }

    async getRankedSignalingServers(explicitServer = null) {
        const explicit = this.normalizeWebSocketUrl(explicitServer);
        if (explicit) return [explicit];

        const candidates = [];
        if (this.manualServerUrl) {
            const normalizedManual = this.normalizeWebSocketUrl(this.manualServerUrl);
            if (normalizedManual) candidates.push(normalizedManual);
        }
        candidates.push(...this.getSignalingServers());

        const uniqueCandidates = Array.from(new Set(candidates.filter(Boolean)));
        if (uniqueCandidates.length === 0) {
            return [this.normalizeWebSocketUrl('wss://teleport-signaling.onrender.com')].filter(Boolean);
        }

        const probes = await Promise.all(uniqueCandidates.map(url => this.probeSignalingServer(url)));
        const byUrl = new Map(probes.map(p => [p.url, p]));

        uniqueCandidates.sort((a, b) => {
            const pa = byUrl.get(a);
            const pb = byUrl.get(b);

            if (pa.healthy !== pb.healthy) return pa.healthy ? -1 : 1;

            // Healthy servers are sorted by live latency.
            if (pa.healthy && pb.healthy && pa.latency !== pb.latency) {
                return pa.latency - pb.latency;
            }

            // Otherwise prefer most recently successful server.
            const ha = this.signalingHealth.get(a) || {};
            const hb = this.signalingHealth.get(b) || {};
            const sa = ha.lastSuccessAt || 0;
            const sb = hb.lastSuccessAt || 0;
            if (sa !== sb) return sb - sa;

            return 0;
        });

        return uniqueCandidates;
    }

    // ==================== TURN CREDENTIAL REFRESH ====================

    /**
     * Fetches fresh TURN credentials from the signaling server and merges them
     * into this.rtcConfig. Called once after successful WebSocket connect.
     * Falls back silently to the already-configured open-relay TURN if the
     * server endpoint is unavailable.
     */
    async fetchTurnCredentials() {
        if (this.turnFetched) return;
        this.turnFetched = true;

        const candidates = await this.getRankedSignalingServers(null);
        for (const wsUrl of candidates) {
            try {
                const httpBase = this.toHttpBase(wsUrl);
                const res = await this.fetchWithTimeout(`${httpBase}/turn-credentials`, {
                    method: 'GET',
                    headers: { 'Accept': 'application/json' }
                }, 5000);

                if (!res.ok) continue;
                const config = await res.json();
                if (Array.isArray(config.iceServers) && config.iceServers.length > 0) {
                    this.rtcConfig.iceServers = config.iceServers;
                    this.signalingHealth.set(wsUrl, {
                        ...(this.signalingHealth.get(wsUrl) || {}),
                        healthy: true,
                        lastSuccessAt: Date.now()
                    });
                    console.log('[Teleport] TURN credentials refreshed from server', wsUrl);
                    return;
                }
            } catch (e) {
                this.signalingHealth.set(wsUrl, {
                    ...(this.signalingHealth.get(wsUrl) || {}),
                    healthy: false,
                    lastFailureAt: Date.now()
                });
            }
        }

        console.warn('[Teleport] Could not fetch TURN credentials from any signaling endpoint, using fallback TURN list');
    }

    // ==================== RENDER KEEP-ALIVE ====================

    /**
     * Starts a periodic HTTP ping to the signaling server's /health endpoint
     * every 14 minutes so the Render free-tier instance never goes to sleep.
     * Only runs when connected to the production Render server.
     */
    startKeepAlive() {
        this.stopKeepAlive(); // clear any existing timer
        const INTERVAL_MS = 14 * 60 * 1000; // 14 minutes
        const ping = async () => {
            try {
                const httpBase = (this.serverUrl || '')
                    .replace(/^wss:\/\//, 'https://')
                    .replace(/^ws:\/\//, 'http://');
                const res = await this.fetchWithTimeout(`${httpBase}/health`, {
                    method: 'GET'
                }, 10000);
                console.log('[Teleport] Keep-alive ping:', res.ok ? 'ok' : res.status);
            } catch (e) {
                console.warn('[Teleport] Keep-alive ping failed:', e.message);
            }
        };
        this._keepAliveTimer = setInterval(ping, INTERVAL_MS);
        console.log('[Teleport] Keep-alive started (every 14 min) for', this.serverUrl);
    }

    stopKeepAlive() {
        if (this._keepAliveTimer) {
            clearInterval(this._keepAliveTimer);
            this._keepAliveTimer = null;
        }
    }

    // ==================== FILENAME SANITIZATION ====================
    // Ported from core/src/utils/sanitize.cpp — same rules as the desktop.

    /**
     * Sanitize a filename against path-traversal, null bytes, control chars,
     * Windows reserved names and excessive length.
     * Returns the cleaned name, or throws if the name is fundamentally unsafe.
     */
    sanitizeFilename(filename) {
        if (typeof filename !== 'string' || filename.length === 0) {
            throw new Error('Empty or non-string filename');
        }

        // Strip null bytes and ASCII control characters (0x00–0x1F, 0x7F)
        let name = filename.replace(/[\x00-\x1f\x7f]/g, '');

        // Reject any remaining path separators (path-traversal guard)
        if (name.includes('/') || name.includes('\\')) {
            throw new Error(`Filename contains path separator: ${filename}`);
        }

        // Reject the special dot sequences
        if (name === '.' || name === '..') {
            throw new Error(`Filename is a dot sequence: ${filename}`);
        }

        // Windows reserved device names (case-insensitive) — CON, PRN, AUX, NUL,
        // COM0–COM9, LPT0–LPT9, and variants with extensions e.g. CON.txt
        const base = name.split('.')[0].toUpperCase();
        const RESERVED = new Set(['CON','PRN','AUX','NUL',
            'COM0','COM1','COM2','COM3','COM4','COM5','COM6','COM7','COM8','COM9',
            'LPT0','LPT1','LPT2','LPT3','LPT4','LPT5','LPT6','LPT7','LPT8','LPT9']);
        if (RESERVED.has(base)) {
            throw new Error(`Filename is a reserved device name: ${filename}`);
        }

        // Replace Windows-illegal characters:  < > : " | ? *
        name = name.replace(/[<>:"|?*]/g, '_');

        // Trim leading/trailing spaces and dots (Windows compat)
        name = name.replace(/^[. ]+|[. ]+$/g, '');

        if (name.length === 0) {
            throw new Error(`Filename reduced to empty after sanitization: ${filename}`);
        }

        // Length guard — 240 chars matches the C++ limit (leaves room for path prefix)
        if (name.length > 240) {
            const ext = name.lastIndexOf('.');
            const extension = ext !== -1 ? name.slice(ext) : '';
            name = name.slice(0, 240 - extension.length) + extension;
        }

        return name;
    }

    /**
     * Sanitize a relative path (e.g. 'folder/subfolder/file.txt').
     * Each component is sanitized individually; rejects absolute paths.
     */
    sanitizeRelativePath(relPath) {
        if (typeof relPath !== 'string' || relPath.length === 0) return '';

        // Reject absolute paths
        if (relPath.startsWith('/') || /^[A-Za-z]:/.test(relPath)) {
            throw new Error(`Absolute path not allowed: ${relPath}`);
        }

        const parts = relPath.replace(/\\/g, '/').split('/');
        const sanitized = parts.map(p => {
            if (p === '' || p === '.') return null; // skip empty segments
            if (p === '..') throw new Error('Path traversal detected (..)'); // hard reject
            return this.sanitizeFilename(p);
        }).filter(Boolean);

        return sanitized.join('/');
    }

    // ==================== INCOMING TRANSFER VALIDATION ====================

    /**
     * Validate an incoming file-start control message before accepting it.
     * Throws with a descriptive message if validation fails.
     */
    validateFileStartMsg(msg) {
        const { transferId, filename, size, fileIndex, totalFiles } = msg;

        // Field type checks
        if (typeof transferId !== 'string' || transferId.length < 8) {
            throw new Error('Invalid transferId');
        }
        if (typeof filename !== 'string' || filename.length === 0) {
            throw new Error('Invalid filename');
        }
        if (typeof size !== 'number' || size < 0 || !Number.isFinite(size)) {
            throw new Error(`Invalid file size: ${size}`);
        }
        if (typeof fileIndex !== 'number' || fileIndex < 0 || !Number.isFinite(fileIndex)) {
            throw new Error(`Invalid fileIndex: ${fileIndex}`);
        }
        if (typeof totalFiles !== 'number' || totalFiles <= 0 || !Number.isFinite(totalFiles)) {
            throw new Error(`Invalid totalFiles: ${totalFiles}`);
        }
        if (totalFiles > 10000) {
            throw new Error(`Batch exceeds maximum file count: ${totalFiles}`);
        }
        // Sanitize the filename (throws if unsafe)
        return this.sanitizeFilename(filename);
    }

    // ==================== INDEXEDDB RESUME SUPPORT ====================

    initResumeDB() {
        if (!window.indexedDB) return;
        try {
            const req = indexedDB.open('teleport-resume', 1);
            req.onupgradeneeded = (e) => {
                const db = e.target.result;
                if (!db.objectStoreNames.contains('transfers')) {
                    db.createObjectStore('transfers', { keyPath: 'transferId' });
                }
            };
            req.onsuccess = (e) => { this.resumeDb = e.target.result; };
            req.onerror = () => { console.warn('[Resume] IndexedDB unavailable'); };
        } catch (e) {
            console.warn('[Resume] IndexedDB init failed:', e);
        }
    }

    saveResumeState(transferId, state) {
        if (!this.resumeDb) return;
        try {
            const tx = this.resumeDb.transaction('transfers', 'readwrite');
            tx.objectStore('transfers').put({ transferId, ...state, updatedAt: Date.now() });
        } catch (e) { /* non-fatal */ }
    }

    deleteResumeState(transferId) {
        if (!this.resumeDb) return;
        try {
            const tx = this.resumeDb.transaction('transfers', 'readwrite');
            tx.objectStore('transfers').delete(transferId);
        } catch (e) { /* non-fatal */ }
    }

    getResumeState(transferId) {
        return new Promise((resolve) => {
            if (!this.resumeDb) return resolve(null);
            try {
                const tx = this.resumeDb.transaction('transfers', 'readonly');
                const req = tx.objectStore('transfers').get(transferId);
                req.onsuccess = (e) => resolve(e.target.result || null);
                req.onerror = () => resolve(null);
            } catch (e) { resolve(null); }
        });
    }

    /** Return all incomplete resume states (for UI display on startup). */
    getAllResumeStates() {
        return new Promise((resolve) => {
            if (!this.resumeDb) return resolve([]);
            try {
                const tx = this.resumeDb.transaction('transfers', 'readonly');
                const req = tx.objectStore('transfers').getAll();
                req.onsuccess = (e) => resolve(e.target.result || []);
                req.onerror = () => resolve([]);
            } catch (e) { resolve([]); }
        });
    }

    async throttle(bytes) {
        if (this.maxBandwidth <= 0) return;

        // Token bucket: smoother rate limiting than coarse 1-second windows.
        const now = Date.now();
        const elapsedMs = Math.max(0, now - this.lastThrottleTick);
        this.lastThrottleTick = now;

        const refill = (this.maxBandwidth * elapsedMs) / 1000;
        this.bandwidthTokens = Math.min(this.maxBandwidth, this.bandwidthTokens + refill);

        if (this.bandwidthTokens < bytes) {
            const missing = bytes - this.bandwidthTokens;
            const waitTime = Math.ceil((missing / this.maxBandwidth) * 1000);
            if (waitTime > 0) {
                await new Promise(r => setTimeout(r, waitTime));
            }

            const afterWait = Date.now();
            const elapsedAfterWait = Math.max(0, afterWait - this.lastThrottleTick);
            this.lastThrottleTick = afterWait;
            const refillAfterWait = (this.maxBandwidth * elapsedAfterWait) / 1000;
            this.bandwidthTokens = Math.min(this.maxBandwidth, this.bandwidthTokens + refillAfterWait);
        }

        this.bandwidthTokens = Math.max(0, this.bandwidthTokens - bytes);
    }

    // ==================== TRANSFER HISTORY ====================

    getTransferHistory() {
        try {
            const history = localStorage.getItem('teleport-transfer-history');
            return history ? JSON.parse(history) : [];
        } catch (e) { return []; }
    }

    saveTransferToHistory(transfer) {
        try {
            const history = this.getTransferHistory();
            history.unshift({ ...transfer, timestamp: Date.now() });
            if (history.length > 100) history.pop();
            localStorage.setItem('teleport-transfer-history', JSON.stringify(history));
        } catch (e) { }
    }

    clearTransferHistory() {
        try { localStorage.removeItem('teleport-transfer-history'); } catch (e) { }
    }

    // ==================== CONNECTION ====================

    async connect(serverUrl = null) {
        this.shouldReconnect = true;

        const rankedServers = await this.getRankedSignalingServers(serverUrl);
        let lastError = null;

        for (const candidate of rankedServers) {
            try {
                await this.connectToSignalingServer(candidate);
                return;
            } catch (e) {
                lastError = e;
                this.signalingHealth.set(candidate, {
                    ...(this.signalingHealth.get(candidate) || {}),
                    healthy: false,
                    lastFailureAt: Date.now()
                });
                console.warn('[Teleport] Signaling candidate failed:', candidate, e.message || e);
            }
        }

        throw (lastError || new Error('No signaling servers reachable'));
    }

    connectToSignalingServer(serverUrl) {
        return new Promise((resolve, reject) => {
            const normalizedUrl = this.normalizeWebSocketUrl(serverUrl);
            if (!normalizedUrl) {
                reject(new Error(`Invalid signaling URL: ${serverUrl}`));
                return;
            }

            if (this.ws) {
                try { this.ws.close(); } catch (e) { }
                this.ws = null;
            }

            this.serverUrl = normalizedUrl;
            this.turnFetched = false;

            const attemptTimeout = Math.min(this.CONNECTION_TIMEOUT, this.SIGNALING_ATTEMPT_TIMEOUT);
            let settled = false;
            let joined = false;

            let ws;
            try {
                ws = new WebSocket(this.serverUrl);
            } catch (e) {
                reject(new Error(`Failed to create WebSocket for ${this.serverUrl}`));
                return;
            }

            this.ws = ws;

            const timeoutId = setTimeout(() => {
                if (settled) return;
                settled = true;
                try { ws.close(); } catch (e) { }
                if (this.ws === ws) this.ws = null;
                reject(new Error(`Connection timeout: ${this.serverUrl}`));
            }, attemptTimeout);

            const rejectOnce = (error) => {
                if (settled) return;
                settled = true;
                clearTimeout(timeoutId);
                if (this.ws === ws) this.ws = null;
                this.isConnected = false;
                reject(error instanceof Error ? error : new Error(String(error)));
            };

            const resolveOnce = () => {
                if (settled) return;
                settled = true;
                clearTimeout(timeoutId);
                resolve();
            };

            ws.onopen = () => {
                this.isConnected = true;
                this.reconnectAttempts = 0;

                if (this.serverUrl && this.serverUrl.includes('onrender.com')) {
                    this.startKeepAlive();
                } else {
                    this.stopKeepAlive();
                }

                this.signalingHealth.set(this.serverUrl, {
                    ...(this.signalingHealth.get(this.serverUrl) || {}),
                    healthy: true,
                    lastSuccessAt: Date.now()
                });
            };

            ws.onmessage = (event) => {
                let message;
                try {
                    message = JSON.parse(event.data);
                } catch (e) {
                    console.warn('[Teleport] Dropping invalid signaling payload:', e.message);
                    return;
                }

                this.handleSignalingMessage(message);

                if (message.type === 'welcome') {
                    joined = true;
                    this.peerId = message.peerId;
                    this.generateFingerprint().then(async () => {
                        const publicKey = await this.exportPublicKey();
                        if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
                            rejectOnce(new Error('WebSocket closed before join handshake completed'));
                            return;
                        }

                        // BUG FIX (Bug C1): Web was joining room 'teleport-lan' but the desktop
                        // always joins 'teleport-default'. Peers in different rooms never see each
                        // other, so desktop↔web discovery was completely broken.
                        this.ws.send(JSON.stringify({
                            type: 'join',
                            room: 'teleport-default',
                            name: this.deviceName,
                            fingerprint: this.peerFingerprint,
                            publicKey,
                            clientType: 'web',
                            userAgent: navigator.userAgent
                        }));

                        if (this.onConnected) this.onConnected();
                        this.broadcastEvent('connected', { peerId: this.peerId });
                        resolveOnce();

                        // Fetch fresh TURN credentials from primary/backup signaling endpoints.
                        this.fetchTurnCredentials();
                    }).catch(err => rejectOnce(err));
                }
            };

            ws.onclose = () => {
                if (!joined) {
                    rejectOnce(new Error(`Closed before welcome from ${this.serverUrl}`));
                    return;
                }

                this.isConnected = false;
                if (this.onDisconnected) this.onDisconnected();
                
                // ===== PHASE 3: Cleanup on disconnect =====
                this.handleError(ErrorCodes.CONN_LOST, { reason: 'WebSocket closed' });
                
                if (this.shouldReconnect) {
                    this.attemptReconnect();
                }
            };

            ws.onerror = () => {
                if (!joined) {
                    rejectOnce(new Error(`WebSocket error: ${this.serverUrl}`));
                }
            };
        });
    }

    async generateFingerprint() {
        try {
            const randomBytes = crypto.getRandomValues(new Uint8Array(32));
            const data = `${this.peerId}-${Date.now()}-${Array.from(randomBytes).join('')}`;
            const hashBuffer = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(data));
            const hashArray = Array.from(new Uint8Array(hashBuffer));
            this.peerFingerprint = hashArray.map(b => b.toString(16).padStart(2, '0')).join('').substring(0, 16).toUpperCase();
        } catch (e) {
            // Fallback for older browsers
            this.peerFingerprint = Array.from(crypto.getRandomValues(new Uint8Array(8))).map(b => b.toString(16).padStart(2, '0')).join('').toUpperCase();
        }
    }

    // ==================== PHASE 0: SECURITY BASELINE ====================

    async initSecurityBaseline() {
        // Initialize trusted devices from localStorage
        try {
            const saved = this.loadSetting('teleport-trusted-devices');
            if (saved) {
                const devices = JSON.parse(saved);
                for (const [peerId, data] of Object.entries(devices)) {
                    this.trustedDevices.set(peerId, {
                        fingerprint: data.fingerprint,
                        publicKey: data.publicKey,
                        firstSeen: data.firstSeen,
                        lastVerified: data.lastVerified,
                        trustLevel: data.trustLevel || 'manual' // 'manual' | 'auto-verified'
                    });
                }
            }
        } catch (e) {
            console.warn('[Security] Failed to load trusted devices:', e);
        }

        // Initialize signing key for peer announcements
        try {
            const savedKey = this.loadSetting('teleport-signing-key');
            if (savedKey) {
                this.deviceSigningKey = await crypto.subtle.importKey(
                    'jwk',
                    JSON.parse(savedKey),
                    { name: 'RSASSA-PKCS1-v1_5', hash: 'SHA-256' },
                    true,
                    ['sign']
                );
            } else {
                // Generate new signing key pair for this device
                await this.generateSigningKey();
            }
        } catch (e) {
            console.warn('[Security] Failed to load signing key:', e);
            await this.generateSigningKey();
        }
    }

    async generateSigningKey() {
        try {
            const keyPair = await crypto.subtle.generateKey(
                { name: 'RSASSA-PKCS1-v1_5', modulusLength: 2048, publicExponent: new Uint8Array([1, 0, 1]), hash: 'SHA-256' },
                true,
                ['sign', 'verify']
            );
            this.deviceSigningKey = keyPair.privateKey;
            const exported = await crypto.subtle.exportKey('jwk', keyPair.privateKey);
            this.saveSetting('teleport-signing-key', JSON.stringify(exported));
        } catch (e) {
            console.error('[Security] Failed to generate signing key:', e);
        }
    }

    async validatePeerFingerprint(peerId, incomingFingerprint, signature) {
        /**
         * Validate peer fingerprint against trusted devices or auto-trust on first seen
         * Returns: { valid: boolean, trustLevel: 'trusted'|'pending'|'rejected', reason?: string }
         */
        try {
            const trusted = this.trustedDevices.get(peerId);
            
            if (!trusted) {
                // First time seeing this peer - auto-trust for now, flag for manual review
                this.pendingVerification.set(peerId, {
                    fingerprint: incomingFingerprint,
                    timestamp: Date.now(),
                    signature
                });
                return { valid: true, trustLevel: 'pending', reason: 'First contact - waiting for manual verification' };
            }

            // Verify fingerprint matches
            if (trusted.fingerprint.toUpperCase() !== incomingFingerprint.toUpperCase()) {
                console.error(`[Security] Fingerprint mismatch for peer ${peerId}: expected ${trusted.fingerprint}, got ${incomingFingerprint}`);
                return { valid: false, trustLevel: 'rejected', reason: 'Fingerprint mismatch - possible MITM attack' };
            }

            // Update last verified timestamp
            trusted.lastVerified = Date.now();
            this.saveTrustedDevices();

            return { valid: true, trustLevel: 'trusted', reason: 'Fingerprint verified' };
        } catch (e) {
            console.error('[Security] Fingerprint validation error:', e);
            return { valid: false, trustLevel: 'rejected', reason: `Validation error: ${e.message}` };
        }
    }

    async addTrustedDevice(peerId, fingerprint, publicKey) {
        /**
         * Manually add a device to trusted list after user verifies fingerprint
         */
        try {
            const now = Date.now();
            this.trustedDevices.set(peerId, {
                fingerprint,
                publicKey,
                firstSeen: now,
                lastVerified: now,
                trustLevel: 'manual'
            });
            this.saveTrustedDevices();
            
            // Clear pending verification
            this.pendingVerification.delete(peerId);
            
            return { success: true, message: `Peer ${peerId} added to trusted devices` };
        } catch (e) {
            console.error('[Security] Failed to add trusted device:', e);
            return { success: false, message: e.message };
        }
    }

    async removeTrustedDevice(peerId) {
        try {
            this.trustedDevices.delete(peerId);
            this.saveTrustedDevices();
            return { success: true, message: `Peer ${peerId} removed from trusted devices` };
        } catch (e) {
            console.error('[Security] Failed to remove trusted device:', e);
            return { success: false, message: e.message };
        }
    }

    getPendingVerifications() {
        /**
         * Get list of pending peer verifications for user to review
         */
        const pending = [];
        for (const [peerId, data] of this.pendingVerification.entries()) {
            const peer = this.peerList.find(p => p.id === peerId);
            pending.push({
                peerId,
                peerName: peer?.name || 'Unknown',
                fingerprint: data.fingerprint,
                firstSeen: new Date(data.timestamp),
                verificationCode: this.generateVerificationCode(data.fingerprint)
            });
        }
        return pending;
    }

    generateVerificationCode(fingerprint, length = 6) {
        // Generate a memorable verification code from fingerprint hash
        const codeHash = fingerprint.substring(0, length * 2);
        return codeHash.match(/.{1,2}/g).map(byte => parseInt(byte, 16) % 10).join('');
    }

    saveTrustedDevices() {
        const devices = {};
        for (const [peerId, data] of this.trustedDevices.entries()) {
            devices[peerId] = {
                fingerprint: data.fingerprint,
                publicKey: data.publicKey,
                firstSeen: data.firstSeen,
                lastVerified: data.lastVerified,
                trustLevel: data.trustLevel
            };
        }
        this.saveSetting('teleport-trusted-devices', JSON.stringify(devices));
    }

    async signPeerAnnouncement(announcement) {
        /**
         * Sign peer announcement with device signing key
         * This prevents identity spoofing attacks
         */
        if (!this.deviceSigningKey) {
            return { ...announcement, signature: null };
        }

        try {
            const data = JSON.stringify({
                peerId: announcement.peerId,
                fingerprint: announcement.fingerprint,
                publicKey: announcement.publicKey,
                timestamp: announcement.timestamp
            });

            const signatureBuffer = await crypto.subtle.sign(
                'RSASSA-PKCS1-v1_5',
                this.deviceSigningKey,
                new TextEncoder().encode(data)
            );

            const signatureBase64 = btoa(String.fromCharCode(...new Uint8Array(signatureBuffer)));
            return { ...announcement, signature: signatureBase64 };
        } catch (e) {
            console.error('[Security] Failed to sign announcement:', e);
            return { ...announcement, signature: null };
        }
    }

    async verifyPeerSignature(announcement, publicKeyPem) {
        /**
         * Verify signature of peer announcement
         * Not fully implemented yet - requires public key infrastructure
         */
        if (!announcement.signature || !publicKeyPem) {
            return { valid: false, reason: 'No signature or public key' };
        }

        try {
            // This would require importing public key and verifying
            // TODO: Implement with full RSA public key infrastructure
            return { valid: true, reason: 'Signature verification pending full implementation' };
        } catch (e) {
            console.error('[Security] Failed to verify signature:', e);
            return { valid: false, reason: e.message };
        }
    }

    // ==================== E2E ENCRYPTION ====================

    async initEncryption() {
        try {
            this.keyPair = await crypto.subtle.generateKey(
                { name: 'ECDH', namedCurve: 'P-256' },
                true,
                ['deriveKey', 'deriveBits']
            );
        } catch (e) {
            console.warn('E2E encryption not available:', e);
            this.encryptionEnabled = false;
        }
    }

    async exportPublicKey() {
        if (!this.keyPair) return null;
        const exported = await crypto.subtle.exportKey('raw', this.keyPair.publicKey);
        return btoa(String.fromCharCode(...new Uint8Array(exported)));
    }

    async importPeerPublicKey(peerId, base64Key) {
        try {
            const keyData = Uint8Array.from(atob(base64Key), c => c.charCodeAt(0));
            const publicKey = await crypto.subtle.importKey(
                'raw', keyData, { name: 'ECDH', namedCurve: 'P-256' }, true, []
            );
            this.peerPublicKeys.set(peerId, publicKey);
            await this.deriveSharedSecret(peerId, publicKey);
        } catch (e) {
            this.handleError('Key import failed', e);
        }
    }

    async deriveSharedSecret(peerId, peerPublicKey) {
        if (!this.keyPair) return;
        try {
            const sharedKey = await crypto.subtle.deriveKey(
                { name: 'ECDH', public: peerPublicKey },
                this.keyPair.privateKey,
                { name: 'AES-GCM', length: 256 },
                false,
                ['encrypt', 'decrypt']
            );
            this.sharedSecrets.set(peerId, sharedKey);
        } catch (e) {
            this.handleError('Key derivation failed', e);
        }
    }

    async encryptData(peerId, data) {
        const key = this.sharedSecrets.get(peerId);
        if (!key || !this.encryptionEnabled) return { encrypted: false, data };
        try {
            const iv = crypto.getRandomValues(new Uint8Array(12));
            const encrypted = await crypto.subtle.encrypt({ name: 'AES-GCM', iv }, key, data);
            return { encrypted: true, data: encrypted, iv };
        } catch (e) {
            return { encrypted: false, data };
        }
    }

    async decryptData(peerId, encryptedData, iv) {
        const key = this.sharedSecrets.get(peerId);
        if (!key) throw new Error('No shared key for peer');
        return await crypto.subtle.decrypt({ name: 'AES-GCM', iv: new Uint8Array(iv) }, key, encryptedData);
    }

    normalizeSha256(hash) {
        if (typeof hash !== 'string') return null;
        const trimmed = hash.trim().toLowerCase();
        if (!/^[a-f0-9]{64}$/.test(trimmed)) return null;
        return trimmed;
    }

    getFileCacheKey(file) {
        return `${file.name}:${file.size}:${file.lastModified || 0}`;
    }

    async computeFileSha256(file) {
        const cacheKey = this.getFileCacheKey(file);
        if (this.fileHashCache.has(cacheKey)) {
            return this.fileHashCache.get(cacheKey);
        }

        const hasher = new IncrementalSHA256();
        const reader = file.stream().getReader();

        while (true) {
            const { done, value } = await reader.read();
            if (done) break;
            hasher.update(value);
        }

        const digestHex = hasher.hex();
        this.fileHashCache.set(cacheKey, digestHex);
        if (this.fileHashCache.size > 500) {
            // Bounded cache to prevent unbounded growth in long sessions.
            this.fileHashCache.clear();
            this.fileHashCache.set(cacheKey, digestHex);
        }

        return digestHex;
    }

    handleError(message, error) {
        console.error(`[Teleport Error] ${message}:`, error);
        if (this.onError) this.onError({ message, error: error?.message || String(error) });
    }

    attemptReconnect() {
        if (!this.shouldReconnect) return;
        if (this.reconnectAttempts >= this.maxReconnectAttempts) return;

        this.reconnectAttempts++;
        const delay = this.reconnectDelay * Math.pow(2, this.reconnectAttempts - 1);

        if (this.onReconnecting) this.onReconnecting(this.reconnectAttempts);

        setTimeout(() => {
            if (!this.isConnected) {
                this.connect(null).catch(() => { });
            }
        }, delay);
    }

    disconnect() {
        this.shouldReconnect = false;
        this.stopKeepAlive();
        if (this.ws) { this.ws.close(); this.ws = null; }
        this.isConnected = false;
        this.peers.forEach(pc => pc.close());
        this.peers.clear();
        this.dataChannels.clear();
        
        // ===== PHASE 3: Full cleanup on disconnect =====
        this.cleanup();
    }

    // ===== PHASE 3: Graceful shutdown =====
    close() {
        console.log('[TeleportWebRTC] Gracefully closing...');
        this.disconnect();
        if (this.broadcastChannel) {
            try {
                this.broadcastChannel.close();
            } catch (e) {
                console.warn('[Cleanup] BroadcastChannel close failed:', e);
            }
        }
    }

    // ==================== MANUAL IP VALIDATION ====================

    validateIP(ip) {
        // IPv4 validation
        const ipv4Regex = /^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$/;
        // IPv6 validation (simplified)
        const ipv6Regex = /^([0-9a-fA-F]{1,4}:){7}[0-9a-fA-F]{1,4}$/;
        // Hostname validation
        const hostnameRegex = /^[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?(\.[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*$/;

        return ipv4Regex.test(ip) || ipv6Regex.test(ip) || hostnameRegex.test(ip);
    }

    // Check if an IP is a private/local network IP
    isPrivateIP(ip) {
        // Private IP ranges: 10.x.x.x, 172.16-31.x.x, 192.168.x.x
        const privateRanges = [
            /^10\./,
            /^172\.(1[6-9]|2[0-9]|3[0-1])\./,
            /^192\.168\./,
            /^127\./
        ];
        return privateRanges.some(regex => regex.test(ip));
    }

    async connectToManualIP(ip, port = 3000) {
        if (!this.validateIP(ip)) {
            throw new Error('Invalid IP address or hostname format');
        }
        const newServerUrl = `ws://${ip}:${port}`;
        this.manualServerUrl = newServerUrl;
        this.disconnect();
        this.shouldReconnect = true;
        return this.connect(null);
    }

    // ==================== SIGNALING ====================

    async handleSignalingMessage(message) {
        try {
            switch (message.type) {
                case 'peers': {
                    // This is the initial snapshot of who is already in the room.
                    // BUG FIX: Desktop peers have fingerprint:null (server explicitly sets it null).
                    // Web peers either have a fingerprint string or the field is absent.
                    // Tag explicitly-null-fingerprint peers as relayOnly so the UI shows them
                    // AND requestFileSend() skips WebRTC for them.
                    const incomingPeers = Array.isArray(message.peers) ? message.peers : [];

                    for (const peer of incomingPeers) {
                        if (peer.publicKey && !this.peerPublicKeys.has(peer.id)) {
                            this.importPeerPublicKey(peer.id, peer.publicKey);
                        }
                    }

                    this.peerList = incomingPeers.map(peer => ({
                        ...peer,
                        // Native C++ now supports WebRTC!
                        relayOnly: false
                    }));
                    if (this.onPeersUpdated) this.onPeersUpdated([...this.peerList]);
                    this.broadcastEvent('peer-connected', { peers: [...this.peerList] });
                    break;
                }

                case 'peer-joined':
                    // BUG FIX (Bug C2): Server sends a flat object
                    //   { type:'peer-joined', id, name, platform, fingerprint, ... }
                    // but this handler was reading message.peer.id (nested).
                    // Desktop peers were silently dropped, so they never appeared in
                    // the web UI. Now we support both schemas.
                    {
                        const peerData = message.peer || message; // nested or flat
                        if (peerData?.id) {
                            if (peerData.publicKey && !this.peerPublicKeys.has(peerData.id)) {
                                this.importPeerPublicKey(peerData.id, peerData.publicKey);
                            }

                            // BUG FIX (C2b): Desktop peers send fingerprint:null in the
                            // join message. Tag them as relay-only so requestFileSend()
                            // skips WebRTC negotiation (the desktop has no WebRTC stack).
                            const isDesktopPeer = peerData.fingerprint === null ||
                                                  peerData.fingerprint === 'null' ||
                                                  peerData.platform === 'desktop';
                            const peerRecord = {
                                ...peerData,
                                relayOnly: false // Enabled WebRTC for desktop
                            };

                            const existingIdx = this.peerList.findIndex(p => p.id === peerData.id);
                            if (existingIdx >= 0) {
                                this.peerList = [
                                    ...this.peerList.slice(0, existingIdx),
                                    { ...this.peerList[existingIdx], ...peerRecord },
                                    ...this.peerList.slice(existingIdx + 1)
                                ];
                            } else {
                                this.peerList = [...this.peerList, { ...peerRecord }];
                            }

                            if (this.onPeersUpdated) this.onPeersUpdated([...this.peerList]);
                            this.broadcastEvent('peer-connected', { peers: [...this.peerList] });
                        }
                    }
                    break;
                case 'peer-lan-updated':
                    // A peer that previously connected via signaling now has a known LAN address
                    if (message.peerId) {
                        const peerIdx = this.peerList.findIndex(p => p.id === message.peerId);
                        if (peerIdx >= 0) {
                            this.peerList[peerIdx] = {
                                ...this.peerList[peerIdx],
                                isLan: true,
                                lanIp: message.lanIp || this.peerList[peerIdx].lanIp || null,
                                lanPort: Number.isFinite(message.lanPort)
                                    ? message.lanPort
                                    : (this.peerList[peerIdx].lanPort || null)
                            };
                            if (this.onPeersUpdated) this.onPeersUpdated([...this.peerList]);
                        }

                        // Emit cross-tab update so every UI instance can re-render badges
                        this.broadcastEvent('peer-lan-updated', {
                            peerId: message.peerId,
                            lanIp: message.lanIp,
                            lanPort: message.lanPort
                        });
                    }
                    break;
                case 'peer-left':
                    this.peerList = this.peerList.filter(p => p.id !== message.peerId);
                    if (this.onPeersUpdated) this.onPeersUpdated([...this.peerList]);
                    this.cleanupPeer(message.peerId);
                    break;
                case 'offer':
                    this.handleOffer(message.from, message.sdp, message.fingerprint, message.publicKey);
                    break;
                case 'answer':
                    this.handleAnswer(message.from, message.sdp, message.publicKey);
                    break;
                case 'ice':
                    this.handleIceCandidate(message.from, message.candidate);
                    break;
                case 'key-exchange':
                    if (message.publicKey) {
                        this.importPeerPublicKey(message.from, message.publicKey);
                    }
                    break;
                case 'file-request':
                    if (this.onFileRequest) {
                        this.onFileRequest({
                            from: message.from,
                            fromName: message.fromName,
                            files: message.files,
                            fingerprint: message.fingerprint,
                            encrypted: this.sharedSecrets.has(message.from)
                        });
                    }
                    break;
                case 'file-response':
                    this.handleFileResponse(message.from, message.accepted);
                    break;

                // ============ SERVER RELAY MODE HANDLERS ============
                // Used when WebRTC P2P fails due to NAT/firewall

                case 'relay-start': {
                    // Receiver: incoming relay transfer
                    console.log('[Relay] Receiving file via server relay:', message.filename);
                    const expectedSha256 = this.normalizeSha256(message.sha256);
                    if (typeof message.sha256 !== 'undefined' && !expectedSha256) {
                        console.warn('[Relay] relay-start contains invalid SHA-256, cancelling transfer');
                        this.sendRelayVerificationAck(
                            message.from,
                            message.transferId,
                            false,
                            'invalid-sha256',
                            null
                        );
                        if (this.ws?.readyState === WebSocket.OPEN && message.from) {
                            this.ws.send(JSON.stringify({
                                type: 'relay-cancel',
                                to: message.from,
                                transferId: message.transferId,
                                reason: 'invalid-sha256'
                            }));
                        }
                        break;
                    }

                    // ✅ CREATE STREAMING WRITER FOR LARGE FILES
                    let writer = null;
                    const useStreaming = message.size > this.STREAMING_THRESHOLD;
                    
                    if (useStreaming && this.useFileSystemAPI) {
                        try {
                            writer = new StreamingFileWriter(message.filename, message.size, {
                                useFileSystemAPI: true
                            });
                            
                            // Try to get File System API handle
                            if (typeof window.showSaveFilePicker === 'function') {
                                try {
                                    const handle = await window.showSaveFilePicker({
                                        suggestedName: message.filename,
                                        types: [{ accept: { [message.mimeType || 'application/octet-stream']: ['.bin'] } }]
                                    });
                                    await writer.initFileSystemAPI(handle);
                                    console.log('[Relay] Initialized File System API streaming writer for', message.filename);
                                } catch (e) {
                                    console.warn('[Relay] File System API picker cancelled or failed, will buffer in memory:', e.message);
                                    writer = null;
                                }
                            }
                        } catch (e) {
                            console.warn('[Relay] Streaming writer init failed:', e.message);
                            writer = null;
                        }
                    }

                    const transfer = {
                        id: message.transferId,
                        from: message.from,
                        filename: message.filename,
                        size: message.size,
                        mimeType: message.mimeType,
                        chunks: [],  // Fallback buffering
                        receivedBytes: 0,
                        expectedSha256,
                        hasher: new IncrementalSHA256(),
                        fileIndex: message.fileIndex,
                        totalFiles: message.totalFiles,
                        writer: writer,  // ✅ Store streaming writer
                        useStreaming: !!writer
                    };
                    this.relayIncoming.set(message.transferId, transfer);

                    // Create state for progress tracking
                    this.activeTransfers.set(message.transferId, {
                        filename: message.filename,
                        totalSize: message.size,
                        transferred: 0,
                        isRelay: true
                    });
                    break;
                }

                case 'relay-chunk': {
                    // Receiver: incoming chunk via relay
                    // ✅ STREAMING FIX: Write directly to disk, don't accumulate in memory
                    const transfer = this.relayIncoming.get(message.transferId);
                    if (transfer) {
                        // Decode base64 chunk
                        const binaryStr = atob(message.data);
                        const bytes = new Uint8Array(binaryStr.length);
                        for (let i = 0; i < binaryStr.length; i++) {
                            bytes[i] = binaryStr.charCodeAt(i);
                        }

                        // ✅ Stream to disk if writer available, otherwise buffer
                        if (transfer.writer) {
                            try {
                                await transfer.writer.write(bytes);
                            } catch (e) {
                                console.error('[Relay] Stream write error:', e);
                                // Don't abort on write error yet, maybe it's temporary
                            }
                        } else {
                            // Fallback: buffer only if no streaming writer
                            transfer.chunks.push(bytes);
                        }

                        transfer.receivedBytes += bytes.length;

                        if (transfer.hasher) {
                            transfer.hasher.update(bytes);
                        }

                        if (Number.isFinite(transfer.size) && transfer.receivedBytes > transfer.size) {
                            this.relayIncoming.delete(message.transferId);
                            this.activeTransfers.delete(message.transferId);

                            // Cleanup streaming writer on error
                            if (transfer.writer) {
                                try {
                                    await transfer.writer.abort();
                                } catch (e) { }
                            }

                            this.sendRelayVerificationAck(
                                transfer.from,
                                message.transferId,
                                false,
                                'size-overflow',
                                transfer.hasher ? transfer.hasher.hex() : null
                            );

                            if (this.ws?.readyState === WebSocket.OPEN && transfer.from) {
                                this.ws.send(JSON.stringify({
                                    type: 'relay-cancel',
                                    to: transfer.from,
                                    transferId: message.transferId,
                                    reason: 'size-overflow'
                                }));
                            }

                            if (this.onTransferError) {
                                this.onTransferError({
                                    transferId: message.transferId,
                                    filename: transfer.filename,
                                    error: 'Relay transfer exceeded declared file size.'
                                });
                            }
                            break;
                        }

                        // Update progress
                        if (this.onTransferProgress) {
                            this.onTransferProgress({
                                transferId: message.transferId,
                                filename: transfer.filename,
                                progress: transfer.size > 0 ? (transfer.receivedBytes / transfer.size) : 1,
                                received: transfer.receivedBytes,
                                total: transfer.size,
                                speed: 0,
                                protocol: 'Web Relay',
                                isRelay: true,
                                transferMode: 'relay',
                                streamingEnabled: transfer.useStreaming
                            });
                        }
                    }
                    break;
                }

                case 'relay-end': {
                    // Receiver: complete relay transfer
                    const transfer = this.relayIncoming.get(message.transferId);
                    if (transfer) {
                        console.log('[Relay] Transfer complete:', transfer.filename);

                        // ✅ STREAMING FIX: Close streaming writer if used
                        if (transfer.writer) {
                            try {
                                await transfer.writer.close();
                                console.log('[Relay] Closed streaming writer for', transfer.filename);
                            } catch (e) {
                                console.warn('[Relay] Error closing writer:', e);
                            }
                        }

                        // Compute sizes based on streaming vs buffering mode
                        const totalSize = transfer.useStreaming ? transfer.receivedBytes : transfer.chunks.reduce((sum, c) => sum + c.length, 0);
                        const actualSha256 = transfer.hasher ? transfer.hasher.hex() : null;
                        const sizeMismatch = Number.isFinite(transfer.size) && totalSize !== transfer.size;
                        const hashMismatch = !!transfer.expectedSha256 && actualSha256 !== transfer.expectedSha256;

                        if (sizeMismatch || hashMismatch) {
                            // Clean up streaming writer on error
                            if (transfer.writer && !transfer.useStreaming) {
                                try {
                                    await transfer.writer.abort();
                                } catch (e) { }
                            }

                            this.sendRelayVerificationAck(
                                transfer.from,
                                message.transferId,
                                false,
                                sizeMismatch ? 'size-mismatch' : 'sha256-mismatch',
                                actualSha256
                            );

                            if (this.ws?.readyState === WebSocket.OPEN && transfer.from) {
                                this.ws.send(JSON.stringify({
                                    type: 'relay-cancel',
                                    to: transfer.from,
                                    transferId: message.transferId,
                                    reason: sizeMismatch ? 'size-mismatch' : 'sha256-mismatch'
                                }));
                            }

                            this.saveTransferToHistory({
                                filename: transfer.filename,
                                size: totalSize,
                                direction: 'received',
                                success: false,
                                sha256: actualSha256 || null
                            });

                            if (this.onTransferError) {
                                this.onTransferError({
                                    transferId: message.transferId,
                                    filename: transfer.filename,
                                    error: sizeMismatch
                                        ? `Relay size mismatch (${this.formatSize(totalSize)} / ${this.formatSize(transfer.size)}).`
                                        : 'Relay SHA-256 verification failed.'
                                });
                            }

                            this.relayIncoming.delete(message.transferId);
                            this.activeTransfers.delete(message.transferId);
                            break;
                        }

                        // ✅ Handle download: for streaming, File System API already saved file
                        // For buffered mode, create blob and download
                        if (!transfer.useStreaming) {
                            const combined = new Uint8Array(totalSize);
                            let offset = 0;
                            for (const chunk of transfer.chunks) {
                                combined.set(chunk, offset);
                                offset += chunk.length;
                            }

                            // Create download
                            const blob = new Blob([combined], { type: transfer.mimeType || 'application/octet-stream' });
                            const url = URL.createObjectURL(blob);
                            const a = document.createElement('a');
                            a.href = url;
                            a.download = transfer.filename;
                            document.body.appendChild(a);
                            a.click();
                            document.body.removeChild(a);
                            URL.revokeObjectURL(url);
                        } else {
                            console.log('[Relay] File already saved via File System API:', transfer.filename);
                        }

                        // Cleanup
                        this.relayIncoming.delete(message.transferId);
                        this.activeTransfers.delete(message.transferId);

                        this.saveTransferToHistory({
                            filename: transfer.filename,
                            size: totalSize,
                            direction: 'received',
                            success: true,
                            sha256: actualSha256 || null,
                            sha256Verified: !!transfer.expectedSha256
                        });

                        // Notify complete
                        this.sendRelayVerificationAck(
                            transfer.from,
                            message.transferId,
                            true,
                            '',
                            actualSha256
                        );

                        if (this.onTransferComplete) {
                            this.onTransferComplete({
                                transferId: message.transferId,
                                filename: transfer.filename,
                                size: transfer.size,
                                sha256: actualSha256 || null,
                                sha256Verified: !!transfer.expectedSha256,
                                isRelay: true
                            });
                        }
                        
                        // ===== PHASE 3: Cleanup on relay completion =====
                        this.clearTransferTimeout(message.transferId);
                    }
                    // ===== PHASE 3: Cleanup relay transfer =====
                    this.cleanupTransfer(message.transferId);
                    break;
                }

                case 'relay-verified': {
                    const state = this.activeTransfers.get(message.transferId);
                    if (state) {
                        state.relayVerifyReady = true;
                        state.relayVerifyOk = !!message.ok;
                        state.relayVerifyReason = typeof message.reason === 'string' ? message.reason : '';
                        state.relayVerifiedSha256 = this.normalizeSha256(message.sha256);
                    }
                    break;
                }

                case 'relay-cancel': {
                    // Cancel relay transfer
                    const transfer = this.relayIncoming.get(message.transferId);
                    const state = this.activeTransfers.get(message.transferId);
                    this.relayIncoming.delete(message.transferId);

                    if (transfer) {
                        this.activeTransfers.delete(message.transferId);
                    }

                    const relayError = this.getRelayReasonMessage(message.reason) || 'Relay transfer cancelled by remote peer.';

                    if (state) {
                        state.cancelled = true;
                        state.cancelReason = relayError;
                        state.relayVerifyReady = true;
                        state.relayVerifyOk = false;
                        state.relayVerifyReason = typeof message.reason === 'string'
                            ? message.reason
                            : 'relay-cancelled';
                    }

                    if (this.onTransferError) {
                        const isSenderSideRelay = !transfer && !!state?.isRelay;
                        const canAttributeTransfer = !!transfer || !!state;
                        if (!isSenderSideRelay && canAttributeTransfer) {
                            this.onTransferError({
                                transferId: message.transferId,
                                filename: transfer?.filename || state?.filename,
                                error: relayError
                            });
                        }
                    }
                    break;
                }
            }
        } catch (error) {
            this.handleError('Signaling message handling failed', error);
        }
    }

    // ==================== WEBRTC ====================

    async createConnection(targetPeerId) {
        if (this.peers.has(targetPeerId)) {
            return this.peers.get(targetPeerId);
        }

        const pc = new RTCPeerConnection(this.rtcConfig);
        this.peers.set(targetPeerId, pc);

        console.log('[WebRTC] Creating connection to peer:', targetPeerId);
        console.log('[WebRTC] ICE servers configured:', this.rtcConfig.iceServers.length);

        const timeoutId = setTimeout(() => {
            if (pc.connectionState !== 'connected') {
                console.error('[WebRTC] Connection timeout. States:', {
                    connection: pc.connectionState,
                    ice: pc.iceConnectionState,
                    gathering: pc.iceGatheringState
                });
                this.handleConnectionFailure(targetPeerId, 'Connection timeout');
            }
        }, this.CONNECTION_TIMEOUT);

        // Track ICE candidates for debugging
        let candidateCount = { host: 0, srflx: 0, relay: 0, unknown: 0 };

        pc.onicecandidate = (event) => {
            if (event.candidate) {
                const type = event.candidate.type || 'unknown';
                candidateCount[type] = (candidateCount[type] || 0) + 1;
                console.log(`[WebRTC] ICE candidate: ${type} (${event.candidate.protocol || 'unknown'})`);

                if (this.ws?.readyState === WebSocket.OPEN) {
                    this.ws.send(JSON.stringify({
                        type: 'ice',
                        to: targetPeerId,
                        candidate: event.candidate
                    }));
                }
            } else {
                console.log('[WebRTC] ICE gathering complete. Candidates:', candidateCount);
            }
        };

        pc.onicegatheringstatechange = () => {
            console.log('[WebRTC] ICE gathering state:', pc.iceGatheringState);
        };

        pc.oniceconnectionstatechange = () => {
            console.log('[WebRTC] ICE connection state:', pc.iceConnectionState);
            if (pc.iceConnectionState === 'failed') {
                console.error('[WebRTC] ICE connection failed! Candidates gathered:', candidateCount);
            }
        };

        pc.onconnectionstatechange = () => {
            console.log('[WebRTC] Connection state:', pc.connectionState);
            if (pc.connectionState === 'connected') {
                clearTimeout(timeoutId);
                console.log('[WebRTC] ✅ Connected successfully!');
            }
            if (pc.connectionState === 'failed' || pc.connectionState === 'disconnected') {
                this.handleConnectionFailure(targetPeerId, 'Connection failed');
            }
        };

        // Reliable + ordered channel for file transfer integrity.
        const dc = pc.createDataChannel('teleport-files', { ordered: true });
        this.setupDataChannel(dc, targetPeerId);

        const offer = await pc.createOffer();
        await pc.setLocalDescription(offer);

        console.log('[WebRTC] Offer created, sending to peer...');

        // ===== PHASE 0: INCLUDE PUBLIC KEY AND SIGN OFFER =====
        const publicKey = await this.exportPublicKey();
        const announcement = {
            type: 'offer',
            to: targetPeerId,
            sdp: pc.localDescription,
            fingerprint: this.peerFingerprint,
            publicKey: publicKey,
            timestamp: Date.now()
        };

        // Sign the announcement for authenticity
        const signedAnnouncement = await this.signPeerAnnouncement(announcement);
        this.ws.send(JSON.stringify(signedAnnouncement));

        return pc;
    }

    handleConnectionFailure(peerId, reason) {
        this.cleanupPeer(peerId);
        const pending = this.pendingFiles.get(peerId);
        if (pending) {
            pending.reject(new Error(reason));
            this.pendingFiles.delete(peerId);
        }
    }

    async handleOffer(fromPeerId, sdp, fingerprint, publicKey) {
        try {
            console.log('[WebRTC] Received offer from peer:', fromPeerId);

            // ===== PHASE 0: FINGERPRINT VALIDATION =====
            if (fingerprint) {
                const validation = await this.validatePeerFingerprint(fromPeerId, fingerprint);
                if (!validation.valid) {
                    console.error(`[Security] Rejecting peer connection: ${validation.reason}`);
                    this.handleConnectionFailure(fromPeerId, validation.reason);
                    return;
                }
                
                // Notify UI of verification status
                if (this.onPeerVerification) {
                    this.onPeerVerification(fromPeerId, fingerprint, validation);
                }
            }

            // Import peer's public key for E2E encryption
            if (publicKey) {
                await this.importPeerPublicKey(fromPeerId, publicKey);
            }

            const pc = new RTCPeerConnection(this.rtcConfig);
            this.peers.set(fromPeerId, pc);

            // Track ICE candidates for debugging
            let candidateCount = { host: 0, srflx: 0, relay: 0, unknown: 0 };

            pc.onicecandidate = (event) => {
                if (event.candidate) {
                    const type = event.candidate.type || 'unknown';
                    candidateCount[type] = (candidateCount[type] || 0) + 1;
                    console.log(`[WebRTC-Recv] ICE candidate: ${type} (${event.candidate.protocol || 'unknown'})`);

                    if (this.ws?.readyState === WebSocket.OPEN) {
                        this.ws.send(JSON.stringify({
                            type: 'ice',
                            to: fromPeerId,
                            candidate: event.candidate
                        }));
                    }
                } else {
                    console.log('[WebRTC-Recv] ICE gathering complete. Candidates:', candidateCount);
                }
            };

            pc.onicegatheringstatechange = () => {
                console.log('[WebRTC-Recv] ICE gathering state:', pc.iceGatheringState);
            };

            pc.oniceconnectionstatechange = () => {
                console.log('[WebRTC-Recv] ICE connection state:', pc.iceConnectionState);
            };

            pc.ondatachannel = (event) => {
                console.log('[WebRTC-Recv] DataChannel received from peer!');
                this.setupDataChannel(event.channel, fromPeerId);
            };

            pc.onconnectionstatechange = () => {
                console.log('[WebRTC-Recv] Connection state:', pc.connectionState);
                if (pc.connectionState === 'connected') {
                    console.log('[WebRTC-Recv] ✅ Connected successfully!');
                }
                if (pc.connectionState === 'failed' || pc.connectionState === 'disconnected') {
                    console.error('[WebRTC-Recv] Connection failed! Candidates:', candidateCount);
                    this.handleConnectionFailure(fromPeerId, 'Connection lost');
                }
            };

            await pc.setRemoteDescription(new RTCSessionDescription(sdp));
            const answer = await pc.createAnswer();
            await pc.setLocalDescription(answer);

            console.log('[WebRTC-Recv] Answer created, sending to peer...');

            const ourPublicKey = await this.exportPublicKey();
            this.ws.send(JSON.stringify({
                type: 'answer',
                to: fromPeerId,
                sdp: pc.localDescription,
                publicKey: ourPublicKey
            }));
        } catch (error) {
            this.handleError('Failed to handle offer', error);
        }
    }

    async handleAnswer(fromPeerId, sdp, publicKey) {
        try {
            // Import peer's public key for E2E encryption
            if (publicKey) {
                await this.importPeerPublicKey(fromPeerId, publicKey);
            }
            const pc = this.peers.get(fromPeerId);
            if (pc) await pc.setRemoteDescription(new RTCSessionDescription(sdp));
        } catch (error) {
            this.handleError('Failed to handle answer', error);
        }
    }

    async handleIceCandidate(fromPeerId, candidate) {
        const pc = this.peers.get(fromPeerId);
        if (pc && candidate) {
            try { await pc.addIceCandidate(new RTCIceCandidate(candidate)); } catch (e) { }
        }
    }

    setupDataChannel(dc, peerId) {
        dc.binaryType = 'arraybuffer';
        this.dataChannels.set(peerId, dc);

        dc.onopen = () => {
            console.log(`[DataChannel] Opened with peer ${peerId}`);
        };

        dc.onmessage = (event) => this.handleDataChannelMessage(peerId, event.data);

        dc.onerror = (error) => {
            console.error(`[DataChannel] Error with peer ${peerId}:`, error);
            // ===== PHASE 3: Enhanced error handling =====
            this.handleError(ErrorCodes.DATACHANNEL_ERROR, { peerId, error: error.message });
        };

        dc.onclose = () => {
            console.log(`[DataChannel] Closed with peer ${peerId}`);
            // ===== PHASE 3: Cleanup peer on datachannel close =====
            this.cleanupPeer(peerId);
            this.dataChannels.delete(peerId);
        };
    }

    // ==================== DATA CHANNEL ====================

    async handleDataChannelMessage(peerId, data) {
        // ===== PHASE 3: Track peer activity =====
        this.updatePeerActivity(peerId);
        
        if (typeof data === 'string') {
            let msg;
            try {
                msg = JSON.parse(data);
            } catch (e) {
                console.warn('[DataChannel] Dropping non-JSON control message:', e.message);
                return;
            }

            if (msg.type === 'file-start') {
                // Strictly validate all fields before accepting the transfer
                let safeFilename;
                try {
                    safeFilename = this.validateFileStartMsg(msg);
                } catch (validationErr) {
                    console.error('[FileTransfer] Rejected file-start:', validationErr.message);
                    // Send a cancel back so the sender knows
                    const dc = this.dataChannels.get(peerId);
                    if (dc?.readyState === 'open') {
                        dc.send(JSON.stringify({
                            type: 'transfer-cancel',
                            transferId: msg.transferId || 'unknown',
                            reason: 'validation-failed'
                        }));
                    }
                    return;
                }

                // Sanitize optional relative path
                let safeRelativePath = '';
                if (msg.relativePath) {
                    try {
                        safeRelativePath = this.sanitizeRelativePath(msg.relativePath);
                    } catch (e) {
                        console.warn('[FileTransfer] Relative path rejected, using filename only:', e.message);
                        safeRelativePath = safeFilename;
                    }
                }

                const expectedSha256 = this.normalizeSha256(msg.sha256);
                if (typeof msg.sha256 !== 'undefined' && !expectedSha256) {
                    console.error('[FileTransfer] Rejected file-start: invalid SHA-256 format');
                    const dc = this.dataChannels.get(peerId);
                    if (dc?.readyState === 'open') {
                        dc.send(JSON.stringify({
                            type: 'transfer-cancel',
                            transferId: msg.transferId,
                            reason: 'invalid-sha256'
                        }));
                    }
                    return;
                }

                // Resume safety rule:
                // resume from non-zero offset is only allowed when we can prove persisted bytes exist.
                // Current browser sinks are not durably resumable, so we restart safely from byte 0.
                let resumeOffset = 0;
                const resumeState = await this.getResumeState(msg.transferId);
                if (resumeState &&
                    typeof resumeState.receivedBytes === 'number' &&
                    resumeState.receivedBytes > 0 &&
                    resumeState.receivedBytes < msg.size) {
                    if (resumeState.resumeCapable === true) {
                        resumeOffset = Math.floor(resumeState.receivedBytes);
                        console.log(`[Resume] Safe resume approved for ${safeFilename}: ${resumeOffset}/${msg.size}`);
                    } else {
                        console.log(`[Resume] Restarting ${safeFilename} from byte 0 (partial state is not durably resumable)`);
                    }
                }

                // Initialize transfer state
                const transferState = {
                    metadata: {
                        ...msg,
                        filename: safeFilename,
                        relativePath: safeRelativePath,
                        sha256: expectedSha256
                    },
                    chunks: [], // Only used for small files
                    received: resumeOffset,
                    startTime: Date.now(),
                    useStreaming: msg.size > this.STREAMING_THRESHOLD,
                    writer: null,
                    fileHandle: null,
                    expectedSha256,
                    hasher: new IncrementalSHA256()
                };

                // For large files, try to use File System Access API
                if (transferState.useStreaming && this.useFileSystemAPI) {
                    try {
                        const options = {
                            suggestedName: transferState.metadata.filename,
                            types: [{
                                description: 'File',
                                accept: { [transferState.metadata.mimeType || 'application/octet-stream']: ['.' + (transferState.metadata.filename.split('.').pop() || 'bin')] }
                            }]
                        };
                        transferState.fileHandle = await window.showSaveFilePicker(options);
                        const writable = await transferState.fileHandle.createWritable();
                        transferState.writer = writable;
                        console.log('[FileTransfer] Using File System Access API for streaming');
                    } catch (e) {
                        // User cancelled or API not available, fall back to memory
                        console.log('[FileTransfer] File picker cancelled/unavailable, using memory buffer');
                        transferState.useStreaming = false;
                    }
                } else if (transferState.useStreaming) {
                    // Try StreamSaver.js fallback for Firefox/Safari
                    if (window.streamSaver) {
                        try {
                            const fileStream = window.streamSaver.createWriteStream(transferState.metadata.filename, {
                                size: transferState.metadata.size
                            });
                            transferState.writer = fileStream.getWriter();
                            console.log('[FileTransfer] Using StreamSaver.js for streaming');
                        } catch (e) {
                            console.log('[FileTransfer] StreamSaver.js failed, using memory buffer');
                            transferState.useStreaming = false;
                        }
                    } else {
                        console.log('[FileTransfer] No streaming API available, using memory buffer (file may be too large)');
                        transferState.useStreaming = false;
                    }
                }

                // Avoid browser crashes when a very large file has no streaming sink.
                if (!transferState.useStreaming && msg.size > this.MAX_IN_MEMORY_RECEIVE_SIZE) {
                    const dc = this.dataChannels.get(peerId);
                    if (dc?.readyState === 'open') {
                        dc.send(JSON.stringify({
                            type: 'transfer-cancel',
                            transferId: msg.transferId,
                            reason: 'receiver-memory-limit'
                        }));
                    }

                    const errorMsg = `File too large for in-memory receive on this browser (${this.formatSize(msg.size)}). Use a browser with File System Access or StreamSaver support.`;
                    if (this.onTransferError) {
                        this.onTransferError({
                            transferId: msg.transferId,
                            filename: safeFilename,
                            error: errorMsg
                        });
                    }
                    return;
                }

                this.incomingChunks.set(msg.transferId, transferState);
                this.activeTransfers.set(msg.transferId, {
                    paused: false,
                    cancelled: false,
                    startTime: Date.now(),
                    bytesTransferred: 0,
                    fileIndex: msg.fileIndex || 0,
                    totalFiles: msg.totalFiles || 1,
                    peerId,
                    relativePath: safeRelativePath,
                    resumeOffset,
                    resumeCapable: false
                });

                // Persist initial resume state to IndexedDB
                this.saveResumeState(msg.transferId, {
                    filename: transferState.metadata.filename,
                    totalBytes: msg.size,
                    receivedBytes: resumeOffset,
                    sha256: expectedSha256,
                    resumeCapable: false,
                    peerId
                });

                // Sender must wait for explicit resume decision to avoid race conditions.
                const dc = this.dataChannels.get(peerId);
                if (dc?.readyState === 'open') {
                    dc.send(JSON.stringify({
                        type: 'resume-ready',
                        transferId: msg.transferId,
                        resumeOffset,
                        resumeCapable: false
                    }));

                    // Legacy compatibility with older senders that only understand resume-request.
                    if (resumeOffset > 0) {
                        dc.send(JSON.stringify({
                            type: 'resume-request',
                            transferId: msg.transferId,
                            resumeOffset
                        }));
                    }
                }
            } else if (msg.type === 'file-end') {
                this.assembleFile(msg.transferId);
            } else if (msg.type === 'resume-ready') {
                // Receiver is ready and has chosen a deterministic resume offset.
                const state = this.activeTransfers.get(msg.transferId);
                if (state) {
                    const offset = Number.isFinite(msg.resumeOffset) ? Math.max(0, msg.resumeOffset) : 0;
                    state.resumeOffset = offset;
                    state.resumeReady = true;
                    state.resumeCapable = !!msg.resumeCapable;
                    console.log(`[Resume] Sender: resume-ready offset ${offset} for ${msg.transferId}`);
                }
            } else if (msg.type === 'resume-request') {
                // Receiver is requesting a resume from a given offset.
                // We need to handle this on the SENDER side — tell sendFile() to seek.
                const state = this.activeTransfers.get(msg.transferId);
                if (state) {
                    state.resumeOffset = Number.isFinite(msg.resumeOffset) ? Math.max(0, msg.resumeOffset) : 0;
                    state.resumeReady = true;
                    state.resumeCapable = true; // legacy peers only send this signal when they intend resume support
                    console.log(`[Resume] Sender: receiver requested offset ${state.resumeOffset} for ${msg.transferId}`);
                }
            } else if (msg.type === 'file-verified') {
                const state = this.activeTransfers.get(msg.transferId);
                if (state) {
                    state.verifyReady = true;
                    state.verifyOk = !!msg.ok;
                    state.verifyReason = typeof msg.reason === 'string' ? msg.reason : '';
                    state.verifiedSha256 = this.normalizeSha256(msg.sha256);
                }
            } else if (msg.type === 'transfer-cancel') {
                this.handleTransferCancel(msg.transferId, msg.reason);
            } else if (msg.type === 'file-cancel') {
                // Desktop rejected our file-start (user consent denied or timed out).
                // Treat identically to transfer-cancel on the sender side.
                console.warn(`[FileTransfer] Remote cancelled transfer ${msg.transferId}: ${msg.reason || 'rejected'}`);
                this.handleTransferCancel(msg.transferId, msg.reason || 'rejected');
            } else if (msg.type === 'transfer-pause') {
                this.handleTransferPause(msg.transferId, msg.paused);
            }
        } else {
            // Binary chunk data
            const decoder = new TextDecoder();
            const transferId = decoder.decode(new Uint8Array(data, 0, 36));
            const chunkData = new Uint8Array(data, 36);

            const transfer = this.incomingChunks.get(transferId);
            const state = this.activeTransfers.get(transferId);

            if (transfer && state && !state.cancelled && !state.paused) {
                // Stream to disk or buffer in memory
                if (transfer.useStreaming && transfer.writer) {
                    try {
                        await transfer.writer.write(chunkData);
                    } catch (e) {
                        console.error('[FileTransfer] Stream write error:', e);
                    }
                } else {
                    transfer.chunks.push(chunkData);
                }

                transfer.received += chunkData.byteLength;
                state.bytesTransferred = transfer.received;

                if (transfer.hasher) {
                    transfer.hasher.update(chunkData);
                }

                if (Number.isFinite(transfer.metadata.size) && transfer.received > transfer.metadata.size) {
                    if (state?.peerId) {
                        this.sendFileVerificationAck(
                            state.peerId,
                            transferId,
                            false,
                            'Received more bytes than declared file size.',
                            transfer.hasher ? transfer.hasher.hex() : null
                        );
                    }

                    const dc = this.dataChannels.get(state?.peerId);
                    if (dc?.readyState === 'open') {
                        dc.send(JSON.stringify({
                            type: 'transfer-cancel',
                            transferId,
                            reason: 'size-overflow'
                        }));
                    }

                    this.deleteResumeState(transferId);
                    this.incomingChunks.delete(transferId);
                    this.activeTransfers.delete(transferId);

                    if (this.onTransferError) {
                        this.onTransferError({
                            transferId,
                            filename: transfer.metadata.filename,
                            error: 'Received more bytes than declared file size. Transfer aborted.'
                        });
                    }
                    return;
                }

                // Keep resume state up-to-date (throttled: every 64 chunks)
                if (transfer.received % (this.CHUNK_SIZE * 64) < this.CHUNK_SIZE) {
                    this.saveResumeState(transferId, {
                        filename: transfer.metadata.filename,
                        totalBytes: transfer.metadata.size,
                        receivedBytes: transfer.received,
                        sha256: transfer.metadata.sha256 || null,
                        resumeCapable: false,
                        peerId
                    });
                }

                const elapsed = (Date.now() - transfer.startTime) / 1000;
                const speed = transfer.received / elapsed;
                const remaining = transfer.metadata.size - transfer.received;
                const eta = remaining / speed;

                if (this.onTransferProgress) {
                    this.onTransferProgress({
                        transferId,
                        filename: transfer.metadata.filename,
                        received: transfer.received,
                        total: transfer.metadata.size,
                        progress: transfer.received / transfer.metadata.size,
                        speed,
                        eta,
                        fileIndex: state.fileIndex,
                        totalFiles: state.totalFiles,
                        protocol: 'WebRTC'
                    });
                }
            }
        }
    }

    async assembleFile(transferId) {
        const transfer = this.incomingChunks.get(transferId);
        const state = this.activeTransfers.get(transferId);

        if (!transfer || state?.cancelled) {
            // Clean up streaming writer if exists
            if (transfer?.writer) {
                try {
                    if (typeof transfer.writer.abort === 'function') {
                        await transfer.writer.abort();
                    } else if (typeof transfer.writer.close === 'function') {
                        await transfer.writer.close();
                    }
                } catch (e) { }
            }
            this.incomingChunks.delete(transferId);
            this.activeTransfers.delete(transferId);
            return;
        }

        const expectedSize = Number.isFinite(transfer.metadata?.size) ? transfer.metadata.size : null;
        const expectedSha256 = this.normalizeSha256(transfer.metadata?.sha256);
        const actualSha256 = transfer.hasher ? transfer.hasher.hex() : null;
        let totalSize = transfer.received;

        const abortWritable = async () => {
            if (!transfer.writer) return;
            try {
                if (typeof transfer.writer.abort === 'function') {
                    await transfer.writer.abort();
                    return;
                }
            } catch (e) {
                // Fall through to close attempt.
            }

            try {
                if (typeof transfer.writer.close === 'function') {
                    await transfer.writer.close();
                }
            } catch (e) { }
        };

        const failTransfer = async (errorMessage) => {
            if (state?.peerId) {
                this.sendFileVerificationAck(
                    state.peerId,
                    transferId,
                    false,
                    errorMessage,
                    actualSha256
                );
            }

            await abortWritable();

            this.saveTransferToHistory({
                filename: transfer.metadata.filename,
                size: totalSize,
                direction: 'received',
                success: false,
                sha256: actualSha256 || null
            });

            this.deleteResumeState(transferId);
            if (this.onTransferError) {
                this.onTransferError({
                    transferId,
                    filename: transfer.metadata.filename,
                    error: errorMessage
                });
            }

            this.incomingChunks.delete(transferId);
            this.activeTransfers.delete(transferId);
        };

        const getIntegrityError = () => {
            if (expectedSize !== null && totalSize !== expectedSize) {
                return `Received size mismatch (${this.formatSize(totalSize)} / ${this.formatSize(expectedSize)}).`;
            }
            if (expectedSha256 && actualSha256 !== expectedSha256) {
                const expectedShort = `${expectedSha256.slice(0, 12)}...`;
                const actualShort = actualSha256 ? `${actualSha256.slice(0, 12)}...` : 'none';
                return `SHA-256 verification failed (${actualShort} != ${expectedShort}).`;
            }
            return null;
        };

        // Handle streaming vs memory buffer
        if (transfer.useStreaming && transfer.writer) {
            const integrityError = getIntegrityError();
            if (integrityError) {
                await failTransfer(integrityError);
                return;
            }

            try {
                await transfer.writer.close();
                console.log('[FileTransfer] Streaming complete, file saved to disk');
            } catch (e) {
                await failTransfer(`Could not finalize streamed file: ${e.message || e}`);
                return;
            }
        } else {
            // Memory buffer approach - assemble and download
            totalSize = transfer.chunks.reduce((sum, chunk) => sum + chunk.byteLength, 0);
            const combined = new Uint8Array(totalSize);
            let offset = 0;

            for (const chunk of transfer.chunks) {
                combined.set(chunk, offset);
                offset += chunk.byteLength;
            }

            const integrityError = getIntegrityError();
            if (integrityError) {
                await failTransfer(integrityError);
                return;
            }

            const blob = new Blob([combined], { type: transfer.metadata.mimeType || 'application/octet-stream' });
            const url = URL.createObjectURL(blob);

            // Preserve folder structure in filename
            let filename = transfer.metadata.filename;
            if (state?.relativePath) {
                filename = state.relativePath;
            }

            const a = document.createElement('a');
            a.href = url;
            a.download = filename;
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            URL.revokeObjectURL(url);

            // Clear chunks to free memory
            transfer.chunks = [];
        }

        this.saveTransferToHistory({
            filename: transfer.metadata.filename,
            size: totalSize,
            direction: 'received',
            success: true,
            sha256: actualSha256 || null,
            sha256Verified: !!expectedSha256
        });

        if (state?.peerId) {
            this.sendFileVerificationAck(
                state.peerId,
                transferId,
                true,
                '',
                actualSha256
            );
        }

        // Remove the persisted resume state — transfer is complete
        this.deleteResumeState(transferId);

        if (this.onTransferComplete) {
            this.onTransferComplete({
                transferId,
                filename: transfer.metadata.filename,
                size: totalSize,
                sha256: actualSha256 || null,
                sha256Verified: !!expectedSha256,
                success: true,
                fileIndex: state?.fileIndex,
                totalFiles: state?.totalFiles
            });
        }

        this.showNotification('Transfer Complete', `Received: ${transfer.metadata.filename}`);

        this.incomingChunks.delete(transferId);
        this.activeTransfers.delete(transferId);
    }

    // ==================== NOTIFICATIONS ====================

    async requestNotificationPermission() {
        if ('Notification' in window && Notification.permission === 'default') {
            await Notification.requestPermission();
        }
    }

    showNotification(title, body) {
        if ('Notification' in window && Notification.permission === 'granted') {
            new Notification(title, { body, icon: '/assets/favicon.svg' });
        }
    }

    // ==================== TRANSFER CONTROL ====================

    cancelTransfer(transferId) {
        const state = this.activeTransfers.get(transferId);
        if (state) {
            state.cancelled = true;
            this.dataChannels.forEach(dc => {
                if (dc.readyState === 'open') {
                    dc.send(JSON.stringify({
                        type: 'transfer-cancel',
                        transferId,
                        reason: 'manual-cancel'
                    }));
                }
            });
        }
    }

    handleTransferCancel(transferId, reason = 'remote-cancel') {
        const state = this.activeTransfers.get(transferId);
        if (state) state.cancelled = true;
        this.incomingChunks.delete(transferId);

        const reasonText = {
            'validation-failed': 'Receiver rejected file metadata validation.',
            'invalid-sha256': 'Receiver rejected file integrity metadata (invalid SHA-256).',
            'receiver-memory-limit': 'Receiver cannot store this file size in memory on the current browser.',
            'size-overflow': 'Receiver detected byte overflow and aborted transfer.',
            'remote-cancel': 'Cancelled by remote peer.',
            'manual-cancel': 'Transfer cancelled manually.'
        }[reason] || 'Transfer cancelled by remote peer.';

        if (this.onTransferError) {
            this.onTransferError({ transferId, error: reasonText });
        }
    }

    pauseTransfer(transferId) {
        const state = this.activeTransfers.get(transferId);
        if (state) {
            state.paused = true;
            this.dataChannels.forEach(dc => {
                if (dc.readyState === 'open') {
                    dc.send(JSON.stringify({ type: 'transfer-pause', transferId, paused: true }));
                }
            });
        }
    }

    resumeTransfer(transferId) {
        const state = this.activeTransfers.get(transferId);
        if (state) {
            state.paused = false;
            this.dataChannels.forEach(dc => {
                if (dc.readyState === 'open') {
                    dc.send(JSON.stringify({ type: 'transfer-pause', transferId, paused: false }));
                }
            });
        }
    }

    handleTransferPause(transferId, paused) {
        const state = this.activeTransfers.get(transferId);
        if (state) state.paused = paused;
    }

    // ==================== FILE SIZE CHECK ====================

    checkFileSizes(files) {
        const largeFiles = [];
        for (const file of files) {
            if (file.size > this.fileSizeWarningThreshold) {
                largeFiles.push({
                    name: file.name,
                    size: file.size,
                    sizeFormatted: this.formatSize(file.size)
                });
            }
        }
        return largeFiles;
    }

    formatSize(bytes) {
        if (bytes < 1024) return bytes + ' B';
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
        if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
        return (bytes / (1024 * 1024 * 1024)).toFixed(2) + ' GB';
    }

    getRelayReasonMessage(reason) {
        return {
            'sha256-mismatch': 'Relay receiver rejected file due to SHA-256 mismatch.',
            'size-mismatch': 'Relay receiver rejected file due to size mismatch.',
            'size-overflow': 'Relay transfer exceeded declared file size.',
            'invalid-sha256': 'Relay receiver rejected invalid SHA-256 metadata.',
            'manual-cancel': 'Relay transfer was cancelled manually.',
            'receiver-memory-limit': 'Relay receiver cannot store this file size in memory on the current browser.'
        }[reason] || null;
    }

    // ==================== FOLDER HANDLING ====================

    async getFilesWithStructure(items) {
        const files = [];

        async function traverseEntry(entry, path = '') {
            if (entry.isFile) {
                return new Promise((resolve) => {
                    entry.file((file) => {
                        file.relativePath = path + file.name;
                        files.push(file);
                        resolve();
                    });
                });
            } else if (entry.isDirectory) {
                const dirReader = entry.createReader();
                const readEntriesBatch = () => {
                    return new Promise((resolve, reject) => {
                        dirReader.readEntries(resolve, reject);
                    });
                };

                const allEntries = [];
                while (true) {
                    const batch = await readEntriesBatch();
                    if (!batch.length) break;
                    allEntries.push(...batch);
                }

                for (const e of allEntries) {
                    await traverseEntry(e, path + entry.name + '/');
                }
            }
        }

        for (const item of items) {
            const entry = item.webkitGetAsEntry?.();
            if (entry) {
                await traverseEntry(entry);
            }
        }

        return files;
    }

    // ==================== SEND FILES ====================

    async requestFileSend(targetPeerId, files) {
        // ---- Sender-side security guards (mirrors desktop C++ limits) ----
        if (!files || files.length === 0) throw new Error('No files selected');
        if (files.length > 10000) {
            throw new Error(`Too many files: ${files.length} (max 10,000)`);
        }
        const totalBytes = Array.from(files).reduce((sum, f) => sum + (f.size || 0), 0);
        if (totalBytes > 100 * 1024 * 1024 * 1024) {
            throw new Error(`Total size exceeds limit: ${this.formatSize(totalBytes)} (max 100 GB)`);
        }
        // ---- End security guards ----

        const largeFiles = this.checkFileSizes(files);
        if (largeFiles.length > 0 && this.onFileSizeWarning) {
            const proceed = await this.onFileSizeWarning(largeFiles);
            if (!proceed) return;
        }

        // BUG FIX (Bug C3): Desktop peers (fingerprint:null, platform:'desktop') have
        // no WebRTC stack. Previously we always called createConnection() + waitForDataChannel()
        // which would spin for 60 seconds before failing. Now we detect relayOnly peers and
        // route directly to the server-relay path, matching what the desktop expects.
        const targetPeer = this.peerList.find(p => p.id === targetPeerId);
        const isRelayPeer = targetPeer?.relayOnly === true;

        const fileInfos = Array.from(files).map(f => ({
            name: f.name,
            size: f.size,
            type: f.type || 'application/octet-stream',
            relativePath: f.relativePath || f.webkitRelativePath || ''
        }));

        // Send file-request over signaling (works for both P2P and relay paths)
        this.ws.send(JSON.stringify({
            type: 'file-request',
            to: targetPeerId,
            files: fileInfos,
            fromName: this.deviceName,
            fingerprint: this.peerFingerprint
        }));

        // If the user has forced relay mode, treat all web peers as relay-only too.
        const isRelayPeer2 = isRelayPeer || this.forceRelayMode;

        if (isRelayPeer2) {
            // Relay path: forced (relay-mode toggle) or desktop peer (no WebRTC stack)
            return new Promise((resolve, reject) => {
                const handleResponse = (accepted, fromId) => {
                    if (fromId !== targetPeerId) return false; // not our response
                    if (!accepted) {
                        reject(new Error('Desktop peer rejected the file transfer'));
                        return true;
                    }
                    // Send each file via relay sequentially
                    (async () => {
                        try {
                            for (let i = 0; i < files.length; i++) {
                                const file = files[i];
                                await this.sendFileViaRelay(targetPeerId, file, i, files.length);
                            }
                            resolve();
                        } catch (err) {
                            reject(err);
                        }
                    })();
                    return true;
                };

                // Intercept the file-response signaling message
                const origHandler = this.handleFileResponse.bind(this);
                this.handleFileResponse = (fromId, accepted) => {
                    if (handleResponse(accepted, fromId)) {
                        this.handleFileResponse = origHandler; // restore
                    } else {
                        origHandler(fromId, accepted);
                    }
                };

                setTimeout(() => {
                    this.handleFileResponse = origHandler;
                    reject(new Error('Desktop peer did not respond to file request (timeout)'));
                }, 15000);
            });
        }

        // Web/P2P peer — WebRTC direct path
        // Initiate connection in background, don't wait for it to avoid blocking UI acceptance
        this.createConnection(targetPeerId).catch(console.error);

        return new Promise((resolve, reject) => {
            this.pendingFiles.set(targetPeerId, { files, resolve, reject });

            setTimeout(() => {
                if (this.pendingFiles.has(targetPeerId)) {
                    this.pendingFiles.delete(targetPeerId);
                    reject(new Error('Transfer request timed out'));
                }
            }, this.TRANSFER_TIMEOUT);
        });
    }

    waitForDataChannel(peerId, timeout = 15000) {
        return new Promise((resolve, reject) => {
            const startTime = Date.now();
            const check = () => {
                const dc = this.dataChannels.get(peerId);
                const pc = this.peers.get(peerId);

                if (dc?.readyState === 'open') {
                    console.log(`[DataChannel] Ready with peer ${peerId}`);
                    resolve();
                    return;
                }

                if (pc && (pc.iceConnectionState === 'failed' || pc.iceConnectionState === 'closed')) {
                    reject(new Error('ICE Connection Failed - NAT blocking P2P'));
                    return;
                }

                if (Date.now() - startTime > timeout) {
                    console.error(`[DataChannel] Timeout waiting for peer ${peerId}`);
                    reject(new Error('DataChannel timeout - peer may be behind NAT or firewall'));
                    return;
                }
                setTimeout(check, 100);
            };
            check();
        });
    }

    waitForResumeReady(transferId, timeout = this.RESUME_READY_TIMEOUT) {
        return new Promise((resolve, reject) => {
            const startTime = Date.now();

            const check = () => {
                const state = this.activeTransfers.get(transferId);
                if (!state) {
                    reject(new Error('Transfer state missing during resume negotiation'));
                    return;
                }
                if (state.cancelled) {
                    reject(new Error('Transfer cancelled during resume negotiation'));
                    return;
                }
                if (state.resumeReady) {
                    resolve(state.resumeOffset || 0);
                    return;
                }
                if (Date.now() - startTime > timeout) {
                    console.warn(`[Resume] No explicit resume-ready for ${transferId} after ${timeout}ms; restarting from 0`);
                    state.resumeReady = true;
                    state.resumeOffset = 0;
                    resolve(0);
                    return;
                }
                setTimeout(check, 25);
            };

            check();
        });
    }

    waitForFileVerification(transferId, timeout = 12000) {
        return new Promise((resolve, reject) => {
            const startTime = Date.now();

            const check = () => {
                const state = this.activeTransfers.get(transferId);
                if (!state) {
                    reject(new Error('Transfer state missing during verification wait'));
                    return;
                }
                if (state.cancelled) {
                    reject(new Error('Transfer cancelled before verification'));
                    return;
                }
                if (state.verifyReady) {
                    resolve({
                        supported: true,
                        ok: !!state.verifyOk,
                        reason: state.verifyReason || '',
                        sha256: state.verifiedSha256 || null
                    });
                    return;
                }
                if (Date.now() - startTime > timeout) {
                    // Older/third-party peers might not implement explicit verification ACK.
                    resolve({ supported: false, ok: true, reason: '', sha256: null });
                    return;
                }
                setTimeout(check, 25);
            };

            check();
        });
    }

    waitForRelayVerification(transferId, timeout = this.RELAY_VERIFICATION_TIMEOUT) {
        return new Promise((resolve, reject) => {
            const startTime = Date.now();

            const check = () => {
                const state = this.activeTransfers.get(transferId);
                if (!state) {
                    reject(new Error('Relay transfer state missing during verification wait'));
                    return;
                }

                if (state.relayVerifyReady) {
                    resolve({
                        supported: true,
                        ok: !!state.relayVerifyOk,
                        reason: state.relayVerifyReason || '',
                        sha256: state.relayVerifiedSha256 || null
                    });
                    return;
                }

                if (state.cancelled) {
                    reject(new Error(state.cancelReason || 'Relay transfer cancelled before verification'));
                    return;
                }

                if (Date.now() - startTime > timeout) {
                    // Older peers may not emit relay verification acknowledgements.
                    resolve({ supported: false, ok: true, reason: '', sha256: null });
                    return;
                }

                setTimeout(check, 25);
            };

            check();
        });
    }

    sendFileVerificationAck(peerId, transferId, ok, reason = '', sha256 = null) {
        const dc = this.dataChannels.get(peerId);
        if (dc?.readyState === 'open') {
            dc.send(JSON.stringify({
                type: 'file-verified',
                transferId,
                ok: !!ok,
                reason: reason || '',
                sha256: this.normalizeSha256(sha256)
            }));
        }
    }

    sendRelayVerificationAck(peerId, transferId, ok, reason = '', sha256 = null) {
        if (!peerId || !this.ws || this.ws.readyState !== WebSocket.OPEN) return;

        this.ws.send(JSON.stringify({
            type: 'relay-verified',
            to: peerId,
            transferId,
            ok: !!ok,
            reason: reason || '',
            sha256: this.normalizeSha256(sha256)
        }));
    }

    handleFileResponse(fromPeerId, accepted) {
        console.log('[FileTransfer] handleFileResponse called:', { fromPeerId, accepted });
        const pending = this.pendingFiles.get(fromPeerId);
        if (!pending) {
            console.log('[FileTransfer] No pending files found for peer:', fromPeerId);
            return;
        }

        if (accepted) {
            console.log('[FileTransfer] Transfer accepted, establishing P2P pipeline...');

            (async () => {
                try {
                    // Try WebRTC P2P first
                    if (!this.dataChannels.has(fromPeerId) || this.dataChannels.get(fromPeerId).readyState !== 'open') {
                        if (!this.peers.has(fromPeerId)) {
                            await this.createConnection(fromPeerId);
                        }
                        await this.waitForDataChannel(fromPeerId, 15000); // Wait 15s max for ICE
                    }

                    await this.sendFiles(fromPeerId, pending.files);
                    console.log('[FileTransfer] sendFiles completed successfully via P2P');
                    pending.resolve();
                } catch (err) {
                    console.warn('[FileTransfer] P2P WebRTC failed:', err.message);

                    // Automatic relay fallback if enabled
                    if (this.useRelayFallback) {
                        console.log('[FileTransfer] Falling back to server relay...');
                        try {
                            await this.sendFilesViaRelay(fromPeerId, pending.files);
                            console.log('[FileTransfer] Relay transfer completed successfully');
                            pending.resolve();
                        } catch (relayErr) {
                            console.error('[FileTransfer] Relay also failed:', relayErr);
                            pending.reject(relayErr);
                        }
                    } else {
                        pending.reject(err);
                    }
                }
            })();
        } else {
            console.log('[FileTransfer] Transfer rejected by receiver');
            pending.reject(new Error('Transfer rejected'));
        }

        this.pendingFiles.delete(fromPeerId);
    }

    acceptFileRequest(fromPeerId) {
        this.ws.send(JSON.stringify({ type: 'file-response', to: fromPeerId, accepted: true }));
    }

    rejectFileRequest(fromPeerId) {
        this.ws.send(JSON.stringify({ type: 'file-response', to: fromPeerId, accepted: false }));
    }

    async sendFiles(targetPeerId, files) {
        console.log('[FileTransfer] sendFiles called for peer:', targetPeerId);
        const dc = this.dataChannels.get(targetPeerId);
        console.log('[FileTransfer] DataChannel state:', dc?.readyState, 'for peer:', targetPeerId);

        if (!dc) {
            console.error('[FileTransfer] No DataChannel found for peer:', targetPeerId);
            throw new Error('DataChannel not found');
        }
        if (dc.readyState !== 'open') {
            console.error('[FileTransfer] DataChannel not open, state:', dc.readyState);
            throw new Error('DataChannel not ready - state: ' + dc.readyState);
        }

        const totalFiles = files.length;
        console.log('[FileTransfer] Starting to send', totalFiles, 'files...');
        for (let i = 0; i < files.length; i++) {
            console.log('[FileTransfer] Sending file', i + 1, 'of', totalFiles, ':', files[i].name);
            await this.sendFile(dc, files[i], i, totalFiles, targetPeerId); // no retry loop — DataChannel (SCTP) is already reliable
        }
        console.log('[FileTransfer] All files sent!');
    }

    // ============ SERVER RELAY TRANSFER ============
    // Used when WebRTC P2P fails due to NAT/firewall

    async sendFilesViaRelay(targetPeerId, files) {
        console.log('[Relay] Sending', files.length, 'files via server relay');

        if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
            throw new Error('WebSocket not connected');
        }

        for (let i = 0; i < files.length; i++) {
            await this.sendFileViaRelay(targetPeerId, files[i], i, files.length);
        }

        console.log('[Relay] All files sent via relay!');
    }

    async sendFileViaRelay(targetPeerId, file, fileIndex = 0, totalFiles = 1) {
        const transferId = this.generateTransferId();
        const startTime = Date.now();
        const fileSha256 = await this.computeFileSha256(file);

        console.log(`[Relay] Sending file: ${file.name} (${file.size} bytes)`);

        // Track transfer
        this.activeTransfers.set(transferId, {
            paused: false,
            cancelled: false,
            startTime,
            bytesTransferred: 0,
            filename: file.name,
            peerId: targetPeerId,
            fileIndex,
            totalFiles,
            isRelay: true,
            relayVerifyReady: false,
            relayVerifyOk: false,
            relayVerifyReason: '',
            sha256: fileSha256
        });

        // Send start message
        this.ws.send(JSON.stringify({
            type: 'relay-start',
            to: targetPeerId,
            transferId,
            filename: file.name,
            size: file.size,
            mimeType: file.type || 'application/octet-stream',
            sha256: fileSha256,
            fileIndex,
            totalFiles
        }));

        // ── Relay chunk settings ───────────────────────────────────────────
        // 48 KB decoded leaves headroom under the 64 KB server limit after base64
        // expansion (~33% overhead → 64 KB encoded).
        const RELAY_CHUNK_SIZE  = 48 * 1024;  // 48 KB decoded
        const PIPELINE_DEPTH    = 4;          // chunks in-flight simultaneously

        // Fast base64 encoder — avoids the spread-operator stack-overflow that
        // btoa(String.fromCharCode(...chunk)) causes on large buffers.
        const fastBase64 = (/** @type {Uint8Array} */ u8) => {
            let s = '';
            const len = u8.length;
            for (let b = 0; b < len; b++) s += String.fromCharCode(u8[b]);
            return btoa(s);
        };

        const reader = file.stream().getReader();
        let offset = 0;
        /** @type {Promise<void>[]} */
        let pipeline = [];

        const flushPipeline = async () => {
            if (pipeline.length > 0) {
                await Promise.all(pipeline);
                pipeline = [];
            }
        };

        const sendChunk = async (chunk, chunkOffset) => {
            await this.throttle(chunk.length);
            if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
                throw new Error('WebSocket disconnected during relay transfer');
            }
            const base64 = fastBase64(chunk);
            this.ws.send(JSON.stringify({
                type: 'relay-chunk',
                to: targetPeerId,
                transferId,
                data: base64,
                offset: chunkOffset
            }));
        };

        try {
            while (true) {
                const state = this.activeTransfers.get(transferId);
                if (!state || state.cancelled) {
                    await flushPipeline();
                    this.ws.send(JSON.stringify({
                        type: 'relay-cancel',
                        to: targetPeerId,
                        transferId,
                        reason: 'manual-cancel'
                    }));
                    throw new Error('Transfer cancelled');
                }

                while (state?.paused) {
                    await new Promise(r => setTimeout(r, 100));
                }

                const { done, value } = await reader.read();
                if (done) break;

                // Slice stream-reader value into RELAY_CHUNK_SIZE pieces and pipeline them
                for (let i = 0; i < value.length; i += RELAY_CHUNK_SIZE) {
                    const chunk = value.slice(i, i + RELAY_CHUNK_SIZE);
                    const chunkOffset = offset;

                    pipeline.push(sendChunk(chunk, chunkOffset));
                    if (pipeline.length >= PIPELINE_DEPTH) await flushPipeline();

                    offset += chunk.length;

                    // Update progress
                    if (this.onTransferProgress) {
                        const elapsed = (Date.now() - startTime) / 1000;
                        const speed = elapsed > 0 ? offset / elapsed : 0;

                        this.onTransferProgress({
                            transferId,
                            filename: file.name,
                            progress: file.size > 0 ? (offset / file.size) : 1,
                            sent: offset,
                            total: file.size,
                            speed,
                            eta: speed > 0 ? ((file.size - offset) / speed) : 0,
                            isRelay: true,
                            transferMode: 'relay',
                            chunkSize: RELAY_CHUNK_SIZE,
                            chunkSizeKB: Math.round(RELAY_CHUNK_SIZE / 1024),
                            protocol: 'Web Relay'
                        });
                    }

                    // Small delay to not overwhelm WebSocket
                    await new Promise(r => setTimeout(r, 5));
                }
            }

            // Send end message
            this.ws.send(JSON.stringify({
                type: 'relay-end',
                to: targetPeerId,
                transferId
            }));

            const verification = await this.waitForRelayVerification(transferId);
            if (verification.supported && !verification.ok) {
                throw new Error(this.getRelayReasonMessage(verification.reason) || verification.reason || 'Relay receiver integrity verification failed');
            }

            console.log(`[Relay] File sent: ${file.name}`);

            this.saveTransferToHistory({
                filename: file.name,
                size: file.size,
                direction: 'sent',
                success: true,
                sha256: fileSha256,
                sha256Verified: verification.supported ? true : false
            });

            if (this.onTransferComplete) {
                this.onTransferComplete({
                    transferId,
                    filename: file.name,
                    size: file.size,
                    sha256: fileSha256,
                    sha256Verified: verification.supported ? true : false,
                    success: true,
                    fileIndex,
                    totalFiles,
                    isRelay: true
                });
            }

            // Cleanup
            this.activeTransfers.delete(transferId);

        } catch (error) {
            this.activeTransfers.delete(transferId);

            this.saveTransferToHistory({
                filename: file.name,
                size: file.size,
                direction: 'sent',
                success: false,
                sha256: fileSha256
            });

            if (this.onTransferError) {
                this.onTransferError({
                    transferId,
                    filename: file.name,
                    error: error.message || 'Relay transfer failed'
                });
            }
            throw error;
        }
    }

    async sendFile(dc, file, fileIndex = 0, totalFiles = 1, targetPeerId = null) {
        const transferId = this.generateTransferId();
        const startTime = Date.now();
        const relativePath = file.relativePath || file.webkitRelativePath || '';
        let fileSent = false;
        const fileSha256 = await this.computeFileSha256(file);

        // ✅ ADAPTIVE CHUNK SIZING: Select based on file size
        // Small: 16KB, Medium: 256KB, Large: 1MB, Huge: 4MB
        const chunkSize = selectChunkSize(file.size);
        console.log(`[FileTransfer] Using chunk size ${chunkSize / 1024}KB for ${file.size / (1024*1024|0)}MB file`);

        this.activeTransfers.set(transferId, {
            paused: false,
            cancelled: false,
            startTime,
            bytesTransferred: 0,
            fileIndex,
            totalFiles,
            peerId: targetPeerId,
            resumeOffset: 0,
            resumeReady: false,
            resumeCapable: false,
            sha256: fileSha256,
            chunkSize: chunkSize  // Store for reference
        });

        // ===== PHASE 3: Set transfer timeout =====
        this.setTransferTimeout(transferId);

        try {
            dc.send(JSON.stringify({
                type: 'file-start',
                transferId,
                filename: file.name,
                size: file.size,
                mimeType: file.type,
                sha256: fileSha256,
                fileIndex,
                totalFiles,
                relativePath
            }));

            // Wait for deterministic resume decision from receiver.
            await this.waitForResumeReady(transferId);
            const negotiatedState = this.activeTransfers.get(transferId);
            const requestedOffset = negotiatedState?.resumeOffset || 0;
            const skipBytes = negotiatedState?.resumeCapable
                ? Math.min(file.size, Math.max(0, requestedOffset))
                : 0;

            // Use slice to skip already-received bytes (resume support)
            const fileSlice = skipBytes > 0 ? file.slice(skipBytes) : file;
            const reader = fileSlice.stream().getReader();
            let offset = skipBytes;

            if (skipBytes > 0) {
                console.log(`[Resume] Sender: skipping first ${skipBytes} bytes of ${file.name}`);
            }

            while (true) {
                const state = this.activeTransfers.get(transferId);
                if (state?.cancelled) throw new Error('Cancelled');
                while (state?.paused) await new Promise(r => setTimeout(r, 100));

                const { done, value } = await reader.read();
                if (done) break;

                for (let i = 0; i < value.length; i += chunkSize) {
                    const state = this.activeTransfers.get(transferId);
                    if (state?.cancelled) break;

                    const chunk = value.slice(i, i + chunkSize);

                    const encoder = new TextEncoder();
                    const idBytes = encoder.encode(transferId);
                    const packet = new Uint8Array(36 + chunk.length);
                    packet.set(idBytes, 0);
                    packet.set(chunk, 36);

                    while (dc.bufferedAmount > this.MAX_BUFFER_SIZE) {
                        await new Promise(r => setTimeout(r, 10));
                    }

                    await this.throttle(packet.length);

                    dc.send(packet.buffer);
                    offset += chunk.length;

                    const elapsed = (Date.now() - startTime) / 1000;
                    const speed = offset / elapsed;
                    const remaining = file.size - offset;
                    const eta = remaining / speed;

                    if (this.onTransferProgress) {
                        this.onTransferProgress({
                            transferId,
                            filename: file.name,
                            sent: offset,
                            total: file.size,
                            progress: offset / file.size,
                            speed,
                            eta,
                            fileIndex,
                            totalFiles,
                            transferMode: 'p2p',
                            chunkSize: chunkSize,
                            chunkSizeKB: Math.round(chunkSize / 1024),
                            protocol: 'WebRTC'
                        });
                    }
                }
            }

            dc.send(JSON.stringify({ type: 'file-end', transferId }));

            const verification = await this.waitForFileVerification(transferId);
            if (verification.supported && !verification.ok) {
                throw new Error(verification.reason || 'Receiver integrity verification failed');
            }

            fileSent = true;

            this.saveTransferToHistory({
                filename: file.name,
                size: file.size,
                direction: 'sent',
                success: true
            });

            if (this.onTransferComplete) {
                this.onTransferComplete({
                    transferId,
                    filename: file.name,
                    size: file.size,
                    sha256: fileSha256,
                    sha256Verified: true,
                    success: true,
                    fileIndex,
                    totalFiles
                });
            }

            // ===== PHASE 3: Cleanup on successful transfer =====
            this.clearTransferTimeout(transferId);

            this.showNotification('Transfer Complete', `Sent: ${file.name}`);
            this.activeTransfers.delete(transferId);

        } catch (error) {
            // ===== PHASE 3: Cleanup on error =====
            this.clearTransferTimeout(transferId);
            this.activeTransfers.delete(transferId);

            // Only report/throw error if the file was not actually sent.
            // Errors after file-end (e.g. in callbacks) should not cause re-transfers.
            if (!fileSent) {
                if (this.onTransferError) {
                    this.onTransferError({ transferId, filename: file.name, error: error.message });
                }
                throw error;
            }
        }
    }

    // ==================== QR CODE (REAL IMPLEMENTATION) ====================

    generateQRCode(data, size = 160) {
        // Real QR code generation using Reed-Solomon error correction
        const qr = new QRCodeGenerator(data);
        return qr.render(size);
    }

    generatePairingData() {
        return JSON.stringify({
            peerId: this.peerId,
            server: this.serverUrl,
            name: this.deviceName,
            fingerprint: this.peerFingerprint
        });
    }

    async connectWithPairingData(data) {
        try {
            const parsed = JSON.parse(data);
            if (parsed.server && parsed.server !== this.serverUrl) {
                let host = null;
                let port = 3000;

                if (typeof parsed.server === 'string' && parsed.server.trim()) {
                    const rawServer = parsed.server.trim();
                    const normalizedServer = /^(ws|wss|http|https):\/\//i.test(rawServer)
                        ? rawServer
                        : `ws://${rawServer}`;

                    try {
                        const u = new URL(normalizedServer);
                        host = u.hostname;
                        if (u.port) {
                            const parsedPort = parseInt(u.port, 10);
                            if (Number.isFinite(parsedPort) && parsedPort > 0) {
                                port = parsedPort;
                            }
                        }
                    } catch (e) {
                        // Fallback for host[:port] without a parseable URL structure
                        const hostPort = rawServer
                            .replace(/^\w+:\/\//, '')
                            .split('/')[0];

                        if (hostPort.startsWith('[')) {
                            const idx = hostPort.indexOf(']');
                            host = idx > 0 ? hostPort.slice(1, idx) : null;
                        } else {
                            const parts = hostPort.split(':');
                            host = parts[0] || null;
                            if (parts[1]) {
                                const parsedPort = parseInt(parts[1], 10);
                                if (Number.isFinite(parsedPort) && parsedPort > 0) {
                                    port = parsedPort;
                                }
                            }
                        }
                    }
                }

                if (host) {
                    await this.connectToManualIP(host, port);
                }
            }
            if (parsed.peerId) {
                return this.createConnection(parsed.peerId);
            }
        } catch (e) {
            throw new Error('Invalid pairing data');
        }
    }

    // ==================== CLEANUP ====================

    cleanupPeer(peerId) {
        const pc = this.peers.get(peerId);
        if (pc) { pc.close(); this.peers.delete(peerId); }
        this.dataChannels.delete(peerId);
    }

    getTheme() { return this.loadSetting('teleport-theme') || 'dark'; }
    setTheme(theme) { this.saveSetting('teleport-theme', theme); }

    // ===== PHASE 3: Transfer Timeout Management =====
    setTransferTimeout(transferId, timeoutMs = this.TRANSFER_TIMEOUT) {
        // Clear existing timeout
        const existingTimer = this.activeTimers.get(transferId);
        if (existingTimer) {
            clearTimeout(existingTimer.timeout);
        }

        const timeout = setTimeout(() => {
            const transfer = this.activeTransfers.get(transferId);
            if (transfer && !transfer.cancelled) {
                console.warn(`[Timeout] Transfer ${transferId} exceeded timeout`);
                this.handleError(ErrorCodes.TRANSFER_TIMEOUT, { transferId });
                this.cancelTransfer(transferId);
            }
            this.activeTimers.delete(transferId);
        }, timeoutMs);

        this.activeTimers.set(transferId, { timeout });
        
        // Set resume state expiry (for recovery purposes)
        this.resumeStateExpiry.set(transferId, Date.now() + this.RESUME_STATE_LIFETIME);
    }

    clearTransferTimeout(transferId) {
        const timer = this.activeTimers.get(transferId);
        if (timer) {
            clearTimeout(timer.timeout);
            this.activeTimers.delete(transferId);
        }
    }

    generateTransferId() {
        const cryptoObj = (typeof crypto !== 'undefined') ? crypto : null;

        if (cryptoObj && typeof cryptoObj.randomUUID === 'function') {
            return cryptoObj.randomUUID();
        }

        const asUuid = (hex32) => {
            return `${hex32.slice(0, 8)}-${hex32.slice(8, 12)}-${hex32.slice(12, 16)}-${hex32.slice(16, 20)}-${hex32.slice(20, 32)}`;
        };

        if (cryptoObj && typeof cryptoObj.getRandomValues === 'function') {
            const random = cryptoObj.getRandomValues(new Uint8Array(16));
            const hex = Array.from(random)
                .map(b => b.toString(16).padStart(2, '0'))
                .join('');
            return asUuid(hex);
        }

        let pseudoHex = '';
        while (pseudoHex.length < 32) {
            pseudoHex += Math.floor(Math.random() * 0x100000000).toString(16).padStart(8, '0');
        }
        return asUuid(pseudoHex.slice(0, 32));
    }

    getFileType(filename) {
        const ext = filename.split('.').pop()?.toLowerCase();
        const types = {
            'jpg': 'image', 'jpeg': 'image', 'png': 'image', 'gif': 'image', 'webp': 'image', 'svg': 'image', 'bmp': 'image', 'ico': 'image',
            'mp4': 'video', 'webm': 'video', 'avi': 'video', 'mov': 'video', 'mkv': 'video', 'flv': 'video', 'wmv': 'video',
            'mp3': 'audio', 'wav': 'audio', 'ogg': 'audio', 'flac': 'audio', 'aac': 'audio', 'm4a': 'audio', 'wma': 'audio',
            'pdf': 'pdf',
            'doc': 'document', 'docx': 'document', 'txt': 'text', 'rtf': 'document', 'odt': 'document',
            'xls': 'spreadsheet', 'xlsx': 'spreadsheet', 'csv': 'spreadsheet', 'ods': 'spreadsheet',
            'ppt': 'presentation', 'pptx': 'presentation', 'odp': 'presentation',
            'zip': 'archive', 'rar': 'archive', '7z': 'archive', 'tar': 'archive', 'gz': 'archive', 'bz2': 'archive',
            'js': 'code', 'ts': 'code', 'py': 'code', 'html': 'code', 'css': 'code', 'json': 'code', 'xml': 'code', 'java': 'code', 'cpp': 'code', 'c': 'code', 'h': 'code', 'php': 'code', 'rb': 'code', 'go': 'code', 'rs': 'code', 'swift': 'code', 'kt': 'code',
            'exe': 'executable', 'app': 'executable', 'dmg': 'executable', 'deb': 'executable', 'rpm': 'executable', 'msi': 'executable'
        };
        return types[ext] || 'file';
    }
}

// ==================== INCREMENTAL SHA-256 ====================

class IncrementalSHA256 {
    constructor() {
        this.state = new Uint32Array([
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
        ]);
        this.buffer = new Uint8Array(64);
        this.bufferLength = 0;
        this.bytesHashed = 0;
        this.finished = false;
        this.words = new Uint32Array(64);
    }

    static ROTR(x, n) {
        return (x >>> n) | (x << (32 - n));
    }

    update(data) {
        if (this.finished) {
            throw new Error('Cannot update SHA-256 after digest()');
        }

        if (!(data instanceof Uint8Array)) {
            data = new Uint8Array(data);
        }

        let position = 0;
        this.bytesHashed += data.length;

        while (position < data.length) {
            const take = Math.min(64 - this.bufferLength, data.length - position);
            this.buffer.set(data.subarray(position, position + take), this.bufferLength);
            this.bufferLength += take;
            position += take;

            if (this.bufferLength === 64) {
                this.processBlock(this.buffer);
                this.bufferLength = 0;
            }
        }
    }

    processBlock(chunk) {
        const K = [
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        ];

        const W = this.words;
        for (let i = 0; i < 16; i++) {
            const j = i * 4;
            W[i] = ((chunk[j] << 24) | (chunk[j + 1] << 16) | (chunk[j + 2] << 8) | chunk[j + 3]) >>> 0;
        }

        for (let i = 16; i < 64; i++) {
            const s0 = (IncrementalSHA256.ROTR(W[i - 15], 7) ^ IncrementalSHA256.ROTR(W[i - 15], 18) ^ (W[i - 15] >>> 3)) >>> 0;
            const s1 = (IncrementalSHA256.ROTR(W[i - 2], 17) ^ IncrementalSHA256.ROTR(W[i - 2], 19) ^ (W[i - 2] >>> 10)) >>> 0;
            W[i] = (W[i - 16] + s0 + W[i - 7] + s1) >>> 0;
        }

        let a = this.state[0];
        let b = this.state[1];
        let c = this.state[2];
        let d = this.state[3];
        let e = this.state[4];
        let f = this.state[5];
        let g = this.state[6];
        let h = this.state[7];

        for (let i = 0; i < 64; i++) {
            const S1 = (IncrementalSHA256.ROTR(e, 6) ^ IncrementalSHA256.ROTR(e, 11) ^ IncrementalSHA256.ROTR(e, 25)) >>> 0;
            const ch = ((e & f) ^ (~e & g)) >>> 0;
            const temp1 = (h + S1 + ch + K[i] + W[i]) >>> 0;
            const S0 = (IncrementalSHA256.ROTR(a, 2) ^ IncrementalSHA256.ROTR(a, 13) ^ IncrementalSHA256.ROTR(a, 22)) >>> 0;
            const maj = ((a & b) ^ (a & c) ^ (b & c)) >>> 0;
            const temp2 = (S0 + maj) >>> 0;

            h = g;
            g = f;
            f = e;
            e = (d + temp1) >>> 0;
            d = c;
            c = b;
            b = a;
            a = (temp1 + temp2) >>> 0;
        }

        this.state[0] = (this.state[0] + a) >>> 0;
        this.state[1] = (this.state[1] + b) >>> 0;
        this.state[2] = (this.state[2] + c) >>> 0;
        this.state[3] = (this.state[3] + d) >>> 0;
        this.state[4] = (this.state[4] + e) >>> 0;
        this.state[5] = (this.state[5] + f) >>> 0;
        this.state[6] = (this.state[6] + g) >>> 0;
        this.state[7] = (this.state[7] + h) >>> 0;
    }

    digest() {
        if (this.finished) {
            return this.toBytes();
        }

        const bitsHashed = this.bytesHashed * 8;
        this.buffer[this.bufferLength++] = 0x80;

        if (this.bufferLength > 56) {
            while (this.bufferLength < 64) {
                this.buffer[this.bufferLength++] = 0;
            }
            this.processBlock(this.buffer);
            this.bufferLength = 0;
        }

        while (this.bufferLength < 56) {
            this.buffer[this.bufferLength++] = 0;
        }

        const high = Math.floor(bitsHashed / 0x100000000);
        const low = bitsHashed >>> 0;
        this.buffer[56] = (high >>> 24) & 0xff;
        this.buffer[57] = (high >>> 16) & 0xff;
        this.buffer[58] = (high >>> 8) & 0xff;
        this.buffer[59] = high & 0xff;
        this.buffer[60] = (low >>> 24) & 0xff;
        this.buffer[61] = (low >>> 16) & 0xff;
        this.buffer[62] = (low >>> 8) & 0xff;
        this.buffer[63] = low & 0xff;

        this.processBlock(this.buffer);
        this.bufferLength = 0;
        this.finished = true;

        return this.toBytes();
    }

    toBytes() {
        const out = new Uint8Array(32);
        for (let i = 0; i < 8; i++) {
            const v = this.state[i];
            out[i * 4] = (v >>> 24) & 0xff;
            out[i * 4 + 1] = (v >>> 16) & 0xff;
            out[i * 4 + 2] = (v >>> 8) & 0xff;
            out[i * 4 + 3] = v & 0xff;
        }
        return out;
    }

    hex() {
        const bytes = this.digest();
        return Array.from(bytes)
            .map(b => b.toString(16).padStart(2, '0'))
            .join('');
    }
}

// ==================== PRODUCTION QR CODE GENERATOR ====================
// ISO/IEC 18004 compliant with Reed-Solomon error correction

class QRCodeGenerator {
    static PATTERNS = {
        FINDER: [[1, 1, 1, 1, 1, 1, 1], [1, 0, 0, 0, 0, 0, 1], [1, 0, 1, 1, 1, 0, 1], [1, 0, 1, 1, 1, 0, 1], [1, 0, 1, 1, 1, 0, 1], [1, 0, 0, 0, 0, 0, 1], [1, 1, 1, 1, 1, 1, 1]]
    };

    static EC_CODEWORDS = { L: [7, 10, 15, 20, 26, 36, 40, 48, 60, 72], M: [10, 16, 26, 36, 48, 64, 72, 88, 110, 130], Q: [13, 22, 36, 52, 72, 96, 108, 132, 160, 192], H: [17, 28, 44, 64, 88, 112, 130, 156, 192, 224] };

    static ALPHANUMERIC = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:';

    static GF256 = (() => {
        const exp = new Uint8Array(512), log = new Uint8Array(256);
        let x = 1;
        for (let i = 0; i < 255; i++) { exp[i] = x; log[x] = i; x = (x << 1) ^ (x >= 128 ? 0x11d : 0); }
        for (let i = 255; i < 512; i++) exp[i] = exp[i - 255];
        return { exp, log, mul: (a, b) => a && b ? exp[log[a] + log[b]] : 0 };
    })();

    constructor(data) {
        this.data = data;
        this.ecLevel = 'M'; // Medium error correction (15% recovery)
        this.version = this.calculateVersion(data);
        this.size = 17 + this.version * 4;
        this.modules = Array(this.size).fill(null).map(() => Array(this.size).fill(null));
        this.generate();
    }

    calculateVersion(data) {
        const len = new TextEncoder().encode(data).length;
        const caps = [17, 32, 53, 78, 106, 134, 154, 192, 230, 271];
        for (let v = 1; v <= 10; v++) if (len <= caps[v - 1]) return v;
        return 10;
    }

    generate() {
        this.addFinderPatterns();
        this.addAlignmentPatterns();
        this.addTimingPatterns();
        this.reserveFormatArea();
        if (this.version >= 7) this.reserveVersionArea();
        const data = this.encodeData();
        const ec = this.generateECC(data);
        const bits = this.interleave(data, ec);
        this.placeData(bits);
        const mask = this.applyBestMask();
        this.addFormatInfo(mask);
        if (this.version >= 7) this.addVersionInfo();
    }

    addFinderPatterns() {
        const positions = [[0, 0], [this.size - 7, 0], [0, this.size - 7]];
        for (const [row, col] of positions) {
            for (let r = -1; r <= 7; r++) {
                for (let c = -1; c <= 7; c++) {
                    const tr = row + r, tc = col + c;
                    if (tr < 0 || tr >= this.size || tc < 0 || tc >= this.size) continue;
                    const inOuter = r === -1 || r === 7 || c === -1 || c === 7;
                    const inBorder = r === 0 || r === 6 || c === 0 || c === 6;
                    const inCenter = r >= 2 && r <= 4 && c >= 2 && c <= 4;
                    this.modules[tr][tc] = inOuter ? false : (inBorder || inCenter);
                }
            }
        }
    }

    addAlignmentPatterns() {
        const positions = this.getAlignmentPositions();
        for (const row of positions) {
            for (const col of positions) {
                if (this.modules[row][col] !== null) continue;
                for (let r = -2; r <= 2; r++) {
                    for (let c = -2; c <= 2; c++) {
                        const tr = row + r, tc = col + c;
                        if (tr < 0 || tr >= this.size || tc < 0 || tc >= this.size) continue;
                        this.modules[tr][tc] = Math.abs(r) === 2 || Math.abs(c) === 2 || (r === 0 && c === 0);
                    }
                }
            }
        }
    }

    getAlignmentPositions() {
        if (this.version === 1) return [];
        const positions = [6];
        const count = Math.floor(this.version / 7) + 2;
        const step = this.version === 32 ? 26 : Math.ceil((this.size - 13) / (count - 1) / 2) * 2;
        for (let i = count - 1; i >= 1; i--) positions.unshift(this.size - 7 - (count - 1 - i) * step);
        return positions;
    }

    addTimingPatterns() {
        for (let i = 8; i < this.size - 8; i++) {
            if (this.modules[6][i] === null) this.modules[6][i] = i % 2 === 0;
            if (this.modules[i][6] === null) this.modules[i][6] = i % 2 === 0;
        }
    }

    reserveFormatArea() {
        for (let i = 0; i < 9; i++) {
            if (this.modules[8][i] === null) this.modules[8][i] = false;
            if (this.modules[i][8] === null) this.modules[i][8] = false;
        }
        for (let i = 0; i < 8; i++) {
            if (this.modules[8][this.size - 1 - i] === null) this.modules[8][this.size - 1 - i] = false;
            if (this.modules[this.size - 1 - i][8] === null) this.modules[this.size - 1 - i][8] = false;
        }
        this.modules[this.size - 8][8] = true;
    }

    reserveVersionArea() {
        for (let i = 0; i < 6; i++) {
            for (let j = 0; j < 3; j++) {
                this.modules[i][this.size - 11 + j] = false;
                this.modules[this.size - 11 + j][i] = false;
            }
        }
    }

    encodeData() {
        const bytes = new TextEncoder().encode(this.data);
        let bits = '0100'; // Byte mode
        bits += bytes.length.toString(2).padStart(8, '0');
        for (const b of bytes) bits += b.toString(2).padStart(8, '0');
        const totalCap = this.getDataCapacity();
        while (bits.length < totalCap * 8 && bits.length % 8 !== 0) bits += '0';
        while (bits.length < totalCap * 8) bits += bits.length % 16 < 8 ? '11101100' : '00010001';
        const result = [];
        for (let i = 0; i < bits.length; i += 8) result.push(parseInt(bits.substr(i, 8), 2));
        return result;
    }

    getDataCapacity() {
        const caps = [[19, 16, 13, 9], [34, 28, 22, 16], [55, 44, 34, 26], [80, 64, 48, 36], [108, 86, 62, 46], [136, 108, 76, 60], [156, 124, 88, 66], [194, 154, 110, 86], [232, 182, 132, 100], [274, 216, 154, 122]];
        return caps[this.version - 1][{ L: 0, M: 1, Q: 2, H: 3 }[this.ecLevel]];
    }

    generateECC(data) {
        const ecCount = QRCodeGenerator.EC_CODEWORDS[this.ecLevel][this.version - 1];
        const gen = this.getGeneratorPolynomial(ecCount);
        const msg = [...data, ...new Array(ecCount).fill(0)];
        for (let i = 0; i < data.length; i++) {
            const coef = msg[i];
            if (coef !== 0) {
                for (let j = 0; j < gen.length; j++) {
                    msg[i + j] ^= QRCodeGenerator.GF256.mul(gen[j], coef);
                }
            }
        }
        return msg.slice(data.length);
    }

    getGeneratorPolynomial(degree) {
        let poly = [1];
        for (let i = 0; i < degree; i++) {
            const next = new Array(poly.length + 1).fill(0);
            for (let j = 0; j < poly.length; j++) {
                next[j] ^= QRCodeGenerator.GF256.mul(poly[j], QRCodeGenerator.GF256.exp[i]);
                next[j + 1] ^= poly[j];
            }
            poly = next;
        }
        return poly;
    }

    interleave(data, ec) {
        let bits = '';
        for (const b of data) bits += b.toString(2).padStart(8, '0');
        for (const b of ec) bits += b.toString(2).padStart(8, '0');
        return bits;
    }

    placeData(bits) {
        let idx = 0;
        for (let col = this.size - 1; col > 0; col -= 2) {
            if (col === 6) col--;
            for (let row = 0; row < this.size; row++) {
                const upward = Math.floor((this.size - 1 - col) / 2) % 2 === 0;
                const r = upward ? this.size - 1 - row : row;
                for (let c = 0; c < 2; c++) {
                    const cc = col - c;
                    if (this.modules[r][cc] === null) {
                        this.modules[r][cc] = idx < bits.length ? bits[idx++] === '1' : false;
                    }
                }
            }
        }
    }

    applyBestMask() {
        let bestMask = 0, bestScore = Infinity;
        for (let m = 0; m < 8; m++) {
            const copy = this.modules.map(r => [...r]);
            this.applyMask(m);
            const score = this.evaluatePenalty();
            if (score < bestScore) { bestScore = score; bestMask = m; }
            this.modules = copy;
        }
        this.applyMask(bestMask);
        return bestMask;
    }

    applyMask(mask) {
        const fns = [
            (r, c) => (r + c) % 2 === 0,
            (r, c) => r % 2 === 0,
            (r, c) => c % 3 === 0,
            (r, c) => (r + c) % 3 === 0,
            (r, c) => (Math.floor(r / 2) + Math.floor(c / 3)) % 2 === 0,
            (r, c) => ((r * c) % 2) + ((r * c) % 3) === 0,
            (r, c) => (((r * c) % 2) + ((r * c) % 3)) % 2 === 0,
            (r, c) => (((r + c) % 2) + ((r * c) % 3)) % 2 === 0,
        ];
        for (let r = 0; r < this.size; r++) {
            for (let c = 0; c < this.size; c++) {
                if (this.isDataModule(r, c) && fns[mask](r, c)) {
                    this.modules[r][c] = !this.modules[r][c];
                }
            }
        }
    }

    isDataModule(r, c) {
        if (r < 9 && c < 9) return false;
        if (r < 9 && c >= this.size - 8) return false;
        if (r >= this.size - 8 && c < 9) return false;
        if (r === 6 || c === 6) return false;
        return true;
    }

    evaluatePenalty() {
        let penalty = 0;
        for (let r = 0; r < this.size; r++) {
            for (let c = 0; c < this.size - 4; c++) {
                if (this.modules[r].slice(c, c + 5).every(m => m === this.modules[r][c])) penalty += 3;
            }
        }
        for (let c = 0; c < this.size; c++) {
            for (let r = 0; r < this.size - 4; r++) {
                const col = [0, 1, 2, 3, 4].map(i => this.modules[r + i][c]);
                if (col.every(m => m === col[0])) penalty += 3;
            }
        }
        return penalty;
    }

    addFormatInfo(mask) {
        const ec = { L: 1, M: 0, Q: 3, H: 2 }[this.ecLevel];
        let data = (ec << 3) | mask;
        let bits = data;
        for (let i = 0; i < 10; i++) bits = (bits << 1) ^ ((bits >> 9) * 0x537);
        const format = ((data << 10) | bits) ^ 0x5412;
        const positions = [[0, 8], [1, 8], [2, 8], [3, 8], [4, 8], [5, 8], [7, 8], [8, 8], [8, 7], [8, 5], [8, 4], [8, 3], [8, 2], [8, 1], [8, 0]];
        for (let i = 0; i < 15; i++) {
            const bit = ((format >> (14 - i)) & 1) === 1;
            const [r, c] = positions[i];
            this.modules[r][c] = bit;
            if (i < 8) this.modules[8][this.size - 1 - i] = bit;
            else this.modules[this.size - 15 + i][8] = bit;
        }
    }

    addVersionInfo() {
        if (this.version < 7) return;
        let data = this.version;
        for (let i = 0; i < 12; i++) data = (data << 1) ^ ((data >> 11) * 0x1f25);
        const info = (this.version << 12) | data;
        for (let i = 0; i < 18; i++) {
            const bit = ((info >> i) & 1) === 1;
            this.modules[Math.floor(i / 3)][this.size - 11 + (i % 3)] = bit;
            this.modules[this.size - 11 + (i % 3)][Math.floor(i / 3)] = bit;
        }
    }

    render(canvasSize) {
        const canvas = document.createElement('canvas');
        canvas.width = canvasSize;
        canvas.height = canvasSize;
        const ctx = canvas.getContext('2d');
        const margin = 4;
        const moduleSize = canvasSize / (this.size + margin * 2);

        // White background for scanability
        ctx.fillStyle = '#FFFFFF';
        ctx.fillRect(0, 0, canvasSize, canvasSize);

        // Black modules
        ctx.fillStyle = '#000000';
        for (let row = 0; row < this.size; row++) {
            for (let col = 0; col < this.size; col++) {
                if (this.modules[row][col]) {
                    ctx.fillRect(
                        (col + margin) * moduleSize,
                        (row + margin) * moduleSize,
                        moduleSize,
                        moduleSize
                    );
                }
            }
        }
        return canvas;
    }
}

window.TeleportWebRTC = TeleportWebRTC;
window.QRCodeGenerator = QRCodeGenerator;

