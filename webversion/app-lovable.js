/**
 * Teleport Web App — Production UI v3.1
 * B&W redesign + all critical bug fixes applied
 *
 * IMPORTANT: Load order matters. This file must load AFTER teleport-webrtc.js
 * (which defines the TeleportWebRTC class). If the engine fails to load,
 * polyfills below will provide degraded fallbacks.
 */

(function () {
    'use strict';

    if (typeof TeleportWebRTC === 'undefined') {
        console.error('[Teleport] TeleportWebRTC class not found — teleport-webrtc.js may have failed to load. UI will use polyfill fallbacks.');
    }

    const teleport = new TeleportWebRTC();

    // ══════════════════════════════════════════════════════════════
    // POLYFILLS — patch missing methods on TeleportWebRTC instance
    // ══════════════════════════════════════════════════════════════

    if (!teleport.getTheme) {
        teleport.getTheme = () => {
            try { return localStorage.getItem('teleport-theme') || 'dark'; }
            catch (e) { return 'dark'; }
        };
    }

    if (!teleport.setTheme) {
        teleport.setTheme = (t) => {
            try { localStorage.setItem('teleport-theme', t); } catch (e) {}
        };
    }

    if (!teleport.getFileType) {
        teleport.getFileType = function (filename) {
            const ext = (filename || '').split('.').pop().toLowerCase();
            const map = {
                jpg: 'image', jpeg: 'image', png: 'image', gif: 'image',
                webp: 'image', svg: 'image', bmp: 'image', ico: 'image', avif: 'image',
                mp4: 'video', mkv: 'video', avi: 'video', mov: 'video',
                webm: 'video', flv: 'video', wmv: 'video', m4v: 'video',
                mp3: 'audio', wav: 'audio', ogg: 'audio', flac: 'audio',
                aac: 'audio', m4a: 'audio', opus: 'audio', wma: 'audio',
                pdf: 'pdf',
                doc: 'document', docx: 'document', odt: 'document',
                rtf: 'document', pages: 'document',
                xls: 'spreadsheet', xlsx: 'spreadsheet', ods: 'spreadsheet',
                csv: 'spreadsheet', numbers: 'spreadsheet',
                ppt: 'presentation', pptx: 'presentation', odp: 'presentation', key: 'presentation',
                zip: 'archive', rar: 'archive', '7z': 'archive',
                tar: 'archive', gz: 'archive', bz2: 'archive', xz: 'archive',
                js: 'code', ts: 'code', py: 'code', java: 'code', c: 'code',
                cpp: 'code', h: 'code', hpp: 'code', css: 'code', html: 'code',
                json: 'code', xml: 'code', yaml: 'code', yml: 'code',
                sh: 'code', go: 'code', rs: 'code', kt: 'code', swift: 'code',
                rb: 'code', php: 'code', cs: 'code', vue: 'code', jsx: 'code',
                exe: 'executable', dmg: 'executable', apk: 'executable',
                app: 'executable', deb: 'executable', rpm: 'executable', msi: 'executable',
                txt: 'text', md: 'text', log: 'text', ini: 'text', cfg: 'text',
            };
            return map[ext] || 'file';
        };
    }

    if (!teleport.generatePairingData) {
        teleport.generatePairingData = function () {
            if (!teleport.peerId) return null;
            try {
                return JSON.stringify({
                    id: teleport.peerId,
                    name: teleport.deviceName,
                    fingerprint: teleport.peerFingerprint || ''
                });
            } catch (e) { return teleport.peerId; }
        };
    }

    if (!teleport.validateIP) {
        teleport.validateIP = function (ip) {
            if (!ip || typeof ip !== 'string') return false;
            const s = ip.trim();
            if (!s || s.length > 253) return false;
            return /^[\w.\-:]+$/.test(s);
        };
    }

    if (!teleport.requestNotificationPermission) {
        teleport.requestNotificationPermission = function () {
            if (typeof Notification !== 'undefined' && Notification.permission === 'default') {
                Notification.requestPermission().catch(() => {});
            }
        };
    }

    if (!teleport.connectToManualIP) {
        teleport.connectToManualIP = function (ip) {
            // Try reconnecting to a custom signaling server IP
            const wsUrl = ip.includes(':')
                ? `ws://${ip}`
                : `ws://${ip}:3001`;
            return new Promise((resolve, reject) => {
                try {
                    const ws = new WebSocket(wsUrl);
                    ws.onopen  = () => { ws.close(); resolve(); };
                    ws.onerror = () => reject(new Error('Could not connect to ' + wsUrl));
                    setTimeout(() => reject(new Error('Connection timeout')), 5000);
                } catch (e) { reject(e); }
            });
        };
    }

    if (!teleport.setDeviceName) {
        teleport.setDeviceName = function (name) {
            if (name) {
                teleport.deviceName = name;
                try { localStorage.setItem('teleport-device-name', name); } catch (e) {}
            }
        };
    }

    if (!teleport.generateQRCode) {
        // Fallback when teleport-webrtc.js hasn't loaded — show an error
        // instead of drawing a fake QR code.
        teleport.generateQRCode = function (data, size) {
            console.warn('[Polyfill] QR generation unavailable — teleport-webrtc.js not loaded');
            const canvas = document.createElement('canvas');
            canvas.width  = size || 160;
            canvas.height = size || 160;
            const ctx = canvas.getContext('2d');
            ctx.fillStyle = '#fff';
            ctx.fillRect(0, 0, canvas.width, canvas.height);
            ctx.fillStyle = '#999';
            ctx.font = '12px system-ui';
            ctx.textAlign = 'center';
            ctx.fillText('QR unavailable', canvas.width / 2, canvas.height / 2);
            return canvas;
        };
    }

    // Transfer control method polyfills
    if (!teleport.pauseTransfer) {
        teleport.pauseTransfer = (id) => console.warn('[Polyfill] pauseTransfer not implemented, id:', id);
    }
    if (!teleport.resumeTransfer) {
        teleport.resumeTransfer = (id) => console.warn('[Polyfill] resumeTransfer not implemented, id:', id);
    }
    if (!teleport.cancelTransfer) {
        teleport.cancelTransfer = (id) => console.warn('[Polyfill] cancelTransfer not implemented, id:', id);
    }
    if (!teleport.acceptFileRequest) {
        teleport.acceptFileRequest = (from) => console.warn('[Polyfill] acceptFileRequest not implemented, from:', from);
    }
    if (!teleport.rejectFileRequest) {
        teleport.rejectFileRequest = (from) => console.warn('[Polyfill] rejectFileRequest not implemented, from:', from);
    }
    if (!teleport.requestFileSend) {
        teleport.requestFileSend = () => Promise.reject(new Error('requestFileSend not implemented'));
    }

    // ══════════════════════════════════════════════════════════════
    // STATE
    // ══════════════════════════════════════════════════════════════

    let peers = [];
    let selectedFiles = [];
    let selectedPeer = null;
    let transfers = new Map();
    let isReceiving = false;
    let pendingRequestQueue = [];
    let currentTheme = teleport.getTheme() || 'dark';
    const previewUrls = new Set();

    // Backward-compatible getter: returns the currently-displayed request (or null).
    Object.defineProperty(window, 'pendingRequest', {
        get() { return pendingRequestQueue[0] || null; },
        configurable: true,
    });

    // Request notification permission
    teleport.requestNotificationPermission();

    // ══════════════════════════════════════════════════════════════
    // AUDIO
    // ══════════════════════════════════════════════════════════════

    let audioCtx = null;

    function getAudioContext() {
        if (!audioCtx) {
            audioCtx = new (window.AudioContext || window.webkitAudioContext)();
        }
        return audioCtx;
    }

    const sounds = {
        click:        () => playTone(900,  0.04, 'sine'),
        success:      () => playTone(880,  0.12, 'sine'),
        error:        () => playTone(280,  0.18, 'sawtooth'),
        notification: () => playTone(660,  0.10, 'sine'),
        celebration:  () => {
            playTone(523, 0.25, 'sine');
            setTimeout(() => playTone(659, 0.25, 'sine'), 100);
            setTimeout(() => playTone(784, 0.25, 'sine'), 200);
            setTimeout(() => playTone(1047, 0.35, 'sine'), 300);
        }
    };

    function playTone(frequency, duration, type = 'sine') {
        try {
            const ctx = getAudioContext();
            if (ctx.state === 'suspended') ctx.resume();
            const osc  = ctx.createOscillator();
            const gain = ctx.createGain();
            osc.connect(gain);
            gain.connect(ctx.destination);
            osc.frequency.value = frequency;
            osc.type = type;
            gain.gain.setValueAtTime(0.12, ctx.currentTime);
            gain.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + duration);
            osc.start(ctx.currentTime);
            osc.stop(ctx.currentTime + duration);
        } catch (e) { /* audio unavailable */ }
    }

    // ══════════════════════════════════════════════════════════════
    // SVG ICON LIBRARY
    // ══════════════════════════════════════════════════════════════

    const SVG_PROPS = 'viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"';

    const icons = {
        file:       `<svg ${SVG_PROPS}><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/></svg>`,
        image:      `<svg ${SVG_PROPS}><rect x="3" y="3" width="18" height="18" rx="2"/><circle cx="8.5" cy="8.5" r="1.5"/><polyline points="21 15 16 10 5 21"/></svg>`,
        video:      `<svg ${SVG_PROPS}><rect x="2" y="2" width="20" height="20" rx="2.18"/><path d="M10 8l6 4-6 4V8z"/></svg>`,
        audio:      `<svg ${SVG_PROPS}><path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/></svg>`,
        pdf:        `<svg ${SVG_PROPS}><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/><line x1="9" y1="15" x2="15" y2="15"/></svg>`,
        document:   `<svg ${SVG_PROPS}><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/><line x1="16" y1="13" x2="8" y2="13"/><line x1="16" y1="17" x2="8" y2="17"/><polyline points="10 9 9 9 8 9"/></svg>`,
        text:       `<svg ${SVG_PROPS}><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/></svg>`,
        spreadsheet:`<svg ${SVG_PROPS}><rect x="3" y="3" width="18" height="18" rx="2"/><path d="M3 9h18M9 3v18"/></svg>`,
        presentation:`<svg ${SVG_PROPS}><rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8M12 17v4"/></svg>`,
        archive:    `<svg ${SVG_PROPS}><polyline points="21 8 21 21 3 21 3 8"/><rect x="1" y="3" width="22" height="5"/><line x1="10" y1="12" x2="14" y2="12"/></svg>`,
        code:       `<svg ${SVG_PROPS}><polyline points="16 18 22 12 16 6"/><polyline points="8 6 2 12 8 18"/></svg>`,
        executable: `<svg ${SVG_PROPS}><rect x="4" y="4" width="16" height="16" rx="2"/><path d="M9 12l2 2 4-4"/></svg>`,
        check:      `<svg ${SVG_PROPS}><polyline points="20 6 9 17 4 12"/></svg>`,
        x:          `<svg ${SVG_PROPS}><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>`,
        info:       `<svg ${SVG_PROPS}><circle cx="12" cy="12" r="10"/><line x1="12" y1="16" x2="12" y2="12"/><line x1="12" y1="8" x2="12.01" y2="8"/></svg>`,
        warning:    `<svg ${SVG_PROPS}><path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg>`,
        lock:       `<svg ${SVG_PROPS}><rect x="3" y="11" width="18" height="11" rx="2" ry="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/></svg>`,
        unlock:     `<svg ${SVG_PROPS}><rect x="3" y="11" width="18" height="11" rx="2" ry="2"/><path d="M7 11V7a5 5 0 0 1 9.9-1"/></svg>`,
        wifi:       `<svg ${SVG_PROPS}><path d="M1.42 9a16 16 0 0 1 21.16 0"/><path d="M5 12.55a11 11 0 0 1 14.08 0"/><path d="M8.53 16.11a6 6 0 0 1 6.95 0"/><line x1="12" y1="20" x2="12.01" y2="20"/></svg>`,
        monitor:    `<svg ${SVG_PROPS}><rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8M12 17v4"/></svg>`,
        send:       `<svg ${SVG_PROPS}><line x1="22" y1="2" x2="11" y2="13"/><polygon points="22 2 15 22 11 13 2 9 22 2"/></svg>`,
        spin:       `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><circle cx="12" cy="12" r="10" stroke-dasharray="60 40"/></svg>`,
    };

    function getFileIcon(filename) {
        const type = teleport.getFileType(filename);
        return icons[type] || icons.file;
    }

    function getFileTypeClass(filename) {
        return teleport.getFileType(filename) || 'file';
    }

    // ══════════════════════════════════════════════════════════════
    // DOM REFERENCES
    // ══════════════════════════════════════════════════════════════

    const statusDot          = document.getElementById('status-dot');
    const statusText         = document.getElementById('status-text');
    const devicesGrid        = document.getElementById('devices-grid');
    const dropZone           = document.getElementById('drop-zone');
    const fileInput          = document.getElementById('file-input');
    const folderInput        = document.getElementById('folder-input');
    const fileList           = document.getElementById('file-list');
    const recipientSelect    = document.getElementById('recipient-select');
    const sendBtn            = document.getElementById('send-btn');
    const receiveToggle      = document.getElementById('receive-toggle');
    const receiveStatus      = document.getElementById('receive-status');
    const transfersList      = document.getElementById('transfers-list');
    const historyList        = document.getElementById('history-list');
    const deviceNameInput    = document.getElementById('device-name-input');
    const peerIdDisplay      = document.getElementById('peer-id');
    const fingerprintDisplay = document.getElementById('fingerprint-display');
    const fileModal          = document.getElementById('file-modal');
    const modalDesc          = document.getElementById('modal-desc');
    const modalFiles         = document.getElementById('modal-files');
    const modalFingerprint   = document.getElementById('modal-fingerprint');
    const modalAccept        = document.getElementById('modal-accept');
    const modalReject        = document.getElementById('modal-reject');
    const toastContainer     = document.getElementById('toast-container');
    const themeToggle        = document.getElementById('theme-toggle');
    const themeToggleHeader  = document.getElementById('theme-toggle-header');
    const manualIpInput      = document.getElementById('manual-ip');
    const manualConnectBtn   = document.getElementById('manual-connect-btn');
    const qrCodeContainer    = document.getElementById('qr-code');
    const bandwidthSlider    = document.getElementById('bandwidth-slider');
    const bandwidthValue     = document.getElementById('bandwidth-value');

    // ══════════════════════════════════════════════════════════════
    // RIPPLE EFFECT
    // ══════════════════════════════════════════════════════════════

    function addRipple(element, event) {
        if (!element || !event || event.clientX === undefined) return;
        try {
            const ripple = document.createElement('span');
            ripple.className = 'ripple';
            const rect = element.getBoundingClientRect();
            const size = Math.max(rect.width, rect.height);
            ripple.style.cssText = [
                `width:${size}px`,
                `height:${size}px`,
                `left:${event.clientX - rect.left - size / 2}px`,
                `top:${event.clientY - rect.top - size / 2}px`,
            ].join(';');
            // Only set overflow on elements that need it (not sidebar tabs or circular buttons)
            const skip = element.classList.contains('tab-btn') ||
                         element.classList.contains('receive-toggle');
            if (!skip) {
                element.style.position = 'relative';
                element.style.overflow = 'hidden';
            }
            element.appendChild(ripple);
            setTimeout(() => ripple.remove(), 700);
        } catch (e) { /* ripple is cosmetic only */ }
    }

    // ══════════════════════════════════════════════════════════════
    // SKELETON LOADERS
    // ══════════════════════════════════════════════════════════════

    function showSkeletons(container, count = 3) {
        container.innerHTML = Array(count).fill(`
            <div class="skeleton-card">
                <div class="skeleton skeleton-icon"></div>
                <div class="sk-content">
                    <div class="skeleton skeleton-title"></div>
                    <div class="skeleton skeleton-text"></div>
                </div>
            </div>
        `).join('');
    }

    // ══════════════════════════════════════════════════════════════
    // TAB NAVIGATION
    // ══════════════════════════════════════════════════════════════

    const tabButtons  = document.querySelectorAll('.tab-btn');
    const tabPanes    = document.querySelectorAll('.tab-pane');

    function switchTab(tabName) {
        try {
            tabButtons.forEach(b => b.classList.remove('active'));
            tabPanes.forEach(p => p.classList.remove('active'));

            const targetBtn  = document.querySelector(`.tab-btn[data-tab="${tabName}"]`);
            const targetPane = document.getElementById(`tab-${tabName}`);

            if (targetBtn)  targetBtn.classList.add('active');
            if (targetPane) targetPane.classList.add('active');

            if (tabName === 'settings') generateQRCode();
        } catch (e) {
            console.error('[Teleport] Tab switch error:', e);
        }
    }

    tabButtons.forEach(btn => {
        btn.addEventListener('click', (e) => {
            try {
                sounds.click();
                addRipple(btn, e);
                switchTab(btn.dataset.tab);
            } catch (err) {
                console.error('[Teleport] Tab click error:', err);
            }
        });
    });

    // Keyboard nav for sidebar
    tabButtons.forEach(btn => {
        btn.addEventListener('keydown', (e) => {
            if (e.key === 'Enter' || e.key === ' ') {
                e.preventDefault();
                switchTab(btn.dataset.tab);
            }
        });
    });

    // ══════════════════════════════════════════════════════════════
    // TOAST NOTIFICATIONS
    // ══════════════════════════════════════════════════════════════

    function showToast(message, type = 'info') {
        // Don't play sound here — callers already play appropriate sound
        const typeIconMap = {
            success: icons.check,
            error:   icons.x,
            info:    icons.info,
            warning: icons.warning,
        };

        const toast = document.createElement('div');
        toast.className = `toast ${type}`;
        toast.innerHTML = `
            <div class="toast-dot">${typeIconMap[type] || typeIconMap.info}</div>
            <div class="toast-message">${escapeHtml(message)}</div>
        `;
        if (toastContainer) toastContainer.appendChild(toast);

        const removeToast = () => {
            if (!toast.isConnected) return;
            toast.classList.add('removing');
            setTimeout(() => toast.remove(), 300);
        };

        const timeoutId = setTimeout(removeToast, 4500);
        toast.addEventListener('click', () => { clearTimeout(timeoutId); removeToast(); });
    }

    // ══════════════════════════════════════════════════════════════
    // CONFETTI (monochrome)
    // ══════════════════════════════════════════════════════════════

    function celebrate() {
        sounds.celebration();
        const colors = ['#FFFFFF', '#DDDDDD', '#BBBBBB', '#999999', '#777777', '#EEEEEE'];
        for (let i = 0; i < 48; i++) {
            const piece = document.createElement('div');
            piece.className = 'confetti-piece';
            piece.style.left = Math.random() * 100 + 'vw';
            piece.style.top  = '-10px';
            piece.style.background = colors[Math.floor(Math.random() * colors.length)];
            piece.style.animationDelay    = (Math.random() * 0.6) + 's';
            piece.style.animationDuration = (2 + Math.random() * 2) + 's';
            document.body.appendChild(piece);
            setTimeout(() => piece.remove(), 4000);
        }
    }

    // ══════════════════════════════════════════════════════════════
    // FORMATTING HELPERS
    // ══════════════════════════════════════════════════════════════

    function formatSize(bytes) {
        if (bytes < 1024)                  return bytes + ' B';
        if (bytes < 1024 * 1024)           return (bytes / 1024).toFixed(1) + ' KB';
        if (bytes < 1024 * 1024 * 1024)   return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
        return (bytes / (1024 * 1024 * 1024)).toFixed(2) + ' GB';
    }

    function formatSpeed(bps) {
        if (bps < 1024)       return bps.toFixed(0) + ' B/s';
        if (bps < 1024*1024)  return (bps / 1024).toFixed(1) + ' KB/s';
        return (bps / (1024*1024)).toFixed(1) + ' MB/s';
    }

    function formatETA(seconds) {
        if (!isFinite(seconds) || seconds < 0) return '--';
        if (seconds < 60)   return Math.ceil(seconds) + 's';
        if (seconds < 3600) return Math.ceil(seconds / 60) + 'm ' + Math.ceil(seconds % 60) + 's';
        return Math.floor(seconds / 3600) + 'h ' + Math.ceil((seconds % 3600) / 60) + 'm';
    }

    function formatTime(timestamp) {
        const date = new Date(timestamp);
        const diff = Date.now() - date;
        if (diff < 60000)     return 'Just now';
        if (diff < 3600000)   return Math.floor(diff / 60000) + 'm ago';
        if (diff < 86400000)  return Math.floor(diff / 3600000) + 'h ago';
        return date.toLocaleDateString();
    }

    function normalizeProgressValue(value) {
        if (typeof value !== 'number' || Number.isNaN(value)) return 0;
        const n = value > 1 ? value / 100 : value;
        return Math.max(0, Math.min(1, n));
    }

    function escapeHtml(str) {
        if (typeof str !== 'string') return '';
        const div = document.createElement('div');
        div.textContent = str;
        return div.innerHTML;
    }

    function clearPreviewUrls() {
        for (const url of previewUrls) URL.revokeObjectURL(url);
        previewUrls.clear();
    }

    // ══════════════════════════════════════════════════════════════
    // CONNECTION STATUS
    // ══════════════════════════════════════════════════════════════

    function updateConnectionStatus(connected, reconnecting = false) {
        if (statusDot)  statusDot.classList.remove('connected', 'reconnecting');
        if (reconnecting) {
            if (statusDot)  statusDot.classList.add('reconnecting');
            if (statusText) statusText.textContent = 'Reconnecting…';
        } else if (connected) {
            if (statusDot)  statusDot.classList.add('connected');
            if (statusText) statusText.textContent = 'Connected';
        } else {
            if (statusText) statusText.textContent = 'Disconnected';
        }
    }

    // ══════════════════════════════════════════════════════════════
    // DEVICES
    // ══════════════════════════════════════════════════════════════

    function renderDevices() {
        if (peers.length === 0) {
            devicesGrid.innerHTML = `
                <div class="empty-state" style="grid-column:1/-1">
                    <div class="empty-radar">
                        <div class="ring"></div>
                        <div class="ring"></div>
                        <div class="ring"></div>
                        <div class="center">
                            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                                <circle cx="11" cy="11" r="8"/>
                                <path d="M21 21l-4.35-4.35"/>
                            </svg>
                        </div>
                    </div>
                    <h3>Searching for devices…</h3>
                    <p>Make sure other devices have Teleport open</p>
                </div>
            `;
            return;
        }

        devicesGrid.innerHTML = peers.map((peer, idx) => {
            const lanBadge = peer.isLan
                ? `<span class="lan-badge">${icons.wifi} LAN</span>`
                : '';
            const fpHtml = peer.fingerprint
                ? `<div class="device-fingerprint">${escapeHtml(peer.fingerprint)}</div>`
                : '';

            return `
                <div class="device-card ${selectedPeer === peer.id ? 'selected' : ''}"
                     data-peer-id="${escapeHtml(peer.id)}"
                     style="animation-delay:${idx * 0.07}s">
                    <div class="device-icon-wrap">
                        ${icons.monitor}
                    </div>
                    <div class="device-name">${escapeHtml(peer.name)}</div>
                    <div class="device-meta">
                        <span class="device-online-dot"></span>
                        Online${lanBadge}
                    </div>
                    ${fpHtml}
                </div>
            `;
        }).join('');

        devicesGrid.querySelectorAll('.device-card').forEach(card => {
            card.addEventListener('click', () => {
                sounds.click();
                selectedPeer = card.dataset.peerId;
                renderDevices();
                updateRecipientSelect();
            });
        });
    }

    function updateRecipientSelect() {
        recipientSelect.innerHTML = '<option value="">— Choose a device —</option>';
        peers.forEach(peer => {
            const opt = document.createElement('option');
            opt.value = peer.id;
            opt.textContent = peer.name;
            if (peer.id === selectedPeer) opt.selected = true;
            recipientSelect.appendChild(opt);
        });
        updateSendButton();
    }

    // ══════════════════════════════════════════════════════════════
    // FILE LIST
    // ══════════════════════════════════════════════════════════════

    function renderFileList() {
        if (selectedFiles.length === 0) {
            clearPreviewUrls();
            fileList.innerHTML = '';
            return;
        }

        clearPreviewUrls();
        const totalSize = selectedFiles.reduce((s, f) => s + f.size, 0);

        const summaryHtml = `
            <div class="file-summary">
                <span>${selectedFiles.length} file(s) selected</span>
                <span>${formatSize(totalSize)}</span>
            </div>
        `;

        const itemsHtml = selectedFiles.map((file, idx) => {
            const isImage = file.type.startsWith('image/');
            const fileType = getFileTypeClass(file.name);
            let previewContent = '';

            if (isImage) {
                const url = URL.createObjectURL(file);
                previewUrls.add(url);
                previewContent = `<img src="${url}" class="file-preview" alt="preview">`;
            } else {
                previewContent = getFileIcon(file.name);
            }

            const displayName = file.relativePath || file.webkitRelativePath || file.name;
            const pathHint = file.relativePath
                ? ' • ' + escapeHtml(file.relativePath.split('/').slice(0, -1).join('/'))
                : '';

            return `
                <div class="file-item" style="animation-delay:${idx * 0.04}s">
                    <div class="file-icon ${fileType} ${isImage ? 'has-preview' : ''}">
                        ${previewContent}
                    </div>
                    <div class="file-info">
                        <div class="file-name" title="${escapeHtml(displayName)}">${escapeHtml(file.name)}</div>
                        <div class="file-size">${formatSize(file.size)}${pathHint}</div>
                    </div>
                    <button class="file-remove" data-idx="${idx}" title="Remove file">
                        ${icons.x}
                    </button>
                </div>
            `;
        }).join('');

        fileList.innerHTML = summaryHtml + itemsHtml;

        fileList.querySelectorAll('.file-remove').forEach(btn => {
            btn.addEventListener('click', (e) => {
                e.stopPropagation();
                sounds.click();
                const idx = parseInt(btn.dataset.idx);
                selectedFiles.splice(idx, 1);
                renderFileList();
                updateSendButton();
            });
        });
    }

    function updateSendButton() {
        const recipient = recipientSelect.value;
        sendBtn.disabled = selectedFiles.length === 0 || !recipient;
    }

    // ══════════════════════════════════════════════════════════════
    // TRANSFERS
    // ══════════════════════════════════════════════════════════════

    function renderTransfers() {
        if (transfers.size === 0) {
            transfersList.innerHTML = `
                <div class="empty-state small">
                    <p style="color:var(--text-4)">No active transfers</p>
                </div>
            `;
            return;
        }

        const statusIconMap = {
            'complete':  icons.check,
            'sending':   icons.send,
            'receiving': `<svg ${SVG_PROPS}><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>`,
            'paused':    `<svg ${SVG_PROPS}><rect x="6" y="4" width="4" height="16"/><rect x="14" y="4" width="4" height="16"/></svg>`,
            'error':     icons.x,
            'cancelled': icons.x,
        };

        transfersList.innerHTML = Array.from(transfers.values()).map(t => {
            const pct       = Math.round(t.progress * 100);
            const speedText = Number.isFinite(t.speed) ? formatSpeed(Math.max(0, t.speed)) : '--';
            const etaText   = Number.isFinite(t.eta) ? formatETA(t.eta) : '--';
            const fileProgress = t.totalFiles > 1
                ? `<div class="transfer-file-count">File ${(t.fileIndex || 0) + 1} / ${t.totalFiles}</div>`
                : '';
            const protocolTag = t.protocol && t.protocol !== 'WebRTC'
                ? `<span style="font-size:11px;color:var(--text-4);margin-left:6px;">[${escapeHtml(t.protocol)}]</span>`
                : '';

            const hasControls = !['complete','error','cancelled'].includes(t.status);
            const controlsHtml = hasControls ? `
                <div class="transfer-controls">
                    <button class="control-btn ${t.status === 'paused' ? 'resume-btn' : 'pause-btn'}"
                            data-action="${t.status === 'paused' ? 'resume' : 'pause'}">
                        ${t.status === 'paused' ? 'Resume' : 'Pause'}
                    </button>
                    <button class="control-btn cancel-btn" data-action="cancel">Cancel</button>
                </div>
            ` : '';

            return `
                <div class="transfer-card" data-transfer-id="${escapeHtml(t.id)}">
                    <div class="transfer-header">
                        <div class="transfer-file-icon">${getFileIcon(t.filename)}</div>
                        <div class="transfer-info">
                            <div class="transfer-title">${escapeHtml(t.filename)}${protocolTag}</div>
                            ${fileProgress}
                        </div>
                        <span class="transfer-status-badge ${t.status}">
                            ${escapeHtml(t.status.charAt(0).toUpperCase() + t.status.slice(1))}
                        </span>
                    </div>
                    <div class="progress-bar">
                        <div class="progress-fill" style="width:${pct}%"></div>
                    </div>
                    <div class="transfer-meta">
                        <span>${formatSize(t.transferred || 0)} / ${formatSize(t.total)}</span>
                        <span>${speedText} · ETA: ${etaText}</span>
                        <span>${pct}%</span>
                    </div>
                    ${controlsHtml}
                </div>
            `;
        }).join('');

        transfersList.querySelectorAll('.control-btn').forEach(btn => {
            btn.addEventListener('click', (e) => {
                e.stopPropagation();
                sounds.click();
                const card = btn.closest('.transfer-card');
                const tid  = card.dataset.transferId;
                const action = btn.dataset.action;
                const t = transfers.get(tid);
                if (!t) return;

                if (action === 'pause') {
                    teleport.pauseTransfer(tid);
                    t.status = 'paused';
                    renderTransfers();
                } else if (action === 'resume') {
                    teleport.resumeTransfer(tid);
                    t.status = t.direction === 'send' ? 'sending' : 'receiving';
                    renderTransfers();
                } else if (action === 'cancel') {
                    teleport.cancelTransfer(tid);
                    t.status = 'cancelled';
                    showToast('Transfer cancelled', 'info');
                    setTimeout(() => { transfers.delete(tid); renderTransfers(); }, 2000);
                }
            });
        });
    }

    // ══════════════════════════════════════════════════════════════
    // HISTORY
    // ══════════════════════════════════════════════════════════════

    function renderHistory() {
        if (!historyList) return;
        const history = teleport.getTransferHistory?.() || [];

        if (history.length === 0) {
            historyList.innerHTML = `
                <div class="empty-state small">
                    <p style="color:var(--text-4)">No transfer history yet</p>
                </div>
            `;
            return;
        }

        historyList.innerHTML = `
            <div class="history-header">
                <span style="font-size:13px;color:var(--text-3)">Recent (${history.length})</span>
                <button class="clear-history-btn" id="clear-history">Clear All</button>
            </div>
            ${history.slice(0, 20).map(h => `
                <div class="history-item ${h.success ? 'success' : 'failed'}">
                    <div class="history-icon">${getFileIcon(h.filename)}</div>
                    <div class="history-info">
                        <div class="history-name">${escapeHtml(h.filename)}</div>
                        <div class="history-meta">
                            ${formatSize(h.size)} · ${h.direction === 'sent' ? 'Sent' : 'Received'} · ${formatTime(h.timestamp)}
                        </div>
                    </div>
                    <div class="history-status-icon">
                        ${h.success ? icons.check : icons.x}
                    </div>
                </div>
            `).join('')}
        `;

        document.getElementById('clear-history')?.addEventListener('click', () => {
            teleport.clearTransferHistory?.();
            renderHistory();
            showToast('History cleared', 'info');
        });
    }

    // ══════════════════════════════════════════════════════════════
    // FILE REQUEST MODAL
    // ══════════════════════════════════════════════════════════════

    function showFileRequestModal(request) {
        sounds.notification();
        pendingRequestQueue.push(request);
        // Only show the modal if no other request is currently displayed.
        if (pendingRequestQueue.length === 1) {
            renderFileRequestModal(request);
        }
    }

    function renderFileRequestModal(request) {
        const encBadge = request.encrypted
            ? `<span class="encryption-badge encrypted">${icons.lock} Encrypted</span>`
            : `<span class="encryption-badge">${icons.unlock} Unencrypted</span>`;

        modalDesc.innerHTML = `${escapeHtml(request.fromName)} wants to send you files ${encBadge}`;

        modalFiles.innerHTML = (request.files || []).map(f => `
            <div class="modal-file-item">
                <div class="modal-file-icon">${getFileIcon(f.name)}</div>
                <span class="modal-file-name">${escapeHtml(f.name)}</span>
                <span class="modal-file-size">${formatSize(f.size)}</span>
            </div>
        `).join('');

        if (request.fingerprint && modalFingerprint) {
            modalFingerprint.innerHTML = `
                <div class="fingerprint-verify">
                    <div class="fingerprint-label">
                        ${icons.lock}
                        Verify sender fingerprint
                    </div>
                    <div class="fingerprint-code">${escapeHtml(request.fingerprint)}</div>
                </div>
            `;
        }

        fileModal.classList.add('active');
    }

    function hideFileRequestModal() {
        fileModal.classList.remove('active');
        pendingRequestQueue.shift();
        // Show the next queued request if any.
        if (pendingRequestQueue.length > 0) {
            setTimeout(() => renderFileRequestModal(pendingRequestQueue[0]), 300);
        }
    }

    // ══════════════════════════════════════════════════════════════
    // QR CODE
    // ══════════════════════════════════════════════════════════════

    function generateQRCode() {
        if (!qrCodeContainer || !teleport.peerId) return;
        const data = teleport.generatePairingData();
        if (!data) return;

        try {
            const canvas = teleport.generateQRCode(data, 180);
            canvas.style.borderRadius = '12px';
            canvas.style.border = '1px solid var(--border)';
            qrCodeContainer.innerHTML = '';
            qrCodeContainer.appendChild(canvas);

            const info = document.createElement('div');
            info.className = 'qr-info';
            info.innerHTML = `
                <div class="qr-peer-id">Scan to connect</div>
                <div class="qr-fingerprint">${escapeHtml(teleport.peerFingerprint || '')}</div>
            `;
            qrCodeContainer.appendChild(info);
        } catch (e) {
            qrCodeContainer.innerHTML = `<p style="color:var(--text-4);font-size:13px;">QR generation unavailable</p>`;
        }
    }

    // ══════════════════════════════════════════════════════════════
    // THEME
    // ══════════════════════════════════════════════════════════════

    const MOON_ICON = `<path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"/>`;
    const SUN_ICON  = `
        <circle cx="12" cy="12" r="5"/>
        <line x1="12" y1="1" x2="12" y2="3"/>
        <line x1="12" y1="21" x2="12" y2="23"/>
        <line x1="4.22" y1="4.22" x2="5.64" y2="5.64"/>
        <line x1="18.36" y1="18.36" x2="19.78" y2="19.78"/>
        <line x1="1" y1="12" x2="3" y2="12"/>
        <line x1="21" y1="12" x2="23" y2="12"/>
        <line x1="4.22" y1="19.78" x2="5.64" y2="18.36"/>
        <line x1="18.36" y1="5.64" x2="19.78" y2="4.22"/>
    `;

    function applyTheme(theme) {
        document.body.classList.toggle('light-theme', theme === 'light');
        teleport.setTheme(theme);
        currentTheme = theme;

        const isDark   = theme === 'dark';
        const iconPath = isDark ? MOON_ICON : SUN_ICON;

        ['theme-icon-header', 'theme-icon-settings'].forEach(id => {
            const el = document.getElementById(id);
            if (el) el.innerHTML = iconPath;
        });

        const label = document.getElementById('theme-label');
        if (label) label.textContent = isDark ? 'Dark Mode' : 'Light Mode';
    }

    // ══════════════════════════════════════════════════════════════
    // BANDWIDTH
    // ══════════════════════════════════════════════════════════════

    function updateBandwidth() {
        if (!bandwidthSlider || !bandwidthValue) return;
        const val = parseInt(bandwidthSlider.value);
        const limit = val === 0 ? 0 : val * 1024 * 1024;
        teleport.setBandwidthLimit?.(limit);
        bandwidthValue.textContent = val === 0 ? 'Unlimited' : `${val} MB/s`;
    }

    // ══════════════════════════════════════════════════════════════
    // EVENT HANDLERS
    // ══════════════════════════════════════════════════════════════

    fileInput?.addEventListener('change', (e) => {
        selectedFiles = [...selectedFiles, ...Array.from(e.target.files)];
        renderFileList();
        updateSendButton();
        if (e.target.files.length > 0) {
            showToast(`${e.target.files.length} file(s) added`, 'success');
        }
        e.target.value = ''; // allow re-selecting same file
    });

    // Folder upload — feature detection for webkitdirectory (Chrome/Edge only)
    const folderBtn = document.getElementById('folder-btn');
    const supportsFolderUpload = (() => {
        try {
            const input = document.createElement('input');
            input.type = 'file';
            return 'webkitdirectory' in input || 'directory' in input;
        } catch (e) { return false; }
    })();

    if (!supportsFolderUpload && folderBtn) {
        folderBtn.title = 'Folder upload is only supported in Chrome and Edge';
        folderBtn.style.opacity = '0.5';
        folderBtn.addEventListener('click', (e) => {
            e.preventDefault();
            e.stopPropagation();
            showToast('Folder upload is only supported in Chrome and Edge', 'warning');
        }, true);
    } else {
        folderBtn?.addEventListener('click', () => {
            sounds.click();
            folderInput?.click();
        });
    }

    folderInput?.addEventListener('change', async (e) => {
        const files = Array.from(e.target.files);
        files.forEach(f => { f.relativePath = f.webkitRelativePath || f.name; });
        selectedFiles = [...selectedFiles, ...files];
        renderFileList();
        updateSendButton();
        showToast(`${files.length} file(s) from folder added`, 'success');
        e.target.value = '';
    });

    // Drag and drop
    dropZone?.addEventListener('dragover', (e) => {
        e.preventDefault();
        dropZone.classList.add('dragover');
    });

    dropZone?.addEventListener('dragleave', (e) => {
        if (!dropZone.contains(e.relatedTarget)) {
            dropZone.classList.remove('dragover');
        }
    });

    dropZone?.addEventListener('drop', async (e) => {
        e.preventDefault();
        dropZone.classList.remove('dragover');

        const items = e.dataTransfer.items;
        let hasDirectory = false;
        for (const item of items) {
            const entry = item.webkitGetAsEntry?.();
            if (entry?.isDirectory) { hasDirectory = true; break; }
        }

        if (hasDirectory && teleport.getFilesWithStructure) {
            showToast('Processing folder…', 'info');
            try {
                const files = await teleport.getFilesWithStructure(items);
                selectedFiles = [...selectedFiles, ...files];
                renderFileList();
                updateSendButton();
                sounds.success();
                showToast(`${files.length} file(s) from folder ready`, 'success');
            } catch (err) {
                sounds.error();
                showToast('Failed to read folder', 'error');
            }
        } else {
            const files = Array.from(e.dataTransfer.files);
            selectedFiles = [...selectedFiles, ...files];
            renderFileList();
            updateSendButton();
            sounds.success();
            showToast(`${files.length} file(s) ready`, 'success');
        }
    });

    dropZone?.addEventListener('click', () => {
        sounds.click();
        fileInput?.click();
    });

    recipientSelect?.addEventListener('change', () => {
        sounds.click();
        selectedPeer = recipientSelect.value;
        // Validate the selected peer still exists
        if (selectedPeer && !peers.some(p => p.id === selectedPeer)) {
            selectedPeer = null;
            showToast('Peer is no longer available', 'warning');
        }
        updateSendButton();
    });

    sendBtn?.addEventListener('click', async (e) => {
        if (selectedFiles.length === 0 || !selectedPeer) return;
        // Validate peer still exists before sending
        if (!peers.some(p => p.id === selectedPeer)) {
            selectedPeer = null;
            updateSendButton();
            showToast('Peer disconnected — select another device', 'error');
            return;
        }
        addRipple(sendBtn, e);
        sounds.click();

        sendBtn.disabled = true;
        sendBtn.innerHTML = `
            <svg class="spin" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" style="width:16px;height:16px;">
                <circle cx="12" cy="12" r="10" stroke-dasharray="60 40"/>
            </svg>
            Sending…
        `;

        try {
            await teleport.requestFileSend(selectedPeer, selectedFiles);
            showToast('All files sent!', 'success');
            selectedFiles = [];
            renderFileList();
        } catch (err) {
            sounds.error();
            showToast(err.message || 'Transfer failed', 'error');
        }

        sendBtn.innerHTML = `
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="width:16px;height:16px;">
                <line x1="22" y1="2" x2="11" y2="13"/>
                <polygon points="22 2 15 22 11 13 2 9 22 2"/>
            </svg>
            Send Files
        `;
        updateSendButton();
        renderHistory();
    });

    receiveToggle?.addEventListener('click', (e) => {
        sounds.click();
        addRipple(receiveToggle, e);
        isReceiving = !isReceiving;

        if (isReceiving) {
            receiveToggle.classList.add('active');
            if (receiveStatus) receiveStatus.textContent = 'Ready to receive';
            showToast('Now accepting transfers', 'success');
        } else {
            receiveToggle.classList.remove('active');
            if (receiveStatus) receiveStatus.textContent = 'Click to start receiving';
        }
    });


    modalAccept?.addEventListener('click', (e) => {
        addRipple(modalAccept, e);
        sounds.success();
        const request = pendingRequestQueue[0];
        if (request) {
            teleport.acceptFileRequest(request.from);
            showToast('Receiving files…', 'success');
        }
        hideFileRequestModal();
    });

    modalReject?.addEventListener('click', (e) => {
        addRipple(modalReject, e);
        sounds.click();
        const request = pendingRequestQueue[0];
        if (request) {
            teleport.rejectFileRequest(request.from);
            showToast('Transfer declined', 'info');
        }
        hideFileRequestModal();
    });

    // Close modal on overlay click
    fileModal?.addEventListener('click', (e) => {
        if (e.target === fileModal) hideFileRequestModal();
    });

    deviceNameInput?.addEventListener('change', () => {
        sounds.click();
        teleport.setDeviceName(deviceNameInput.value.trim() || teleport.deviceName);
        showToast('Device name saved', 'success');
    });

    themeToggle?.addEventListener('click', () => {
        sounds.click();
        applyTheme(currentTheme === 'dark' ? 'light' : 'dark');
    });

    themeToggleHeader?.addEventListener('click', () => {
        sounds.click();
        applyTheme(currentTheme === 'dark' ? 'light' : 'dark');
    });

    bandwidthSlider?.addEventListener('input', updateBandwidth);

    manualConnectBtn?.addEventListener('click', async () => {
        const ip = manualIpInput?.value?.trim();
        if (!ip) { showToast('Enter an IP address', 'error'); return; }

        if (teleport.validateIP && !teleport.validateIP(ip)) {
            sounds.error();
            showToast('Invalid IP address format', 'error');
            return;
        }

        sounds.click();
        showToast('Connecting…', 'info');

        try {
            await teleport.connectToManualIP(ip);
            showToast('Connected to ' + ip, 'success');
        } catch (err) {
            sounds.error();
            showToast('Connection failed: ' + (err.message || 'unknown error'), 'error');
        }
    });

    // ══════════════════════════════════════════════════════════════
    // WEBRTC CALLBACKS
    // ══════════════════════════════════════════════════════════════

    teleport.onConnected = () => {
        updateConnectionStatus(true);
        if (peerIdDisplay)      peerIdDisplay.textContent      = teleport.peerId || '';
        if (fingerprintDisplay) fingerprintDisplay.textContent = teleport.peerFingerprint || 'N/A';
        sounds.success();
        showToast('Connected to network', 'success');
        generateQRCode();
    };

    teleport.onDisconnected = () => {
        updateConnectionStatus(false);
        // Show "Unavailable" instead of leaving "Generating…" forever
        if (fingerprintDisplay && fingerprintDisplay.textContent === 'Generating…') {
            fingerprintDisplay.textContent = 'Unavailable';
        }
        showToast('Disconnected from server', 'error');
        // sound played by showToast callers - don't double-play here
    };

    teleport.onReconnecting = (attempt) => {
        updateConnectionStatus(false, true);
        showToast(`Reconnecting (attempt ${attempt}/5)…`, 'warning');
    };

    teleport.onConnectionFailed = (error) => {
        updateConnectionStatus(false);
        if (statusText) statusText.textContent = 'Offline';
        sounds.error();
        showToast('Connection failed — retrying in 5s…', 'error');
        setTimeout(() => {
            if (!teleport.isConnected) {
                teleport.connect().catch(() => {});
            }
        }, 5000);
    };

    teleport.onPeersUpdated = (newPeers) => {
        const prevCount = peers.length;
        peers = newPeers;
        renderDevices();
        updateRecipientSelect();
        const diff = newPeers.length - prevCount;
        if (diff > 0) showToast(`${diff} new device(s) found`, 'success');
    };

    // LAN peer announce re-render
    try {
        if (teleport.broadcastChannel) {
            const _orig = teleport.broadcastChannel.onmessage;
            teleport.broadcastChannel.onmessage = (event) => {
                try {
                    if (_orig) _orig(event);
                    if (event.data?.type === 'peer-lan-updated') renderDevices();
                } catch (e) { /* ignore broadcastChannel errors */ }
            };
        }
    } catch (e) { /* broadcastChannel unavailable */ }

    teleport.onFileRequest = (request) => {
        if (isReceiving) {
            showFileRequestModal(request);
        } else {
            teleport.rejectFileRequest(request.from);
            showToast('Transfer declined (receiving mode off)', 'info');
        }
    };

    teleport.onFileSizeWarning = (largeFiles) => {
        return new Promise(resolve => {
            const modal = document.getElementById('large-file-modal');
            const desc  = document.getElementById('large-file-desc');
            const list  = document.getElementById('large-file-list');
            const cancelBtn    = document.getElementById('large-file-cancel');
            const proceedBtn   = document.getElementById('large-file-proceed');

            if (!modal) { resolve(confirm('Large files detected. Continue?')); return; }

            desc.textContent = `${largeFiles.length} file(s) over 100 MB detected:`;
            list.innerHTML = largeFiles.map(f =>
                `<div class="modal-file-item">
                    <span class="modal-file-name">${escapeHtml(f.name)}</span>
                    <span class="modal-file-size">${f.sizeFormatted}</span>
                </div>`
            ).join('');

            modal.classList.add('active');

            const cleanup = (result) => {
                modal.classList.remove('active');
                cancelBtn.removeEventListener('click', onCancel);
                proceedBtn.removeEventListener('click', onProceed);
                modal.removeEventListener('click', onOverlay);
                resolve(result);
            };
            const onCancel  = () => cleanup(false);
            const onProceed = () => cleanup(true);
            const onOverlay = (e) => { if (e.target === modal) cleanup(false); };

            cancelBtn.addEventListener('click', onCancel);
            proceedBtn.addEventListener('click', onProceed);
            modal.addEventListener('click', onOverlay);
        });
    };

    teleport.onPeerVerification = (peerId, fingerprint) => {
        console.log(`[Security] Peer ${peerId} fingerprint: ${fingerprint}`);
    };

    // Throttle progress re-renders to max 8fps to prevent jank
    let _progressRenderPending = false;

    teleport.onTransferProgress = (progress) => {
        const np = normalizeProgressValue(progress.progress);
        let t = transfers.get(progress.transferId);

        if (!t) {
            t = {
                id:          progress.transferId || (progress.filename + Date.now()),
                filename:    progress.filename || 'Unknown file',
                total:       typeof progress.total === 'number' ? progress.total : 0,
                transferred: 0,
                progress:    0,
                status:      progress.sent !== undefined ? 'sending' : 'receiving',
                direction:   progress.sent !== undefined ? 'send' : 'receive',
                speed:       0,
                eta:         0,
                fileIndex:   progress.fileIndex  || 0,
                totalFiles:  progress.totalFiles || 1,
            };
            transfers.set(t.id, t);
        }

        t.transferred = progress.received ?? progress.sent ?? t.transferred ?? 0;
        t.progress    = np;
        t.speed       = typeof progress.speed === 'number' ? progress.speed : t.speed;
        t.eta         = typeof progress.eta   === 'number' ? progress.eta   : t.eta;
        t.fileIndex   = Number.isFinite(progress.fileIndex)  ? progress.fileIndex  : t.fileIndex;
        t.totalFiles  = Number.isFinite(progress.totalFiles) ? progress.totalFiles : t.totalFiles;
        t.protocol    = progress.protocol || t.protocol || 'WebRTC';
        if (typeof progress.total === 'number') t.total = progress.total;

        // Switch to Transfers tab automatically on first progress event
        const isFirstEvent = !_progressRenderPending &&
            !document.getElementById('tab-transfers')?.classList.contains('active');
        if (isFirstEvent && t.status === 'receiving') switchTab('transfers');

        if (!_progressRenderPending) {
            _progressRenderPending = true;
            requestAnimationFrame(() => {
                renderTransfers();
                _progressRenderPending = false;
            });
        }
    };

    teleport.onTransferComplete = (result) => {
        let t = transfers.get(result.transferId);
        if (!t) {
            t = {
                id:          result.transferId,
                filename:    result.filename,
                total:       typeof result.size === 'number' ? result.size : 0,
                transferred: typeof result.size === 'number' ? result.size : 0,
                progress:    1,
                status:      'complete',
                direction:   'receive',
                speed: 0, eta: 0,
                fileIndex:  Number.isFinite(result.fileIndex)  ? result.fileIndex  : 0,
                totalFiles: Number.isFinite(result.totalFiles) ? result.totalFiles : 1,
            };
            transfers.set(result.transferId, t);
        }

        t.progress    = 1;
        t.status      = 'complete';
        t.transferred = t.total;
        renderTransfers();

        setTimeout(() => { transfers.delete(result.transferId); renderTransfers(); }, 5000);

        if (!result.totalFiles || result.fileIndex === result.totalFiles - 1) celebrate();

        showToast(`${result.filename} complete!`, 'success');
        renderHistory();
    };

    teleport.onTransferError = (error) => {
        sounds.error();
        const t = transfers.get(error.transferId);
        if (t) { t.status = 'error'; renderTransfers(); }
        showToast(error.error || 'Transfer failed', 'error');
    };

    teleport.onError = (error) => {
        console.error('[Teleport]', error);
        sounds.error();
        showToast(error.message || 'An error occurred', 'error');
    };

    // ══════════════════════════════════════════════════════════════
    // INIT
    // ══════════════════════════════════════════════════════════════

    if (deviceNameInput) deviceNameInput.value = teleport.deviceName || '';
    applyTheme(currentTheme);
    renderHistory();
    renderTransfers();

    // Restore bandwidth setting
    try {
        const savedBW = teleport.getBandwidthLimit?.() || 0;
        if (bandwidthSlider) {
            bandwidthSlider.value = Math.round(savedBW / (1024 * 1024));
            updateBandwidth();
        }
    } catch (e) { /* getBandwidthLimit unavailable */ }

    // Show skeletons while connecting
    if (devicesGrid) showSkeletons(devicesGrid, 3);

    // Fallback: if fingerprint still shows "Generating…" after 10 seconds,
    // it means connection failed or fingerprint generation failed.
    setTimeout(() => {
        if (fingerprintDisplay && fingerprintDisplay.textContent === 'Generating…') {
            fingerprintDisplay.textContent = 'Unavailable';
        }
    }, 10000);

    // Cleanup preview URLs on page hide
    window.addEventListener('beforeunload', clearPreviewUrls);
    window.addEventListener('pagehide', clearPreviewUrls);

    // Global unhandled JS error logger  
    window.addEventListener('unhandledrejection', (e) => {
        console.error('[Teleport] Unhandled promise rejection:', e.reason);
    });

    // ===== SERVICE WORKER UPDATE NOTIFICATION =====
    const updateBanner    = document.getElementById('update-banner');
    const updateBannerBtn = document.getElementById('update-banner-btn');
    if ('serviceWorker' in navigator) {
        navigator.serviceWorker.addEventListener('message', (event) => {
            if (event.data?.type === 'SW_UPDATED' && updateBanner) {
                updateBanner.style.display = 'flex';
            }
        });
        updateBannerBtn?.addEventListener('click', () => {
            navigator.serviceWorker.controller?.postMessage('skipWaiting');
            location.reload();
        });
        // Also listen for controllerchange (new SW took over)
        navigator.serviceWorker.addEventListener('controllerchange', () => {
            location.reload();
        });
    }

    // Connect
    teleport.connect().catch(err => {
        sounds.error();
        showToast('Server offline — start the signaling server first', 'error');
        if (statusText) statusText.textContent = 'Offline';
        renderDevices(); // show empty state (clears skeletons)
        console.error('[Teleport] Connect error:', err.message || err);
    });

    console.log('%cTeleport Web v3.1', 'font-size:16px;font-weight:bold;color:#fff;background:#000;padding:4px 10px;border-radius:4px;');
})();
