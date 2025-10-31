#!/bin/bash
set -e

if [ -z "$1" ]; then
    echo "Usage: $0 <host> <port>"
    echo "  Example: $0 10.0.2.15 5555"
    exit 1
fi

if [ -z "$2" ]; then
    echo "Usage: $0 <host> <port>"
    echo "  Example: $0 10.0.2.15 5555"
    exit 1
fi

HOST=$1
PORT=$2

echo "Sending UDP packets to $HOST:$PORT (Ctrl+C to stop)"

COUNT=0
while true; do
    COUNT=$((COUNT + 1))
    TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[$TIMESTAMP] Sending packet #$COUNT to $HOST:$PORT"
    echo "hello from packet $COUNT" | nc -u -w 1 "$HOST" "$PORT"
    sleep 1
done
