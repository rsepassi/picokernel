#!/usr/bin/env python3
"""Simple UDP echo server for testing VMOS network functionality."""

import socket
import sys

def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 5555

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('0.0.0.0', port))

    print(f"UDP echo server listening on port {port}", flush=True)

    try:
        while True:
            data, addr = sock.recvfrom(4096)
            print(f"Received {len(data)} bytes from {addr}: {data}", flush=True)
            sock.sendto(data, addr)
            print(f"Echoed back to {addr}", flush=True)
    except KeyboardInterrupt:
        print("\nShutting down", flush=True)
    finally:
        sock.close()

if __name__ == '__main__':
    main()
