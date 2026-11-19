#!/usr/bin/env python3
"""Find a PS5 Harness on the LAN after a restart, DHCP change or port change.

The harness answers UDP queries on its discovery port (default 8799) and also
serves a small public JSON document at /.well-known/ps5h. Neither response
contains the bearer token, session content or filesystem paths.

Examples:
    python3 tools/ps5h-discover.py
    python3 tools/ps5h-discover.py --wait
    python3 tools/ps5h-discover.py --subnet 192.168.4.0/24
    PS5H_TOKEN=... python3 tools/ps5h-discover.py

Only scan a network you are authorized to use. This helper never sends the
bearer token; it optionally prints a URL containing a token supplied by the
caller so the page can be opened directly.
"""

from __future__ import annotations

import argparse
import ipaddress
import json
import os
import socket
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from urllib.parse import quote

DISCOVERY_QUERY = b"PS5H_DISCOVER_V1\n"
SERVICE = "ps5-harness"


def parse_document(data: bytes):
    try:
        document = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None
    if not isinstance(document, dict) or document.get("service") != SERVICE:
        return None
    if not isinstance(document.get("http_port"), int):
        return None
    return document


def udp_discover(broadcast: str, port: int, timeout: float):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(timeout)
    try:
        sock.sendto(DISCOVERY_QUERY, (broadcast, port))
    except OSError as error:
        print(f"discovery send failed: {error}", file=sys.stderr)
        sock.close()
        return []
    found = {}
    deadline = time.monotonic() + timeout
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        sock.settimeout(remaining)
        try:
            data, peer = sock.recvfrom(65535)
        except socket.timeout:
            break
        except OSError:
            break
        document = parse_document(data)
        if document is None:
            continue
        document["_source"] = "udp"
        document["_address"] = peer[0]
        key = (document.get("title_id"), document.get("build"),
               document.get("http_port"), peer[0])
        found[key] = document
    sock.close()
    return list(found.values())


def http_discover(host: str, port: int, timeout: float):
    request = (
        "GET /.well-known/ps5h HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        "Connection: close\r\n\r\n"
    ).encode("ascii")
    try:
        with socket.create_connection((host, port), timeout=timeout) as sock:
            sock.sendall(request)
            chunks = []
            while True:
                chunk = sock.recv(8192)
                if not chunk:
                    break
                chunks.append(chunk)
                if sum(len(item) for item in chunks) > 256 * 1024:
                    return None
    except OSError:
        return None
    response = b"".join(chunks)
    head, separator, body = response.partition(b"\r\n\r\n")
    if not separator or not head.startswith(b"HTTP/1."):
        return None
    status = head.split(b"\r\n", 1)[0]
    if b" 200 " not in status:
        return None
    document = parse_document(body)
    if document is None:
        return None
    document["_source"] = "tcp"
    document["_address"] = host
    return document


def scan_subnet(subnet: str, ports, timeout: float, workers: int):
    network = ipaddress.ip_network(subnet, strict=False)
    hosts = list(network.hosts())
    if not hosts:
        hosts = [network.network_address]
    if len(hosts) * max(1, len(ports)) > 4096:
        raise ValueError("subnet is too large; use a /24 or smaller")
    found = {}
    with ThreadPoolExecutor(max_workers=max(1, workers)) as pool:
        futures = {
            pool.submit(http_discover, str(host), port, timeout): (str(host), port)
            for host in hosts for port in ports
        }
        for future in as_completed(futures):
            document = future.result()
            if document is None:
                continue
            key = (document.get("title_id"), document.get("build"),
                   document.get("http_port"), document.get("_address"))
            found[key] = document
    return list(found.values())


def url_for(document, token: str):
    host = document.get("_address")
    port = document.get("http_port")
    if host and port:
        url = f"http://{host}:{port}/"
    else:
        urls = document.get("urls") or []
        url = next((item for item in urls if isinstance(item, str)), "")
    if token and url:
        url += ("&" if "?" in url else "?") + "token=" + quote(token, safe="")
    return url


def describe(document, token: str):
    return (
        f"{url_for(document, token)}  "
        f"build={document.get('build', '?')} "
        f"title={document.get('title_id', '?')} "
        f"source={document.get('_source', '?')}"
    )


def main():
    parser = argparse.ArgumentParser(
        description="Find a running PS5 Harness after an IP or port change.")
    parser.add_argument("--broadcast", default="255.255.255.255",
                        help="UDP broadcast address (default: 255.255.255.255)")
    parser.add_argument("--discovery-port", type=int, default=8799,
                        help="harness UDP discovery port (default: 8799)")
    parser.add_argument("--subnet",
                        help="optional CIDR for a TCP fallback scan, e.g. 192.168.4.0/24")
    parser.add_argument("--ports", default="8791,8790,8789,8787",
                        help="comma-separated TCP ports for --subnet fallback")
    parser.add_argument("--timeout", type=float, default=1.5,
                        help="seconds to wait for each discovery response")
    parser.add_argument("--workers", type=int, default=32,
                        help="TCP fallback concurrency (default: 32)")
    parser.add_argument("--wait", action="store_true",
                        help="keep looking; print a new URL when the console moves")
    parser.add_argument("--interval", type=float, default=3.0,
                        help="seconds between --wait passes (default: 3)")
    parser.add_argument("--token", default=os.environ.get("PS5H_TOKEN", ""),
                        help="optional bearer token; prefer PS5H_TOKEN in the environment")
    parser.add_argument("--json", action="store_true",
                        help="print discovery documents as JSON")
    args = parser.parse_args()

    if not 0 < args.discovery_port <= 65535:
        parser.error("--discovery-port must be 1..65535")
    try:
        ports = sorted({int(item) for item in args.ports.split(",") if item.strip()})
    except ValueError:
        parser.error("--ports must be comma-separated integers")

    seen = set()
    wait_reported = False
    try:
        while True:
            results = udp_discover(args.broadcast, args.discovery_port, args.timeout)
            if not results and args.subnet:
                results = scan_subnet(args.subnet, ports, args.timeout, args.workers)
            if args.json:
                for document in results:
                    print(json.dumps(document, sort_keys=True))
            else:
                for document in results:
                    key = (document.get("_address"), document.get("http_port"),
                           document.get("build"))
                    if key in seen:
                        continue
                    seen.add(key)
                    print(describe(document, args.token))
            if not results and not args.wait:
                print(
                    "No console found. Check that the harness is running, that "
                    "the phone/computer is on the same LAN, and that the "
                    "discovery UDP port is not blocked; or pass --subnet for a "
                    "TCP fallback.",
                    file=sys.stderr)
                return 1
            if not args.wait:
                return 0 if results else 1
            if not results and not wait_reported:
                print("Waiting for a console (Ctrl-C to stop)…", flush=True)
                wait_reported = True
            time.sleep(max(0.25, args.interval))
    except KeyboardInterrupt:
        return 130
    except ValueError as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
