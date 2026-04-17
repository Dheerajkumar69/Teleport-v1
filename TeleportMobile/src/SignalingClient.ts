/**
 * Teleport SignalingClient — React Native
 * Speaks the same protocol as teleport-webrtc.js (web version).
 */

export type PeerInfo = {
  id: string;
  name: string;
  fingerprint: string | null;
  publicKey: string | null;
  clientType: string;
};

export type FileInfo = {
  name: string;
  size: number;
  mimeType?: string;
};

export type SignalingMessage =
  | { type: 'welcome'; peerId: string }
  | { type: 'peers'; peers: PeerInfo[] }
  | { type: 'peer-joined'; peer: PeerInfo }
  | { type: 'peer-left'; peerId: string }
  | { type: 'offer'; from: string; sdp: string; fingerprint: string | null; publicKey: string | null }
  | { type: 'answer'; from: string; sdp: string }
  | { type: 'ice'; from: string; candidate: { candidate: string; sdpMid: string | null; sdpMLineIndex: number | null } }
  | { type: 'file-request'; from: string; fromName: string; fingerprint: string | null; files: FileInfo[] }
  | { type: 'file-response'; from: string; accepted: boolean }
  | { type: 'relay-start'; from: string; transferId: string; filename: string; size: number; mimeType: string; fileIndex: number; totalFiles: number; sha256: string | null }
  | { type: 'relay-chunk'; from: string; transferId: string; data: string; offset: number }
  | { type: 'relay-end'; from: string; transferId: string }
  | { type: 'relay-cancel'; from: string; transferId: string; reason: string | null }
  | { type: 'relay-verified'; from: string; transferId: string; ok: boolean; sha256: string | null };

// ============================================================================
// CONFIG
// ============================================================================

export let USE_USB_FORWARD = false;

export function setUsbMode(enabled: boolean): void {
  USE_USB_FORWARD = enabled;
}

const USB_DEVICE_URL = 'ws://localhost:3000'; // After `adb reverse tcp:3000 tcp:3000`

const CLOUD_SIGNALING_SERVERS = [
  'wss://teleport-signaling.dheeraj-kumar-28c.workers.dev',
  'wss://teleport-signaling.onrender.com',
];

const MAX_RECONNECT_ATTEMPTS = 10;
const INITIAL_RECONNECT_DELAY_MS = 1000;
const MAX_RECONNECT_DELAY_MS = 30000;
const PING_INTERVAL_MS = 20000;
const MESSAGE_SIZE_LIMIT = 1024 * 1024; // 1MB — matches server limit

// ============================================================================
// SignalingClient
// ============================================================================

export class SignalingClient {
  private ws: WebSocket | null = null;
  private peerId: string | null = null;
  private deviceName: string;
  private room: string;
  private fingerprint: string | null;
  private publicKey: string | null;

  private reconnectAttempts = 0;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private pingTimer: ReturnType<typeof setInterval> | null = null;
  private destroyed = false;
  private currentServerIndex = 0;

  public onPeerId: ((id: string) => void) | null = null;
  public onPeers: ((peers: PeerInfo[]) => void) | null = null;
  public onMessage: ((msg: SignalingMessage) => void) | null = null;
  public onConnected: (() => void) | null = null;
  public onDisconnected: (() => void) | null = null;
  public onReconnecting: ((attempt: number) => void) | null = null;

  constructor(opts: {
    deviceName: string;
    room?: string;
    fingerprint?: string | null;
    publicKey?: string | null;
  }) {
    this.deviceName = opts.deviceName;
    this.room = opts.room ?? 'teleport-default';
    this.fingerprint = opts.fingerprint ?? null;
    this.publicKey = opts.publicKey ?? null;
  }

  connect(): void {
    if (this.destroyed) { return; }
    this._connect();
  }

  disconnect(): void {
    this.destroyed = true;
    this._clearTimers();
    if (this.ws) {
      try { this.ws.close(1000, 'Client disconnect'); } catch (_) {}
      this.ws = null;
    }
  }

  isConnected(): boolean {
    return this.ws?.readyState === WebSocket.OPEN;
  }

  getPeerId(): string | null { return this.peerId; }

  send(message: Record<string, unknown>): boolean {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
      console.warn('[Signaling] Cannot send — not connected');
      return false;
    }
    try {
      const str = JSON.stringify(message);
      if (str.length > MESSAGE_SIZE_LIMIT) {
        console.error('[Signaling] Message too large, dropping');
        return false;
      }
      this.ws.send(str);
      return true;
    } catch (e) {
      console.error('[Signaling] Send failed:', e);
      return false;
    }
  }

  sendOffer(toPeerId: string, sdp: string): void {
    this.send({ type: 'offer', to: toPeerId, sdp, fingerprint: this.fingerprint, publicKey: this.publicKey });
  }
  sendAnswer(toPeerId: string, sdp: string): void {
    this.send({ type: 'answer', to: toPeerId, sdp });
  }
  sendIce(toPeerId: string, candidate: object): void {
    this.send({ type: 'ice', to: toPeerId, candidate });
  }
  sendFileRequest(toPeerId: string, files: FileInfo[]): void {
    this.send({ type: 'file-request', to: toPeerId, files });
  }
  sendFileResponse(toPeerId: string, accepted: boolean): void {
    this.send({ type: 'file-response', to: toPeerId, accepted });
  }
  sendRelayStart(toPeerId: string, opts: {
    transferId: string; filename: string; size: number;
    mimeType: string; fileIndex: number; totalFiles: number; sha256: string | null;
  }): void {
    this.send({ type: 'relay-start', to: toPeerId, ...opts });
  }
  sendRelayChunk(toPeerId: string, transferId: string, data: string, offset: number): void {
    this.send({ type: 'relay-chunk', to: toPeerId, transferId, data, offset });
  }
  sendRelayEnd(toPeerId: string, transferId: string): void {
    this.send({ type: 'relay-end', to: toPeerId, transferId });
  }
  sendRelayCancel(toPeerId: string, transferId: string, reason: string): void {
    this.send({ type: 'relay-cancel', to: toPeerId, transferId, reason });
  }
  sendRelayVerified(toPeerId: string, transferId: string, ok: boolean, sha256: string | null): void {
    this.send({ type: 'relay-verified', to: toPeerId, transferId, ok, sha256 });
  }

  private _getNextServerUrl(): string {
    if (USE_USB_FORWARD) { return USB_DEVICE_URL; }
    const url = CLOUD_SIGNALING_SERVERS[this.currentServerIndex % CLOUD_SIGNALING_SERVERS.length]!;
    this.currentServerIndex++;
    return url;
  }

  private _connect(): void {
    if (this.destroyed) { return; }
    const url = this._getNextServerUrl();
    console.log(`[Signaling] Connecting to ${url} (attempt ${this.reconnectAttempts + 1})`);
    try {
      this.ws = new WebSocket(url);
    } catch (e) {
      console.error('[Signaling] WebSocket construction failed:', e);
      this._scheduleReconnect();
      return;
    }

    this.ws.onopen = () => {
      console.log('[Signaling] Connected to', url);
      this.reconnectAttempts = 0;
      this._startPing();
    };

    this.ws.onmessage = (evt) => {
      this._handleMessage(evt.data as string);
    };

    this.ws.onclose = (evt) => {
      console.log(`[Signaling] Closed: code=${evt.code}`);
      this._clearTimers();
      this.onDisconnected?.();
      if (!this.destroyed) { this._scheduleReconnect(); }
    };

    this.ws.onerror = () => {
      // onclose fires right after
    };
  }

  private _handleMessage(raw: string): void {
    let msg: SignalingMessage;
    try {
      msg = JSON.parse(raw) as SignalingMessage;
    } catch {
      console.error('[Signaling] Invalid JSON, dropping');
      return;
    }

    if (msg.type === 'welcome') {
      this.peerId = msg.peerId;
      console.log('[Signaling] My peer ID:', this.peerId);
      this.onPeerId?.(this.peerId);
      this.send({
        type: 'join',
        room: this.room,
        name: this.deviceName,
        fingerprint: this.fingerprint,
        publicKey: this.publicKey,
        clientType: 'android',
      });
      this.onConnected?.();
      return;
    }

    if (msg.type === 'peers') {
      this.onPeers?.(msg.peers);
    }
    this.onMessage?.(msg);
  }

  private _scheduleReconnect(): void {
    if (this.destroyed) { return; }
    if (this.reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
      console.error('[Signaling] Max reconnect attempts reached');
      return;
    }
    const delay = Math.min(
      INITIAL_RECONNECT_DELAY_MS * Math.pow(2, this.reconnectAttempts),
      MAX_RECONNECT_DELAY_MS,
    );
    this.reconnectAttempts++;
    console.log(`[Signaling] Reconnecting in ${delay}ms (attempt ${this.reconnectAttempts})`);
    this.onReconnecting?.(this.reconnectAttempts);
    this.reconnectTimer = setTimeout(() => { this._connect(); }, delay);
  }

  private _startPing(): void {
    this._clearTimers();
    this.pingTimer = setInterval(() => {
      if (this.ws?.readyState === WebSocket.OPEN) {
        this.send({ type: 'pong' });
      }
    }, PING_INTERVAL_MS);
  }

  private _clearTimers(): void {
    if (this.pingTimer) { clearInterval(this.pingTimer); this.pingTimer = null; }
    if (this.reconnectTimer) { clearTimeout(this.reconnectTimer); this.reconnectTimer = null; }
  }
}
