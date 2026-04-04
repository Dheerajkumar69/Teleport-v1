import asyncio
import websockets
import json

async def web_client():
    async with websockets.connect("ws://localhost:3000") as ws:
        welcome = await ws.recv()
        print(f"[Web] Recv: {welcome}")
        
        await ws.send(json.dumps({
            "type": "join",
            "room": "teleport-default",
            "name": "Web Client",
            "clientType": "web"
        }))
        
        peers = await ws.recv()
        print(f"[Web] Recv: {peers}")
        
        while True:
            msg = await ws.recv()
            print(f"[Web] Recv: {msg}")

async def desktop_client():
    await asyncio.sleep(2) # Connect after Web
    async with websockets.connect("ws://localhost:3000") as ws:
        welcome = await ws.recv()
        print(f"[Desktop] Recv: {welcome}")
        
        # Desktop sends exactly this string format:
        payload = "{\"type\":\"join\",\"room\":\"teleport-default\",\"name\":\"Desktop Client\",\"platform\":\"desktop\"}"
        await ws.send(payload)
        
        peers = await ws.recv()
        print(f"[Desktop] Recv: {peers}")
        
        while True:
            msg = await ws.recv()
            print(f"[Desktop] Recv: {msg}")

async def main():
    await asyncio.gather(
        web_client(),
        desktop_client()
    )

asyncio.run(main())
