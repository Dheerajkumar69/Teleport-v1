/**
 * WebRTC Engine — Unit Tests
 * Tests pure function logic extracted from teleport-webrtc.js.
 * Run: node __tests__/engine-unit.test.js
 */

const assert = require('assert');

// ============================================================================
// Replicate pure standalone functions from the engine
// ============================================================================

function selectChunkSize(fileSize) {
    if (fileSize < 100 * 1024 * 1024) return 16 * 1024;
    if (fileSize < 500 * 1024 * 1024) return 256 * 1024;
    if (fileSize < 2 * 1024 * 1024 * 1024) return 1024 * 1024;
    return 4 * 1024 * 1024;
}

function formatSize(bytes) {
    if (bytes < 1024) return bytes + " B";
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
    if (bytes < 1024 * 1024 * 1024)
        return (bytes / (1024 * 1024)).toFixed(1) + " MB";
    return (bytes / (1024 * 1024 * 1024)).toFixed(2) + " GB";
}

function sanitizeFilename(filename) {
    if (!filename || typeof filename !== 'string') {
        throw new Error('Invalid filename');
    }
    const trimmed = filename.trim();
    if (trimmed.length === 0) throw new Error('Empty filename');
    if (trimmed.includes('\0')) throw new Error('Filename contains null byte');
    if (/\.\./.test(trimmed)) throw new Error('Path traversal detected');
    const reserved = /^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)/i;
    if (reserved.test(trimmed)) throw new Error('Reserved Windows filename');
    return trimmed;
}

function normalizeSha256(hash) {
    if (hash === null || hash === undefined) return null;
    if (typeof hash !== 'string') return null;
    const trimmed = hash.trim();
    if (trimmed.length !== 64) return null;
    if (!/^[0-9a-fA-F]{64}$/.test(trimmed)) return null;
    return trimmed.toLowerCase();
}

// ============================================================================
// IncrementalSHA256 mock (structure test only, not real SHA-256)
// ============================================================================
class IncrementalSHA256 {
    constructor() {
        this.chunks = [];
        this.totalSize = 0;
    }
    update(data) {
        this.chunks.push(data);
        this.totalSize += data.byteLength || data.length || 0;
    }
    async hex() {
        // Simplified mock — XOR all bytes into 32-byte buffer
        const arr = new Uint8Array(32);
        for (const chunk of this.chunks) {
            const bytes = chunk instanceof Uint8Array ? chunk : new Uint8Array(chunk);
            for (let i = 0; i < bytes.length; i++) {
                arr[i % 32] ^= bytes[i];
            }
        }
        return Array.from(arr).map(b => b.toString(16).padStart(2, '0')).join('');
    }
}

// ============================================================================
// Test runner
// ============================================================================
let passed = 0;
let failed = 0;

function test(name, fn) {
    try {
        const result = fn();
        if (result && typeof result.then === 'function') {
            return result
                .then(() => { passed++; console.log(`  ✓ ${name}`); })
                .catch(e => { failed++; console.error(`  ✗ ${name}`); console.error(`    ${e.message}`); });
        }
        passed++;
        console.log(`  ✓ ${name}`);
    } catch (e) {
        failed++;
        console.error(`  ✗ ${name}`);
        console.error(`    ${e.message}`);
    }
}

// ============================================================================
// selectChunkSize
// ============================================================================
console.log('\nselectChunkSize:');

test('small file (< 100MB) → 16KB chunks', () => {
    assert.strictEqual(selectChunkSize(0), 16 * 1024);
    assert.strictEqual(selectChunkSize(1024), 16 * 1024);
    assert.strictEqual(selectChunkSize(50 * 1024 * 1024), 16 * 1024);
    assert.strictEqual(selectChunkSize(99 * 1024 * 1024 + 1023), 16 * 1024);
});

test('medium file (100MB–500MB) → 256KB chunks', () => {
    assert.strictEqual(selectChunkSize(100 * 1024 * 1024), 256 * 1024);
    assert.strictEqual(selectChunkSize(300 * 1024 * 1024), 256 * 1024);
    assert.strictEqual(selectChunkSize(499 * 1024 * 1024 + 1023 * 1024), 256 * 1024);
});

test('large file (500MB–2GB) → 1MB chunks', () => {
    assert.strictEqual(selectChunkSize(500 * 1024 * 1024), 1024 * 1024);
    assert.strictEqual(selectChunkSize(1024 * 1024 * 1024), 1024 * 1024);
    assert.strictEqual(selectChunkSize(2 * 1024 * 1024 * 1024 - 1), 1024 * 1024);
});

test('huge file (>= 2GB) → 4MB chunks', () => {
    assert.strictEqual(selectChunkSize(2 * 1024 * 1024 * 1024), 4 * 1024 * 1024);
    assert.strictEqual(selectChunkSize(5 * 1024 * 1024 * 1024), 4 * 1024 * 1024);
});

// ============================================================================
// formatSize
// ============================================================================
console.log('\nformatSize:');

test('formats bytes', () => {
    assert.strictEqual(formatSize(0), '0 B');
    assert.strictEqual(formatSize(500), '500 B');
    assert.strictEqual(formatSize(1023), '1023 B');
});

test('formats kilobytes', () => {
    assert.strictEqual(formatSize(1024), '1.0 KB');
    assert.strictEqual(formatSize(1536), '1.5 KB');
});

test('formats megabytes', () => {
    assert.strictEqual(formatSize(1024 * 1024), '1.0 MB');
    assert.strictEqual(formatSize(1024 * 1024 * 5.5), '5.5 MB');
});

test('formats gigabytes', () => {
    assert.strictEqual(formatSize(1024 * 1024 * 1024), '1.00 GB');
    assert.strictEqual(formatSize(1024 * 1024 * 1024 * 2.5), '2.50 GB');
});

// ============================================================================
// sanitizeFilename
// ============================================================================
console.log('\nsanitizeFilename:');

test('accepts normal filename', () => {
    assert.strictEqual(sanitizeFilename('document.pdf'), 'document.pdf');
    assert.strictEqual(sanitizeFilename('my_file-2024.txt'), 'my_file-2024.txt');
});

test('trims whitespace', () => {
    assert.strictEqual(sanitizeFilename('  file.txt  '), 'file.txt');
});

test('rejects null/undefined/empty', () => {
    assert.throws(() => sanitizeFilename(null), /Invalid/);
    assert.throws(() => sanitizeFilename(undefined), /Invalid/);
    assert.throws(() => sanitizeFilename(''), /Invalid/);
    assert.throws(() => sanitizeFilename('   '), /Empty/);
});

test('rejects null bytes', () => {
    assert.throws(() => sanitizeFilename('file\x00.txt'), /null/);
});

test('rejects path traversal', () => {
    assert.throws(() => sanitizeFilename('../../../etc/passwd'), /traversal/);
    assert.throws(() => sanitizeFilename('foo/../bar'), /traversal/);
});

test('rejects Windows reserved names', () => {
    assert.throws(() => sanitizeFilename('CON'), /Reserved/);
    assert.throws(() => sanitizeFilename('NUL.txt'), /Reserved/);
    assert.throws(() => sanitizeFilename('PRN'), /Reserved/);
    assert.throws(() => sanitizeFilename('COM1'), /Reserved/);
    assert.throws(() => sanitizeFilename('LPT9.log'), /Reserved/);
});

// ============================================================================
// normalizeSha256
// ============================================================================
console.log('\nnormalizeSha256:');

test('returns null for null/undefined', () => {
    assert.strictEqual(normalizeSha256(null), null);
    assert.strictEqual(normalizeSha256(undefined), null);
});

test('returns null for non-string', () => {
    assert.strictEqual(normalizeSha256(123), null);
    assert.strictEqual(normalizeSha256({}), null);
});

test('returns null for wrong length', () => {
    assert.strictEqual(normalizeSha256('abc123'), null);
    assert.strictEqual(normalizeSha256('a'.repeat(63)), null);
    assert.strictEqual(normalizeSha256('a'.repeat(65)), null);
});

test('returns null for non-hex characters', () => {
    assert.strictEqual(normalizeSha256('g'.repeat(64)), null);
    assert.strictEqual(normalizeSha256('z'.repeat(64)), null);
});

test('normalizes valid SHA-256 hash', () => {
    const input = '  ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789  ';
    assert.strictEqual(normalizeSha256(input), 'abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789');
});

test('accepts uppercase hex', () => {
    const input = 'ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789';
    assert.strictEqual(normalizeSha256(input), 'abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789');
});

// ============================================================================
// IncrementalSHA256
// ============================================================================
console.log('\nIncrementalSHA256:');

test('creates instance with zero state', async () => {
    const h = new IncrementalSHA256();
    assert.strictEqual(h.chunks.length, 0);
    assert.strictEqual(h.totalSize, 0);
});

test('update accumulates chunks and totalSize', () => {
    const h = new IncrementalSHA256();
    h.update(new Uint8Array([1, 2, 3]));
    h.update(new Uint8Array([4, 5]));
    assert.strictEqual(h.chunks.length, 2);
    assert.strictEqual(h.totalSize, 5);
});

test('hex returns a 64-char hex string', async () => {
    const h = new IncrementalSHA256();
    h.update(new Uint8Array([1, 2, 3]));
    const result = await h.hex();
    assert.strictEqual(typeof result, 'string');
    assert.strictEqual(result.length, 64);
    assert.ok(/^[0-9a-f]{64}$/.test(result), 'Expected lowercase hex string');
});

test('hex is deterministic for same input', async () => {
    const h1 = new IncrementalSHA256();
    h1.update(new Uint8Array([10, 20, 30]));
    const h2 = new IncrementalSHA256();
    h2.update(new Uint8Array([10, 20, 30]));
    assert.strictEqual(await h1.hex(), await h2.hex());
});

test('different input produces different hash', async () => {
    const h1 = new IncrementalSHA256();
    h1.update(new Uint8Array([1, 2, 3]));
    const h2 = new IncrementalSHA256();
    h2.update(new Uint8Array([4, 5, 6]));
    assert.notStrictEqual(await h1.hex(), await h2.hex());
});

// ============================================================================
// IP validation (inline logic from server)
// ============================================================================
console.log('\nIP validation:');

function isValidIP(ip) {
    if (!ip || typeof ip !== 'string') return false;
    const parts = ip.split('.');
    if (parts.length !== 4) return false;
    return parts.every(p => {
        if (!/^\d{1,3}$/.test(p)) return false;
        const n = parseInt(p, 10);
        return n >= 0 && n <= 255;
    });
}

test('accepts valid IPv4', () => {
    assert.strictEqual(isValidIP('192.168.1.1'), true);
    assert.strictEqual(isValidIP('0.0.0.0'), true);
    assert.strictEqual(isValidIP('255.255.255.255'), true);
    assert.strictEqual(isValidIP('10.0.0.1'), true);
});

test('rejects invalid IPs', () => {
    assert.strictEqual(isValidIP('256.1.1.1'), false);
    assert.strictEqual(isValidIP('1.1.1'), false);
    assert.strictEqual(isValidIP('abc.def.ghi.jkl'), false);
    assert.strictEqual(isValidIP(''), false);
    assert.strictEqual(isValidIP(null), false);
    assert.strictEqual(isValidIP(undefined), false);
    assert.strictEqual(isValidIP('1.2.3.4.5'), false);
});

// ============================================================================
// Summary
// ============================================================================
console.log(`\n${'='.repeat(50)}`);
console.log(`Results: ${passed} passed, ${failed} failed`);
console.log(`${'='.repeat(50)}\n`);

process.exit(failed > 0 ? 1 : 0);
