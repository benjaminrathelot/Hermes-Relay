from __future__ import annotations

import ipaddress
import os
import socket
import time


def _is_useful_ip(value: str) -> bool:
    try:
        ip = ipaddress.ip_address(value)
    except ValueError:
        return False
    return not ip.is_loopback and not ip.is_link_local and not ip.is_unspecified


def _collect_candidate_ips() -> list[str]:
    found: set[str] = set()
    hostname = socket.gethostname()
    try:
        for item in socket.getaddrinfo(hostname, None, type=socket.SOCK_STREAM):
            address = item[4][0]
            if _is_useful_ip(address):
                found.add(address)
    except OSError:
        pass
    for target in [("1.1.1.1", 53), ("8.8.8.8", 53), ("2606:4700:4700::1111", 53)]:
        family = socket.AF_INET6 if ":" in target[0] else socket.AF_INET
        sock = socket.socket(family, socket.SOCK_DGRAM)
        try:
            sock.connect(target)
            address = sock.getsockname()[0]
            if _is_useful_ip(address):
                found.add(address)
        except OSError:
            pass
        finally:
            sock.close()
    return sorted(found)


def _probe_internet(timeout_seconds: float = 1.5) -> bool:
    for target in [("1.1.1.1", 53), ("8.8.8.8", 53), ("2606:4700:4700::1111", 53)]:
        try:
            with socket.create_connection(target, timeout=timeout_seconds):
                return True
        except OSError:
            continue
    return False


def collect_network_snapshot() -> dict:
    addresses = _collect_candidate_ips()
    private_addresses = []
    for value in addresses:
        try:
            if ipaddress.ip_address(value).is_private:
                private_addresses.append(value)
        except ValueError:
            continue
    internet_reachable = _probe_internet() if addresses else False
    lan_available = bool(private_addresses)
    if internet_reachable and lan_available:
        mode = "internet-and-lan"
        advisory = [
            "Internet appears reachable. Keep relay peers configured, but continue exporting bundles for failover.",
            "A private LAN address is present. Local peer sync should work if other relays are on the same segment.",
        ]
    elif lan_available:
        mode = "lan-only"
        advisory = [
            "Private LAN connectivity is available. Prefer relay peers, local hotspot links, and file bundles.",
            "No Internet reachability probe succeeded. Treat this node as island-capable and plan USB or manual ferry paths.",
        ]
    elif addresses:
        mode = "limited-network"
        advisory = [
            "A network interface exists, but no private LAN address was detected and Internet reachability failed.",
            "Try another Wi-Fi or hotspot, or export a bundle for manual transfer.",
        ]
    else:
        mode = "offline"
        advisory = [
            "No non-loopback network address was detected.",
            "Use file export, QR, or a relay box on another network. This desktop shell does not yet roam across Wi-Fi networks automatically.",
        ]
    return {
        "checked_at_unix": int(time.time()),
        "hostname": socket.gethostname(),
        "platform": os.name,
        "addresses": addresses,
        "private_lan_addresses": private_addresses,
        "lan_available": lan_available,
        "internet_reachable": internet_reachable,
        "mode": mode,
        "advisory": advisory,
    }
