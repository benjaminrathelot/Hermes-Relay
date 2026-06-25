from __future__ import annotations

import ctypes
from dataclasses import asdict, dataclass
from typing import Optional

from .binding import (
    HermesError,
    IdentitySummaryStruct,
    MessageSummaryStruct,
    PeerStruct,
    StoreStatsStruct,
    get_binding,
)


def _as_bytes(text: Optional[str]) -> Optional[bytes]:
    if text is None:
        return None
    return str(text).encode("utf-8")


def _struct_text(raw: ctypes.Array[ctypes.c_char]) -> str:
    return bytes(raw).split(b"\0", 1)[0].decode("utf-8", errors="replace")


@dataclass(frozen=True)
class IdentitySummary:
    alias: str
    fingerprint_hex: str
    recipient_hint_hex: str


@dataclass(frozen=True)
class MessageSummary:
    envelope_id_hex: str
    created_at_unix: int
    expires_at_unix: int
    pow_difficulty: int
    payload_size: int


@dataclass(frozen=True)
class StoreStats:
    envelope_count: int
    total_bytes: int
    expired_count: int
    weakest_pow: int
    strongest_pow: int


@dataclass(frozen=True)
class RelayPeer:
    address: str
    alias: str
    last_success_unix: int
    last_attempt_unix: int
    consecutive_failures: int
    learned_automatically: int
    inactive: int


def _identity_summary_from_struct(data: IdentitySummaryStruct) -> IdentitySummary:
    return IdentitySummary(
        alias=_struct_text(data.alias),
        fingerprint_hex=_struct_text(data.fingerprint_hex),
        recipient_hint_hex=_struct_text(data.recipient_hint_hex),
    )


def _message_summary_from_struct(data: MessageSummaryStruct) -> MessageSummary:
    return MessageSummary(
        envelope_id_hex=_struct_text(data.envelope_id_hex),
        created_at_unix=int(data.created_at_unix),
        expires_at_unix=int(data.expires_at_unix),
        pow_difficulty=int(data.pow_difficulty),
        payload_size=int(data.payload_size),
    )


def _store_stats_from_struct(data: StoreStatsStruct) -> StoreStats:
    return StoreStats(
        envelope_count=int(data.envelope_count),
        total_bytes=int(data.total_bytes),
        expired_count=int(data.expired_count),
        weakest_pow=int(data.weakest_pow),
        strongest_pow=int(data.strongest_pow),
    )


def _peer_from_struct(data: PeerStruct) -> RelayPeer:
    return RelayPeer(
        address=_struct_text(data.address),
        alias=_struct_text(data.alias),
        last_success_unix=int(data.last_success_unix),
        last_attempt_unix=int(data.last_attempt_unix),
        consecutive_failures=int(data.consecutive_failures),
        learned_automatically=int(data.learned_automatically),
        inactive=int(data.inactive),
    )


class HermesClient:
    def __init__(self) -> None:
        self.binding = get_binding()

    @property
    def native_library_path(self) -> str:
        return str(self.binding.paths.library_path)

    @property
    def native_cli_path(self) -> Optional[str]:
        return str(self.binding.paths.cli_path) if self.binding.paths.cli_path else None

    def info(self) -> dict:
        return {
            "version": self.binding.version(),
            "library_path": self.native_library_path,
            "cli_path": self.native_cli_path,
        }

    def generate_identity(self, alias: str, identity_path: str, contact_path: str) -> IdentitySummary:
        summary = IdentitySummaryStruct()
        self.binding.check(
            self.binding.lib.hermes_api_identity_generate_files(
                _as_bytes(alias),
                _as_bytes(identity_path),
                _as_bytes(contact_path),
                ctypes.byref(summary),
            )
        )
        return _identity_summary_from_struct(summary)

    def identity_summary(self, identity_path: str) -> IdentitySummary:
        summary = IdentitySummaryStruct()
        self.binding.check(
            self.binding.lib.hermes_api_identity_load_summary(
                _as_bytes(identity_path),
                ctypes.byref(summary),
            )
        )
        return _identity_summary_from_struct(summary)

    def contact_summary(self, contact_path: str) -> IdentitySummary:
        summary = IdentitySummaryStruct()
        self.binding.check(
            self.binding.lib.hermes_api_contact_load_summary(
                _as_bytes(contact_path),
                ctypes.byref(summary),
            )
        )
        return _identity_summary_from_struct(summary)

    def create_message(
        self,
        identity_path: str,
        contact_path: str,
        store_path: Optional[str],
        message: str,
        ttl_seconds: int = 0,
        pow_difficulty: int = 28,
        out_envelope_path: Optional[str] = None,
    ) -> MessageSummary:
        summary = MessageSummaryStruct()
        self.binding.check(
            self.binding.lib.hermes_api_create_message(
                _as_bytes(identity_path),
                _as_bytes(contact_path),
                _as_bytes(store_path) if store_path else None,
                _as_bytes(message),
                int(ttl_seconds),
                int(pow_difficulty),
                _as_bytes(out_envelope_path) if out_envelope_path else None,
                ctypes.byref(summary),
            )
        )
        return _message_summary_from_struct(summary)

    def verify_envelope(self, envelope_path: str, now_unix: int = 0) -> MessageSummary:
        summary = MessageSummaryStruct()
        self.binding.check(
            self.binding.lib.hermes_api_verify_envelope_file(
                _as_bytes(envelope_path),
                int(now_unix),
                ctypes.byref(summary),
            )
        )
        return _message_summary_from_struct(summary)

    def decrypt_envelope(self, identity_path: str, envelope_path: str) -> str:
        buf_len = ctypes.c_size_t(4096)
        buf = ctypes.create_string_buffer(buf_len.value)
        self.binding.check(
            self.binding.lib.hermes_api_decrypt_envelope_file(
                _as_bytes(identity_path),
                _as_bytes(envelope_path),
                buf,
                ctypes.byref(buf_len),
            )
        )
        return buf.raw[: buf_len.value].decode("utf-8", errors="replace")

    def import_bundle(self, store_path: str, bundle_path: str, now_unix: int = 0) -> int:
        imported = ctypes.c_size_t(0)
        self.binding.check(
            self.binding.lib.hermes_api_import_bundle(
                _as_bytes(store_path),
                _as_bytes(bundle_path),
                int(now_unix),
                ctypes.byref(imported),
            )
        )
        return int(imported.value)

    def export_bundle(self, store_path: str, bundle_path: str, now_unix: int = 0) -> None:
        self.binding.check(
            self.binding.lib.hermes_api_export_bundle(
                _as_bytes(store_path),
                _as_bytes(bundle_path),
                int(now_unix),
            )
        )

    def store_stats(self, store_path: str, now_unix: int = 0) -> StoreStats:
        stats = StoreStatsStruct()
        self.binding.check(
            self.binding.lib.hermes_api_store_stats(
                _as_bytes(store_path),
                int(now_unix),
                ctypes.byref(stats),
            )
        )
        return _store_stats_from_struct(stats)

    def store_inventory(self, store_path: str, max_messages: int = 512) -> list[MessageSummary]:
        array_type = MessageSummaryStruct * max_messages
        items = array_type()
        count = ctypes.c_size_t(0)
        self.binding.check(
            self.binding.lib.hermes_api_store_list_inventory(
                _as_bytes(store_path),
                items,
                max_messages,
                ctypes.byref(count),
            )
        )
        return [_message_summary_from_struct(items[index]) for index in range(count.value)]

    def decrypt_stored_message(
        self,
        store_path: str,
        identity_path: str,
        envelope_id_hex: str,
    ) -> dict:
        buf_len = ctypes.c_size_t(4096)
        buf = ctypes.create_string_buffer(buf_len.value)
        summary = MessageSummaryStruct()
        self.binding.check(
            self.binding.lib.hermes_api_store_decrypt_message(
                _as_bytes(store_path),
                _as_bytes(identity_path),
                _as_bytes(envelope_id_hex),
                buf,
                ctypes.byref(buf_len),
                ctypes.byref(summary),
            )
        )
        return {
            "summary": asdict(_message_summary_from_struct(summary)),
            "plaintext": buf.raw[: buf_len.value].decode("utf-8", errors="replace"),
        }

    def cleanup_store(self, store_path: str, now_unix: int = 0) -> None:
        self.binding.check(
            self.binding.lib.hermes_api_store_cleanup(
                _as_bytes(store_path),
                int(now_unix),
            )
        )

    def relay_init(self, root: str, listen_addr: Optional[str] = None) -> None:
        self.binding.check(self.binding.lib.hermes_api_relay_init(_as_bytes(root), _as_bytes(listen_addr)))

    def relay_add_peer(self, root: str, peer_addr: str, alias: Optional[str] = None) -> None:
        self.binding.check(
            self.binding.lib.hermes_api_relay_add_peer(
                _as_bytes(root),
                _as_bytes(peer_addr),
                _as_bytes(alias),
            )
        )

    def relay_list_peers(self, root: str, max_peers: int = 256) -> list[RelayPeer]:
        array_type = PeerStruct * max_peers
        items = array_type()
        count = ctypes.c_size_t(0)
        self.binding.check(
            self.binding.lib.hermes_api_relay_list_peers(
                _as_bytes(root),
                items,
                max_peers,
                ctypes.byref(count),
            )
        )
        return [_peer_from_struct(items[index]) for index in range(count.value)]

    def relay_process_imports(self, root: str) -> int:
        processed = ctypes.c_size_t(0)
        self.binding.check(
            self.binding.lib.hermes_api_relay_process_imports(
                _as_bytes(root),
                ctypes.byref(processed),
            )
        )
        return int(processed.value)

    def relay_export_latest(self, root: str, out_path: str) -> None:
        self.binding.check(
            self.binding.lib.hermes_api_relay_export_latest(
                _as_bytes(root),
                _as_bytes(out_path),
            )
        )

    def relay_sync_once(self, root: str) -> int:
        synced = ctypes.c_size_t(0)
        self.binding.check(
            self.binding.lib.hermes_api_relay_sync_once(
                _as_bytes(root),
                ctypes.byref(synced),
            )
        )
        return int(synced.value)

    def relay_status(self, root: str) -> str:
        buf_len = ctypes.c_size_t(65536)
        buf = ctypes.create_string_buffer(buf_len.value)
        self.binding.check(
            self.binding.lib.hermes_api_relay_read_status(
                _as_bytes(root),
                buf,
                ctypes.byref(buf_len),
            )
        )
        return buf.raw[: buf_len.value].decode("utf-8", errors="replace")
