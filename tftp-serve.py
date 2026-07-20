#!/usr/bin/env python3
"""Read-only TFTP server for RedBoot netboot testing.

Usage:
    ./tftp-serve.py [directory] [listen-ip] [port]

Defaults: directory=output/images, listen-ip=0.0.0.0, port=69 (needs root
on most systems; pass a port >1024 and point RedBoot's `load -p <port>` at
it if you can't bind 69).
"""
import argparse
import os
import sys

import tftpy

def parse_args():
    parser = argparse.ArgumentParser(
        description="Serve a directory read-only over TFTP for RedBoot netbooting."
    )
    parser.add_argument(
        "directory",
        nargs="?",
        default="output/images",
        help="directory to serve (default: output/images)",
    )
    parser.add_argument(
        "listen_ip",
        nargs="?",
        default="0.0.0.0",
        help="IP address to bind (default: 0.0.0.0)",
    )
    parser.add_argument(
        "port",
        nargs="?",
        type=int,
        default=69,
        help="UDP port to bind (default: 69)",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    root = os.path.abspath(args.directory)

    if not os.path.isdir(root):
        print(f"error: serve directory does not exist: {root}", file=sys.stderr)
        sys.exit(1)

    print(
        f"tftp-serve: serving {root} on {args.listen_ip}:{args.port} "
        "(read-only)"
    )
    server = tftpy.TftpServer(tftproot=root)
    server.listen(listenip=args.listen_ip, listenport=args.port)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass