/**
 * TypeScript service layer for Teleport native module
 * Handles real file transfer events from native code
 * Includes QR Code Pairing, Hotspot Mode, and WiFi Direct
 */
import { NativeModules, NativeEventEmitter, Platform } from 'react-native';

const { TeleportModule } = NativeModules;

export interface TeleportDevice {
  id: string;
  name: string;
  ip: string;
  port: number;
  os: string;
}

export interface TransferProgress {
  bytesTransferred: number;
  totalBytes: number;
  percent: number;
  currentFile: string;
  filesCompleted: number;
  filesTotal: number;
}

export interface QrPairingInfo {
  ip: string;
  port: number;
  sessionToken: string;
  deviceName: string;
  expiresAt: number;
  valid?: boolean;
}

export interface HotspotInfo {
  ssid: string;
  password: string;
  gatewayIp: string;
  controlPort: number;
  isActive: boolean;
  clientCount: number;
}

export interface IncomingFilesInfo {
  senderName: string;
  senderIp: string;
  fileCount: number;
  totalSize: number;
  files: Array<{ name: string; size: number }>;
}

type EventCallback<T> = (data: T) => void;

class TeleportService {
  private eventEmitter: NativeEventEmitter | null = null;
  private initialized = false;
  private subscriptions: any[] = [];

  async initialize(deviceName: string): Promise<boolean> {
    try {
      const result = await TeleportModule.initialize(deviceName);
      if (result) {
        this.eventEmitter = new NativeEventEmitter(TeleportModule);
        this.initialized = true;
        console.log('[TeleportService] Initialized successfully');
      }
      return result;
    } catch (error) {
      console.error('[TeleportService] Failed to initialize:', error);
      return false;
    }
  }

  async destroy(): Promise<void> {
    this.subscriptions.forEach(sub => sub.remove());
    this.subscriptions = [];

    if (this.initialized) {
      await TeleportModule.destroy();
      this.eventEmitter = null;
      this.initialized = false;
    }
  }

  // ============================================================================
  // Device Discovery
  // ============================================================================

  async startDiscovery(): Promise<boolean> {
    if (!this.initialized) return false;
    try {
      return await TeleportModule.startDiscovery();
    } catch (error) {
      console.error('[TeleportService] startDiscovery error:', error);
      return false;
    }
  }

  async stopDiscovery(): Promise<boolean> {
    if (!this.initialized) return false;
    try {
      return await TeleportModule.stopDiscovery();
    } catch (error) {
      console.error('[TeleportService] stopDiscovery error:', error);
      return false;
    }
  }

  async getDevices(): Promise<TeleportDevice[]> {
    if (!this.initialized) return [];
    try {
      const json = await TeleportModule.getDevices();
      return JSON.parse(json) as TeleportDevice[];
    } catch (error) {
      console.error('[TeleportService] getDevices error:', error);
      return [];
    }
  }

  // ============================================================================
  // File Transfer
  // ============================================================================

  async sendFiles(targetId: string, filePaths: string[]): Promise<boolean> {
    if (!this.initialized) return false;
    try {
      console.log('[TeleportService] Sending files:', filePaths, 'to:', targetId);
      return await TeleportModule.sendFiles(targetId, filePaths);
    } catch (error) {
      console.error('[TeleportService] sendFiles error:', error);
      return false;
    }
  }

  async startReceiving(outputDir: string): Promise<boolean> {
    if (!this.initialized) return false;
    try {
      console.log('[TeleportService] Starting receiving to:', outputDir);
      return await TeleportModule.startReceiving(outputDir);
    } catch (error) {
      console.error('[TeleportService] startReceiving error:', error);
      return false;
    }
  }

  async stopReceiving(): Promise<boolean> {
    if (!this.initialized) return false;
    try {
      return await TeleportModule.stopReceiving();
    } catch (error) {
      console.error('[TeleportService] stopReceiving error:', error);
      return false;
    }
  }

  // ============================================================================
  // QR Code Pairing
  // ============================================================================

  async generateQrPairing(expirySeconds: number = 300): Promise<QrPairingInfo | null> {
    if (!this.initialized) return null;
    try {
      const json = await TeleportModule.generateQrPairing(expirySeconds);
      if (!json) return null;
      return JSON.parse(json) as QrPairingInfo;
    } catch (error) {
      console.error('[TeleportService] generateQrPairing error:', error);
      return null;
    }
  }

  async connectViaQr(qrData: string): Promise<boolean> {
    if (!this.initialized) return false;
    try {
      console.log('[TeleportService] Connecting via QR');
      return await TeleportModule.connectViaQr(qrData);
    } catch (error) {
      console.error('[TeleportService] connectViaQr error:', error);
      return false;
    }
  }

  async validateQrPairing(qrData: string): Promise<QrPairingInfo | null> {
    try {
      const json = await TeleportModule.validateQrPairing(qrData);
      if (!json) return null;
      return JSON.parse(json) as QrPairingInfo;
    } catch (error) {
      console.error('[TeleportService] validateQrPairing error:', error);
      return null;
    }
  }

  // ============================================================================
  // Hotspot Mode
  // ============================================================================

  async isHotspotSupported(): Promise<boolean> {
    try {
      return await TeleportModule.isHotspotSupported();
    } catch (error) {
      return false;
    }
  }

  async createHotspot(): Promise<HotspotInfo | null> {
    if (!this.initialized) return null;
    try {
      const json = await TeleportModule.createHotspot();
      if (!json) return null;
      return JSON.parse(json) as HotspotInfo;
    } catch (error) {
      console.error('[TeleportService] createHotspot error:', error);
      return null;
    }
  }

  async destroyHotspot(): Promise<boolean> {
    if (!this.initialized) return false;
    try {
      return await TeleportModule.destroyHotspot();
    } catch (error) {
      console.error('[TeleportService] destroyHotspot error:', error);
      return false;
    }
  }

  async getHotspotInfo(): Promise<HotspotInfo | null> {
    if (!this.initialized) return null;
    try {
      const json = await TeleportModule.getHotspotInfo();
      if (!json) return null;
      return JSON.parse(json) as HotspotInfo;
    } catch (error) {
      return null;
    }
  }

  async detectHotspot(): Promise<string> {
    try {
      return await TeleportModule.detectHotspot() || '';
    } catch (error) {
      return '';
    }
  }

  // ============================================================================
  // WiFi Direct
  // ============================================================================

  async isWifiDirectSupported(): Promise<boolean> {
    try {
      return await TeleportModule.isWifiDirectSupported();
    } catch (error) {
      return false;
    }
  }

  async wifiDirectDisconnect(): Promise<boolean> {
    if (!this.initialized) return false;
    try {
      return await TeleportModule.wifiDirectDisconnect();
    } catch (error) {
      return false;
    }
  }

  // ============================================================================
  // Event Listeners
  // ============================================================================

  onDeviceDiscovered(callback: EventCallback<TeleportDevice>): () => void {
    if (!this.eventEmitter) return () => { };
    const subscription = this.eventEmitter.addListener('onDeviceDiscovered', (data: string) => {
      try {
        const device = JSON.parse(data) as TeleportDevice;
        console.log('[TeleportService] Device discovered:', device.name);
        callback(device);
      } catch (e) {
        console.error('[TeleportService] Failed to parse device:', e);
      }
    });
    this.subscriptions.push(subscription);
    return () => subscription.remove();
  }

  onDeviceLost(callback: EventCallback<string>): () => void {
    if (!this.eventEmitter) return () => { };
    const subscription = this.eventEmitter.addListener('onDeviceLost', (deviceId: string) => {
      console.log('[TeleportService] Device lost:', deviceId);
      callback(deviceId);
    });
    this.subscriptions.push(subscription);
    return () => subscription.remove();
  }

  onProgress(callback: EventCallback<TransferProgress>): () => void {
    if (!this.eventEmitter) return () => { };
    const subscription = this.eventEmitter.addListener('onProgress', (data: string) => {
      try {
        const progress = JSON.parse(data) as TransferProgress;
        console.log('[TeleportService] Progress:', progress.percent.toFixed(1) + '%');
        callback(progress);
      } catch (e) {
        console.error('[TeleportService] Failed to parse progress:', e);
      }
    });
    this.subscriptions.push(subscription);
    return () => subscription.remove();
  }

  onComplete(callback: EventCallback<number>): () => void {
    if (!this.eventEmitter) return () => { };
    const subscription = this.eventEmitter.addListener('onComplete', (data: string) => {
      const errorCode = parseInt(data, 10);
      console.log('[TeleportService] Transfer complete, error code:', errorCode);
      callback(errorCode);
    });
    this.subscriptions.push(subscription);
    return () => subscription.remove();
  }

  onIncomingFiles(callback: EventCallback<IncomingFilesInfo>): () => void {
    if (!this.eventEmitter) return () => { };
    const subscription = this.eventEmitter.addListener('onIncomingFiles', (data: string) => {
      try {
        const info = JSON.parse(data) as IncomingFilesInfo;
        console.log('[TeleportService] Incoming files from:', info.senderName);
        callback(info);
      } catch (e) {
        console.error('[TeleportService] Failed to parse incoming files:', e);
      }
    });
    this.subscriptions.push(subscription);
    return () => subscription.remove();
  }

  async acceptIncomingFiles(): Promise<boolean> {
    if (!this.initialized) return false;
    try {
      return await TeleportModule.acceptIncomingFiles();
    } catch (error) {
      console.error('[TeleportService] acceptIncomingFiles error:', error);
      return false;
    }
  }

  async rejectIncomingFiles(): Promise<boolean> {
    if (!this.initialized) return false;
    try {
      return await TeleportModule.rejectIncomingFiles();
    } catch (error) {
      console.error('[TeleportService] rejectIncomingFiles error:', error);
      return false;
    }
  }
}

export const teleportService = new TeleportService();
export default teleportService;

