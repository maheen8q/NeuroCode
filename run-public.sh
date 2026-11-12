#!/bin/bash
# Starts backend, frontend and a Cloudflare quick tunnel in the background.
cd ~/NeuroCode
mkdir -p logs

pkill -f "build/neurocode" 2>/dev/null
pkill -f "vite" 2>/dev/null
pkill -f "cloudflared tunnel" 2>/dev/null
sleep 1

(cd backend && nohup ./build/neurocode > ../logs/backend.log 2>&1 &)
(cd frontend && nohup npm run dev -- --host 127.0.0.1 > ../logs/frontend.log 2>&1 &)
sleep 5
nohup cloudflared tunnel --url http://127.0.0.1:5173 > logs/tunnel.log 2>&1 &

echo "Waiting for the public URL..."
for i in $(seq 1 30); do
  URL=$(grep -o 'https://[a-z0-9-]*\.trycloudflare\.com' logs/tunnel.log | head -1)
  [ -n "$URL" ] && break
  sleep 1
done
echo "NeuroCode is live at: ${URL:-not found yet, check logs/tunnel.log}"