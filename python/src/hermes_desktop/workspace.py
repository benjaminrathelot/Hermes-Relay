from __future__ import annotations

import json
import os
import re
import shutil
import sys
from dataclasses import asdict
from pathlib import Path

from .core import HermesClient


def _safe_name(alias: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "-", alias.strip()).strip("-")
    if not cleaned:
        raise ValueError("alias must contain at least one usable character")
    return cleaned.lower()


def _safe_artifact_name(name: str, default_suffix: str) -> str:
    path = Path(name.strip())
    stem = _safe_name(path.stem or path.name)
    suffix = path.suffix.lower()
    if not re.fullmatch(r"\.[a-z0-9]{1,8}", suffix):
        suffix = default_suffix
    return f"{stem}{suffix}"


def default_workspace_root() -> Path:
    override = os.environ.get("HERMES_DESKTOP_HOME")
    if override:
        return Path(override).expanduser()
    if os.name == "nt":
        base = Path(os.environ.get("APPDATA") or (Path.home() / "AppData" / "Roaming"))
        return base / "HermesRelayDesktop"
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Application Support" / "HermesRelayDesktop"
    base = Path(os.environ.get("XDG_DATA_HOME") or (Path.home() / ".local" / "share"))
    return base / "hermes-relay-desktop"


class HermesWorkspace:
    def __init__(self, root: str | Path, client: HermesClient | None = None):
        self.root = Path(root)
        self.client = client or HermesClient()

    @property
    def identities_dir(self) -> Path:
        return self.root / "identities"

    @property
    def contacts_dir(self) -> Path:
        return self.root / "contacts"

    @property
    def stores_dir(self) -> Path:
        return self.root / "stores"

    @property
    def relays_dir(self) -> Path:
        return self.root / "relays"

    @property
    def drops_dir(self) -> Path:
        return self.root / "drops"

    @property
    def exports_dir(self) -> Path:
        return self.root / "exports"

    def bootstrap(self) -> dict:
        for path in [
            self.root,
            self.identities_dir,
            self.contacts_dir,
            self.stores_dir,
            self.relays_dir,
            self.drops_dir,
            self.exports_dir,
        ]:
            path.mkdir(parents=True, exist_ok=True)
        return self.overview()

    def identity_path(self, alias: str) -> Path:
        return self.identities_dir / f"{_safe_name(alias)}.id"

    def contact_path(self, alias: str) -> Path:
        return self.contacts_dir / f"{_safe_name(alias)}.contact"

    def store_path(self, name: str) -> Path:
        return self.stores_dir / _safe_name(name)

    def relay_root(self, name: str) -> Path:
        return self.relays_dir / _safe_name(name)

    def generate_identity(self, alias: str) -> dict:
        self.bootstrap()
        identity_path = self.identity_path(alias)
        contact_path = self.contact_path(alias)
        summary = self.client.generate_identity(alias, str(identity_path), str(contact_path))
        return {
            "identity_path": str(identity_path),
            "contact_path": str(contact_path),
            **asdict(summary),
        }

    def import_contact(self, source_path: str, alias: str | None = None) -> dict:
        self.bootstrap()
        source = Path(source_path)
        summary = self.client.contact_summary(str(source))
        target_alias = alias or summary.alias or Path(source_path).stem
        target_path = self.contact_path(target_alias)
        self.contacts_dir.mkdir(parents=True, exist_ok=True)
        if source.resolve() != target_path.resolve():
            shutil.copyfile(source, target_path)
        return {
            "contact_path": str(target_path),
            "source_path": str(source),
            "stored_alias": target_alias,
            **asdict(summary),
        }

    def list_identities(self) -> list[dict]:
        items: list[dict] = []
        if not self.identities_dir.exists():
            return items
        for path in sorted(self.identities_dir.glob("*.id")):
            summary = self.client.identity_summary(str(path))
            items.append({"path": str(path), **asdict(summary)})
        return items

    def list_contacts(self) -> list[dict]:
        items: list[dict] = []
        if not self.contacts_dir.exists():
            return items
        for path in sorted(self.contacts_dir.glob("*.contact")):
            summary = self.client.contact_summary(str(path))
            items.append({"path": str(path), **asdict(summary)})
        return items

    def list_stores(self) -> list[dict]:
        items: list[dict] = []
        if not self.stores_dir.exists():
            return items
        for path in sorted(self.stores_dir.iterdir()):
            if path.is_dir():
                try:
                    stats = self.client.store_stats(str(path))
                    items.append({"name": path.name, "path": str(path), **asdict(stats)})
                except Exception:
                    items.append({"name": path.name, "path": str(path), "error": "unreadable"})
        return items

    def list_relays(self) -> list[dict]:
        items: list[dict] = []
        if not self.relays_dir.exists():
            return items
        for path in sorted(self.relays_dir.iterdir()):
            if path.is_dir():
                items.append(
                    {
                        "name": path.name,
                        "path": str(path),
                        "status_path": str(path / "run" / "status.json"),
                        "peers_path": str(path / "peers.txt"),
                    }
                )
        return items

    def create_message(
        self,
        sender_alias: str,
        recipient_alias: str,
        store_name: str,
        message: str,
        ttl_seconds: int = 0,
        pow_difficulty: int = 28,
        export_name: str | None = None,
    ) -> dict:
        self.bootstrap()
        store_path = self.store_path(store_name)
        store_path.mkdir(parents=True, exist_ok=True)
        out_path = self.exports_dir / _safe_artifact_name(export_name, ".hrm") if export_name else None
        summary = self.client.create_message(
            str(self.identity_path(sender_alias)),
            str(self.contact_path(recipient_alias)),
            str(store_path),
            message,
            ttl_seconds=ttl_seconds,
            pow_difficulty=pow_difficulty,
            out_envelope_path=str(out_path) if out_path else None,
        )
        return {
            "store_path": str(store_path),
            "export_path": str(out_path) if out_path else None,
            **asdict(summary),
        }

    def list_store_messages(self, store_name: str) -> list[dict]:
        self.bootstrap()
        return [asdict(item) for item in self.client.store_inventory(str(self.store_path(store_name)))]

    def read_store_message(self, store_name: str, identity_alias: str, envelope_id_hex: str) -> dict:
        self.bootstrap()
        return self.client.decrypt_stored_message(
            str(self.store_path(store_name)),
            str(self.identity_path(identity_alias)),
            envelope_id_hex,
        )

    def cleanup_store(self, store_name: str) -> dict:
        self.bootstrap()
        store_path = self.store_path(store_name)
        self.client.cleanup_store(str(store_path))
        return {"store_name": store_name, "store_path": str(store_path)}

    def import_bundle(self, store_name: str, source_path: str) -> dict:
        self.bootstrap()
        store_path = self.store_path(store_name)
        store_path.mkdir(parents=True, exist_ok=True)
        imported = self.client.import_bundle(str(store_path), source_path)
        return {
            "store_name": store_name,
            "store_path": str(store_path),
            "source_path": str(Path(source_path)),
            "imported": imported,
        }

    def export_bundle(self, store_name: str, output_name: str) -> dict:
        self.bootstrap()
        store_path = self.store_path(store_name)
        output_path = self.exports_dir / _safe_artifact_name(output_name, ".bundle")
        self.client.export_bundle(str(store_path), str(output_path))
        return {
            "store_name": store_name,
            "store_path": str(store_path),
            "output_path": str(output_path),
        }

    def ensure_relay(self, relay_name: str, listen_addr: str) -> dict:
        self.bootstrap()
        root = self.relay_root(relay_name)
        self.client.relay_init(str(root), listen_addr)
        return {"name": relay_name, "root": str(root), "listen_addr": listen_addr}

    def relay_status(self, relay_name: str) -> dict:
        self.bootstrap()
        root = self.relay_root(relay_name)
        status_path = root / "run" / "status.json"
        config_path = root / "relay.conf"
        result = {
            "name": relay_name,
            "root": str(root),
            "state": "not_initialized",
            "status_path": str(status_path),
            "config_path": str(config_path),
        }
        if config_path.exists():
            result["state"] = "initialized"
            result["listen_addr"] = self._read_relay_config_value(config_path, "listen_addr") or ""
        if status_path.exists():
            loaded = json.loads(status_path.read_text(encoding="utf-8"))
            loaded["name"] = relay_name
            loaded["root"] = str(root)
            loaded["status_path"] = str(status_path)
            loaded["config_path"] = str(config_path)
            return loaded
        return result

    def relay_peers(self, relay_name: str) -> list[dict]:
        self.bootstrap()
        root = self.relay_root(relay_name)
        return [asdict(item) for item in self.client.relay_list_peers(str(root))]

    def relay_add_peer(self, relay_name: str, peer_addr: str, alias: str | None = None) -> dict:
        self.bootstrap()
        root = self.relay_root(relay_name)
        self.client.relay_add_peer(str(root), peer_addr, alias)
        return {"relay_name": relay_name, "peer_addr": peer_addr, "alias": alias or ""}

    def relay_sync_once(self, relay_name: str) -> dict:
        self.bootstrap()
        root = self.relay_root(relay_name)
        synced = self.client.relay_sync_once(str(root))
        return {"relay_name": relay_name, "synced_peers": synced}

    def relay_process_imports(self, relay_name: str) -> dict:
        self.bootstrap()
        root = self.relay_root(relay_name)
        processed = self.client.relay_process_imports(str(root))
        return {"relay_name": relay_name, "processed_imports": processed}

    def relay_export_latest(self, relay_name: str, output_name: str) -> dict:
        self.bootstrap()
        root = self.relay_root(relay_name)
        out_path = self.exports_dir / _safe_artifact_name(output_name, ".bundle")
        self.client.relay_export_latest(str(root), str(out_path))
        return {"relay_name": relay_name, "output_path": str(out_path)}

    def relay_drop_file(self, relay_name: str, source_path: str) -> dict:
        self.bootstrap()
        root = self.relay_root(relay_name)
        import_dir = root / "import"
        import_dir.mkdir(parents=True, exist_ok=True)
        source = Path(source_path)
        target = import_dir / _safe_artifact_name(source.name, source.suffix or ".bundle")
        shutil.copyfile(source, target)
        return {
            "relay_name": relay_name,
            "source_path": str(source),
            "dropped_path": str(target),
        }

    def overview(self) -> dict:
        return {
            "root": str(self.root),
            "drops_dir": str(self.drops_dir),
            "exports_dir": str(self.exports_dir),
            "identities": self.list_identities(),
            "contacts": self.list_contacts(),
            "stores": self.list_stores(),
            "relays": self.list_relays(),
        }

    @staticmethod
    def _read_relay_config_value(path: Path, key: str) -> str | None:
        try:
            for raw_line in path.read_text(encoding="utf-8").splitlines():
                if raw_line.startswith(f"{key}="):
                    return raw_line.split("=", 1)[1].strip()
        except OSError:
            return None
        return None
