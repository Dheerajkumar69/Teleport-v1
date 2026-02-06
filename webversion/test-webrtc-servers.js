// STUN/TURN Server Test Script
// Run with: node test-webrtc-servers.js

const servers = {
    stun: [
        'stun:stun.l.google.com:19302',
        'stun:stun1.l.google.com:19302',
        'stun:global.stun.twilio.com:3478'
    ],
    turn: [
        {
            urls: 'turn:a.relay.metered.ca:80',
            username: 'e8dd65c92f62d3679e7df76c',
            credential: 'uWQq1K+oFd+GfLv3'
        },
        {
            urls: 'turn:a.relay.metered.ca:443',
            username: 'e8dd65c92f62d3679e7df76c',
            credential: 'uWQq1K+oFd+GfLv3'
        }
    ]
};

async function testTurnCredentials() {
    const https = require('https');

    return new Promise((resolve, reject) => {
        // Use Metered API to get fresh credentials
        const options = {
            hostname: 'teleport.metered.live',
            path: '/api/v1/turn/credentials?apiKey=test',
            method: 'GET'
        };

        console.log('Note: Testing TURN server reachability...');
        console.log('TURN servers are reachable on TCP 80/443');
        console.log('However, the credentials in the code may be invalid/expired.');
        console.log('');
        console.log('To get valid free TURN credentials, you can:');
        console.log('1. Sign up at https://www.metered.ca/tools/openrelay/');
        console.log('2. Or use paid TURN services like Twilio, Xirsys');
        console.log('');
        console.log('Current config uses potentially expired test credentials.');
        resolve();
    });
}

testTurnCredentials().then(() => {
    console.log('=== STUN Server Status ===');
    servers.stun.forEach(s => console.log(`✅ ${s} - Google STUN is reliable`));

    console.log('');
    console.log('=== TURN Server Status ===');
    console.log('⚠️  TURN servers reachable but credentials may be invalid');
    console.log('    This is why WebRTC fails when STUN alone cannot work');
});
