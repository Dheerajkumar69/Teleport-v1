/**
 * Settings Storage - AsyncStorage wrapper for persisting app settings
 * Provides type-safe access to all user preferences
 */
import AsyncStorage from '@react-native-async-storage/async-storage';

export interface AppSettings {
    deviceName: string;
    downloadPath: string;
    autoAccept: boolean;
    vibrationEnabled: boolean;
}

export interface TransferRecord {
    id: string;
    type: 'sent' | 'received';
    fileName: string;
    fileSize: number;
    deviceName: string;
    timestamp: number;
    status: 'success' | 'failed';
}

const SETTINGS_KEY = '@teleport_settings';
const HISTORY_KEY = '@teleport_history';
const MAX_HISTORY_ITEMS = 100;

const DEFAULT_SETTINGS: AppSettings = {
    deviceName: 'TeleportMobile',
    downloadPath: '/storage/emulated/0/Download',
    autoAccept: false,
    vibrationEnabled: true,
};

// Security: Allowed path prefixes for download directory
const ALLOWED_PATH_PREFIXES = [
    '/storage/emulated/0/Download',
    '/storage/emulated/0/Documents',
    '/storage/emulated/0/DCIM',
    '/storage/emulated/0/Music',
    '/storage/emulated/0/Pictures',
    '/storage/emulated/0/Movies',
    '/data/user/0/', // App private directory
    '/sdcard/',
];

/**
 * Validate download path to prevent path injection attacks.
 * Returns sanitized path or default if invalid.
 */
export function validateDownloadPath(path: string): string {
    // Reject null/undefined/empty
    if (!path || typeof path !== 'string') {
        return DEFAULT_SETTINGS.downloadPath;
    }

    // Normalize path
    const normalized = path.trim().replace(/\/+/g, '/').replace(/\/$/, '');

    // Reject path traversal attempts
    if (normalized.includes('..') || normalized.includes('./')) {
        console.warn('[SettingsStorage] Path traversal attempt blocked:', path);
        return DEFAULT_SETTINGS.downloadPath;
    }

    // Check if path starts with allowed prefix
    const isAllowed = ALLOWED_PATH_PREFIXES.some(prefix =>
        normalized.startsWith(prefix)
    );

    if (!isAllowed) {
        console.warn('[SettingsStorage] Path not in allowed list:', path);
        return DEFAULT_SETTINGS.downloadPath;
    }

    return normalized;
}

// Device name validation constants
const MAX_DEVICE_NAME_LENGTH = 32;
const MIN_DEVICE_NAME_LENGTH = 1;
// Allow alphanumeric, spaces, hyphens, underscores only
const DEVICE_NAME_REGEX = /^[a-zA-Z0-9\s\-_]+$/;

/**
 * Validate and sanitize device name to prevent XSS and injection attacks.
 * @param name The device name to validate
 * @returns Sanitized device name or default if invalid
 */
export function validateDeviceName(name: string): string {
    // Reject null/undefined/empty
    if (!name || typeof name !== 'string') {
        return DEFAULT_SETTINGS.deviceName;
    }

    // Trim whitespace
    let sanitized = name.trim();

    // Check minimum length
    if (sanitized.length < MIN_DEVICE_NAME_LENGTH) {
        return DEFAULT_SETTINGS.deviceName;
    }

    // Enforce maximum length
    if (sanitized.length > MAX_DEVICE_NAME_LENGTH) {
        sanitized = sanitized.substring(0, MAX_DEVICE_NAME_LENGTH);
    }

    // Remove dangerous characters (HTML/script injection prevention)
    sanitized = sanitized
        .replace(/[<>'"&]/g, '') // Remove HTML special chars
        .replace(/[\x00-\x1F\x7F]/g, '') // Remove control characters
        .replace(/\s+/g, ' '); // Normalize whitespace

    // Validate against allowed pattern
    if (!DEVICE_NAME_REGEX.test(sanitized)) {
        // Extract only allowed characters
        sanitized = sanitized.replace(/[^a-zA-Z0-9\s\-_]/g, '');
    }

    // Final check - must have content
    if (sanitized.length === 0) {
        return DEFAULT_SETTINGS.deviceName;
    }

    return sanitized;
}

/**
 * Load settings from AsyncStorage with fallback to defaults
 */
export async function loadSettings(): Promise<AppSettings> {
    try {
        const json = await AsyncStorage.getItem(SETTINGS_KEY);
        if (json) {
            const saved = JSON.parse(json) as Partial<AppSettings>;
            return { ...DEFAULT_SETTINGS, ...saved };
        }
    } catch (error) {
        console.error('[SettingsStorage] Load error:', error);
    }
    return { ...DEFAULT_SETTINGS };
}

/**
 * Save settings to AsyncStorage
 */
export async function saveSettings(settings: AppSettings): Promise<boolean> {
    try {
        await AsyncStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
        return true;
    } catch (error) {
        console.error('[SettingsStorage] Save error:', error);
        return false;
    }
}

/**
 * Update a single setting value
 */
export async function updateSetting<K extends keyof AppSettings>(
    key: K,
    value: AppSettings[K]
): Promise<AppSettings> {
    const current = await loadSettings();
    const updated = { ...current, [key]: value };
    await saveSettings(updated);
    return updated;
}

/**
 * Load transfer history from AsyncStorage
 */
export async function loadHistory(): Promise<TransferRecord[]> {
    try {
        const json = await AsyncStorage.getItem(HISTORY_KEY);
        if (json) {
            return JSON.parse(json) as TransferRecord[];
        }
    } catch (error) {
        console.error('[SettingsStorage] History load error:', error);
    }
    return [];
}

/**
 * Add a transfer record to history
 * Maintains max history size and sorts by timestamp descending
 */
export async function addToHistory(record: TransferRecord): Promise<TransferRecord[]> {
    try {
        const history = await loadHistory();
        const updated = [record, ...history].slice(0, MAX_HISTORY_ITEMS);
        await AsyncStorage.setItem(HISTORY_KEY, JSON.stringify(updated));
        return updated;
    } catch (error) {
        console.error('[SettingsStorage] History add error:', error);
        return [];
    }
}

/**
 * Clear all transfer history
 */
export async function clearHistory(): Promise<boolean> {
    try {
        await AsyncStorage.removeItem(HISTORY_KEY);
        return true;
    } catch (error) {
        console.error('[SettingsStorage] History clear error:', error);
        return false;
    }
}

/**
 * Generate unique ID for transfer records
 */
export function generateTransferId(): string {
    return `${Date.now()}-${Math.random().toString(36).substring(2, 9)}`;
}
