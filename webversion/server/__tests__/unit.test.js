/**
 * Signaling Server — Unit Tests
 * Tests pure validation helpers and rate-limiting logic.
 * Run: node __tests__/unit.test.js
 */

const assert = require('assert');

// Import validation functions from the server module (non-main export path)
const {
    isValidBase64,
    base64DecodedLength,
    isValidSha256Hex,
    isValidSdp,
    isValidIceCandidate,
    isValidIpAddress,
    isValidPort,
    validateRelayStart,
    validateFileElement,
    checkRateLimit,
    checkPeerRateLimit,
} = require('../signaling');

let passed = 0;
let failed = 0;

function test(name, fn) {
    try {
        fn();
        passed++;
        console.log(`  ✓ ${name}`);
    } catch (e) {
        failed++;
        console.error(`  ✗ ${name}`);
        console.error(`    ${e.message}`);
    }
}

// ============================================================================
// isValidBase64
// ============================================================================
console.log('\nisValidBase64:');

test('returns true for valid base64', () => {
    assert.strictEqual(isValidBase64('SGVsbG8='), true);
    assert.strictEqual(isValidBase64('AQIDBA=='), true);
    assert.strictEqual(isValidBase64(''), false);
});

test('rejects non-padded base64', () => {
    assert.strictEqual(isValidBase64('SGVsbG8'), false);
});

test('rejects strings with invalid chars', () => {
    assert.strictEqual(isValidBase64('SGVsbG8!'), false);
    assert.strictEqual(isValidBase64('hello world'), false);
});

test('rejects non-string input', () => {
    assert.strictEqual(isValidBase64(null), false);
    assert.strictEqual(isValidBase64(undefined), false);
    assert.strictEqual(isValidBase64(123), false);
});

test('accepts base64 with no padding', () => {
    // Length % 4 === 0 is required by the implementation
    assert.strictEqual(isValidBase64('YQ=='), true);
    assert.strictEqual(isValidBase64('YWI='), true);
    assert.strictEqual(isValidBase64('YWJj'), true);
});

// ============================================================================
// base64DecodedLength
// ============================================================================
console.log('\nbase64DecodedLength:');

test('computes decoded length correctly', () => {
    assert.strictEqual(base64DecodedLength('AQIDBA=='), 4); // 4 bytes
    assert.strictEqual(base64DecodedLength('SGVsbG8='), 5); // "Hello" = 5 bytes
    assert.strictEqual(base64DecodedLength(''), 0);
    assert.strictEqual(base64DecodedLength(null), 0);
});

test('handles padding correctly', () => {
    // "a" = 1 byte → base64 "YQ==" (2 padding)
    assert.strictEqual(base64DecodedLength('YQ=='), 1);
    // "ab" = 2 bytes → base64 "YWI=" (1 padding)
    assert.strictEqual(base64DecodedLength('YWI='), 2);
    // "abc" = 3 bytes → base64 "YWJj" (0 padding)
    assert.strictEqual(base64DecodedLength('YWJj'), 3);
});

// ============================================================================
// isValidSha256Hex
// ============================================================================
console.log('\nisValidSha256Hex:');

test('accepts valid 64-char hex', () => {
    const valid = 'a'.repeat(64);
    assert.strictEqual(isValidSha256Hex(valid), true);
});

test('accepts mixed-case hex', () => {
    const mixed = 'aBcDeF0123456789aBcDeF0123456789aBcDeF0123456789aBcDeF0123456789';
    assert.strictEqual(isValidSha256Hex(mixed), true);
});

test('rejects non-hex chars', () => {
    const bad = 'g'.repeat(64);
    assert.strictEqual(isValidSha256Hex(bad), false);
});

test('rejects wrong length', () => {
    assert.strictEqual(isValidSha256Hex('abc123'), false);
    assert.strictEqual(isValidSha256Hex('a'.repeat(63)), false);
    assert.strictEqual(isValidSha256Hex('a'.repeat(65)), false);
});

test('rejects non-string', () => {
    assert.strictEqual(isValidSha256Hex(null), false);
    assert.strictEqual(isValidSha256Hex(123), false);
});

// ============================================================================
// isValidSdp
// ============================================================================
console.log('\nisValidSdp:');

test('accepts valid offer SDP', () => {
    assert.strictEqual(isValidSdp({ type: 'offer', sdp: 'v=0\r\n...' }), true);
});

test('accepts valid answer SDP', () => {
    assert.strictEqual(isValidSdp({ type: 'answer', sdp: 'v=0\r\n...' }), true);
});

test('rejects missing type', () => {
    assert.strictEqual(isValidSdp({ sdp: 'v=0\r\n...' }), false);
});

test('rejects invalid type', () => {
    assert.strictEqual(isValidSdp({ type: 'invalid', sdp: 'v=0\r\n...' }), false);
});

test('rejects empty sdp', () => {
    assert.strictEqual(isValidSdp({ type: 'offer', sdp: '' }), false);
});

test('rejects too-long sdp (>64KB)', () => {
    assert.strictEqual(isValidSdp({ type: 'offer', sdp: 'x'.repeat(65537) }), false);
});

test('rejects non-object', () => {
    assert.strictEqual(isValidSdp(null), false);
    assert.strictEqual(isValidSdp('string'), false);
});

// ============================================================================
// isValidIceCandidate
// ============================================================================
console.log('\nisValidIceCandidate:');

test('accepts valid candidate', () => {
    const c = { candidate: 'candidate:1 1 UDP 2122252543 192.168.1.1 50000 typ host', sdpMid: '0', sdpMLineIndex: 0 };
    assert.strictEqual(isValidIceCandidate(c), true);
});

test('accepts candidate without optional fields', () => {
    assert.strictEqual(isValidIceCandidate({ candidate: 'candidate:1 1 UDP 2122252543 192.168.1.1 50000 typ host' }), true);
});

test('rejects missing candidate string', () => {
    assert.strictEqual(isValidIceCandidate({}), false);
    assert.strictEqual(isValidIceCandidate({ candidate: '' }), false);
});

test('rejects invalid sdpMLineIndex', () => {
    assert.strictEqual(isValidIceCandidate({ candidate: 'c', sdpMLineIndex: -1 }), false);
    assert.strictEqual(isValidIceCandidate({ candidate: 'c', sdpMLineIndex: 'abc' }), false);
});

test('rejects non-object', () => {
    assert.strictEqual(isValidIceCandidate(null), false);
});

// ============================================================================
// isValidIpAddress
// ============================================================================
console.log('\nisValidIpAddress:');

test('accepts valid IPv4', () => {
    assert.strictEqual(isValidIpAddress('192.168.1.1'), true);
    assert.strictEqual(isValidIpAddress('0.0.0.0'), true);
    assert.strictEqual(isValidIpAddress('255.255.255.255'), true);
});

test('rejects invalid IPv4', () => {
    assert.strictEqual(isValidIpAddress('256.1.1.1'), false);
    assert.strictEqual(isValidIpAddress('1.2.3'), false);
    assert.strictEqual(isValidIpAddress('abc.def.ghi.jkl'), false);
});

test('accepts valid IPv6', () => {
    assert.strictEqual(isValidIpAddress('::1'), true);
    assert.strictEqual(isValidIpAddress('fe80::1'), true);
    assert.strictEqual(isValidIpAddress('2001:0db8:85a3:0000:0000:8a2e:0370:7334'), true);
});

test('rejects empty and too-long', () => {
    assert.strictEqual(isValidIpAddress(''), false);
    assert.strictEqual(isValidIpAddress('a'.repeat(46)), false);
});

// ============================================================================
// isValidPort
// ============================================================================
console.log('\nisValidPort:');

test('accepts valid ports', () => {
    assert.strictEqual(isValidPort(1), true);
    assert.strictEqual(isValidPort(80), true);
    assert.strictEqual(isValidPort(65535), true);
});

test('rejects invalid ports', () => {
    assert.strictEqual(isValidPort(0), false);
    assert.strictEqual(isValidPort(-1), false);
    assert.strictEqual(isValidPort(65536), false);
    assert.strictEqual(isValidPort(1.5), false);
    assert.strictEqual(isValidPort('80'), false);
});

// ============================================================================
// validateRelayStart
// ============================================================================
console.log('\nvalidateRelayStart:');

test('accepts valid relay-start', () => {
    assert.strictEqual(validateRelayStart({ filename: 'test.txt', size: 1024 }), true);
});

test('rejects empty filename', () => {
    assert.strictEqual(validateRelayStart({ filename: '', size: 1024 }), false);
});

test('rejects too-long filename', () => {
    assert.strictEqual(validateRelayStart({ filename: 'x'.repeat(1025), size: 1024 }), false);
});

test('rejects negative size', () => {
    assert.strictEqual(validateRelayStart({ filename: 'test.txt', size: -1 }), false);
});

test('rejects non-finite size', () => {
    assert.strictEqual(validateRelayStart({ filename: 'test.txt', size: Infinity }), false);
    assert.strictEqual(validateRelayStart({ filename: 'test.txt', size: NaN }), false);
});

// ============================================================================
// validateFileElement
// ============================================================================
console.log('\nvalidateFileElement:');

test('accepts valid file element', () => {
    assert.strictEqual(validateFileElement({ name: 'file.txt', size: 100 }), true);
});

test('rejects missing name', () => {
    assert.strictEqual(validateFileElement({ size: 100 }), false);
});

test('rejects empty name', () => {
    assert.strictEqual(validateFileElement({ name: '', size: 100 }), false);
});

test('rejects negative size', () => {
    assert.strictEqual(validateFileElement({ name: 'f.txt', size: -1 }), false);
});

test('rejects non-object', () => {
    assert.strictEqual(validateFileElement(null), false);
    assert.strictEqual(validateFileElement('string'), false);
});

// ============================================================================
// Rate Limiting
// ============================================================================
console.log('\ncheckRateLimit:');

test('allows first connection', () => {
    const ip = '10.0.0.' + Math.floor(Math.random() * 254 + 1);
    assert.strictEqual(checkRateLimit(ip), true);
});

test('rate limits after max connections', () => {
    const ip = '10.0.1.' + Math.floor(Math.random() * 254 + 1);
    // Exhaust the limit
    for (let i = 0; i < 10; i++) checkRateLimit(ip);
    // 11th should be rejected
    assert.strictEqual(checkRateLimit(ip), false);
});

// ============================================================================
// Summary
// ============================================================================
console.log(`\n${'='.repeat(50)}`);
console.log(`Results: ${passed} passed, ${failed} failed`);
console.log(`${'='.repeat(50)}\n`);

process.exit(failed > 0 ? 1 : 0);
