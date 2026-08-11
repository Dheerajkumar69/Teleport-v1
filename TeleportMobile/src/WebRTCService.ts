/**
 * Teleport WebRTCService — React Native / Android
 * WebRTC P2P file transfer engine interoperable with teleport-webrtc.js.
 *
 * Binary framing (matches web engine exactly):
 *   0x01 = FILE_META  (JSON)
 *   0x02 = CHUNK      (raw bytes)
 *   0x03 = DONE       (JSON)
 *   0x04 = CANCEL
 */

import {
  RTCPeerConnection,
  RTCIceCandidate,
  RTCSessionDescription,
} from 'react-native-webrtc';
import type RTCDataChannelType from 'react-native-webrtc/lib/typescript/RTCDataChannel';
import ReactNativeBlobUtil from 'react-native-blob-util';

import { SignalingClient, FileInfo, PeerInfo, SignalingMessage } from './SignalingClient';

// ============================================================================
// TYPES
// ============================================================================

export type TransferDirection = 'send' | 'receive';

export type TransferState = {
  transferId: string;
  peerId: string;
  direction: TransferDirection;
  filename: string;
  fileSize: number;
  bytesTransferred: number;
  fileIndex: number;
  totalFiles: number;
  startedAt: number;
  writerPath: string | null;
  sha256: string | null;
  writeOffset: number;
  chunkCount: number;
};

export type WebRTCPeer = {
  peerId: string;
  name: string;
  clientType: string;
  pc: RTCPeerConnection;       // react-native-webrtc RTCPeerConnection
  dc: RTCDataChannelType | null;
  dcReady: boolean;
  pendingCandidates: any[];
  iceDone: boolean;
  relayFallback: boolean;
};

export type FilePickResult = {
  uri: string;
  name: string;
  size: number;
  type?: string;
};

export type ProgressInfo = {
  transferId: string;
  peerId: string;
  direction: TransferDirection;
  filename: string;
  bytesTransferred: number;
  totalBytes: number;
  percent: number;
  speedBps: number;
};

export type TransferResult = {
  transferId: string;
  peerId: string;
  filename: string;
  savedPath?: string;
  success: boolean;
  error?: string;
};

// ============================================================================
// CONSTANTS
// ============================================================================

const CHUNK_SMALL    = 16  * 1024;        // 16 KB for files < 100 MB
const CHUNK_MEDIUM  = 256 * 1024;        // 256 KB for files 100–500 MB
const CHUNK_LARGE   = 1024 * 1024;       // 1 MB for files 500 MB–2 GB
const CHUNK_HUGE    = 4   * 1024 * 1024;  // 4 MB for files >= 2 GB

const MAX_BUFFER_BYTES = 16 * 1024 * 1024;  // 16 MB pause threshold
const LOW_BUFFER_BYTES = 1  * 1024 * 1024;  // 1 MB resume threshold
const ICE_TIMEOUT_MS   = 15_000;
const RELAY_CHUNK_SIZE = 48 * 1024; // 48 KB → base64 fits under 64 KB WS limit

const MSG_FILE_META = 0x01;
const MSG_CHUNK     = 0x02;
const MSG_DONE      = 0x03;
const MSG_CANCEL    = 0x04;

const ICE_SERVERS = [
  { urls: 'stun:stun.l.google.com:19302' },
  { urls: 'stun:stun1.l.google.com:19302' },
  { urls: 'stun:stun2.l.google.com:19302' },
  {
    urls: [
      'turn:openrelay.metered.ca:80',
      'turn:openrelay.metered.ca:80?transport=tcp',
      'turn:openrelay.metered.ca:443?transport=tcp',
    ],
    username: 'openrelayproject',
    credential: 'openrelayproject',
  },
];

function genId(): string {
  return 'rn_' + Math.random().toString(36).slice(2, 9) + '_' + Date.now().toString(36);
}
function selectChunkSize(fileSize: number): number {
  if (fileSize < 100 * 1024 * 1024) return CHUNK_SMALL;    // <100 MB  → 16 KB
  if (fileSize < 500 * 1024 * 1024) return CHUNK_MEDIUM;   // 100–500 MB → 256 KB
  if (fileSize < 2 * 1024 * 1024 * 1024) return CHUNK_LARGE; // 500 MB–2 GB → 1 MB
  return CHUNK_HUGE;                                         // ≥2 GB → 4 MB
}

// ============================================================================
// WebRTCService
// ============================================================================

export class WebRTCService {
  private signaling: SignalingClient;
  private peers = new Map<string, WebRTCPeer>();
  private transfers = new Map<string, TransferState>();
  private myPeerId: string | null = null;
  private myName: string;

  private sendQueue: Array<{ peerId: string; files: FilePickResult[] }> = [];
  private pendingFileRequests = new Map<string, { from: string; fromName: string; files: FileInfo[] }>();
  private relayReceiveBuffers = new Map<string, { chunks: Uint8Array[]; meta: any }>();
  private currentReceive: TransferState | null = null;

  public onPeersUpdated: ((peers: PeerInfo[]) => void) | null = null;
  public onIncomingFileRequest: ((from: string, fromName: string, files: FileInfo[]) => void) | null = null;
  public onProgress: ((info: ProgressInfo) => void) | null = null;
  public onTransferComplete: ((result: TransferResult) => void) | null = null;
  public onTransferError: ((result: TransferResult) => void) | null = null;
  public onConnected: (() => void) | null = null;
  public onDisconnected: (() => void) | null = null;

  constructor(deviceName: string, opts: { room?: string } = {}) {
    this.myName = deviceName;
    this.signaling = new SignalingClient({ deviceName, room: opts.room ?? 'teleport-default' });
    this._setupSignaling();
  }

  // ============================================================================
  // Lifecycle
  // ============================================================================

  start(): void { this.signaling.connect(); }

  stop(): void {
    this.signaling.disconnect();
    for (const [id] of this.peers) { this._cleanupPeer(id); }
    this.peers.clear();
    this.transfers.clear();
  }

  isConnected(): boolean { return this.signaling.isConnected(); }
  getMyPeerId(): string | null { return this.myPeerId; }

  // ============================================================================
  // Send files
  // ============================================================================

  async sendFiles(toPeerId: string, files: FilePickResult[]): Promise<boolean> {
    const fileInfos: FileInfo[] = files.map(f => ({
      name: f.name, size: f.size, mimeType: f.type ?? 'application/octet-stream',
    }));
    this.signaling.sendFileRequest(toPeerId, fileInfos);
    this.sendQueue.push({ peerId: toPeerId, files });

    const peer = this.peers.get(toPeerId);
    if (!peer) {
      await this._createPeerAsOfferer(toPeerId);
    }
    return true;
  }

  async acceptIncomingTransfer(fromPeerId: string): Promise<void> {
    if (!this.pendingFileRequests.has(fromPeerId)) { return; }
    this.signaling.sendFileResponse(fromPeerId, true);
  }

  rejectIncomingTransfer(fromPeerId: string): void {
    this.signaling.sendFileResponse(fromPeerId, false);
    this.pendingFileRequests.delete(fromPeerId);
  }

  // ============================================================================
  // Signaling
  // ============================================================================

  private _setupSignaling(): void {
    this.signaling.onPeerId = (id) => { this.myPeerId = id; };
    this.signaling.onPeers = (list) => { this.onPeersUpdated?.(list); };
    this.signaling.onConnected = () => { this.onConnected?.(); };
    this.signaling.onDisconnected = () => { this.onDisconnected?.(); };
    this.signaling.onMessage = (msg: SignalingMessage) => { this._handleSignalingMsg(msg); };
  }

  private _handleSignalingMsg(msg: SignalingMessage): void {
    switch (msg.type) {
      case 'peers': this.onPeersUpdated?.(msg.peers); break;
      case 'peer-joined': this.onPeersUpdated?.([msg.peer]); break;
      case 'peer-left': this._cleanupPeer(msg.peerId); break;
      case 'offer': this._handleOffer(msg.from, msg.sdp, msg.fingerprint, msg.publicKey); break;
      case 'answer': this._handleAnswer(msg.from, msg.sdp); break;
      case 'ice': this._handleIce(msg.from, msg.candidate); break;
      case 'file-request': this._handleFileRequest(msg.from, msg.fromName, msg.files); break;
      case 'file-response': this._handleFileResponse(msg.from, msg.accepted); break;
      case 'relay-start': this._handleRelayStart(msg as any); break;
      case 'relay-chunk': this._handleRelayChunk(msg as any); break;
      case 'relay-end': this._handleRelayEnd(msg.from, msg.transferId); break;
      case 'relay-cancel': this._handleRelayCancel(msg.from, msg.transferId, msg.reason); break;
    }
  }

  // ============================================================================
  // WebRTC — connection
  // ============================================================================

  private async _createPeerAsOfferer(peerId: string): Promise<void> {
    const pc = this._createPeerConnection(peerId);

    // Create DataChannel (offerer creates it)
    const dc = (pc.createDataChannel as any)('teleport', { ordered: true }) as RTCDataChannelType;
    const peer = this.peers.get(peerId)!;
    peer.dc = dc;
    this._setupDataChannel(peerId, dc);

    const offer = await pc.createOffer({});
    await pc.setLocalDescription(offer as RTCSessionDescription);
    this.signaling.sendOffer(peerId, (offer as any).sdp);

    // ICE timeout → relay fallback
    setTimeout(() => {
      const p = this.peers.get(peerId);
      if (p && !p.dcReady && !p.relayFallback) {
        console.warn(`[WebRTC] ICE timeout for ${peerId} → relay fallback`);
        p.relayFallback = true;
        this._flushSendQueueRelay(peerId);
      }
    }, ICE_TIMEOUT_MS);
  }

  private async _handleOffer(fromPeerId: string, sdp: string, _fp: string | null, _pk: string | null): Promise<void> {
    const pc = this._createPeerConnection(fromPeerId);
    const peer = this.peers.get(fromPeerId)!;

    await pc.setRemoteDescription(new RTCSessionDescription({ type: 'offer', sdp }));

    // Drain queued ICE candidates
    for (const c of peer.pendingCandidates) {
      try { await pc.addIceCandidate(new RTCIceCandidate(c)); } catch (_) {}
    }
    peer.pendingCandidates = [];

    const answer = await pc.createAnswer();
    await pc.setLocalDescription(answer as RTCSessionDescription);
    this.signaling.sendAnswer(fromPeerId, (answer as any).sdp);

    // Answerer listens for DataChannel
    (pc as any).addEventListener('datachannel', (evt: any) => {
      const dc = evt.channel as RTCDataChannelType;
      peer.dc = dc;
      this._setupDataChannel(fromPeerId, dc);
    });
  }

  private async _handleAnswer(fromPeerId: string, sdp: string): Promise<void> {
    const peer = this.peers.get(fromPeerId);
    if (!peer) { return; }
    await peer.pc.setRemoteDescription(new RTCSessionDescription({ type: 'answer', sdp }));
    for (const c of peer.pendingCandidates) {
      try { await peer.pc.addIceCandidate(new RTCIceCandidate(c)); } catch (_) {}
    }
    peer.pendingCandidates = [];
  }

  private async _handleIce(fromPeerId: string, candidate: any): Promise<void> {
    const peer = this.peers.get(fromPeerId);
    if (!peer) { return; }
    if (peer.pc.remoteDescription) {
      try { await peer.pc.addIceCandidate(new RTCIceCandidate(candidate)); } catch (_) {}
    } else {
      peer.pendingCandidates.push(candidate);
    }
  }

  private _createPeerConnection(peerId: string): RTCPeerConnection {
    if (this.peers.has(peerId)) { return this.peers.get(peerId)!.pc; }

    const pc = new RTCPeerConnection({ iceServers: ICE_SERVERS } as any);

    const peer: WebRTCPeer = {
      peerId, name: 'Unknown', clientType: 'unknown',
      pc, dc: null, dcReady: false, pendingCandidates: [], iceDone: false, relayFallback: false,
    };
    this.peers.set(peerId, peer);

    // EventTarget-style listeners (react-native-webrtc API)
    (pc as any).addEventListener('icecandidate', (evt: any) => {
      if (evt.candidate) {
        this.signaling.sendIce(peerId, evt.candidate.toJSON ? evt.candidate.toJSON() : evt.candidate);
      } else {
        peer.iceDone = true;
      }
    });

    (pc as any).addEventListener('connectionstatechange', () => {
      const state = pc.connectionState;
      console.log(`[WebRTC] ${peerId} connection: ${state}`);
      if ((state === 'failed' || state === 'disconnected') && !peer.relayFallback) {
        peer.relayFallback = true;
        this._flushSendQueueRelay(peerId);
      }
    });

    return pc;
  }

  // ============================================================================
  // DataChannel
  // ============================================================================

  private _setupDataChannel(peerId: string, dc: RTCDataChannelType): void {
    dc.binaryType = 'arraybuffer';
    dc.bufferedAmountLowThreshold = LOW_BUFFER_BYTES;

    // Use property callbacks — correctly typed in react-native-webrtc
    (dc as any).onopen = () => {
      console.log('[WebRTC] DC open with', peerId);
      const p = this.peers.get(peerId);
      if (p) { p.dcReady = true; }
      this._flushSendQueue(peerId);
    };

    (dc as any).onclose = () => {
      const p = this.peers.get(peerId);
      if (p) { p.dcReady = false; }
    };

    (dc as any).onmessage = (evt: any) => {
      this._handleDCMessage(peerId, evt.data as ArrayBuffer);
    };
  }

  // ============================================================================
  // Sending over DataChannel
  // ============================================================================

  private _flushSendQueue(peerId: string): void {
    const items = this.sendQueue.filter(q => q.peerId === peerId);
    this.sendQueue = this.sendQueue.filter(q => q.peerId !== peerId);
    for (const item of items) { this._sendFilesOverDC(peerId, item.files); }
  }

  private async _sendFilesOverDC(peerId: string, files: FilePickResult[]): Promise<void> {
    const peer = this.peers.get(peerId);
    if (!peer?.dc || !peer.dcReady) { return; }

    const transferId = genId();

    for (let fileIndex = 0; fileIndex < files.length; fileIndex++) {
      const file = files[fileIndex]!;
      const chunkSize = selectChunkSize(file.size);

      // FILE_META frame
      const meta = JSON.stringify({
        transferId, filename: file.name, size: file.size,
        mimeType: file.type ?? 'application/octet-stream',
        fileIndex, totalFiles: files.length, sha256: null,
      });
      peer.dc.send(this._buildFrame(MSG_FILE_META, new TextEncoder().encode(meta)));

      const state: TransferState = {
        transferId, peerId, direction: 'send',
        filename: file.name, fileSize: file.size, bytesTransferred: 0,
        fileIndex, totalFiles: files.length, startedAt: Date.now(),
        writerPath: null, sha256: null, writeOffset: 0, chunkCount: 0,
      };
      this.transfers.set(transferId + '_' + fileIndex, state);

      try {
        const filePath = file.uri.replace('file://', '');
        // Read full file as base64 (react-native-blob-util)
        const b64 = await ReactNativeBlobUtil.fs.readFile(filePath, 'base64');
        const allBytes = this._b64ToUint8(b64);

        let offset = 0;
        const startedAt = Date.now();

        while (offset < allBytes.length) {
          // Back-pressure: pause if buffer full
          if (peer.dc.bufferedAmount > MAX_BUFFER_BYTES) {
            await this._waitDrain(peer.dc);
          }
          const end = Math.min(offset + chunkSize, allBytes.length);
          const chunk = allBytes.slice(offset, end);
          peer.dc.send(this._buildFrame(MSG_CHUNK, chunk));

          offset = end;
          state.bytesTransferred = offset;

          const elapsed = (Date.now() - startedAt) / 1000;
          this.onProgress?.({
            transferId, peerId, direction: 'send', filename: file.name,
            bytesTransferred: offset, totalBytes: file.size,
            percent: (offset / file.size) * 100,
            speedBps: elapsed > 0 ? offset / elapsed : 0,
          });
        }

        // DONE frame
        const donePayload = JSON.stringify({ transferId, fileIndex, sha256: null });
        peer.dc.send(this._buildFrame(MSG_DONE, new TextEncoder().encode(donePayload)));
        this.onTransferComplete?.({ transferId, peerId, filename: file.name, success: true });
      } catch (err: any) {
        try { peer.dc.send(this._buildFrame(MSG_CANCEL, new TextEncoder().encode(transferId))); } catch (_) {}
        this.onTransferError?.({ transferId, peerId, filename: file.name, success: false, error: err?.message });
      }
    }
  }

  private _waitDrain(dc: RTCDataChannelType): Promise<void> {
    return new Promise<void>((resolve) => {
      const check = () => {
        if (dc.bufferedAmount <= LOW_BUFFER_BYTES || dc.readyState !== 'open') { resolve(); }
        else { setTimeout(check, 50); }
      };
      check();
    });
  }

  // ============================================================================
  // Receiving over DataChannel
  // ============================================================================

  private _handleDCMessage(peerId: string, data: ArrayBuffer): void {
    const view = new Uint8Array(data);
    if (view.length === 0) { return; }
    const msgType = view[0]!;
    const payload = view.slice(1);

    switch (msgType) {
      case MSG_FILE_META: this._rxFileMeta(peerId, payload); break;
      case MSG_CHUNK: this._rxChunk(peerId, payload); break;
      case MSG_DONE: this._rxDone(peerId, payload); break;
      case MSG_CANCEL: this._rxCancel(peerId, new TextDecoder().decode(payload)); break;
      default: console.warn('[WebRTC] Unknown msg type:', msgType);
    }
  }

  private async _rxFileMeta(peerId: string, payload: Uint8Array): Promise<void> {
    try {
      const meta = JSON.parse(new TextDecoder().decode(payload));
      const savePath = `${ReactNativeBlobUtil.fs.dirs.DownloadDir}/${meta.filename}`;
      // Create/truncate destination file
      await ReactNativeBlobUtil.fs.writeFile(savePath, '', 'utf8');

      this.currentReceive = {
        transferId: meta.transferId, peerId, direction: 'receive',
        filename: meta.filename, fileSize: meta.size, bytesTransferred: 0,
        fileIndex: meta.fileIndex ?? 0, totalFiles: meta.totalFiles ?? 1,
        startedAt: Date.now(), writerPath: savePath, sha256: null,
        writeOffset: 0, chunkCount: 0,
      };
      console.log(`[WebRTC] Receiving: ${meta.filename} → ${savePath}`);
    } catch (e) {
      console.error('[WebRTC] FileMeta parse error:', e);
    }
  }

  private async _rxChunk(peerId: string, chunk: Uint8Array): Promise<void> {
    const state = this.currentReceive;
    if (!state) { return; }

    try {
      if (state.writerPath) {
        await ReactNativeBlobUtil.fs.appendFile(state.writerPath, this._uint8ToB64(chunk), 'base64');
        state.writeOffset += chunk.length;
      }
    } catch (e) {
      console.error('[WebRTC] Chunk write error:', e);
    }

    state.bytesTransferred += chunk.length;
    state.chunkCount++;
    const elapsed = (Date.now() - state.startedAt) / 1000;
    this.onProgress?.({
      transferId: state.transferId, peerId, direction: 'receive',
      filename: state.filename,
      bytesTransferred: state.bytesTransferred,
      totalBytes: state.fileSize,
      percent: (state.bytesTransferred / state.fileSize) * 100,
      speedBps: elapsed > 0 ? state.bytesTransferred / elapsed : 0,
    });
  }

  private _rxDone(peerId: string, payload: Uint8Array): void {
    const state = this.currentReceive;
    if (!state) { return; }
    this.currentReceive = null;
    console.log(`[WebRTC] Done: ${state.filename} (${state.bytesTransferred} bytes)`);
    this.onTransferComplete?.({
      transferId: state.transferId, peerId,
      filename: state.filename, savedPath: state.writerPath ?? undefined, success: true,
    });
  }

  private _rxCancel(peerId: string, transferId: string): void {
    this.currentReceive = null;
    this.onTransferError?.({ transferId, peerId, filename: 'unknown', success: false, error: 'Cancelled by sender' });
  }

  // ============================================================================
  // File request / response handlers
  // ============================================================================

  private _handleFileRequest(from: string, fromName: string, files: FileInfo[]): void {
    this.pendingFileRequests.set(from, { from, fromName, files });
    this.onIncomingFileRequest?.(from, fromName, files);
  }

  private _handleFileResponse(from: string, accepted: boolean): void {
    if (!accepted) {
      this.sendQueue = this.sendQueue.filter(q => q.peerId !== from);
      this.onTransferError?.({ transferId: '', peerId: from, filename: '', success: false, error: 'Transfer rejected by peer' });
      return;
    }
    const peer = this.peers.get(from);
    if (peer?.dcReady) { this._flushSendQueue(from); }
  }

  // ============================================================================
  // Relay fallback
  // ============================================================================

  private async _flushSendQueueRelay(peerId: string): Promise<void> {
    const items = this.sendQueue.filter(q => q.peerId === peerId);
    this.sendQueue = this.sendQueue.filter(q => q.peerId !== peerId);
    for (const item of items) { await this._sendFilesViaRelay(peerId, item.files); }
  }

  private async _sendFilesViaRelay(peerId: string, files: FilePickResult[]): Promise<void> {
    const transferId = genId();
    for (let i = 0; i < files.length; i++) {
      const file = files[i]!;
      this.signaling.sendRelayStart(peerId, {
        transferId, filename: file.name, size: file.size,
        mimeType: file.type ?? 'application/octet-stream',
        fileIndex: i, totalFiles: files.length, sha256: null,
      });
      try {
        const b64 = await ReactNativeBlobUtil.fs.readFile(file.uri.replace('file://', ''), 'base64');
        const raw = this._b64ToUint8(b64);
        let offset = 0;
        const startedAt = Date.now();
        while (offset < raw.length) {
          const end = Math.min(offset + RELAY_CHUNK_SIZE, raw.length);
          this.signaling.sendRelayChunk(peerId, transferId, this._uint8ToB64(raw.slice(offset, end)), offset);
          offset = end;
          const elapsed = (Date.now() - startedAt) / 1000;
          this.onProgress?.({
            transferId, peerId, direction: 'send', filename: file.name,
            bytesTransferred: offset, totalBytes: file.size,
            percent: (offset / file.size) * 100,
            speedBps: elapsed > 0 ? offset / elapsed : 0,
          });
          await new Promise<void>(r => setTimeout(r, 10)); // throttle
        }
        this.signaling.sendRelayEnd(peerId, transferId);
        this.onTransferComplete?.({ transferId, peerId, filename: file.name, success: true });
      } catch (err: any) {
        this.signaling.sendRelayCancel(peerId, transferId, err?.message ?? 'error');
        this.onTransferError?.({ transferId, peerId, filename: file.name, success: false, error: err?.message });
      }
    }
  }

  private _handleRelayStart(msg: any): void {
    this.relayReceiveBuffers.set(msg.transferId, { chunks: [], meta: msg });
  }

  private _handleRelayChunk(msg: any): void {
    const buf = this.relayReceiveBuffers.get(msg.transferId);
    if (!buf) { return; }
    buf.chunks.push(this._b64ToUint8(msg.data));
    const received = buf.chunks.reduce((s, c) => s + c.length, 0);
    this.onProgress?.({
      transferId: msg.transferId, peerId: msg.from, direction: 'receive',
      filename: buf.meta.filename, bytesTransferred: received,
      totalBytes: buf.meta.size, percent: (received / buf.meta.size) * 100, speedBps: 0,
    });
  }

  private async _handleRelayEnd(from: string, transferId: string): Promise<void> {
    const buf = this.relayReceiveBuffers.get(transferId);
    if (!buf) { return; }
    this.relayReceiveBuffers.delete(transferId);
    try {
      const totalLen = buf.chunks.reduce((s, c) => s + c.length, 0);
      const assembled = new Uint8Array(totalLen);
      let off = 0;
      for (const c of buf.chunks) { assembled.set(c, off); off += c.length; }
      const savePath = `${ReactNativeBlobUtil.fs.dirs.DownloadDir}/${buf.meta.filename}`;
      await ReactNativeBlobUtil.fs.writeFile(savePath, this._uint8ToB64(assembled), 'base64');
      this.signaling.sendRelayVerified(from, transferId, true, null);
      this.onTransferComplete?.({ transferId, peerId: from, filename: buf.meta.filename, savedPath: savePath, success: true });
    } catch (e: any) {
      this.signaling.sendRelayVerified(from, transferId, false, null);
      this.onTransferError?.({ transferId, peerId: from, filename: buf.meta?.filename ?? '', success: false, error: e?.message });
    }
  }

  private _handleRelayCancel(from: string, transferId: string, reason: string | null): void {
    this.relayReceiveBuffers.delete(transferId);
    this.onTransferError?.({ transferId, peerId: from, filename: '', success: false, error: reason ?? 'Cancelled' });
  }

  // ============================================================================
  // Cleanup
  // ============================================================================

  private _cleanupPeer(peerId: string): void {
    const peer = this.peers.get(peerId);
    if (!peer) { return; }
    try { peer.dc?.close(); } catch (_) {}
    try { peer.pc.close(); } catch (_) {}
    this.peers.delete(peerId);
    this.sendQueue = this.sendQueue.filter(q => q.peerId !== peerId);
  }

  // ============================================================================
  // Utils
  // ============================================================================

  private _buildFrame(type: number, payload: Uint8Array): ArrayBuffer {
    const frame = new Uint8Array(1 + payload.length);
    frame[0] = type;
    frame.set(payload, 1);
    return frame.buffer as ArrayBuffer;
  }

  private _b64ToUint8(b64: string): Uint8Array {
    const bin = atob(b64);
    const out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) { out[i] = bin.charCodeAt(i); }
    return out;
  }

  private _uint8ToB64(bytes: Uint8Array): string {
    let bin = '';
    for (let i = 0; i < bytes.length; i++) { bin += String.fromCharCode(bytes[i]!); }
    return btoa(bin);
  }
}

export default WebRTCService;
