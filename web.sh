#!/bin/bash
# web.sh — Start the Teleport signaling server and serve the web UI locally
#
# Usage:
#   ./web.sh              # start signaling server + open browser
#   ./web.sh --port 3000  # custom port (default: 3000)
#   ./web.sh --no-open    # don't auto-open browser

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_DIR="$SCRIPT_DIR/webversion/server"
WEB_DIR="$SCRIPT_DIR/webversion"

PORT=3000
AUTO_OPEN=1

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --port)    PORT="$2"; shift 2 ;;
        --no-open) AUTO_OPEN=0; shift ;;
        *) shift ;;
    esac
done

# ── Dependency checks ─────────────────────────────────────────────────────────
if ! command -v node &>/dev/null; then
    echo "❌  Node.js not found  →  https://nodejs.org (v18+)"
    exit 1
fi

NODE_VER=$(node -e "process.stdout.write(process.version.replace('v','').split('.')[0])")
if [ "$NODE_VER" -lt 18 ]; then
    echo "❌  Node.js v18+ required (found v$NODE_VER)"
    exit 1
fi

# ── Install npm dependencies if needed ───────────────────────────────────────
if [ ! -d "$SERVER_DIR/node_modules" ]; then
    echo "📦  Installing signaling server dependencies…"
    (cd "$SERVER_DIR" && npm install --silent)
fi

# ── Start signaling server ────────────────────────────────────────────────────
echo "🌐  Starting Teleport signaling server on port $PORT…"
PORT=$PORT node "$SERVER_DIR/signaling.js" &
SERVER_PID=$!

# Give the server a moment to bind
sleep 1

# Check the server actually started
if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "❌  Signaling server failed to start. Check port $PORT is free."
    exit 1
fi

echo "✅  Signaling server running (PID $SERVER_PID) → ws://localhost:$PORT"

# ── Serve the web UI ──────────────────────────────────────────────────────────
# Try python3, then python, then npx serve as fallback
WEB_PORT=$((PORT + 1))

serve_web() {
    if command -v python3 &>/dev/null; then
        echo "🖥️   Serving web UI via Python on http://localhost:$WEB_PORT …"
        python3 -m http.server "$WEB_PORT" --directory "$WEB_DIR" &>/dev/null &
        WEB_PID=$!
    elif command -v python &>/dev/null; then
        echo "🖥️   Serving web UI via Python on http://localhost:$WEB_PORT …"
        (cd "$WEB_DIR" && python -m SimpleHTTPServer "$WEB_PORT") &>/dev/null &
        WEB_PID=$!
    elif command -v npx &>/dev/null; then
        echo "🖥️   Serving web UI via npx serve on http://localhost:$WEB_PORT …"
        npx serve "$WEB_DIR" -l "$WEB_PORT" &>/dev/null &
        WEB_PID=$!
    else
        echo "⚠️   No static file server found (python3/python/npx). Open $WEB_DIR/index.html manually."
        WEB_PID=""
        return
    fi
    sleep 1
}

serve_web

# ── Open browser ──────────────────────────────────────────────────────────────
if [ "$AUTO_OPEN" -eq 1 ] && [ -n "$WEB_PID" ]; then
    URL="http://localhost:$WEB_PORT"
    echo "🔗  Opening $URL …"
    if command -v xdg-open &>/dev/null; then
        xdg-open "$URL" &>/dev/null &
    elif command -v open &>/dev/null; then
        open "$URL" &
    fi
fi

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Teleport Web is running!"
echo "  Signaling server : ws://localhost:$PORT"
echo "  Web UI           : http://localhost:$WEB_PORT"
echo "  Press Ctrl+C to stop both servers."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# ── Cleanup on exit ───────────────────────────────────────────────────────────
cleanup() {
    echo ""
    echo "🛑  Stopping servers…"
    [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null || true
    [ -n "$WEB_PID" ]    && kill "$WEB_PID"    2>/dev/null || true
    exit 0
}
trap cleanup INT TERM

# Wait for both child processes
wait
