/**
 * Jest Unit Tests for SettingsStorage
 * Tests validation functions, storage operations, and edge cases
 */

// Mock AsyncStorage
jest.mock('@react-native-async-storage/async-storage', () => ({
    getItem: jest.fn(),
    setItem: jest.fn(),
    removeItem: jest.fn(),
}));

import AsyncStorage from '@react-native-async-storage/async-storage';
import {
    validateDownloadPath,
    validateDeviceName,
    loadSettings,
    saveSettings,
    loadHistory,
    addToHistory,
    clearHistory,
    generateTransferId,
    AppSettings,
    TransferRecord,
} from '../SettingsStorage';

describe('validateDownloadPath', () => {
    it('should return default for null/undefined', () => {
        expect(validateDownloadPath(null as any)).toBe('/storage/emulated/0/Download');
        expect(validateDownloadPath(undefined as any)).toBe('/storage/emulated/0/Download');
        expect(validateDownloadPath('')).toBe('/storage/emulated/0/Download');
    });

    it('should allow valid paths', () => {
        expect(validateDownloadPath('/storage/emulated/0/Download')).toBe('/storage/emulated/0/Download');
        expect(validateDownloadPath('/storage/emulated/0/Documents')).toBe('/storage/emulated/0/Documents');
        expect(validateDownloadPath('/sdcard/Download')).toBe('/sdcard/Download');
    });

    it('should reject path traversal attempts', () => {
        expect(validateDownloadPath('/storage/emulated/0/../etc/passwd')).toBe('/storage/emulated/0/Download');
        expect(validateDownloadPath('./malicious')).toBe('/storage/emulated/0/Download');
        expect(validateDownloadPath('/storage/emulated/0/Download/../../root')).toBe('/storage/emulated/0/Download');
    });

    it('should reject paths not in allowed list', () => {
        expect(validateDownloadPath('/etc/passwd')).toBe('/storage/emulated/0/Download');
        expect(validateDownloadPath('/root/.ssh')).toBe('/storage/emulated/0/Download');
        expect(validateDownloadPath('/tmp/malicious')).toBe('/storage/emulated/0/Download');
    });

    it('should normalize paths', () => {
        expect(validateDownloadPath('/storage/emulated/0/Download/')).toBe('/storage/emulated/0/Download');
        expect(validateDownloadPath('/storage/emulated/0//Download')).toBe('/storage/emulated/0/Download');
        expect(validateDownloadPath('  /storage/emulated/0/Download  ')).toBe('/storage/emulated/0/Download');
    });
});

describe('validateDeviceName', () => {
    it('should return default for null/undefined/empty', () => {
        expect(validateDeviceName(null as any)).toBe('TeleportMobile');
        expect(validateDeviceName(undefined as any)).toBe('TeleportMobile');
        expect(validateDeviceName('')).toBe('TeleportMobile');
        expect(validateDeviceName('   ')).toBe('TeleportMobile');
    });

    it('should allow valid names', () => {
        expect(validateDeviceName('MyPhone')).toBe('MyPhone');
        expect(validateDeviceName('Phone-123')).toBe('Phone-123');
        expect(validateDeviceName('My_Device')).toBe('My_Device');
        expect(validateDeviceName('Device 2024')).toBe('Device 2024');
    });

    it('should enforce max length (32 chars)', () => {
        const longName = 'A'.repeat(50);
        expect(validateDeviceName(longName).length).toBe(32);
    });

    it('should remove HTML/XSS characters', () => {
        expect(validateDeviceName('<script>alert(1)</script>')).toBe('scriptalert1script');
        expect(validateDeviceName('Name<b>Bold</b>')).toBe('NamebBoldb');
        expect(validateDeviceName("Name'with\"quotes")).toBe('Namewithquotes');
        expect(validateDeviceName('Name&entity')).toBe('Nameentity');
    });

    it('should remove control characters', () => {
        expect(validateDeviceName('Name\x00Hidden')).toBe('NameHidden');
        expect(validateDeviceName('Name\nNewline')).toBe('NameNewline');
        expect(validateDeviceName('Name\tTab')).toBe('NameTab');
    });

    it('should normalize whitespace', () => {
        expect(validateDeviceName('  My   Phone  ')).toBe('My Phone');
        expect(validateDeviceName('Device\t\tName')).toBe('DeviceName');
    });
});

describe('generateTransferId', () => {
    it('should generate unique IDs', () => {
        const id1 = generateTransferId();
        const id2 = generateTransferId();
        expect(id1).not.toBe(id2);
    });

    it('should include timestamp', () => {
        const before = Date.now();
        const id = generateTransferId();
        const after = Date.now();

        const timestamp = parseInt(id.split('-')[0], 10);
        expect(timestamp).toBeGreaterThanOrEqual(before);
        expect(timestamp).toBeLessThanOrEqual(after);
    });
});

describe('loadSettings', () => {
    beforeEach(() => {
        jest.clearAllMocks();
    });

    it('should return defaults when no saved settings', async () => {
        (AsyncStorage.getItem as jest.Mock).mockResolvedValue(null);

        const settings = await loadSettings();
        expect(settings.deviceName).toBe('TeleportMobile');
        expect(settings.autoAccept).toBe(false);
        expect(settings.vibrationEnabled).toBe(true);
    });

    it('should merge saved settings with defaults', async () => {
        (AsyncStorage.getItem as jest.Mock).mockResolvedValue(
            JSON.stringify({ deviceName: 'CustomName' })
        );

        const settings = await loadSettings();
        expect(settings.deviceName).toBe('CustomName');
        expect(settings.downloadPath).toBe('/storage/emulated/0/Download');
    });

    it('should handle storage errors gracefully', async () => {
        (AsyncStorage.getItem as jest.Mock).mockRejectedValue(new Error('Storage error'));

        const settings = await loadSettings();
        expect(settings).toEqual(expect.objectContaining({
            deviceName: 'TeleportMobile',
        }));
    });
});

describe('saveSettings', () => {
    beforeEach(() => {
        jest.clearAllMocks();
    });

    it('should save settings to storage', async () => {
        (AsyncStorage.setItem as jest.Mock).mockResolvedValue(undefined);

        const settings: AppSettings = {
            deviceName: 'TestDevice',
            downloadPath: '/storage/emulated/0/Download',
            autoAccept: true,
            vibrationEnabled: false,
        };

        const result = await saveSettings(settings);
        expect(result).toBe(true);
        expect(AsyncStorage.setItem).toHaveBeenCalledWith(
            '@teleport_settings',
            JSON.stringify(settings)
        );
    });
});

describe('History operations', () => {
    beforeEach(() => {
        jest.clearAllMocks();
    });

    it('should load empty history', async () => {
        (AsyncStorage.getItem as jest.Mock).mockResolvedValue(null);

        const history = await loadHistory();
        expect(history).toEqual([]);
    });

    it('should add record to history', async () => {
        (AsyncStorage.getItem as jest.Mock).mockResolvedValue('[]');
        (AsyncStorage.setItem as jest.Mock).mockResolvedValue(undefined);

        const record: TransferRecord = {
            id: 'test-id',
            type: 'sent',
            fileName: 'test.txt',
            fileSize: 1024,
            deviceName: 'TestDevice',
            timestamp: Date.now(),
            status: 'success',
        };

        const history = await addToHistory(record);
        expect(history[0]).toEqual(record);
    });

    it('should limit history to 100 items', async () => {
        const existingHistory = Array.from({ length: 100 }, (_, i) => ({
            id: `id-${i}`,
            type: 'sent' as const,
            fileName: `file-${i}.txt`,
            fileSize: 1024,
            deviceName: 'Device',
            timestamp: Date.now() - i * 1000,
            status: 'success' as const,
        }));

        (AsyncStorage.getItem as jest.Mock).mockResolvedValue(JSON.stringify(existingHistory));
        (AsyncStorage.setItem as jest.Mock).mockResolvedValue(undefined);

        const newRecord: TransferRecord = {
            id: 'new-id',
            type: 'received',
            fileName: 'new.txt',
            fileSize: 2048,
            deviceName: 'NewDevice',
            timestamp: Date.now(),
            status: 'success',
        };

        const history = await addToHistory(newRecord);
        expect(history.length).toBe(100);
        expect(history[0].id).toBe('new-id');
    });

    it('should clear history', async () => {
        (AsyncStorage.removeItem as jest.Mock).mockResolvedValue(undefined);

        const result = await clearHistory();
        expect(result).toBe(true);
        expect(AsyncStorage.removeItem).toHaveBeenCalledWith('@teleport_history');
    });
});
