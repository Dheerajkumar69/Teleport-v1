/**
 * Signaling Server — Integration Tests
 * Starts the server and tests real WebSocket connections.
 * Run: node __tests__/integration.test.js
 */

const assert = require('assert');
const http = require('http');
const WebSocket = require('ws');

const PORT = 3001;
const WS_URL = `ws://localhost:${PORT}`;

let serverProcess;
let passed = 0;
let failed = 0;

function test(name, fn) {
    return fn()
        .then(() => { passed++; console.log(`  ✓ ${name}`); })
        .catch(e => { failed++; console.error(`  ✗ ${name}`); console.error(`    ${e.message}`); });
}

function connect() {
    return new Promise((resolve, reject) => {
        const ws = new WebSocket(WS_URL);
        ws.on('open', () => resolve(ws));
        ws.on('error', reject);
    });
}

function waitForMessage(ws, type, timeout = 3000) {
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error(`Timeout waiting for ${type}`)), timeout);
        const handler = (data) => {
            const msg = JSON.parse(data);
            if (!type || msg.type === type) {
                clearTimeout(timer);
                ws.removeListener('message', handler);
                resolve(msg);
            }
        };
        ws.on('message', handler);
    });
}

function send(ws, msg) {
    ws.send(JSON.stringify(msg));
}

// ============================================================================
// Setup: start server
// ============================================================================
async function setup() {
    return new Promise((resolve, reject) => {
        serverProcess = require('child_process').fork(
            require('path').join(__dirname, '..', 'signaling.js'),
            [],
            {
                env: { ...process.env, PORT: String(PORT), MAX_CONNECTIONS_PER_MIN: '100' },
                stdio: ['ignore', 'ignore', 'ignore', 'ipc'],
                silent: true,
            }
        );
        setTimeout(resolve, 1500);
        serverProcess.on('error', reject);
    });
}

async function teardown() {
    if (serverProcess) {
        serverProcess.kill('SIGTERM');
        await new Promise(r => setTimeout(r, 500));
    }
}

// ============================================================================
// Tests
// ============================================================================
async function runTests() {
    console.log('\nSignaling Server — Integration Tests\n');

    await test('connect and receive welcome', async () => {
        const ws = await connect();
        const msg = await waitForMessage(ws, 'welcome');
        assert.ok(msg.peerId, 'should have peerId');
        assert.ok(msg.peerId.startsWith('peer_'), 'peerId should start with peer_');
        ws.close();
    });

    await test('join room and receive peer list', async () => {
        const ws = await connect();
        await waitForMessage(ws, 'welcome');
        send(ws, { type: 'join', room: 'test-room', name: 'TestDevice' });
        const peers = await waitForMessage(ws, 'peers');
        assert.strictEqual(peers.type, 'peers');
        assert.ok(Array.isArray(peers.peers), 'peers should be array');
        ws.close();
    });

    await test('peer-joined notification', async () => {
        const ws1 = await connect();
        await waitForMessage(ws1, 'welcome');
        send(ws1, { type: 'join', room: 'discover-room', name: 'Device1' });
        await waitForMessage(ws1, 'peers');

        const ws2 = await connect();
        await waitForMessage(ws2, 'welcome');

        const joinedPromise = waitForMessage(ws1, 'peer-joined');
        send(ws2, { type: 'join', room: 'discover-room', name: 'Device2' });

        const joined = await joinedPromise;
        assert.strictEqual(joined.peer.name, 'Device2');
        ws1.close();
        ws2.close();
    });

    await test('peer-left notification on disconnect', async () => {
        const ws1 = await connect();
        await waitForMessage(ws1, 'welcome');
        send(ws1, { type: 'join', room: 'leave-room', name: 'Device1' });
        await waitForMessage(ws1, 'peers');

        const ws2 = await connect();
        await waitForMessage(ws2, 'welcome');
        send(ws2, { type: 'join', room: 'leave-room', name: 'Device2' });
        await waitForMessage(ws1, 'peer-joined');

        const leftPromise = waitForMessage(ws1, 'peer-left');
        ws2.close();

        const left = await leftPromise;
        assert.ok(left.peerId, 'should have peerId');
    });

    await test('offer/answer/ice relay to target peer', async () => {
        const ws1 = await connect();
        const w1 = await waitForMessage(ws1, 'welcome');
        send(ws1, { type: 'join', room: 'relay-room', name: 'A' });
        await waitForMessage(ws1, 'peers');

        const ws2 = await connect();
        const w2 = await waitForMessage(ws2, 'welcome');

        // Listen for ws2's peer list BEFORE sending join
        const peers2Promise = waitForMessage(ws2, 'peers');
        const joinedPromise = waitForMessage(ws1, 'peer-joined');
        send(ws2, { type: 'join', room: 'relay-room', name: 'B' });
        const peers2 = await peers2Promise;
        await joinedPromise;

        const targetPeerId = peers2.peers[0]?.id;
        if (!targetPeerId) throw new Error('Peers not discovered');

        // ws2 sends offer to ws1 via target
        const offerPromise = waitForMessage(ws1, 'offer');
        send(ws2, {
            type: 'offer',
            to: targetPeerId,
            sdp: { type: 'offer', sdp: 'v=0\r\no=- 1234 1234 IN IP4 0.0.0.0' },
        });
        const offer = await offerPromise;
        assert.strictEqual(offer.from, w2.peerId);
        assert.strictEqual(offer.type, 'offer');

        ws1.close();
        ws2.close();
    });

    await test('double-join does not leak rooms', async () => {
        const ws = await connect();
        await waitForMessage(ws, 'welcome');

        send(ws, { type: 'join', room: 'room-a', name: 'Device1' });
        await waitForMessage(ws, 'peers');

        send(ws, { type: 'join', room: 'room-b', name: 'Device1' });
        await waitForMessage(ws, 'peers');

        ws.close();
    });

    await test('pong keeps connection alive', async () => {
        const ws = await connect();
        await waitForMessage(ws, 'welcome');
        send(ws, { type: 'pong' });
        await new Promise(r => setTimeout(r, 100));
        assert.strictEqual(ws.readyState, WebSocket.OPEN);
        ws.close();
        await new Promise(r => setTimeout(r, 200));
    });

    await test('invalid JSON is silently dropped', async () => {
        const ws = await connect();
        await waitForMessage(ws, 'welcome');
        ws.send('not json{{{');
        await new Promise(r => setTimeout(r, 300));
        assert.strictEqual(ws.readyState, WebSocket.OPEN);
        ws.close();
        await new Promise(r => setTimeout(r, 200));
    });

    await test('unknown message type is handled gracefully', async () => {
        const ws = await connect();
        await waitForMessage(ws, 'welcome');
        send(ws, { type: 'unknown-type-xyz' });
        await new Promise(r => setTimeout(r, 300));
        assert.strictEqual(ws.readyState, WebSocket.OPEN);
        ws.close();
    });

    await test('HTTP health endpoint returns status', async () => {
        const body = await new Promise((resolve, reject) => {
            http.get(`http://localhost:${PORT}/health`, (res) => {
                let data = '';
                res.on('data', d => data += d);
                res.on('end', () => resolve(JSON.parse(data)));
            }).on('error', reject);
        });
        assert.strictEqual(body.status, 'ok');
        assert.strictEqual(body.service, 'teleport-signaling');
        assert.ok(typeof body.peers === 'number');
    });
}

// ============================================================================
// Run
// ============================================================================
(async () => {
    try {
        await setup();
        await runTests();
    } catch (e) {
        console.error('\nFatal:', e.message);
        failed++;
    } finally {
        await teardown();
        console.log(`\n${'='.repeat(50)}`);
        console.log(`Results: ${passed} passed, ${failed} failed`);
        console.log(`${'='.repeat(50)}\n`);
        process.exit(failed > 0 ? 1 : 0);
    }
})();
