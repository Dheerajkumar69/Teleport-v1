/**
 * Teleport Native Module TypeScript Interface
 */
import { NativeModules, NativeEventEmitter, Platform } from 'react-native';

const { TeleportModule } = NativeModules;

export interface Device {
    id: string;
    name: string;
    os: string;
    ip: string;
    port: number;
}

export interface TransferProgress {
    bytesTransferred: number;
    bytesTotal: number;
    speedBps: number;
    filesCompleted: number;
    filesTotal: number;
}

export interface TransferComplete {
    errorCode: number;
    success: boolean;
}

class TeleportService {
    private eventEmitter: NativeEventEmitter | null = null;
    private listeners: Map<string, any> = new Map();

    constructor() {
        if (Platform.OS === 'android' && TeleportModule) {
            this.eventEmitter = new NativeEventEmitter(TeleportModule);
        }
    }

    // Discovery
    async startDiscovery(): Promise<boolean> {
        return TeleportModule.startDiscovery();
    }

    async stopDiscovery(): Promise<boolean> {
        return TeleportModule.stopDiscovery();
    }

    async getDevices(): Promise<Device[]> {
        const json = await TeleportModule.getDevices();
        return JSON.parse(json);
    }

    // Transfer
    async sendFiles(deviceId: string, filePaths: string[]): Promise<boolean> {
        return TeleportModule.sendFiles(deviceId, filePaths);
    }

    async startReceiving(): Promise<boolean> {
        return TeleportModule.startReceiving();
    }

    async stopReceiving(): Promise<boolean> {
        return TeleportModule.stopReceiving();
    }

    // Events
    onDeviceFound(callback: (device: Device) => void): () => void {
        if (!this.eventEmitter) return () => { };
        const subscription = this.eventEmitter.addListener('onDeviceFound', callback);
        return () => subscription.remove();
    }

    onDeviceLost(callback: (data: { id: string }) => void): () => void {
        if (!this.eventEmitter) return () => { };
        const subscription = this.eventEmitter.addListener('onDeviceLost', callback);
        return () => subscription.remove();
    }

    onTransferProgress(callback: (progress: TransferProgress) => void): () => void {
        if (!this.eventEmitter) return () => { };
        const subscription = this.eventEmitter.addListener('onTransferProgress', callback);
        return () => subscription.remove();
    }

    onTransferComplete(callback: (result: TransferComplete) => void): () => void {
        if (!this.eventEmitter) return () => { };
        const subscription = this.eventEmitter.addListener('onTransferComplete', callback);
        return () => subscription.remove();
    }
}

export const teleport = new TeleportService();
export default teleport;
