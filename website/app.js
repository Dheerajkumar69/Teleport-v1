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
                <div class="no-devices">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" style="width:48px;height:48px;margin-bottom:16px;opacity:0.5;">
                        <circle cx="11" cy="11" r="8"/>
                        <path d="M21 21l-4.35-4.35"/>
                    </svg>
                    <p>No devices found</p>
                    <p style="font-size:12px;margin-top:8px;">Make sure other devices are running Teleport</p>
                </div>
            `;
            return;
        }

        devicesGrid.innerHTML = peers.map(peer => `
            <div class="device-card ${selectedPeer === peer.id ? 'selected' : ''}" data-peer-id="${peer.id}">
                <div class="device-name">${escapeHtml(peer.name)}</div>
                <div class="device-info">Web Browser</div>
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
