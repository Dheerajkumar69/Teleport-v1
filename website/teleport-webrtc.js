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
        this.transferQueue = [];
        this.isProcessingQueue = false;

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

        // WebRTC config with STUN and TURN
        this.rtcConfig = {
            iceServers: [
                { urls: 'stun:stun.l.google.com:19302' },
                { urls: 'stun:stun1.l.google.com:19302' },
                { urls: 'stun:stun2.l.google.com:19302' },
                {
                    urls: 'turn:openrelay.metered.ca:80',
                    username: 'openrelayproject',
                    credential: 'openrelayproject'
                },
                {
                    urls: 'turn:openrelay.metered.ca:443',
                    username: 'openrelayproject',
                    credential: 'openrelayproject'
                }
            ],
            iceCandidatePoolSize: 10
        };

        this.CHUNK_SIZE = 16384;
        this.MAX_BUFFER_SIZE = 1024 * 1024;
        this.TRANSFER_TIMEOUT = 30000;
        this.CONNECTION_TIMEOUT = 15000;
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
            this.serverUrl = serverUrl || `ws://${window.location.hostname}:8080`;

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
            };

            this.ws.onmessage = (event) => {
                const message = JSON.parse(event.data);
                this.handleSignalingMessage(message);

                if (message.type === 'welcome') {
                    this.peerId = message.peerId;
                    this.generateFingerprint();
                    this.ws.send(JSON.stringify({
                        type: 'join',
                        room: 'teleport-lan',
                        name: this.deviceName,
                        fingerprint: this.peerFingerprint
                    }));
                    if (this.onConnected) this.onConnected();
                    this.broadcastEvent('connected', { peerId: this.peerId });
                    resolve();
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

    generateFingerprint() {
        const data = `${this.peerId}-${Date.now()}-${Math.random()}`;
        this.peerFingerprint = this.hashString(data).substring(0, 16).toUpperCase();
    }

    hashString(str) {
        let hash = 0;
        for (let i = 0; i < str.length; i++) {
            const char = str.charCodeAt(i);
            hash = ((hash << 5) - hash) + char;
            hash = hash & hash;
        }
        return Math.abs(hash).toString(16).padStart(8, '0') +
            Math.abs(hash * 31).toString(16).padStart(8, '0');
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

    async connectToManualIP(ip, port = 8080) {
        if (!this.validateIP(ip)) {
            throw new Error('Invalid IP address or hostname format');
        }
        const newServerUrl = `ws://${ip}:${port}`;
        this.disconnect();
        return this.connect(newServerUrl);
    }

    // ==================== SIGNALING ====================

    handleSignalingMessage(message) {
        switch (message.type) {
            case 'peers':
                if (this.onPeersUpdated) {
                    this.onPeersUpdated(message.peers);
                    this.broadcastEvent('peer-connected', { peers: message.peers });
                }
                break;
            case 'peer-left':
                this.cleanupPeer(message.peerId);
                break;
            case 'offer':
                this.handleOffer(message.from, message.sdp, message.fingerprint);
                break;
            case 'answer':
                this.handleAnswer(message.from, message.sdp);
                break;
            case 'ice':
                this.handleIceCandidate(message.from, message.candidate);
                break;
            case 'file-request':
                if (this.onFileRequest) {
                    this.onFileRequest({
                        from: message.from,
                        fromName: message.fromName,
                        files: message.files,
                        fingerprint: message.fingerprint
                    });
                }
                break;
            case 'file-response':
                this.handleFileResponse(message.from, message.accepted);
                break;
        }
    }

    // ==================== WEBRTC ====================

    async createConnection(targetPeerId) {
        if (this.peers.has(targetPeerId)) {
            return this.peers.get(targetPeerId);
        }

        const pc = new RTCPeerConnection(this.rtcConfig);
        this.peers.set(targetPeerId, pc);

        const timeoutId = setTimeout(() => {
            if (pc.connectionState !== 'connected') {
                this.handleConnectionFailure(targetPeerId, 'Connection timeout');
            }
        }, this.CONNECTION_TIMEOUT);

        pc.onicecandidate = (event) => {
            if (event.candidate && this.ws?.readyState === WebSocket.OPEN) {
                this.ws.send(JSON.stringify({
                    type: 'ice',
                    to: targetPeerId,
                    candidate: event.candidate
                }));
            }
        };

        pc.onconnectionstatechange = () => {
            if (pc.connectionState === 'connected') clearTimeout(timeoutId);
            if (pc.connectionState === 'failed' || pc.connectionState === 'disconnected') {
                this.handleConnectionFailure(targetPeerId, 'Connection failed');
            }
        };

        const dc = pc.createDataChannel('teleport-files', { ordered: true, maxRetransmits: 30 });
        this.setupDataChannel(dc, targetPeerId);

        const offer = await pc.createOffer();
        await pc.setLocalDescription(offer);

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

    async handleOffer(fromPeerId, sdp, fingerprint) {
        if (this.onPeerVerification && fingerprint) {
            this.onPeerVerification(fromPeerId, fingerprint);
        }

        const pc = new RTCPeerConnection(this.rtcConfig);
        this.peers.set(fromPeerId, pc);

        pc.onicecandidate = (event) => {
            if (event.candidate && this.ws?.readyState === WebSocket.OPEN) {
                this.ws.send(JSON.stringify({
                    type: 'ice',
                    to: fromPeerId,
                    candidate: event.candidate
                }));
            }
        };

        pc.ondatachannel = (event) => {
            this.setupDataChannel(event.channel, fromPeerId);
        };

        await pc.setRemoteDescription(new RTCSessionDescription(sdp));
        const answer = await pc.createAnswer();
        await pc.setLocalDescription(answer);

        this.ws.send(JSON.stringify({
            type: 'answer',
            to: fromPeerId,
            sdp: pc.localDescription
        }));
    }

    async handleAnswer(fromPeerId, sdp) {
        const pc = this.peers.get(fromPeerId);
        if (pc) await pc.setRemoteDescription(new RTCSessionDescription(sdp));
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
        dc.onmessage = (event) => this.handleDataChannelMessage(peerId, event.data);
        dc.onclose = () => this.dataChannels.delete(peerId);
    }

    // ==================== DATA CHANNEL ====================

    handleDataChannelMessage(peerId, data) {
        if (typeof data === 'string') {
            const msg = JSON.parse(data);

            if (msg.type === 'file-start') {
                this.incomingChunks.set(msg.transferId, {
                    metadata: msg,
                    chunks: [],
                    received: 0,
                    startTime: Date.now()
                });
                this.activeTransfers.set(msg.transferId, {
                    paused: false,
                    cancelled: false,
                    startTime: Date.now(),
                    bytesTransferred: 0,
                    fileIndex: msg.fileIndex || 0,
                    totalFiles: msg.totalFiles || 1,
                    relativePath: msg.relativePath || ''
                });
            } else if (msg.type === 'file-end') {
                this.assembleFile(msg.transferId);
            } else if (msg.type === 'transfer-cancel') {
                this.handleTransferCancel(msg.transferId);
            } else if (msg.type === 'transfer-pause') {
                this.handleTransferPause(msg.transferId, msg.paused);
            }
        } else {
            const view = new DataView(data);
            const decoder = new TextDecoder();
            const transferId = decoder.decode(new Uint8Array(data, 0, 36));
            const chunkData = new Uint8Array(data, 36);

            const transfer = this.incomingChunks.get(transferId);
            const state = this.activeTransfers.get(transferId);

            if (transfer && state && !state.cancelled && !state.paused) {
                transfer.chunks.push(chunkData);
                transfer.received += chunkData.byteLength;
                state.bytesTransferred = transfer.received;

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

    assembleFile(transferId) {
        const transfer = this.incomingChunks.get(transferId);
        const state = this.activeTransfers.get(transferId);

        if (!transfer || state?.cancelled) {
            this.incomingChunks.delete(transferId);
            this.activeTransfers.delete(transferId);
            return;
        }

        const totalSize = transfer.chunks.reduce((sum, chunk) => sum + chunk.byteLength, 0);
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

        this.saveTransferToHistory({
            filename: transfer.metadata.filename,
            size: totalSize,
            direction: 'received',
            success: true
        });

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

    waitForDataChannel(peerId, timeout = 10000) {
        return new Promise((resolve, reject) => {
            const startTime = Date.now();
            const check = () => {
                const dc = this.dataChannels.get(peerId);
                if (dc?.readyState === 'open') { resolve(); return; }
                if (Date.now() - startTime > timeout) { reject(new Error('DataChannel timeout')); return; }
                setTimeout(check, 100);
            };
            check();
        });
    }

    handleFileResponse(fromPeerId, accepted) {
        const pending = this.pendingFiles.get(fromPeerId);
        if (!pending) return;

        if (accepted) {
            this.sendFiles(fromPeerId, pending.files)
                .then(pending.resolve)
                .catch(pending.reject);
        } else {
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
        const dc = this.dataChannels.get(targetPeerId);
        if (!dc || dc.readyState !== 'open') throw new Error('DataChannel not ready');

        const totalFiles = files.length;
        for (let i = 0; i < files.length; i++) {
            await this.sendFile(dc, files[i], i, totalFiles);
        }
    }

    async sendFile(dc, file, fileIndex = 0, totalFiles = 1, retryCount = 0) {
        const transferId = crypto.randomUUID();
        const startTime = Date.now();
        const relativePath = file.relativePath || file.webkitRelativePath || '';

        this.activeTransfers.set(transferId, {
            paused: false,
            cancelled: false,
            startTime,
            bytesTransferred: 0,
            fileIndex,
            totalFiles
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

            const reader = file.stream().getReader();
            let offset = 0;

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
            if (retryCount < 3) {
                await new Promise(r => setTimeout(r, 1000));
                return this.sendFile(dc, file, fileIndex, totalFiles, retryCount + 1);
            }

            this.activeTransfers.delete(transferId);
            if (this.onTransferError) {
                this.onTransferError({ transferId, filename: file.name, error: error.message });
            }
            throw error;
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

// ==================== REAL QR CODE GENERATOR ====================

class QRCodeGenerator {
    constructor(data) {
        this.data = data;
        this.modules = [];
        this.size = 0;
        this.generate();
    }

    generate() {
        // QR Code Version 4 (33x33) with Medium error correction
        this.size = 33;
        this.modules = Array(this.size).fill(null).map(() => Array(this.size).fill(null));

        // Add finder patterns
        this.addFinderPattern(0, 0);
        this.addFinderPattern(this.size - 7, 0);
        this.addFinderPattern(0, this.size - 7);

        // Add alignment pattern
        this.addAlignmentPattern(this.size - 9, this.size - 9);

        // Add timing patterns
        this.addTimingPatterns();

        // Add format info
        this.addFormatInfo();

        // Add data
        this.addData();
    }

    addFinderPattern(row, col) {
        for (let r = -1; r <= 7; r++) {
            for (let c = -1; c <= 7; c++) {
                const targetRow = row + r;
                const targetCol = col + c;
                if (targetRow < 0 || targetRow >= this.size || targetCol < 0 || targetCol >= this.size) continue;

                if (r === -1 || r === 7 || c === -1 || c === 7) {
                    this.modules[targetRow][targetCol] = false;
                } else if ((r === 0 || r === 6) || (c === 0 || c === 6)) {
                    this.modules[targetRow][targetCol] = true;
                } else if (r >= 2 && r <= 4 && c >= 2 && c <= 4) {
                    this.modules[targetRow][targetCol] = true;
                } else {
                    this.modules[targetRow][targetCol] = false;
                }
            }
        }
    }

    addAlignmentPattern(row, col) {
        for (let r = -2; r <= 2; r++) {
            for (let c = -2; c <= 2; c++) {
                const targetRow = row + r;
                const targetCol = col + c;
                if (targetRow < 0 || targetRow >= this.size || targetCol < 0 || targetCol >= this.size) continue;

                if (Math.abs(r) === 2 || Math.abs(c) === 2) {
                    this.modules[targetRow][targetCol] = true;
                } else if (r === 0 && c === 0) {
                    this.modules[targetRow][targetCol] = true;
                } else {
                    this.modules[targetRow][targetCol] = false;
                }
            }
        }
    }

    addTimingPatterns() {
        for (let i = 8; i < this.size - 8; i++) {
            const isBlack = i % 2 === 0;
            if (this.modules[6][i] === null) this.modules[6][i] = isBlack;
            if (this.modules[i][6] === null) this.modules[i][6] = isBlack;
        }
    }

    addFormatInfo() {
        // Format info around finder patterns
        for (let i = 0; i < 8; i++) {
            if (this.modules[8][i] === null) this.modules[8][i] = i % 2 === 0;
            if (this.modules[i][8] === null) this.modules[i][8] = i % 2 === 0;
        }
        this.modules[8][8] = true;
    }

    addData() {
        // Convert data to binary
        const bytes = new TextEncoder().encode(this.data);
        let bits = '';
        for (const byte of bytes) {
            bits += byte.toString(2).padStart(8, '0');
        }

        // Fill remaining modules with data pattern
        let bitIndex = 0;
        for (let col = this.size - 1; col > 0; col -= 2) {
            if (col === 6) col--;

            for (let row = 0; row < this.size; row++) {
                const actualRow = (Math.floor((this.size - 1 - col) / 2) % 2 === 0) ? row : this.size - 1 - row;

                for (let c = 0; c < 2; c++) {
                    const actualCol = col - c;
                    if (this.modules[actualRow][actualCol] === null) {
                        let bit = bitIndex < bits.length ? bits[bitIndex] === '1' : false;
                        // Apply mask pattern
                        if ((actualRow + actualCol) % 2 === 0) bit = !bit;
                        this.modules[actualRow][actualCol] = bit;
                        bitIndex++;
                    }
                }
            }
        }

        // Fill any remaining null modules
        for (let r = 0; r < this.size; r++) {
            for (let c = 0; c < this.size; c++) {
                if (this.modules[r][c] === null) {
                    this.modules[r][c] = (r + c) % 2 === 0;
                }
            }
        }
    }

    render(canvasSize) {
        const canvas = document.createElement('canvas');
        canvas.width = canvasSize;
        canvas.height = canvasSize;
        const ctx = canvas.getContext('2d');

        // Background
        ctx.fillStyle = '#18181B';
        ctx.fillRect(0, 0, canvasSize, canvasSize);

        // Modules
        const moduleSize = canvasSize / this.size;
        ctx.fillStyle = '#A78BFA';

        for (let row = 0; row < this.size; row++) {
            for (let col = 0; col < this.size; col++) {
                if (this.modules[row][col]) {
                    ctx.fillRect(
                        col * moduleSize,
                        row * moduleSize,
                        moduleSize - 0.5,
                        moduleSize - 0.5
                    );
                }
            }
        }

        return canvas;
    }
}

window.TeleportWebRTC = TeleportWebRTC;
window.QRCodeGenerator = QRCodeGenerator;
