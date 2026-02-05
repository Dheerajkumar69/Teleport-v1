/**
 * Teleport Web App - UI Logic
 * Connects the UI to the WebRTC engine
 */

(function () {
    'use strict';

    // Initialize WebRTC engine
    const teleport = new TeleportWebRTC();

    // State
    let peers = [];
    let selectedFiles = [];
    let selectedPeer = null;
    let transfers = [];
    let isReceiving = false;
    let pendingRequest = null;

    // DOM Elements
    const statusDot = document.getElementById('status-dot');
    const statusText = document.getElementById('status-text');
    const devicesGrid = document.getElementById('devices-grid');
    const dropZone = document.getElementById('drop-zone');
    const fileInput = document.getElementById('file-input');
    const browseBtn = document.getElementById('browse-btn');
    const fileList = document.getElementById('file-list');
    const recipientSelect = document.getElementById('recipient-select');
    const sendBtn = document.getElementById('send-btn');
    const receiveToggle = document.getElementById('receive-toggle');
    const receiveStatus = document.getElementById('receive-status');
    const transfersList = document.getElementById('transfers-list');
    const deviceNameInput = document.getElementById('device-name-input');
    const peerIdDisplay = document.getElementById('peer-id');
    const fileModal = document.getElementById('file-modal');
    const modalDesc = document.getElementById('modal-desc');
    const modalFiles = document.getElementById('modal-files');
    const modalAccept = document.getElementById('modal-accept');
    const modalReject = document.getElementById('modal-reject');
    const toastContainer = document.getElementById('toast-container');

    // Tab Navigation
    const tabButtons = document.querySelectorAll('.tab-btn');
    const tabContents = document.querySelectorAll('.tab-content');

    tabButtons.forEach(btn => {
        btn.addEventListener('click', () => {
            const tab = btn.dataset.tab;

            tabButtons.forEach(b => b.classList.remove('active'));
            tabContents.forEach(c => c.classList.remove('active'));

            btn.classList.add('active');
            document.getElementById(`tab-${tab}`).classList.add('active');
        });
    });

    // Toast notifications
    function showToast(message, type = 'info') {
        const toast = document.createElement('div');
        toast.className = `toast ${type}`;
        toast.innerHTML = `
            <span>${message}</span>
            <button onclick="this.parentElement.remove()" style="background:none;border:none;color:#A1A1AA;cursor:pointer;margin-left:12px;">&times;</button>
        `;
        toastContainer.appendChild(toast);
        setTimeout(() => toast.remove(), 5000);
    }

    // Format file size
    function formatSize(bytes) {
        if (bytes < 1024) return bytes + ' B';
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
        if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
        return (bytes / (1024 * 1024 * 1024)).toFixed(2) + ' GB';
    }

    // Update connection status
    function updateConnectionStatus(connected) {
        if (connected) {
            statusDot.classList.add('connected');
            statusText.textContent = 'Connected';
        } else {
            statusDot.classList.remove('connected');
            statusText.textContent = 'Disconnected';
        }
    }

    // Render devices grid
    function renderDevices() {
        if (peers.length === 0) {
            devicesGrid.innerHTML = `
            <div class="empty-state">
                <div class="empty-illustration">
                    <div class="radar"></div>
                    <div class="radar"></div>
                    <div class="radar"></div>
                    <div class="center-icon">
                        <svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2" style="width:24px;height:24px;">
                            <circle cx="11" cy="11" r="8"/>
                            <path d="M21 21l-4.35-4.35"/>
                        </svg>
                    </div>
                </div>
                <h3>Searching for devices...</h3>
                <p>Open Teleport on another device to connect</p>
            </div>
        `;
            return;
        }

        // Determine device icon based on name
        function getDeviceIcon(name) {
            const n = name.toLowerCase();
            if (n.includes('windows')) return `<svg viewBox="0 0 24 24" fill="currentColor"><path d="M0 3.449L9.75 2.1v9.451H0m10.949-9.602L24 0v11.4H10.949M0 12.6h9.75v9.451L0 20.699M10.949 12.6H24V24l-12.9-1.801"/></svg>`;
            if (n.includes('mac') || n.includes('ios')) return `<svg viewBox="0 0 24 24" fill="currentColor"><path d="M18.71 19.5c-.83 1.24-1.71 2.45-3.05 2.47-1.34.03-1.77-.79-3.29-.79-1.53 0-2 .77-3.27.82-1.31.05-2.3-1.32-3.14-2.53C4.25 17 2.94 12.45 4.7 9.39c.87-1.52 2.43-2.48 4.12-2.51 1.28-.02 2.5.87 3.29.87.78 0 2.26-1.07 3.81-.91.65.03 2.47.26 3.64 1.98-.09.06-2.17 1.28-2.15 3.81.03 3.02 2.65 4.03 2.68 4.04-.03.07-.42 1.44-1.38 2.83M13 3.5c.73-.83 1.94-1.46 2.94-1.5.13 1.17-.34 2.35-1.04 3.19-.69.85-1.83 1.51-2.95 1.42-.15-1.15.41-2.35 1.05-3.11z"/></svg>`;
            if (n.includes('android')) return `<svg viewBox="0 0 24 24" fill="currentColor"><path d="M17.6 9.48l1.84-3.18c.16-.31.04-.69-.26-.85-.29-.15-.65-.06-.83.22l-1.88 3.24c-1.44-.59-3.03-.9-4.67-.9-1.64 0-3.23.31-4.67.9L5.25 5.67c-.18-.28-.54-.37-.83-.22-.3.16-.42.54-.26.85l1.84 3.18C2.68 11.26.71 14.03.71 17.14h22.58c0-3.11-1.97-5.88-5.69-7.66zM7 15.25c-.69 0-1.25-.56-1.25-1.25s.56-1.25 1.25-1.25 1.25.56 1.25 1.25-.56 1.25-1.25 1.25zm10 0c-.69 0-1.25-.56-1.25-1.25s.56-1.25 1.25-1.25 1.25.56 1.25 1.25-.56 1.25-1.25 1.25z"/></svg>`;
            if (n.includes('linux')) return `<svg viewBox="0 0 24 24" fill="currentColor"><path d="M12.504 0c-.155 0-.315.008-.48.021-4.226.333-3.105 4.807-3.17 6.298-.076 1.092-.3 1.953-1.05 3.02-.885 1.051-2.127 2.75-2.716 4.521-.278.832-.41 1.684-.287 2.489a.424.424 0 00-.11.135c-.26.268-.45.6-.663.839-.199.199-.485.267-.797.4-.313.136-.658.269-.864.68-.09.189-.136.394-.132.602 0 .199.027.4.055.536.058.399.116.728.04.97-.249.68-.28 1.145-.106 1.484.174.334.535.47.94.601.81.2 1.91.135 2.774.6.926.466 1.866.67 2.616.47.526-.116.97-.464 1.208-.946.587-.003 1.22-.043 2.035-.24l.015-.004a3.08 3.08 0 01.66-.082c.172.022.334.073.49.135.11.046.22.094.323.15.376.21.697.48.97.78.182.198.355.408.523.62.082.11.162.22.251.323.067.08.135.157.2.235.126.144.276.28.469.363.193.084.45.096.674 0 .16-.076.284-.198.37-.348.258-.445.454-.951.495-1.527.03-.401-.053-.792-.194-1.064-.042-.066-.093-.125-.11-.199.003-.081.056-.15.079-.241.04-.162.065-.343.08-.7.026-.63.057-1.364.057-2.099 0-.66-.02-1.322-.055-2.00-.037-.674-.063-1.35-.063-.77z"/></svg>`;
            if (n.includes('chrome')) return `<svg viewBox="0 0 24 24" fill="currentColor"><path d="M12 0C8.21 0 4.831 1.757 2.632 4.501l3.953 6.848A5.454 5.454 0 0112 6.545h10.691A12 12 0 0012 0zM1.931 5.47A11.943 11.943 0 000 12c0 6.012 4.42 10.991 10.189 11.864l3.953-6.847a5.45 5.45 0 01-6.865-2.29zm13.342 2.166a5.446 5.446 0 011.636 7.574l.004.006-7.906 13.704A12.002 12.002 0 0024 12c0-1.596-.312-3.12-.875-4.513z"/></svg>`;
            if (n.includes('firefox')) return `<svg viewBox="0 0 24 24" fill="currentColor"><path d="M12 0C5.383 0 0 5.383 0 12s5.383 12 12 12 12-5.383 12-12S18.617 0 12 0z"/></svg>`;
            if (n.includes('safari')) return `<svg viewBox="0 0 24 24" fill="currentColor"><path d="M12 0c6.627 0 12 5.373 12 12s-5.373 12-12 12S0 18.627 0 12 5.373 0 12 0zm-.5 4.5l-.5 8 5.5 3-5-11z"/></svg>`;
            // Default browser icon
            return `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><line x1="2" y1="12" x2="22" y2="12"/><path d="M12 2a15.3 15.3 0 014 10 15.3 15.3 0 01-4 10 15.3 15.3 0 01-4-10 15.3 15.3 0 014-10z"/></svg>`;
        }

        devicesGrid.innerHTML = peers.map(peer => `
        <div class="device-card ${selectedPeer === peer.id ? 'selected' : ''}" data-peer-id="${peer.id}">
            <div class="device-icon">
                ${getDeviceIcon(peer.name)}
            </div>
            <div class="device-name">${escapeHtml(peer.name)}</div>
            <div class="device-info">
                <span class="device-status"></span>
                <span>Online • Ready to receive</span>
            </div>
        </div>
    `).join('');

        // Add click handlers
        devicesGrid.querySelectorAll('.device-card').forEach(card => {
            card.addEventListener('click', () => {
                selectedPeer = card.dataset.peerId;
                renderDevices();
                updateRecipientSelect();
            });
        });
    }
    // Update recipient dropdown
    function updateRecipientSelect() {
        recipientSelect.innerHTML = '<option value="">-- Select a device --</option>';
        peers.forEach(peer => {
            const option = document.createElement('option');
            option.value = peer.id;
            option.textContent = peer.name;
            if (peer.id === selectedPeer) option.selected = true;
            recipientSelect.appendChild(option);
        });
        updateSendButton();
    }

    // Render file list
    function renderFileList() {
        if (selectedFiles.length === 0) {
            fileList.innerHTML = '';
            return;
        }

        fileList.innerHTML = selectedFiles.map((file, idx) => `
            <div class="file-item">
                <div class="file-icon">
                    <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5">
                        <path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8z"/>
                        <polyline points="14 2 14 8 20 8"/>
                    </svg>
                </div>
                <div class="file-info">
                    <div class="file-name">${escapeHtml(file.name)}</div>
                    <div class="file-size">${formatSize(file.size)}</div>
                </div>
                <button class="file-remove" data-idx="${idx}">
                    <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                        <line x1="18" y1="6" x2="6" y2="18"/>
                        <line x1="6" y1="6" x2="18" y2="18"/>
                    </svg>
                </button>
            </div>
        `).join('');

        // Add remove handlers
        fileList.querySelectorAll('.file-remove').forEach(btn => {
            btn.addEventListener('click', (e) => {
                e.stopPropagation();
                const idx = parseInt(btn.dataset.idx);
                selectedFiles.splice(idx, 1);
                renderFileList();
                updateSendButton();
            });
        });
    }

    // Update send button state
    function updateSendButton() {
        const recipient = recipientSelect.value;
        sendBtn.disabled = selectedFiles.length === 0 || !recipient;
    }

    // Render transfers
    function renderTransfers() {
        if (transfers.length === 0) {
            transfersList.innerHTML = `
                <div class="no-devices">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" style="width:48px;height:48px;margin-bottom:16px;opacity:0.5;">
                        <polyline points="23 4 23 10 17 10"/>
                        <path d="M20.49 15A9 9 0 115.64 5.64L1 10"/>
                    </svg>
                    <p>No transfers yet</p>
                </div>
            `;
            return;
        }

        transfersList.innerHTML = transfers.map(t => `
            <div class="transfer-card">
                <div class="transfer-header">
                    <span class="transfer-title">${escapeHtml(t.filename)}</span>
                    <span class="transfer-status ${t.status}">${t.status}</span>
                </div>
                <div class="progress-bar">
                    <div class="progress-fill" style="width: ${t.progress * 100}%"></div>
                </div>
                <div class="transfer-meta">
                    <span>${formatSize(t.transferred || 0)} / ${formatSize(t.total)}</span>
                    <span>${Math.round(t.progress * 100)}%</span>
                </div>
            </div>
        `).join('');
    }

    // Escape HTML for security
    function escapeHtml(str) {
        const div = document.createElement('div');
        div.textContent = str;
        return div.innerHTML;
    }

    // Show file request modal
    function showFileRequestModal(request) {
        pendingRequest = request;
        modalDesc.textContent = `${request.fromName} wants to send you files`;
        modalFiles.innerHTML = request.files.map(f => `
            <div style="padding:8px 0;border-bottom:1px solid rgba(255,255,255,0.1);">
                ${escapeHtml(f.name)} - ${formatSize(f.size)}
            </div>
        `).join('');
        fileModal.classList.add('active');
    }

    function hideFileRequestModal() {
        fileModal.classList.remove('active');
        pendingRequest = null;
    }

    // Event Handlers

    // File input
    fileInput.addEventListener('change', (e) => {
        selectedFiles = Array.from(e.target.files);
        renderFileList();
        updateSendButton();
    });

    browseBtn.addEventListener('click', () => {
        fileInput.click();
    });

    // Drag and drop
    dropZone.addEventListener('dragover', (e) => {
        e.preventDefault();
        dropZone.classList.add('dragover');
    });

    dropZone.addEventListener('dragleave', () => {
        dropZone.classList.remove('dragover');
    });

    dropZone.addEventListener('drop', (e) => {
        e.preventDefault();
        dropZone.classList.remove('dragover');
        selectedFiles = Array.from(e.dataTransfer.files);
        renderFileList();
        updateSendButton();
    });

    dropZone.addEventListener('click', () => {
        fileInput.click();
    });

    // Recipient select
    recipientSelect.addEventListener('change', () => {
        selectedPeer = recipientSelect.value;
        updateSendButton();
    });

    // Send button
    sendBtn.addEventListener('click', async () => {
        if (selectedFiles.length === 0 || !selectedPeer) return;

        sendBtn.disabled = true;
        sendBtn.innerHTML = '<span>Sending...</span>';

        try {
            await teleport.requestFileSend(selectedPeer, selectedFiles);
            showToast('Transfer complete!', 'success');
            selectedFiles = [];
            renderFileList();
        } catch (err) {
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
    });

    // Receive toggle
    receiveToggle.addEventListener('click', () => {
        isReceiving = !isReceiving;

        if (isReceiving) {
            receiveToggle.classList.add('active');
            receiveStatus.textContent = 'Ready to receive files';
        } else {
            receiveToggle.classList.remove('active');
            receiveStatus.textContent = 'Tap to enable receiving';
        }
    });

    // Modal buttons
    modalAccept.addEventListener('click', () => {
        if (pendingRequest) {
            teleport.acceptFileRequest(pendingRequest.from);
            showToast('Transfer accepted', 'success');

            // Add to transfers
            pendingRequest.files.forEach(f => {
                transfers.push({
                    id: Date.now() + Math.random(),
                    filename: f.name,
                    total: f.size,
                    transferred: 0,
                    progress: 0,
                    status: 'receiving'
                });
            });
            renderTransfers();
        }
        hideFileRequestModal();
    });

    modalReject.addEventListener('click', () => {
        if (pendingRequest) {
            teleport.rejectFileRequest(pendingRequest.from);
            showToast('Transfer rejected', 'info');
        }
        hideFileRequestModal();
    });

    // Settings
    deviceNameInput.addEventListener('change', () => {
        teleport.setDeviceName(deviceNameInput.value);
        showToast('Device name updated', 'success');
    });

    // WebRTC Callbacks
    teleport.onConnected = () => {
        updateConnectionStatus(true);
        peerIdDisplay.textContent = teleport.peerId;
        showToast('Connected to signaling server', 'success');
    };

    teleport.onDisconnected = () => {
        updateConnectionStatus(false);
        showToast('Disconnected from server', 'error');
    };

    teleport.onPeersUpdated = (newPeers) => {
        peers = newPeers;
        renderDevices();
        updateRecipientSelect();
    };

    teleport.onFileRequest = (request) => {
        if (isReceiving) {
            showFileRequestModal(request);
        } else {
            teleport.rejectFileRequest(request.from);
        }
    };

    teleport.onTransferProgress = (progress) => {
        // Update transfer in list
        const transfer = transfers.find(t => t.filename === progress.filename);
        if (transfer) {
            transfer.transferred = progress.received || progress.sent;
            transfer.progress = progress.progress;
            renderTransfers();
        }
    };

    teleport.onTransferComplete = (result) => {
        const transfer = transfers.find(t => t.filename === result.filename);
        if (transfer) {
            transfer.progress = 1;
            transfer.status = 'complete';
            renderTransfers();
        }
        showToast(`${result.filename} transfer complete!`, 'success');
    };

    // Initialize
    deviceNameInput.value = teleport.deviceName;

    // Connect to signaling server
    teleport.connect().catch(err => {
        showToast('Failed to connect to server. Make sure the signaling server is running.', 'error');
        statusText.textContent = 'Connection failed';
    });

    console.log('Teleport Web App initialized ⚡');
})();
