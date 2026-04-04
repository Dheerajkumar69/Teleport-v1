const WebSocket = require('ws');

function runWebClient() {
    const ws = new WebSocket('ws://localhost:3000');
    ws.on('message', data => {
        console.log('[Web] Recv:', data.toString());
        const msg = JSON.parse(data);
        if (msg.type === 'welcome') {
            ws.send(JSON.stringify({
                type: 'join',
                room: 'teleport-default',
                name: 'Web Client',
                clientType: 'web'
            }));
        }
    });
}

function runDesktopClient() {
    const ws = new WebSocket('ws://localhost:3000');
    ws.on('message', data => {
        console.log('[Desktop] Recv:', data.toString());
        const msg = JSON.parse(data);
        if (msg.type === 'welcome') {
            ws.send("{\"type\":\"join\",\"room\":\"teleport-default\",\"name\":\"Desktop Client\",\"platform\":\"desktop\"}");
        }
    });
}

runWebClient();
setTimeout(runDesktopClient, 2000);

setTimeout(() => process.exit(0), 5000);
