#!/bin/bash
pkill -f "build/neurocode"
pkill -f "vite"
pkill -f "cloudflared tunnel"
echo "Stopped."