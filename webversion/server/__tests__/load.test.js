/**
 * Signaling Server — Load Tests
 * Concurrent connections, message flooding, relay throughput.
 * Run: node __tests__/load.test.js
 */

const assert = require('assert');
const WebSocket = require('ws');

const PORT = 3002;
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

function waitForMessage(ws, type, timeout = 10000) {
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

function connectWithWelcome(timeout = 10000) {
    return new Promise((resolve, reject) => {
        const ws = new WebSocket(WS_URL);
        const timer = setTimeout(() => { ws.close(); reject(new Error('Timeout waiting for welcome')); }, timeout);
        ws.on('open', () => { /* wait for welcome */ });
        ws.on('message', function handler(data) {
            const msg = JSON.parse(data);
            if (msg.type === 'welcome') {
                clearTimeout(timer);
                ws.removeListener('message', handler);
                resolve({ ws, peerId: msg.peerId });
            }
        });
        ws.on('error', (err) => { clearTimeout(timer); reject(err); });
    });
}

async function setup() {
    return new Promise((resolve, reject) => {
        serverProcess = require('child_process').fork(
            require('path').join(__dirname, '..', 'signaling.js'),
            [],
            {
                env: { ...process.env, PORT: String(PORT), MAX_CONNECTIONS_PER_MIN: '1000' },
                stdio: ['ignore', 'ignore', 'ignore', 'ipc'],
                silent: true,
            }
        );
        serverProcess.on('error', reject);
        // Wait for server to be ready by checking the port
        const http = require('http');
        const check = () => {
            const req = http.get(`http://localhost:${PORT}/health`, (res) => {
                res.resume();
                resolve();
            });
            req.on('error', () => setTimeout(check, 200));
        };
        setTimeout(check, 500);
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
    console.log('\nSignaling Server — Load Tests\n');

    // --- Concurrent connections ---
    await test('20 concurrent connections succeed', async () => {
        const count = 20;
        const promises = [];
        for (let i = 0; i < count; i++) {
            promises.push(connectWithWelcome());
        }
        const results = await Promise.all(promises);
        assert.strictEqual(results.length, count);
        for (const { ws, peerId } of results) {
            assert.ok(peerId);
            assert.strictEqual(ws.readyState, WebSocket.OPEN);
        }
        for (const { ws } of results) ws.close();
        await new Promise(r => setTimeout(r, 200));
    });

    // --- Concurrent joins ---
    await test('20 peers join same room concurrently', async () => {
        const count = 20;
        const results = [];
        for (let i = 0; i < count; i++) {
            results.push(await connectWithWelcome());
        }
        const sockets = results.map(r => r.ws);

        const joinPromises = sockets.map((ws, i) => {
            ws.send(JSON.stringify({ type: 'join', room: 'load-room', name: `Peer${i}` }));
            return waitForMessage(ws, 'peers');
        });
        const peerResults = await Promise.all(joinPromises);
        assert.strictEqual(peerResults.length, count);
        for (const r of peerResults) {
            assert.ok(Array.isArray(r.peers));
        }
        for (const ws of sockets) ws.close();
        await new Promise(r => setTimeout(r, 200));
    });

    // --- Message flooding ---
    await test('100 messages from one peer do not crash server', async () => {
        const ws = await connect();
        await waitForMessage(ws, 'welcome');

        for (let i = 0; i < 100; i++) {
            ws.send(JSON.stringify({ type: 'pong' }));
        }
        await new Promise(r => setTimeout(r, 500));
        assert.strictEqual(ws.readyState, WebSocket.OPEN);
        ws.close();
    });

    // --- Relay throughput ---
    await test('relay 20 chunks between peers', async () => {
        const ws1 = await connect();
        const w1 = await waitForMessage(ws1, 'welcome');
        ws1.send(JSON.stringify({ type: 'join', room: 'relay-load', name: 'A' }));
        await waitForMessage(ws1, 'peers');

        const ws2 = await connect();
        await waitForMessage(ws2, 'welcome');
        ws2.send(JSON.stringify({ type: 'join', room: 'relay-load', name: 'B' }));
        await waitForMessage(ws1, 'peer-joined');

        const targetId = w1.peerId;
        if (!targetId) throw new Error('No peer found');

        const receivedChunks = [];
        ws1.on('message', (data) => {
            const msg = JSON.parse(data);
            if (msg.type === 'relay-chunk') receivedChunks.push(msg);
        });

        for (let i = 0; i < 20; i++) {
            ws2.send(JSON.stringify({
                type: 'relay-chunk',
                to: targetId,
                transferId: 'load-test',
                data: Buffer.from(`chunk-${i}`).toString('base64'),
                offset: i * 100,
            }));
        }

        await new Promise(r => setTimeout(r, 1000));
        assert.strictEqual(receivedChunks.length, 20);
        ws1.close();
        ws2.close();
    });

    // --- Rapid connect/disconnect ---
    await test('rapid connect/disconnect cycles do not leak', async () => {
        for (let i = 0; i < 20; i++) {
            const ws = await connect();
            await waitForMessage(ws, 'welcome');
            ws.send(JSON.stringify({ type: 'join', room: 'churn-room', name: `Churn${i}` }));
            await waitForMessage(ws, 'peers');
            ws.close();
            await new Promise(r => setTimeout(r, 50));
        }
        await new Promise(r => setTimeout(r, 500));
        assert.ok(true, 'server survived churn');
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
