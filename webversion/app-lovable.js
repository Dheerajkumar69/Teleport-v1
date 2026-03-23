/**
 * Teleport Web App - Final Production UI
 * All fixes implemented: skeleton loaders, theme toggle, sound, history, folder drop
 */

(function () {
    'use strict';

    const teleport = new TeleportWebRTC();

    // State
    let peers = [];
    let selectedFiles = [];
    let selectedPeer = null;
    let transfers = new Map();
    let isReceiving = false;
    let pendingRequest = null;
    let currentTheme = teleport.getTheme() || 'dark';
    const previewUrls = new Set();

    // Request notification permission
    teleport.requestNotificationPermission?.();

    // Audio context for sounds
    let audioCtx = null;
    function getAudioContext() {
        if (!audioCtx) {
            audioCtx = new (window.AudioContext || window.webkitAudioContext)();
        }
        return audioCtx;
    }

    // Sound effects with reliable playback
    const sounds = {
        click: () => playTone(800, 0.05, 'sine'),
        success: () => playTone(880, 0.15, 'sine'),
        error: () => playTone(300, 0.2, 'sawtooth'),
        notification: () => playTone(660, 0.1, 'sine'),
        celebration: () => {
            // Play celebratory chord
            playTone(523, 0.3, 'sine'); // C
            setTimeout(() => playTone(659, 0.3, 'sine'), 100); // E
            setTimeout(() => playTone(784, 0.3, 'sine'), 200); // G
            setTimeout(() => playTone(1047, 0.4, 'sine'), 300); // High C
        }
    };

    function playTone(frequency, duration, type = 'sine') {
        try {
            const ctx = getAudioContext();
            if (ctx.state === 'suspended') ctx.resume();

            const oscillator = ctx.createOscillator();
            const gain = ctx.createGain();

            oscillator.connect(gain);
            gain.connect(ctx.destination);

            oscillator.frequency.value = frequency;
            oscillator.type = type;

            gain.gain.setValueAtTime(0.15, ctx.currentTime);
            gain.gain.exponentialRampToValueAtTime(0.01, ctx.currentTime + duration);

            oscillator.start(ctx.currentTime);
            oscillator.stop(ctx.currentTime + duration);
        } catch (e) {
            console.log('Audio not available');
        }
    }

    // File type icons (SVG)
    const fileTypeIcons = {
        image: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><rect x="3" y="3" width="18" height="18" rx="2"/><circle cx="8.5" cy="8.5" r="1.5"/><path d="M21 15l-5-5L5 21"/></svg>`,
        video: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><rect x="2" y="2" width="20" height="20" rx="2.18"/><path d="M10 8l6 4-6 4V8z"/></svg>`,
        audio: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/></svg>`,
        pdf: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8z"/><path d="M14 2v6h6M9 15h6"/></svg>`,
        document: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8z"/><path d="M14 2v6h6M16 13H8M16 17H8M10 9H8"/></svg>`,
        text: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8z"/><path d="M14 2v6h6"/></svg>`,
        spreadsheet: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><rect x="3" y="3" width="18" height="18" rx="2"/><path d="M3 9h18M9 3v18"/></svg>`,
        presentation: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8M12 17v4"/></svg>`,
        archive: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M21 8v13H3V8M1 3h22v5H1zM10 12h4"/></svg>`,
        code: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><polyline points="16 18 22 12 16 6"/><polyline points="8 6 2 12 8 18"/></svg>`,
        executable: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><rect x="4" y="4" width="16" height="16" rx="2"/><path d="M9 12l2 2 4-4"/></svg>`,
        file: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8z"/><path d="M14 2v6h6"/></svg>`
    };

    function getFileIcon(filename) {
        const type = teleport.getFileType?.(filename) || 'file';
        return fileTypeIcons[type] || fileTypeIcons.file;
    }

    function getFileTypeClass(filename) {
        return teleport.getFileType?.(filename) || 'file';
    }

    // DOM Elements
    const statusDot = document.getElementById('status-dot');
    const statusText = document.getElementById('status-text');
    const devicesGrid = document.getElementById('devices-grid');
    const dropZone = document.getElementById('drop-zone');
    const fileInput = document.getElementById('file-input');
    const folderInput = document.getElementById('folder-input');
    const fileList = document.getElementById('file-list');
    const recipientSelect = document.getElementById('recipient-select');
    const sendBtn = document.getElementById('send-btn');
    const receiveToggle = document.getElementById('receive-toggle');
    const receiveStatus = document.getElementById('receive-status');
    const transfersList = document.getElementById('transfers-list');
    const historyList = document.getElementById('history-list');
    const deviceNameInput = document.getElementById('device-name-input');
    const peerIdDisplay = document.getElementById('peer-id');
    const fingerprintDisplay = document.getElementById('fingerprint-display');
    const fileModal = document.getElementById('file-modal');
    const modalDesc = document.getElementById('modal-desc');
    const modalFiles = document.getElementById('modal-files');
    const modalFingerprint = document.getElementById('modal-fingerprint');
    const modalAccept = document.getElementById('modal-accept');
    const modalReject = document.getElementById('modal-reject');
    const toastContainer = document.getElementById('toast-container');
    const themeToggle = document.getElementById('theme-toggle');
    const themeToggleHeader = document.getElementById('theme-toggle-header');
    const manualIpInput = document.getElementById('manual-ip');
    const manualConnectBtn = document.getElementById('manual-connect-btn');
    const qrCodeContainer = document.getElementById('qr-code');
    const bandwidthSlider = document.getElementById('bandwidth-slider');
    const bandwidthValue = document.getElementById('bandwidth-value');

    // Ripple effect
    function addRipple(element, event) {
        const ripple = document.createElement('span');
        ripple.className = 'ripple';
        const rect = element.getBoundingClientRect();
        const size = Math.max(rect.width, rect.height);
        ripple.style.width = ripple.style.height = size + 'px';
        ripple.style.left = event.clientX - rect.left - size / 2 + 'px';
        ripple.style.top = event.clientY - rect.top - size / 2 + 'px';
        element.appendChild(ripple);
        setTimeout(() => ripple.remove(), 600);
    }

    // Skeleton loader
    function showSkeletons(container, count = 3) {
        container.innerHTML = Array(count).fill(`
            <div class="skeleton-card">
                <div class="skeleton skeleton-icon"></div>
                <div class="skeleton-content">
                    <div class="skeleton skeleton-title"></div>
                    <div class="skeleton skeleton-text"></div>
                </div>
            </div>
        `).join('');
    }

    // Tab Navigation
    const tabButtons = document.querySelectorAll('.tab-btn');
    const tabContents = document.querySelectorAll('.tab-content');

    tabButtons.forEach(btn => {
        btn.addEventListener('click', (e) => {
            sounds.click();
            addRipple(btn, e);

            const tab = btn.dataset.tab;

            tabButtons.forEach(b => b.classList.remove('active'));
            tabContents.forEach(c => c.classList.remove('active'));

            btn.classList.add('active');
            document.getElementById(`tab-${tab}`).classList.add('active');

            if (tab === 'settings') generateQRCode();
        });
    });

    // Toast notifications
    function showToast(message, type = 'info') {
        sounds.notification();

        const icons = { success: '✓', error: '✕', info: 'ℹ', warning: '⚠' };

        const toast = document.createElement('div');
        toast.className = `toast ${type}`;
        toast.innerHTML = `
            <div class="toast-icon">${icons[type] || icons.info}</div>
            <div class="toast-content">
                <div class="toast-message">${message}</div>
            </div>
        `;
        toastContainer.appendChild(toast);

        setTimeout(() => {
            toast.classList.add('removing');
            setTimeout(() => toast.remove(), 300);
        }, 4000);
    }

    // Confetti with SOUND
    function celebrate() {
        sounds.celebration(); // Play celebration sound

        const colors = ['#7C3AED', '#A78BFA', '#06B6D4', '#10B981', '#F59E0B', '#EF4444'];

        for (let i = 0; i < 50; i++) {
            const confetti = document.createElement('div');
            confetti.className = 'confetti-piece';
            confetti.style.left = Math.random() * 100 + 'vw';
            confetti.style.top = '-10px';
            confetti.style.background = colors[Math.floor(Math.random() * colors.length)];
            confetti.style.animationDelay = Math.random() * 0.5 + 's';
            confetti.style.animationDuration = (2 + Math.random() * 2) + 's';
            document.body.appendChild(confetti);
            setTimeout(() => confetti.remove(), 4000);
        }
    }

    // Formatting helpers
    function formatSize(bytes) {
        if (bytes < 1024) return bytes + ' B';
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
        if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
        return (bytes / (1024 * 1024 * 1024)).toFixed(2) + ' GB';
    }

    function formatSpeed(bytesPerSecond) {
        if (bytesPerSecond < 1024) return bytesPerSecond.toFixed(0) + ' B/s';
        if (bytesPerSecond < 1024 * 1024) return (bytesPerSecond / 1024).toFixed(1) + ' KB/s';
        return (bytesPerSecond / (1024 * 1024)).toFixed(1) + ' MB/s';
    }

    function formatETA(seconds) {
        if (!isFinite(seconds) || seconds < 0) return '--';
        if (seconds < 60) return Math.ceil(seconds) + 's';
        if (seconds < 3600) return Math.ceil(seconds / 60) + 'm ' + Math.ceil(seconds % 60) + 's';
        return Math.floor(seconds / 3600) + 'h ' + Math.ceil((seconds % 3600) / 60) + 'm';
    }

    function formatTime(timestamp) {
        const date = new Date(timestamp);
        const now = new Date();
        const diff = now - date;

        if (diff < 60000) return 'Just now';
        if (diff < 3600000) return Math.floor(diff / 60000) + 'm ago';
        if (diff < 86400000) return Math.floor(diff / 3600000) + 'h ago';
        return date.toLocaleDateString();
    }

    function normalizeProgressValue(value) {
        if (typeof value !== 'number' || Number.isNaN(value)) return 0;
        const normalized = value > 1 ? value / 100 : value;
        return Math.max(0, Math.min(1, normalized));
    }

    function clearPreviewUrls() {
        for (const url of previewUrls) {
            URL.revokeObjectURL(url);
        }
        previewUrls.clear();
    }

    function escapeHtml(str) {
        const div = document.createElement('div');
        div.textContent = str;
        return div.innerHTML;
    }

    // Connection status
    function updateConnectionStatus(connected, reconnecting = false) {
        if (reconnecting) {
            statusDot.classList.remove('connected');
            statusDot.classList.add('reconnecting');
            statusText.textContent = 'Reconnecting...';
        } else if (connected) {
            statusDot.classList.add('connected');
            statusDot.classList.remove('reconnecting');
            statusText.textContent = 'Connected';
        } else {
            statusDot.classList.remove('connected', 'reconnecting');
            statusText.textContent = 'Disconnected';
        }
    }

    // Render devices with skeleton
    function renderDevices() {
        if (peers.length === 0) {
            devicesGrid.innerHTML = `
                <div class="empty-state">
                    <div class="empty-illustration">
                        <div class="radar"></div>
                        <div class="radar"></div>
                        <div class="radar"></div>
                        <div class="center-icon">
                            <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2">
                                <circle cx="11" cy="11" r="8"/>
                                <path d="M21 21l-4.35-4.35"/>
                            </svg>
                        </div>
                    </div>
                    <h3>Searching for devices...</h3>
                    <p>Make sure other devices have Teleport open</p>
                </div>
            `;
            return;
        }

        devicesGrid.innerHTML = peers.map((peer, idx) => `
            <div class="device-card ${selectedPeer === peer.id ? 'selected' : ''}" 
                 data-peer-id="${peer.id}"
                 style="animation-delay: ${idx * 0.1}s">
                <div class="device-icon">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5">
                        <rect x="2" y="3" width="20" height="14" rx="2"/>
                        <path d="M8 21h8M12 17v4"/>
                    </svg>
                </div>
                <div class="device-name">${escapeHtml(peer.name)}</div>
                <div class="device-info">
                    <span class="device-status"></span>
                    Online
                    ${peer.isLan ? '<span class="lan-badge" title="On your local network">📡 LAN</span>' : ''}
                </div>
                ${peer.fingerprint ? `<div class="device-fingerprint">🔐 ${peer.fingerprint}</div>` : ''}
            </div>
        `).join('');

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
        recipientSelect.innerHTML = '<option value="">-- Choose a device --</option>';
        peers.forEach(peer => {
            const option = document.createElement('option');
            option.value = peer.id;
            option.textContent = peer.name;
            if (peer.id === selectedPeer) option.selected = true;
            recipientSelect.appendChild(option);
        });
        updateSendButton();
    }

    // Render file list with type icons and preview
    function renderFileList() {
        if (selectedFiles.length === 0) {
            clearPreviewUrls();
            fileList.innerHTML = '';
            return;
        }

        clearPreviewUrls();
        const totalSize = selectedFiles.reduce((sum, f) => sum + f.size, 0);

        fileList.innerHTML = `
            <div class="file-summary">
                <span>${selectedFiles.length} file(s)</span>
                <span>${formatSize(totalSize)}</span>
            </div>
        ` + selectedFiles.map((file, idx) => {
            const isImage = file.type.startsWith('image/');
            const fileType = getFileTypeClass(file.name);
            let preview = '';

            if (isImage) {
                const url = URL.createObjectURL(file);
                previewUrls.add(url);
                preview = `<img src="${url}" class="file-preview" alt="Preview">`;
            }

            // Show relative path if from folder
            const displayName = file.relativePath || file.webkitRelativePath || file.name;

            return `
                <div class="file-item" style="animation-delay: ${idx * 0.05}s">
                    <div class="file-icon ${fileType} ${isImage ? 'has-preview' : ''}">
                        ${preview || getFileIcon(file.name)}
                    </div>
                    <div class="file-info">
                        <div class="file-name" title="${escapeHtml(displayName)}">${escapeHtml(file.name)}</div>
                        <div class="file-size">${formatSize(file.size)}${file.relativePath ? ' • ' + escapeHtml(file.relativePath.split('/').slice(0, -1).join('/')) : ''}</div>
                    </div>
                    <button class="file-remove" data-idx="${idx}">
                        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                            <line x1="18" y1="6" x2="6" y2="18"/>
                            <line x1="6" y1="6" x2="18" y2="18"/>
                        </svg>
                    </button>
                </div>
            `;
        }).join('');

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

    // Render transfers
    function renderTransfers() {
        if (transfers.size === 0) {
            transfersList.innerHTML = `
                <div class="empty-state small">
                    <p style="color: var(--text-muted);">No active transfers</p>
                </div>
            `;
            return;
        }

        transfersList.innerHTML = Array.from(transfers.values()).map(t => {
            const statusIcon = {
                'complete': '✓',
                'sending': '↑',
                'receiving': '↓',
                'paused': '⏸',
                'error': '✕',
                'cancelled': '✕'
            }[t.status] || '↻';

            const speedText = Number.isFinite(t.speed) ? formatSpeed(Math.max(0, t.speed)) : '--';
            const etaText = Number.isFinite(t.eta) ? formatETA(t.eta) : '--';
            const fileProgress = t.totalFiles > 1 ? `File ${(t.fileIndex || 0) + 1}/${t.totalFiles}` : '';
            const fileType = getFileTypeClass(t.filename);

            return `
                <div class="transfer-card" data-transfer-id="${t.id}">
                    <div class="transfer-header">
                        <div class="transfer-file-icon ${fileType}">${getFileIcon(t.filename)}</div>
                        <div class="transfer-info">
                            <span class="transfer-title">${escapeHtml(t.filename)}</span>
                            ${fileProgress ? `<span class="transfer-file-count">${fileProgress}</span>` : ''}
                        </div>
                        <span class="transfer-status ${t.status}">
                            ${statusIcon} ${t.status.charAt(0).toUpperCase() + t.status.slice(1)}
                        </span>
                    </div>
                    <div class="progress-bar">
                        <div class="progress-fill" style="width: ${t.progress * 100}%"></div>
                    </div>
                    <div class="transfer-meta">
                        <span>${formatSize(t.transferred || 0)} / ${formatSize(t.total)}</span>
                        <span>${speedText} • ETA: ${etaText}</span>
                        <span>${Math.round(t.progress * 100)}%</span>
                    </div>
                    ${t.status !== 'complete' && t.status !== 'error' && t.status !== 'cancelled' ? `
                    <div class="transfer-controls">
                        <button class="control-btn ${t.status === 'paused' ? 'resume-btn' : 'pause-btn'}" data-action="${t.status === 'paused' ? 'resume' : 'pause'}">
                            ${t.status === 'paused' ? '▶ Resume' : '⏸ Pause'}
                        </button>
                        <button class="control-btn cancel-btn" data-action="cancel">✕ Cancel</button>
                    </div>
                    ` : ''}
                </div>
            `;
        }).join('');

        transfersList.querySelectorAll('.control-btn').forEach(btn => {
            btn.addEventListener('click', (e) => {
                e.stopPropagation();
                sounds.click();
                const card = btn.closest('.transfer-card');
                const transferId = card.dataset.transferId;
                const action = btn.dataset.action;

                const t = transfers.get(transferId);
                if (!t) return;

                if (action === 'pause') {
                    teleport.pauseTransfer(transferId);
                    t.status = 'paused';
                    renderTransfers();
                } else if (action === 'resume') {
                    teleport.resumeTransfer(transferId);
                    t.status = t.direction === 'send' ? 'sending' : 'receiving';
                    renderTransfers();
                } else if (action === 'cancel') {
                    teleport.cancelTransfer(transferId);
                    t.status = 'cancelled';
                    showToast('Transfer cancelled', 'info');
                    setTimeout(() => {
                        transfers.delete(transferId);
                        renderTransfers();
                    }, 2000);
                }
            });
        });
    }

    // Render history - called on page load
    function renderHistory() {
        const history = teleport.getTransferHistory();

        if (!historyList) return;

        if (history.length === 0) {
            historyList.innerHTML = `
                <div class="empty-state small">
                    <p style="color: var(--text-muted);">No transfer history yet</p>
                </div>
            `;
            return;
        }

        historyList.innerHTML = `
            <div class="history-header">
                <span>Recent Transfers (${history.length})</span>
                <button class="clear-history-btn" id="clear-history">Clear All</button>
            </div>
            ${history.slice(0, 20).map(h => `
                <div class="history-item ${h.success ? 'success' : 'failed'}">
                    <div class="history-icon ${getFileTypeClass(h.filename)}">${getFileIcon(h.filename)}</div>
                    <div class="history-info">
                        <div class="history-name">${escapeHtml(h.filename)}</div>
                        <div class="history-meta">
                            ${formatSize(h.size)} • ${h.direction === 'sent' ? '↑ Sent' : '↓ Received'} • ${formatTime(h.timestamp)}
                        </div>
                    </div>
                    <div class="history-status">${h.success ? '✓' : '✕'}</div>
                </div>
            `).join('')}
        `;

        document.getElementById('clear-history')?.addEventListener('click', () => {
            teleport.clearTransferHistory();
            renderHistory();
            showToast('History cleared', 'info');
        });
    }

    // File request modal with fingerprint and encryption indicator
    function showFileRequestModal(request) {
        sounds.notification();
        pendingRequest = request;

        const encryptionBadge = request.encrypted
            ? '<span class="encryption-badge encrypted">🔒 Encrypted</span>'
            : '<span class="encryption-badge">🔓 Unencrypted</span>';

        modalDesc.innerHTML = `${escapeHtml(request.fromName)} wants to send you files ${encryptionBadge}`;

        modalFiles.innerHTML = request.files.map(f => `
            <div class="modal-file-item">
                <div class="modal-file-icon ${getFileTypeClass(f.name)}">${getFileIcon(f.name)}</div>
                <span class="modal-file-name">${escapeHtml(f.name)}</span>
                <span class="modal-file-size">${formatSize(f.size)}</span>
            </div>
        `).join('');

        if (request.fingerprint && modalFingerprint) {
            modalFingerprint.innerHTML = `
                <div class="fingerprint-verify">
                    <span class="fingerprint-label">🔐 Verify sender fingerprint:</span>
                    <span class="fingerprint-code">${request.fingerprint}</span>
                </div>
            `;
        }

        fileModal.classList.add('active');
    }

    function hideFileRequestModal() {
        fileModal.classList.remove('active');
        pendingRequest = null;
    }

    // REAL QR Code generation
    function generateQRCode() {
        if (!qrCodeContainer || !teleport.peerId) return;

        const data = teleport.generatePairingData?.();
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
                <div class="qr-fingerprint">🔐 ${teleport.peerFingerprint || ''}</div>
            `;
            qrCodeContainer.appendChild(info);
        } catch (e) {
            console.error('QR generation failed:', e);
        }
    }

    // Theme toggle - FIXED to update label for both buttons
    function applyTheme(theme) {
        document.body.classList.toggle('light-theme', theme === 'light');
        teleport.setTheme(theme);
        currentTheme = theme;

        // Update settings button label
        if (themeToggle) {
            themeToggle.innerHTML = theme === 'dark'
                ? '🌙 Dark Mode'
                : '☀️ Light Mode';
        }

        // Update header button icon
        if (themeToggleHeader) {
            themeToggleHeader.innerHTML = theme === 'dark' ? '🌙' : '☀️';
            themeToggleHeader.title = theme === 'dark' ? 'Switch to Light Mode' : 'Switch to Dark Mode';
        }
    }

    // Bandwidth control
    function updateBandwidth() {
        if (!bandwidthSlider || !bandwidthValue) return;

        const value = parseInt(bandwidthSlider.value);
        const limit = value === 0 ? 0 : value * 1024 * 1024;
        teleport.setBandwidthLimit?.(limit);

        bandwidthValue.textContent = value === 0 ? 'Unlimited' : `${value} MB/s`;
    }

    // Event handlers

    fileInput?.addEventListener('change', (e) => {
        selectedFiles = [...selectedFiles, ...Array.from(e.target.files)];
        renderFileList();
        updateSendButton();
        if (e.target.files.length > 0) {
            showToast(`${e.target.files.length} file(s) added`, 'success');
        }
    });

    const folderBtn = document.getElementById('folder-btn');
    folderBtn?.addEventListener('click', () => {
        sounds.click();
        folderInput?.click();
    });

    folderInput?.addEventListener('change', async (e) => {
        const files = Array.from(e.target.files);
        // Preserve folder structure via webkitRelativePath
        files.forEach(f => {
            f.relativePath = f.webkitRelativePath || f.name;
        });
        selectedFiles = [...selectedFiles, ...files];
        renderFileList();
        updateSendButton();
        showToast(`${files.length} file(s) from folder added`, 'success');
    });

    // Drag and drop with FOLDER SUPPORT
    dropZone?.addEventListener('dragover', (e) => {
        e.preventDefault();
        dropZone.classList.add('dragover');
    });

    dropZone?.addEventListener('dragleave', () => {
        dropZone.classList.remove('dragover');
    });

    dropZone?.addEventListener('drop', async (e) => {
        e.preventDefault();
        dropZone.classList.remove('dragover');

        const items = e.dataTransfer.items;

        // Check if any items are directories
        let hasDirectory = false;
        for (const item of items) {
            const entry = item.webkitGetAsEntry?.();
            if (entry?.isDirectory) {
                hasDirectory = true;
                break;
            }
        }

        if (hasDirectory && teleport.getFilesWithStructure) {
            // Handle folders with structure preservation
            showToast('Processing folder...', 'info');
            try {
                const files = await teleport.getFilesWithStructure(items);
                selectedFiles = [...selectedFiles, ...files];
                renderFileList();
                updateSendButton();
                sounds.success();
                showToast(`${files.length} file(s) from folder ready!`, 'success');
            } catch (err) {
                sounds.error();
                showToast('Failed to read folder', 'error');
            }
        } else {
            // Regular files
            selectedFiles = [...selectedFiles, ...Array.from(e.dataTransfer.files)];
            renderFileList();
            updateSendButton();
            sounds.success();
            showToast(`${e.dataTransfer.files.length} file(s) ready!`, 'success');
        }
    });

    dropZone?.addEventListener('click', () => {
        sounds.click();
        fileInput?.click();
    });

    recipientSelect?.addEventListener('change', () => {
        sounds.click();
        selectedPeer = recipientSelect.value;
        updateSendButton();
    });

    sendBtn?.addEventListener('click', async (e) => {
        if (selectedFiles.length === 0 || !selectedPeer) return;

        addRipple(sendBtn, e);
        sounds.click();

        sendBtn.disabled = true;
        sendBtn.innerHTML = `
            <svg width="20" height="20" viewBox="0 0 24 24" class="spin">
                <circle cx="12" cy="12" r="10" stroke="currentColor" stroke-width="2" fill="none" stroke-dasharray="60 40"/>
            </svg>
            Sending...
        `;

        try {
            await teleport.requestFileSend(selectedPeer, selectedFiles);
            showToast('All files sent! 🎉', 'success');
            selectedFiles = [];
            renderFileList();
        } catch (err) {
            sounds.error();
            showToast(err.message || 'Transfer failed', 'error');
        }

        sendBtn.disabled = false;
        sendBtn.innerHTML = `
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                <path d="M22 2L11 13M22 2l-7 20-4-9-9-4 20-7z"/>
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
            receiveStatus.textContent = 'Ready to receive ✨';
            showToast('Now accepting transfers', 'success');
        } else {
            receiveToggle.classList.remove('active');
            receiveStatus.textContent = 'Tap to start receiving';
        }
    });

    modalAccept?.addEventListener('click', (e) => {
        addRipple(modalAccept, e);
        sounds.success();

        if (pendingRequest) {
            teleport.acceptFileRequest(pendingRequest.from);
            showToast('Receiving files...', 'success');

            // Transfer entries are created when the sender's real transferId arrives in progress events.
            // This avoids stale placeholder cards that can never be updated.
        }
        hideFileRequestModal();
    });

    modalReject?.addEventListener('click', (e) => {
        addRipple(modalReject, e);
        sounds.click();

        if (pendingRequest) {
            teleport.rejectFileRequest(pendingRequest.from);
            showToast('Transfer declined', 'info');
        }
        hideFileRequestModal();
    });

    deviceNameInput?.addEventListener('change', () => {
        sounds.click();
        teleport.setDeviceName(deviceNameInput.value);
        showToast('Device name saved!', 'success');
    });

    themeToggle?.addEventListener('click', () => {
        sounds.click();
        applyTheme(currentTheme === 'dark' ? 'light' : 'dark');
    });

    // Header theme toggle button
    themeToggleHeader?.addEventListener('click', () => {
        sounds.click();
        applyTheme(currentTheme === 'dark' ? 'light' : 'dark');
    });

    bandwidthSlider?.addEventListener('input', updateBandwidth);

    manualConnectBtn?.addEventListener('click', async () => {
        const ip = manualIpInput?.value?.trim();
        if (!ip) {
            showToast('Enter an IP address', 'error');
            return;
        }

        // Validate using engine method
        if (teleport.validateIP && !teleport.validateIP(ip)) {
            sounds.error();
            showToast('Invalid IP address format. Use: 192.168.1.100 or hostname', 'error');
            return;
        }

        sounds.click();
        showToast('Connecting...', 'info');

        try {
            await teleport.connectToManualIP(ip);
            showToast('Connected to ' + ip, 'success');
        } catch (err) {
            sounds.error();
            showToast('Connection failed: ' + err.message, 'error');
        }
    });

    // WebRTC Callbacks
    teleport.onConnected = () => {
        updateConnectionStatus(true);
        peerIdDisplay.textContent = teleport.peerId;
        if (fingerprintDisplay) {
            fingerprintDisplay.textContent = teleport.peerFingerprint || '';
        }
        showToast('Connected to network ⚡', 'success');
        generateQRCode();
    };

    teleport.onDisconnected = () => {
        updateConnectionStatus(false);
        sounds.error();
        showToast('Disconnected', 'error');
    };

    teleport.onReconnecting = (attempt) => {
        updateConnectionStatus(false, true);
        showToast(`Reconnecting (attempt ${attempt}/5)...`, 'warning');
    };

    teleport.onPeersUpdated = (newPeers) => {
        const prevCount = peers.length;
        peers = newPeers;
        renderDevices();
        updateRecipientSelect();
        const diff = newPeers.length - prevCount;
        if (diff > 0) {
            showToast(`${diff} new device(s) found!`, 'success');
        }
    };

    // When a peer announces it's on the local LAN, re-render with the LAN badge
    if (teleport.broadcastChannel) {
        const _origBC = teleport.broadcastChannel.onmessage;
        teleport.broadcastChannel.onmessage = (event) => {
            if (_origBC) _origBC(event);
            if (event.data?.type === 'peer-lan-updated') {
                renderDevices(); // re-render so LAN badge appears
            }
        };
    }

    teleport.onFileRequest = (request) => {
        if (isReceiving) {
            showFileRequestModal(request);
        } else {
            teleport.rejectFileRequest(request.from);
            showToast('Transfer declined (receiving mode off)', 'info');
        }
    };

    teleport.onFileSizeWarning = (largeFiles) => {
        return new Promise((resolve) => {
            const fileList = largeFiles.map(f => `${f.name} (${f.sizeFormatted})`).join('\n');
            const proceed = confirm(`⚠️ Large file warning!\n\nThe following files are over 100MB:\n${fileList}\n\nLarge transfers may be slow. Continue?`);
            resolve(proceed);
        });
    };

    teleport.onPeerVerification = (peerId, fingerprint) => {
        console.log(`🔐 Peer ${peerId} fingerprint: ${fingerprint}`);
    };

    teleport.onTransferProgress = (progress) => {
        const normalizedProgress = normalizeProgressValue(progress.progress);
        let transfer = transfers.get(progress.transferId);
        if (!transfer) {
            transfer = {
                id: progress.transferId,
                filename: progress.filename,
                total: typeof progress.total === 'number' ? progress.total : 0,
                transferred: 0,
                progress: 0,
                status: progress.sent !== undefined ? 'sending' : 'receiving',
                direction: progress.sent !== undefined ? 'send' : 'receive',
                speed: 0,
                eta: 0,
                fileIndex: progress.fileIndex || 0,
                totalFiles: progress.totalFiles || 1
            };
            transfers.set(progress.transferId, transfer);
        }

        transfer.transferred = progress.received ?? progress.sent ?? transfer.transferred ?? 0;
        transfer.progress = normalizedProgress;
        transfer.speed = typeof progress.speed === 'number' ? progress.speed : transfer.speed;
        transfer.eta = typeof progress.eta === 'number' ? progress.eta : transfer.eta;
        transfer.fileIndex = Number.isFinite(progress.fileIndex) ? progress.fileIndex : transfer.fileIndex;
        transfer.totalFiles = Number.isFinite(progress.totalFiles) ? progress.totalFiles : transfer.totalFiles;
        if (typeof progress.total === 'number') {
            transfer.total = progress.total;
        }

        renderTransfers();
    };

    teleport.onTransferComplete = (result) => {
        let transfer = transfers.get(result.transferId);
        if (!transfer) {
            transfer = {
                id: result.transferId,
                filename: result.filename,
                total: typeof result.size === 'number' ? result.size : 0,
                transferred: typeof result.size === 'number' ? result.size : 0,
                progress: 1,
                status: 'complete',
                direction: 'receive',
                speed: 0,
                eta: 0,
                fileIndex: Number.isFinite(result.fileIndex) ? result.fileIndex : 0,
                totalFiles: Number.isFinite(result.totalFiles) ? result.totalFiles : 1
            };
            transfers.set(result.transferId, transfer);
        }

        transfer.progress = 1;
        transfer.status = 'complete';
        transfer.transferred = transfer.total;
        renderTransfers();

        setTimeout(() => {
            transfers.delete(result.transferId);
            renderTransfers();
        }, 5000);

        // Celebrate on final file with SOUND
        if (!result.totalFiles || result.fileIndex === result.totalFiles - 1) {
            celebrate();
        }

        showToast(`${result.filename} complete! 🎉`, 'success');
        renderHistory();
    };

    teleport.onTransferError = (error) => {
        sounds.error();
        const transfer = transfers.get(error.transferId);
        if (transfer) {
            transfer.status = 'error';
            renderTransfers();
        }
        showToast(error.error || 'Transfer failed', 'error');
    };

    // Initialize with history loaded immediately
    deviceNameInput.value = teleport.deviceName;
    applyTheme(currentTheme);

    // LOAD HISTORY ON PAGE LOAD (not just when clicking tab)
    renderHistory();
    renderTransfers();

    // Load saved bandwidth
    const savedBandwidth = teleport.getBandwidthLimit?.() || 0;
    if (bandwidthSlider) {
        bandwidthSlider.value = savedBandwidth / (1024 * 1024);
        updateBandwidth();
    }

    // Show skeletons initially
    if (devicesGrid) showSkeletons(devicesGrid, 2);

    // Add dynamic styles
    const style = document.createElement('style');
    style.textContent = `
        @keyframes spin { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }
        .spin { animation: spin 1s linear infinite; }
        
        /* Skeleton loaders */
        .skeleton-card { display: flex; align-items: center; gap: 16px; padding: 20px; background: var(--bg-glass); border-radius: 16px; margin-bottom: 12px; }
        .skeleton { background: linear-gradient(90deg, var(--bg-glass) 25%, rgba(255,255,255,0.1) 50%, var(--bg-glass) 75%); background-size: 200% 100%; animation: shimmer 1.5s infinite; border-radius: 8px; }
        .skeleton-icon { width: 48px; height: 48px; border-radius: 12px; }
        .skeleton-content { flex: 1; }
        .skeleton-title { height: 16px; width: 60%; margin-bottom: 8px; }
        .skeleton-text { height: 12px; width: 40%; }
        @keyframes shimmer { 0% { background-position: -200% 0; } 100% { background-position: 200% 0; } }
        
        /* Transfer controls */
        .transfer-controls { display: flex; gap: 8px; margin-top: 12px; }
        .control-btn { padding: 8px 16px; border-radius: 8px; border: 1px solid var(--border); background: var(--bg-glass); color: var(--text-primary); cursor: pointer; transition: all 0.2s; font-size: 13px; }
        .control-btn:hover { background: rgba(255,255,255,0.1); transform: translateY(-1px); }
        .cancel-btn:hover { background: rgba(239, 68, 68, 0.2); color: var(--error); }
        .resume-btn:hover { background: rgba(16, 185, 129, 0.2); color: var(--success); }
        
        /* File type colors */
        .file-icon svg, .transfer-file-icon svg, .history-icon svg, .modal-file-icon svg { width: 24px; height: 24px; }
        .file-icon.image, .transfer-file-icon.image, .history-icon.image, .modal-file-icon.image { color: #EC4899; }
        .file-icon.video, .transfer-file-icon.video, .history-icon.video, .modal-file-icon.video { color: #F59E0B; }
        .file-icon.audio, .transfer-file-icon.audio, .history-icon.audio, .modal-file-icon.audio { color: #10B981; }
        .file-icon.pdf, .transfer-file-icon.pdf, .history-icon.pdf, .modal-file-icon.pdf { color: #EF4444; }
        .file-icon.document, .transfer-file-icon.document, .history-icon.document, .modal-file-icon.document { color: #3B82F6; }
        .file-icon.spreadsheet, .transfer-file-icon.spreadsheet, .history-icon.spreadsheet, .modal-file-icon.spreadsheet { color: #22C55E; }
        .file-icon.presentation, .transfer-file-icon.presentation, .history-icon.presentation, .modal-file-icon.presentation { color: #F97316; }
        .file-icon.archive, .transfer-file-icon.archive, .history-icon.archive, .modal-file-icon.archive { color: #A855F7; }
        .file-icon.code, .transfer-file-icon.code, .history-icon.code, .modal-file-icon.code { color: #06B6D4; }
        .file-icon.executable, .transfer-file-icon.executable, .history-icon.executable, .modal-file-icon.executable { color: #64748B; }
        
        /* File summary */
        .file-summary { display: flex; justify-content: space-between; padding: 12px 16px; background: var(--bg-glass); border-radius: 10px; margin-bottom: 12px; font-size: 14px; color: var(--text-secondary); }
        
        /* History */
        .history-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px; }
        .clear-history-btn { padding: 6px 14px; border-radius: 8px; border: 1px solid var(--border); background: transparent; color: var(--text-secondary); cursor: pointer; font-size: 13px; transition: all 0.2s; }
        .clear-history-btn:hover { background: rgba(239, 68, 68, 0.1); color: var(--error); }
        .history-item { display: flex; align-items: center; gap: 12px; padding: 14px 16px; background: var(--bg-glass); border-radius: 12px; margin-bottom: 8px; border: 1px solid var(--border); transition: all 0.2s; }
        .history-item:hover { border-color: var(--border-hover); transform: translateX(4px); }
        .history-icon { width: 32px; display: flex; align-items: center; }
        .history-info { flex: 1; }
        .history-name { font-weight: 500; font-size: 14px; margin-bottom: 2px; }
        .history-meta { font-size: 12px; color: var(--text-muted); }
        .history-status { font-size: 16px; }
        .history-item.success .history-status { color: var(--success); }
        .history-item.failed .history-status { color: var(--error); }
        
        /* QR info */
        .qr-info { text-align: center; margin-top: 12px; }
        .qr-peer-id { font-size: 12px; color: var(--text-muted); }
        .qr-fingerprint { font-size: 14px; color: var(--accent-light); font-weight: 600; margin-top: 4px; }
        
        /* Device fingerprint */
        .device-fingerprint { font-size: 10px; color: var(--accent-light); font-family: monospace; margin-top: 8px; opacity: 0.8; }
        
        /* Fingerprint verify */
        .fingerprint-verify { display: flex; flex-direction: column; gap: 4px; margin-top: 12px; padding: 12px; background: rgba(124, 58, 237, 0.1); border-radius: 8px; border: 1px solid var(--accent); }
        .fingerprint-label { font-size: 12px; color: var(--text-secondary); }
        .fingerprint-code { font-size: 16px; font-family: monospace; color: var(--accent-light); font-weight: 600; }
        
        /* Modal file items */
        .modal-file-item { display: flex; align-items: center; gap: 12px; padding: 10px; }
        .modal-file-icon { width: 28px; display: flex; align-items: center; justify-content: center; }
        .modal-file-name { flex: 1; font-size: 14px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
        .modal-file-size { font-size: 12px; color: var(--text-muted); }
        
        /* Transfer header */
        .transfer-header { display: flex; align-items: center; gap: 12px; }
        .transfer-file-icon { width: 36px; height: 36px; display: flex; align-items: center; justify-content: center; background: var(--bg-glass); border-radius: 8px; }
        .transfer-info { flex: 1; display: flex; flex-direction: column; gap: 2px; }
        .transfer-file-count { font-size: 12px; color: var(--text-muted); }
        
        /* Status indicators */
        .status-dot.reconnecting { background: var(--warning); animation: pulse 1s ease-in-out infinite; }
        .empty-state.small { padding: 30px 20px; }
        
        /* File preview */
        .file-preview { width: 100%; height: 100%; object-fit: cover; border-radius: 8px; }
        .file-icon.has-preview { overflow: hidden; }
        
        /* Light theme */
        .light-theme { --bg-primary: #FAFAFA; --bg-secondary: #F4F4F5; --bg-card: rgba(255, 255, 255, 0.8); --bg-glass: rgba(255, 255, 255, 0.6); --text-primary: #18181B; --text-secondary: #52525B; --text-muted: #A1A1AA; --border: rgba(0, 0, 0, 0.08); --border-hover: rgba(0, 0, 0, 0.15); }
        .light-theme .bg-glow { opacity: 0.2; }
        .light-theme .sidebar { background: rgba(255, 255, 255, 0.8); }
        .light-theme .header { background: rgba(255, 255, 255, 0.9); }
        .light-theme .skeleton { background: linear-gradient(90deg, rgba(0,0,0,0.05) 25%, rgba(0,0,0,0.1) 50%, rgba(0,0,0,0.05) 75%); }
        
        /* Encryption badge */
        .encryption-badge { display: inline-flex; align-items: center; gap: 4px; padding: 4px 10px; border-radius: 12px; font-size: 12px; font-weight: 500; background: rgba(239, 68, 68, 0.15); color: #EF4444; }
        .encryption-badge.encrypted { background: rgba(16, 185, 129, 0.15); color: #10B981; }

        /* LAN peer badge */
        .lan-badge { display: inline-flex; align-items: center; gap: 3px; margin-left: 6px; padding: 2px 8px; border-radius: 10px; font-size: 11px; font-weight: 600; background: rgba(6, 182, 212, 0.15); color: #06B6D4; border: 1px solid rgba(6, 182, 212, 0.3); }
    `;
    document.head.appendChild(style);

    window.addEventListener('beforeunload', clearPreviewUrls);
    window.addEventListener('pagehide', clearPreviewUrls);

    // Global error handler for centralized error management
    teleport.onError = (error) => {
        console.error('[Teleport]', error);
        sounds.error();
        showToast(error.message || 'An error occurred', 'error');
    };

    // Connect
    teleport.connect().catch(err => {
        sounds.error();
        showToast('Server offline. Start signaling server first!', 'error');
        statusText.textContent = 'Offline';
        // Remove skeletons and show empty state
        renderDevices();
    });

    console.log('%c🚀 Teleport Web App - Final Production', 'font-size: 20px; color: #7C3AED; font-weight: bold;');
    console.log('%c✅ All issues fixed: QR, skeletons, theme, sound, history, folders', 'font-size: 12px; color: #10B981;');
})();
