# Phase 0: Security Baseline Implementation

**Status**: In Progress  
**Priority**: HIGH (Foundational for all cross-platform security)  
**Effort**: 2-3 days

## Overview

Phase 0 implements the minimum viable security baseline for Teleport peer-to-peer transfers:
1. **Fingerprint Validation** - Automatic rejection of fingerprint mismatches (MITM detection)
2. **Device Trust Database** - Persistent storage of trusted peer identities
3. **Peer Announcement Signing** - RSA signatures protect against identity spoofing
4. **Trust Management UI** - User-facing trust verification and device management

## Implementation Status

### ✅ Completed
- [x] Fingerprint validation in `handleOffer()`
- [x] Trusted device storage (Map + localStorage)
- [x] Trust verification methods: `validatePeerFingerprint()`, `addTrustedDevice()`, `removeTrustedDevice()`
- [x] Pending verification tracking and display
- [x] Device signing key generation and storage (RSA 2048-bit)
- [x] Peer announcement signing support

### 🔄 In Progress
- [ ] Trust management UI panel
- [ ] Peer signature verification on receive
- [ ] Public key import/export infrastructure
- [ ] Integration tests for fingerprint validation

### ❌ Not Started
- [ ] macOS/Linux/Windows desktop UI integration
- [ ] Android/iOS mobile trust UI
- [ ] Advanced trust metrics (reputation system)
- [ ] Certificate pinning support

## Technical Details

### 1. Fingerprint Validation

**Location**: `webversion/teleport-webrtc.js` → `validatePeerFingerprint()`

**Behavior**:
```javascript
// First time contact: auto-trust, flag for manual review
// → Returns { valid: true, trustLevel: 'pending' }

// Known peer: verify fingerprint matches stored value
// → Returns { valid: true, trustLevel: 'trusted' }
// → Returns { valid: false, reason: 'Fingerprint mismatch' } ← REJECTS CONNECTION

// Mismatch detected: possible MITM attack
// → Connection is terminated
// → Error: ErrorCodes.AUTHENTICATION_FAILED
```

**Trusted Device Structure** (localStorage):
```json
{
  "peerId": {
    "fingerprint": "A1B2C3D4E5F6G7H8",
    "publicKey": "base64EncodedKey",
    "firstSeen": 1700000000000,
    "lastVerified": 1700100000000,
    "trustLevel": "manual|auto-verified"
  }
}
```

### 2. Device Signing Key

**Location**: `webversion/teleport-webrtc.js` → `generateSigningKey()`

**Algorithm**: RSA-PKCS1-v1_5, 2048-bit, SHA-256 hash

**Stored**: localStorage as JWK encoding

**Usage**: Sign peer announcements to prevent identity spoofing
- When initiating connection: Sign fingerprint + public key
- On receive: Verify signature (if public key available)

### 3. Peer Announcement Structure

**Current** (before):
```json
{
  "type": "offer",
  "to": "peerId",
  "sdp": { ... },
  "fingerprint": "A1B2C3D4E5F6G7H8"
}
```

**Updated** (Phase 0):
```json
{
  "type": "offer",
  "to": "peerId",
  "sdp": { ... },
  "fingerprint": "A1B2C3D4E5F6G7H8",
  "publicKey": "base64EncodedECDHPublicKey",
  "timestamp": 1700100000000,
  "signature": "base64EncodedRSASignature"
}
```

### 4. Trust Management Workflow

**User Verification Flow**:
```
┌─ Peer connects
├─ Fingerprint validation:
│  ├─ Known? → Verify match → Accept/Reject
│  └─ Unknown? → Auto-trust, Flag as pending
├─ User Reviews Pending Verifications (UI panel)
│  ├─ Out-of-band verify (phone call, in-person, etc.)
│  └─ Click "Approve" or "Block"
└─ Transfer proceeds (or rejected)
```

**Verification Code Generation**:
Generate memorable 6-digit code from fingerprint for voice verification:
```
Fingerprint: A1B2C3D4E5F6G7H8
→ Hash first 12 chars: A1 B2 C3 D4 E5 F6
→ Code: 161 178 195 212 229 246
→ Simplified: 1-6-1 7-8-1 (phone-friendly)
```

## Backward Compatibility

**Desktop peers** (C++ core without E2E encryption):
- Send: `fingerprint: null` in announcements
- Status: Relay-only, no P2P
- Security: No end-to-end encryption (falls back to relay)

**Web peers** (current):
- Send: fingerprint + public key + signature
- Status: Full P2P with E2E encryption
- Security: Fingerprint validation + signature verification

## Database Schema

### IndexedDB - Trusted Devices (Future Enhancement)

```javascript
// Schema: "teleport-security", version 2

db.createObjectStore('trustedDevices', { 
  keyPath: 'peerId',
  indexes: [
    { name: 'fingerprint', keyPath: 'fingerprint', unique: true },
    { name: 'trustLevel', keyPath: 'trustLevel' },
    { name: 'lastVerified', keyPath: 'lastVerified' }
  ]
});

// Record structure:
{
  peerId: "uuid-v4",
  fingerprint: "A1B2C3D4E5F6G7H8",      // SHA-256 truncated
  publicKey: "base64",                   // For signature verification
  publicKeyAlgorithm: "ECDH-P256",
  firstSeen: 1700000000000,              // Timestamp
  lastVerified: 1700100000000,           // Timestamp
  lastConnected: 1700150000000,          // Timestamp
  trustLevel: "manual|auto-verified",    // Trust model
  peerName: "User's iPhone",             // Friendly name
  peerPlatform: "web|desktop|android|ios",
  connectionCount: 42,                   // Number of successful transfers
  totalTransferBytes: 5368709120,        // Total bytes transferred
  notes: "Verified in-person at coffee shop"
}
```

### LocalStorage - Fallback Storage (Current)

Key: `teleport-trusted-devices`  
Value: JSON serialized device map

### Security Considerations

1. **No Private Key Export** - Signing keys never leave device
2. **Fingerprint Comparison** - Case-insensitive, whitespace-agnostic
3. **MITM Detection** - Fingerprint mismatch = automatic rejection
4. **No Certificate Chain** - Direct fingerprint comparison (Web-of-trust model)
5. **No Certificate Revocation** - Manual device removal only

## Testing

**Unit Tests** (TODO):
```javascript
describe('Fingerprint Validation', () => {
  it('should accept trusted peer', async () => {
    // Add device to trusted
    await teleport.addTrustedDevice(peerId, fingerprint);
    
    // Validate matching fingerprint
    const result = await teleport.validatePeerFingerprint(peerId, fingerprint);
    expect(result.valid).toBe(true);
    expect(result.trustLevel).toBe('trusted');
  });

  it('should reject mismatched fingerprint', async () => {
    await teleport.addTrustedDevice(peerId, 'EXPECTED_FINGERPRINT');
    
    const result = await teleport.validatePeerFingerprint(peerId, 'DIFFERENT_FINGERPRINT');
    expect(result.valid).toBe(false);
    expect(result.reason).toContain('mismatch');
  });

  it('should auto-trust new peers', async () => {
    const result = await teleport.validatePeerFingerprint('newPeerId', 'FINGERPRINT');
    expect(result.valid).toBe(true);
    expect(result.trustLevel).toBe('pending');
  });
});
```

**Integration Tests** (TODO):
```javascript
// Test fingerprint validation rejects MITM
// Test signature verification on reconnect
// Test expired trust records
// Test device removal prevents connection
```

## Next Steps

**Immediate** (Next session):
1. Implement trust management UI panel (webapp)
2. Add signature verification in peer receive handler
3. Implement desktop UI binding

**Short-term** (Week 2):
1. Create IndexedDB migration script
2. Add Android/iOS trust UI
3. Implement reputation scoring

**Long-term** (Phase 1):
1. Certificate pinning support
2. Device sync across platforms
3. Backup/restore of trusted devices

---

**Related Files**:
- [webversion/teleport-webrtc.js](../webversion/teleport-webrtc.js) - Core implementation
- [webversion/app-lovable.js](../webversion/app-lovable.js) - UI integration (in progress)
- [core/include/teleport/teleport.h](../core/include/teleport/teleport.h) - C API definitions

