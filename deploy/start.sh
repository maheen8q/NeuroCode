#!/bin/sh
set -e
cd /app/backend
./build/neurocode &
nginx -c /app/nginx.conf -g 'daemon off;'