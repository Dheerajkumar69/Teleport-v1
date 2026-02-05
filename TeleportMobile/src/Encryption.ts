/**
 * Encryption Utilities - E2E encryption for file transfers
 * Uses AES-256-GCM for data encryption with ECDH key exchange
 */
import { NativeModules } from 'react-native';

// Use native crypto module if available, otherwise fall back to JS
const { CryptoModule } = NativeModules;

/**
 * Generate a random encryption key (256-bit)
 */
export async function generateKey(): Promise<string> {
    if (CryptoModule?.generateKey) {
        return await CryptoModule.generateKey();
    }

    // Fallback: Generate random bytes using Math.random (less secure)
    const bytes = new Uint8Array(32);
    for (let i = 0; i < 32; i++) {
        bytes[i] = Math.floor(Math.random() * 256);
    }
    return Array.from(bytes)
        .map(b => b.toString(16).padStart(2, '0'))
        .join('');
}

/**
 * Generate a random IV for AES-GCM (96-bit)
 */
export function generateIV(): string {
    const bytes = new Uint8Array(12);
    for (let i = 0; i < 12; i++) {
        bytes[i] = Math.floor(Math.random() * 256);
    }
    return Array.from(bytes)
        .map(b => b.toString(16).padStart(2, '0'))
        .join('');
}

/**
 * Encryption metadata for a transfer session
 */
export interface EncryptionSession {
    sessionId: string;
    key: string;
    iv: string;
    algorithm: 'AES-256-GCM';
    createdAt: number;
}

/**
 * Create a new encryption session for a file transfer
 */
export async function createEncryptionSession(): Promise<EncryptionSession> {
    const key = await generateKey();
    const iv = generateIV();

    return {
        sessionId: `enc_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`,
        key,
        iv,
        algorithm: 'AES-256-GCM',
        createdAt: Date.now(),
    };
}

/**
 * Derive a shared secret using ECDH (if native module available)
 * Falls back to direct key sharing if not available
 */
export async function deriveSharedSecret(
    localPrivateKey: string,
    remotePublicKey: string
): Promise<string> {
    if (CryptoModule?.deriveSharedSecret) {
        return await CryptoModule.deriveSharedSecret(localPrivateKey, remotePublicKey);
    }

    // Fallback: Just use the provided key (less secure, for demo)
    console.warn('[Encryption] Native ECDH not available, using fallback');
    return localPrivateKey;
}

/**
 * Hash data using SHA-256
 */
export async function sha256(data: string): Promise<string> {
    if (CryptoModule?.sha256) {
        return await CryptoModule.sha256(data);
    }

    // Simple fallback hash (not cryptographically secure, for demo only)
    let hash = 0;
    for (let i = 0; i < data.length; i++) {
        const char = data.charCodeAt(i);
        hash = ((hash << 5) - hash) + char;
        hash = hash & hash;
    }
    return Math.abs(hash).toString(16).padStart(16, '0');
}

/**
 * Verify file integrity using checksum
 */
export function calculateChecksum(bytes: Uint8Array): string {
    let checksum = 0;
    for (let i = 0; i < bytes.length; i++) {
        checksum = (checksum + bytes[i]) % 0xFFFFFFFF;
    }
    return checksum.toString(16).padStart(8, '0');
}

/**
 * Transfer encryption state for UI display
 */
export interface EncryptionState {
    isEncrypted: boolean;
    algorithm?: string;
    keyFingerprint?: string;
}

/**
 * Get human-readable encryption state
 */
export function getEncryptionDisplay(session: EncryptionSession | null): EncryptionState {
    if (!session) {
        return { isEncrypted: false };
    }

    return {
        isEncrypted: true,
        algorithm: session.algorithm,
        keyFingerprint: session.key.substring(0, 8) + '...',
    };
}
