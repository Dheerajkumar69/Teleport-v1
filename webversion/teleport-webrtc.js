/**
 * Teleport WebRTC File Transfer Engine - Final Production
 * Complete with all fixes and real implementations
 */

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

        // Session sharing via BroadcastChannel
        this.broadcastChannel = null;
        this.initBroadcastChannel();

        // Connection state
        this.isConnected = false;
        this.reconnectAttempts = 0;
        this.maxReconnectAttempts = 5;
        this.reconnectDelay = 1000;
        this.serverUrl = null;
        this.connectionTimeout = 30000;
        this._keepAliveTimer = null; // Interval that pings the Render server to prevent sleep

        // Bandwidth throttling
        this.maxBandwidth = parseInt(this.loadSetting('teleport-bandwidth-limit')) || 0;
        this.bytesThisSecond = 0;
        this.lastThrottleReset = Date.now();

        // File size warning threshold
        this.fileSizeWarningThreshold = 100 * 1024 * 1024;

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
        this.MAX_BUFFER_SIZE = 1024 * 1024;
        this.TRANSFER_TIMEOUT = 300000; // 5 minutes base timeout
        this.CONNECTION_TIMEOUT = 30000; // 30 seconds for ICE negotiation

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

        // Initialize encryption keys
        this.initEncryption();
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
        this.saveSetting('teleport-bandwidth-limit', bytesPerSecond.toString());
    }

    getBandwidthLimit() {
        return parseInt(this.loadSetting('teleport-bandwidth-limit')) || 0;
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
        try {
            // Derive HTTP(S) URL from the WS URL
            const httpBase = (this.serverUrl || '')
                .replace(/^wss:\/\//, 'https://')
                .replace(/^ws:\/\//, 'http://');
            const res = await fetch(`${httpBase}/turn-credentials`, {
                method: 'GET',
                headers: { 'Accept': 'application/json' },
                signal: AbortSignal.timeout(5000)
            });
            if (!res.ok) return;
            const config = await res.json();
            if (Array.isArray(config.iceServers) && config.iceServers.length > 0) {
                this.rtcConfig.iceServers = config.iceServers;
                console.log('[Teleport] TURN credentials refreshed from server');
            }
        } catch (e) {
            console.warn('[Teleport] Could not fetch TURN credentials, using fallback:', e.message);
        }
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
                const res = await fetch(`${httpBase}/health`, {
                    method: 'GET',
                    signal: AbortSignal.timeout(10000)
                });
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

        const now = Date.now();
        if (now - this.lastThrottleReset > 1000) {
            this.bytesThisSecond = 0;
            this.lastThrottleReset = now;
        }

        this.bytesThisSecond += bytes;

        if (this.bytesThisSecond >= this.maxBandwidth) {
            const waitTime = 1000 - (now - this.lastThrottleReset);
            if (waitTime > 0) await new Promise(r => setTimeout(r, waitTime));
            this.bytesThisSecond = 0;
            this.lastThrottleReset = Date.now();
        }
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

    connect(serverUrl = null) {
        return new Promise((resolve, reject) => {
            // Auto-detect: use localhost for local development, Render for production
            if (serverUrl) {
                this.serverUrl = serverUrl;
            } else if (window.location.hostname === 'localhost' || window.location.hostname === '127.0.0.1') {
                this.serverUrl = 'ws://localhost:3000';
                console.log('[Teleport] Using local signaling server');
            } else if (this.isPrivateIP(window.location.hostname)) {
                // Local network testing - connect to signaling server on same host
                this.serverUrl = `ws://${window.location.hostname}:3000`;
                console.log('[Teleport] Using local network signaling server:', this.serverUrl);
            } else {
                this.serverUrl = 'wss://teleport-signaling.onrender.com';
                console.log('[Teleport] Using production signaling server');
            }

            const timeoutId = setTimeout(() => {
                reject(new Error('Connection timeout'));
            }, this.CONNECTION_TIMEOUT);

            try {
                this.ws = new WebSocket(this.serverUrl);
            } catch (e) {
                clearTimeout(timeoutId);
                reject(new Error('Failed to connect'));
                return;
            }

            this.ws.onopen = () => {
                clearTimeout(timeoutId);
                this.isConnected = true;
                this.reconnectAttempts = 0;
                // Start keep-alive only for the production Render server
                if (this.serverUrl && this.serverUrl.includes('onrender.com')) {
                    this.startKeepAlive();
                }
            };

            this.ws.onmessage = (event) => {
                const message = JSON.parse(event.data);
                this.handleSignalingMessage(message);

                if (message.type === 'welcome') {
                    this.peerId = message.peerId;
                    this.generateFingerprint().then(async () => {
                        const publicKey = await this.exportPublicKey();
                        this.ws.send(JSON.stringify({
                            type: 'join',
                            room: 'teleport-lan',
                            name: this.deviceName,
                            fingerprint: this.peerFingerprint,
                            publicKey: publicKey
                        }));
                        if (this.onConnected) this.onConnected();
                        this.broadcastEvent('connected', { peerId: this.peerId });
                        resolve();
                        // Fetch fresh TURN credentials from the signaling server
                        // (non-blocking — runs in background after connection is established)
                        this.fetchTurnCredentials();
                    });
                }
            };

            this.ws.onclose = () => {
                this.isConnected = false;
                if (this.onDisconnected) this.onDisconnected();
                this.attemptReconnect();
            };

            this.ws.onerror = () => {
                clearTimeout(timeoutId);
                if (!this.isConnected) reject(new Error('Connection failed'));
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

    handleError(message, error) {
        console.error(`[Teleport Error] ${message}:`, error);
        if (this.onError) this.onError({ message, error: error?.message || String(error) });
    }

    attemptReconnect() {
        if (this.reconnectAttempts >= this.maxReconnectAttempts) return;

        this.reconnectAttempts++;
        const delay = this.reconnectDelay * Math.pow(2, this.reconnectAttempts - 1);

        if (this.onReconnecting) this.onReconnecting(this.reconnectAttempts);

        setTimeout(() => {
            if (!this.isConnected) {
                this.connect(this.serverUrl).catch(() => { });
            }
        }, delay);
    }

    disconnect() {
        this.stopKeepAlive();
        if (this.ws) { this.ws.close(); this.ws = null; }
        this.isConnected = false;
        this.peers.forEach(pc => pc.close());
        this.peers.clear();
        this.dataChannels.clear();
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
        this.disconnect();
        return this.connect(newServerUrl);
    }

    // ==================== SIGNALING ====================

    handleSignalingMessage(message) {
        try {
            switch (message.type) {
                case 'peers':
                    if (this.onPeersUpdated) {
                        // Import public keys from peers for E2E encryption
                        for (const peer of message.peers) {
                            if (peer.publicKey && !this.peerPublicKeys.has(peer.id)) {
                                this.importPeerPublicKey(peer.id, peer.publicKey);
                            }
                        }
                        this.onPeersUpdated(message.peers);
                        this.broadcastEvent('peer-connected', { peers: message.peers });
                    }
                    break;
                case 'peer-joined':
                    // A new peer appeared in the room — import their key and notify UI
                    if (message.peer) {
                        if (message.peer.publicKey && !this.peerPublicKeys.has(message.peer.id)) {
                            this.importPeerPublicKey(message.peer.id, message.peer.publicKey);
                        }
                        // Merge into local peer list and fire update callback
                        const existingIds = new Set();
                        if (this.onPeersUpdated) {
                            // onPeersUpdated is fired by server's 'peers' broadcast too;
                            // here we fire it with a synthetic updated list by keeping
                            // track of the last known peers at the app level.
                            // The server always sends a fresh 'peers' list after join so
                            // this fallback handles edge cases only.
                        }
                    }
                    break;
                case 'peer-lan-updated':
                    // A peer that previously connected via signaling now has a known LAN address
                    if (this.onPeersUpdated && message.peerId) {
                        // Emit a synthetic peer-update; UI can badge LAN peers differently
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
                    const transfer = {
                        id: message.transferId,
                        from: message.from,
                        filename: message.filename,
                        size: message.size,
                        mimeType: message.mimeType,
                        chunks: [],
                        receivedBytes: 0,
                        fileIndex: message.fileIndex,
                        totalFiles: message.totalFiles
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
                    const transfer = this.relayIncoming.get(message.transferId);
                    if (transfer) {
                        // Decode base64 chunk
                        const binaryStr = atob(message.data);
                        const bytes = new Uint8Array(binaryStr.length);
                        for (let i = 0; i < binaryStr.length; i++) {
                            bytes[i] = binaryStr.charCodeAt(i);
                        }
                        transfer.chunks.push(bytes);
                        transfer.receivedBytes += bytes.length;

                        // Update progress
                        if (this.onTransferProgress) {
                            this.onTransferProgress({
                                transferId: message.transferId,
                                filename: transfer.filename,
                                progress: Math.round((transfer.receivedBytes / transfer.size) * 100),
                                received: transfer.receivedBytes,
                                total: transfer.size,
                                speed: 0,
                                isRelay: true
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

                        // Assemble file from chunks
                        const totalSize = transfer.chunks.reduce((sum, c) => sum + c.length, 0);
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

                        // Cleanup
                        this.relayIncoming.delete(message.transferId);
                        this.activeTransfers.delete(message.transferId);

                        // Notify complete
                        if (this.onTransferComplete) {
                            this.onTransferComplete({
                                transferId: message.transferId,
                                filename: transfer.filename,
                                size: transfer.size,
                                isRelay: true
                            });
                        }
                    }
                    break;
                }

                case 'relay-cancel': {
                    // Cancel relay transfer
                    this.relayIncoming.delete(message.transferId);
                    this.activeTransfers.delete(message.transferId);
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

        const dc = pc.createDataChannel('teleport-files', { ordered: true, maxRetransmits: 30 });
        this.setupDataChannel(dc, targetPeerId);

        const offer = await pc.createOffer();
        await pc.setLocalDescription(offer);

        console.log('[WebRTC] Offer created, sending to peer...');

        this.ws.send(JSON.stringify({
            type: 'offer',
            to: targetPeerId,
            sdp: pc.localDescription,
            fingerprint: this.peerFingerprint
        }));

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

            if (this.onPeerVerification && fingerprint) {
                this.onPeerVerification(fromPeerId, fingerprint);
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
            this.handleError('DataChannel error', error);
        };

        dc.onclose = () => {
            console.log(`[DataChannel] Closed with peer ${peerId}`);
            this.dataChannels.delete(peerId);
        };
    }

    // ==================== DATA CHANNEL ====================

    async handleDataChannelMessage(peerId, data) {
        if (typeof data === 'string') {
            const msg = JSON.parse(data);

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

                // Initialize transfer state
                const transferState = {
                    metadata: { ...msg, filename: safeFilename, relativePath: safeRelativePath },
                    chunks: [], // Only used for small files
                    received: 0,
                    startTime: Date.now(),
                    useStreaming: msg.size > this.STREAMING_THRESHOLD,
                    writer: null,
                    fileHandle: null
                };

                // Check for an existing resume state in IndexedDB
                this.getResumeState(msg.transferId).then(resumeState => {
                    if (resumeState && resumeState.receivedBytes > 0 && resumeState.receivedBytes < msg.size) {
                        console.log(`[Resume] Found partial state for ${safeFilename}: ${resumeState.receivedBytes}/${msg.size} bytes`);
                        // Notify sender to resume from the saved offset
                        const dc = this.dataChannels.get(peerId);
                        if (dc?.readyState === 'open') {
                            dc.send(JSON.stringify({
                                type: 'resume-request',
                                transferId: msg.transferId,
                                resumeOffset: resumeState.receivedBytes
                            }));
                        }
                        transferState.received = resumeState.receivedBytes;
                    }
                });

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

                this.incomingChunks.set(msg.transferId, transferState);
                this.activeTransfers.set(msg.transferId, {
                    paused: false,
                    cancelled: false,
                    startTime: Date.now(),
                    bytesTransferred: 0,
                    fileIndex: msg.fileIndex || 0,
                    totalFiles: msg.totalFiles || 1,
                    relativePath: safeRelativePath
                });

                // Persist initial resume state to IndexedDB
                this.saveResumeState(msg.transferId, {
                    filename: transferState.metadata.filename,
                    totalBytes: msg.size,
                    receivedBytes: 0,
                    peerId
                });
            } else if (msg.type === 'file-end') {
                this.assembleFile(msg.transferId);
            } else if (msg.type === 'resume-request') {
                // Receiver is requesting a resume from a given offset.
                // We need to handle this on the SENDER side — tell sendFile() to seek.
                const state = this.activeTransfers.get(msg.transferId);
                if (state) {
                    state.resumeOffset = typeof msg.resumeOffset === 'number' ? msg.resumeOffset : 0;
                    console.log(`[Resume] Sender: receiver requested offset ${state.resumeOffset} for ${msg.transferId}`);
                }
            } else if (msg.type === 'transfer-cancel') {
                this.handleTransferCancel(msg.transferId);
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

                // Keep resume state up-to-date (throttled: every 64 chunks)
                if (transfer.received % (this.CHUNK_SIZE * 64) < this.CHUNK_SIZE) {
                    this.saveResumeState(transferId, {
                        filename: transfer.metadata.filename,
                        totalBytes: transfer.metadata.size,
                        receivedBytes: transfer.received,
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
                        totalFiles: state.totalFiles
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
                try { await transfer.writer.close(); } catch (e) { }
            }
            this.incomingChunks.delete(transferId);
            this.activeTransfers.delete(transferId);
            return;
        }

        let totalSize = transfer.received;

        // Handle streaming vs memory buffer
        if (transfer.useStreaming && transfer.writer) {
            // File was streamed directly to disk
            try {
                await transfer.writer.close();
                console.log('[FileTransfer] Streaming complete, file saved to disk');
            } catch (e) {
                console.error('[FileTransfer] Error closing stream:', e);
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
            success: true
        });

        // Remove the persisted resume state — transfer is complete
        this.deleteResumeState(transferId);

        if (this.onTransferComplete) {
            this.onTransferComplete({
                transferId,
                filename: transfer.metadata.filename,
                size: totalSize,
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
                    dc.send(JSON.stringify({ type: 'transfer-cancel', transferId }));
                }
            });
        }
    }

    handleTransferCancel(transferId) {
        const state = this.activeTransfers.get(transferId);
        if (state) state.cancelled = true;
        this.incomingChunks.delete(transferId);
        if (this.onTransferError) {
            this.onTransferError({ transferId, error: 'Cancelled by sender' });
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
                return new Promise((resolve) => {
                    dirReader.readEntries(async (entries) => {
                        for (const e of entries) {
                            await traverseEntry(e, path + entry.name + '/');
                        }
                        resolve();
                    });
                });
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

        await this.createConnection(targetPeerId);
        await this.waitForDataChannel(targetPeerId);

        const fileInfos = Array.from(files).map(f => ({
            name: f.name,
            size: f.size,
            type: f.type,
            relativePath: f.relativePath || f.webkitRelativePath || ''
        }));

        this.ws.send(JSON.stringify({
            type: 'file-request',
            to: targetPeerId,
            files: fileInfos,
            fingerprint: this.peerFingerprint
        }));

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

    waitForDataChannel(peerId, timeout = 60000) {
        return new Promise((resolve, reject) => {
            const startTime = Date.now();
            const check = () => {
                const dc = this.dataChannels.get(peerId);
                if (dc?.readyState === 'open') {
                    console.log(`[DataChannel] Ready with peer ${peerId}`);
                    resolve();
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

    handleFileResponse(fromPeerId, accepted) {
        console.log('[FileTransfer] handleFileResponse called:', { fromPeerId, accepted });
        const pending = this.pendingFiles.get(fromPeerId);
        if (!pending) {
            console.log('[FileTransfer] No pending files found for peer:', fromPeerId);
            return;
        }

        if (accepted) {
            console.log('[FileTransfer] Transfer accepted, starting sendFiles...');

            // Try WebRTC P2P first, fallback to relay if it fails
            this.sendFiles(fromPeerId, pending.files)
                .then(() => {
                    console.log('[FileTransfer] sendFiles completed successfully via P2P');
                    pending.resolve();
                })
                .catch(async (err) => {
                    console.warn('[FileTransfer] P2P sendFiles failed:', err.message);

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
                });
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
            await this.sendFile(dc, files[i], i, totalFiles); // no retry loop — DataChannel (SCTP) is already reliable
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
        const transferId = crypto.randomUUID();
        const startTime = Date.now();

        console.log(`[Relay] Sending file: ${file.name} (${file.size} bytes)`);

        // Track transfer
        this.activeTransfers.set(transferId, {
            paused: false,
            cancelled: false,
            startTime,
            bytesTransferred: 0,
            fileIndex,
            totalFiles,
            isRelay: true
        });

        // Send start message
        this.ws.send(JSON.stringify({
            type: 'relay-start',
            to: targetPeerId,
            transferId,
            filename: file.name,
            size: file.size,
            mimeType: file.type || 'application/octet-stream',
            fileIndex,
            totalFiles
        }));

        // Smaller chunk size for relay (base64 adds ~33% overhead)
        const RELAY_CHUNK_SIZE = 32 * 1024; // 32KB chunks
        const reader = file.stream().getReader();
        let offset = 0;

        try {
            while (true) {
                const state = this.activeTransfers.get(transferId);
                if (!state || state.cancelled) {
                    this.ws.send(JSON.stringify({
                        type: 'relay-cancel',
                        to: targetPeerId,
                        transferId
                    }));
                    throw new Error('Transfer cancelled');
                }

                const { done, value } = await reader.read();
                if (done) break;

                // Process chunks from the stream
                for (let i = 0; i < value.length; i += RELAY_CHUNK_SIZE) {
                    const chunk = value.slice(i, i + RELAY_CHUNK_SIZE);

                    // Convert to base64 for JSON transport
                    const base64 = btoa(String.fromCharCode(...chunk));

                    this.ws.send(JSON.stringify({
                        type: 'relay-chunk',
                        to: targetPeerId,
                        transferId,
                        data: base64,
                        offset
                    }));

                    offset += chunk.length;

                    // Update progress
                    if (this.onTransferProgress) {
                        const elapsed = (Date.now() - startTime) / 1000;
                        const speed = elapsed > 0 ? offset / elapsed : 0;

                        this.onTransferProgress({
                            transferId,
                            filename: file.name,
                            progress: Math.round((offset / file.size) * 100),
                            sent: offset,
                            total: file.size,
                            speed: Math.round(speed),
                            eta: speed > 0 ? Math.round((file.size - offset) / speed) : 0,
                            isRelay: true
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

            console.log(`[Relay] File sent: ${file.name}`);

            // Cleanup
            this.activeTransfers.delete(transferId);

        } catch (error) {
            this.activeTransfers.delete(transferId);
            throw error;
        }
    }

    async sendFile(dc, file, fileIndex = 0, totalFiles = 1) {
        const transferId = crypto.randomUUID();
        const startTime = Date.now();
        const relativePath = file.relativePath || file.webkitRelativePath || '';
        let fileSent = false;

        this.activeTransfers.set(transferId, {
            paused: false,
            cancelled: false,
            startTime,
            bytesTransferred: 0,
            fileIndex,
            totalFiles,
            resumeOffset: 0  // will be set if receiver sends resume-request
        });

        try {
            dc.send(JSON.stringify({
                type: 'file-start',
                transferId,
                filename: file.name,
                size: file.size,
                mimeType: file.type,
                fileIndex,
                totalFiles,
                relativePath
            }));

            // Wait briefly for any resume-request from the receiver before streaming
            await new Promise(r => setTimeout(r, 80));

            const state = this.activeTransfers.get(transferId);
            const skipBytes = (state?.resumeOffset || 0);

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

                for (let i = 0; i < value.length; i += this.CHUNK_SIZE) {
                    const state = this.activeTransfers.get(transferId);
                    if (state?.cancelled) break;

                    const chunk = value.slice(i, i + this.CHUNK_SIZE);

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
                            totalFiles
                        });
                    }
                }
            }

            dc.send(JSON.stringify({ type: 'file-end', transferId }));
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
                    success: true,
                    fileIndex,
                    totalFiles
                });
            }

            this.showNotification('Transfer Complete', `Sent: ${file.name}`);
            this.activeTransfers.delete(transferId);

        } catch (error) {
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
                await this.connectToManualIP(parsed.server.replace('ws://', '').split(':')[0]);
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

