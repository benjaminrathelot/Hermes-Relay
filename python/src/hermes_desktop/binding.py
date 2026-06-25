from __future__ import annotations

import atexit
import ctypes
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


class HermesError(RuntimeError):
    def __init__(self, status_code: int, message: str):
        self.status_code = status_code
        super().__init__(message)


class IdentitySummaryStruct(ctypes.Structure):
    _fields_ = [
        ("alias", ctypes.c_char * 64),
        ("fingerprint_hex", ctypes.c_char * 65),
        ("recipient_hint_hex", ctypes.c_char * 41),
    ]


class MessageSummaryStruct(ctypes.Structure):
    _fields_ = [
        ("envelope_id_hex", ctypes.c_char * 33),
        ("created_at_unix", ctypes.c_uint64),
        ("expires_at_unix", ctypes.c_uint64),
        ("pow_difficulty", ctypes.c_uint32),
        ("payload_size", ctypes.c_uint32),
    ]


class StoreStatsStruct(ctypes.Structure):
    _fields_ = [
        ("envelope_count", ctypes.c_uint64),
        ("total_bytes", ctypes.c_uint64),
        ("expired_count", ctypes.c_uint64),
        ("weakest_pow", ctypes.c_uint64),
        ("strongest_pow", ctypes.c_uint64),
    ]


class PeerStruct(ctypes.Structure):
    _fields_ = [
        ("address", ctypes.c_char * 128),
        ("alias", ctypes.c_char * 64),
        ("last_success_unix", ctypes.c_uint64),
        ("last_attempt_unix", ctypes.c_uint64),
        ("consecutive_failures", ctypes.c_uint32),
        ("learned_automatically", ctypes.c_uint32),
        ("inactive", ctypes.c_uint32),
    ]


@dataclass(frozen=True)
class NativePaths:
    library_path: Path
    cli_path: Optional[Path]


def _candidate_roots() -> list[Path]:
    here = Path(__file__).resolve()
    roots = [
        here.parents[3],
        here.parents[4] if len(here.parents) > 4 else here.parents[-1],
        Path.cwd(),
    ]
    unique: list[Path] = []
    for root in roots:
        if root not in unique:
            unique.append(root)
    return unique


def find_native_paths() -> NativePaths:
    env_lib = os.environ.get("HERMESRELAY_LIBRARY")
    env_cli = os.environ.get("HERMESRELAY_CLI")
    lib_names = ("libhermesrelay.dylib", "libhermesrelay.so", "hermesrelay.dll", "libhermesrelay.dll")

    if env_lib:
        lib_path = Path(env_lib).expanduser().resolve()
        cli_path = Path(env_cli).expanduser().resolve() if env_cli else None
        return NativePaths(lib_path, cli_path if cli_path and cli_path.exists() else None)

    for root in _candidate_roots():
        for candidate in [
            root / "build",
            root / "dist" / "hermes-relay-v1" / "lib",
            root / "lib",
        ]:
            for lib_name in lib_names:
                lib_path = candidate / lib_name
                if lib_path.exists():
                    cli_candidates = [
                        root / "build" / "hermes-cli",
                        root / "dist" / "hermes-relay-v1" / "bin" / "hermes-cli",
                        root / "bin" / "hermes-cli",
                        root / "build" / "hermes-cli.exe",
                        root / "dist" / "hermes-relay-v1" / "bin" / "hermes-cli.exe",
                    ]
                    cli_path = next((path for path in cli_candidates if path.exists()), None)
                    return NativePaths(lib_path.resolve(), cli_path.resolve() if cli_path else None)
    raise FileNotFoundError(
        "unable to locate libhermesrelay shared library; set HERMESRELAY_LIBRARY explicitly"
    )


def _decode_c_string(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("utf-8", errors="replace")


class HermesBinding:
    def __init__(self, paths: Optional[NativePaths] = None):
        self.paths = paths or find_native_paths()
        self.lib = ctypes.CDLL(str(self.paths.library_path))
        self._configure()
        status = int(self.lib.hermes_api_init())
        if status != 0:
            raise HermesError(status, self.status_text(status))
        self._closed = False
        atexit.register(self.close)

    def _configure(self) -> None:
        self.lib.hermes_api_version_string.restype = ctypes.c_char_p
        self.lib.hermes_api_status_string.argtypes = [ctypes.c_int]
        self.lib.hermes_api_status_string.restype = ctypes.c_char_p

        self.lib.hermes_api_init.restype = ctypes.c_int
        self.lib.hermes_api_shutdown.restype = None

        self.lib.hermes_api_identity_generate_files.argtypes = [
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.POINTER(IdentitySummaryStruct),
        ]
        self.lib.hermes_api_identity_generate_files.restype = ctypes.c_int
        self.lib.hermes_api_identity_load_summary.argtypes = [ctypes.c_char_p, ctypes.POINTER(IdentitySummaryStruct)]
        self.lib.hermes_api_identity_load_summary.restype = ctypes.c_int
        self.lib.hermes_api_contact_load_summary.argtypes = [ctypes.c_char_p, ctypes.POINTER(IdentitySummaryStruct)]
        self.lib.hermes_api_contact_load_summary.restype = ctypes.c_int

        self.lib.hermes_api_create_message.argtypes = [
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_uint64,
            ctypes.c_uint32,
            ctypes.c_char_p,
            ctypes.POINTER(MessageSummaryStruct),
        ]
        self.lib.hermes_api_create_message.restype = ctypes.c_int
        self.lib.hermes_api_verify_envelope_file.argtypes = [
            ctypes.c_char_p,
            ctypes.c_uint64,
            ctypes.POINTER(MessageSummaryStruct),
        ]
        self.lib.hermes_api_verify_envelope_file.restype = ctypes.c_int
        self.lib.hermes_api_decrypt_envelope_file.argtypes = [
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_char),
            ctypes.POINTER(ctypes.c_size_t),
        ]
        self.lib.hermes_api_decrypt_envelope_file.restype = ctypes.c_int

        self.lib.hermes_api_import_bundle.argtypes = [
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_uint64,
            ctypes.POINTER(ctypes.c_size_t),
        ]
        self.lib.hermes_api_import_bundle.restype = ctypes.c_int
        self.lib.hermes_api_export_bundle.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64]
        self.lib.hermes_api_export_bundle.restype = ctypes.c_int
        self.lib.hermes_api_store_stats.argtypes = [
            ctypes.c_char_p,
            ctypes.c_uint64,
            ctypes.POINTER(StoreStatsStruct),
        ]
        self.lib.hermes_api_store_stats.restype = ctypes.c_int
        self.lib.hermes_api_store_list_inventory.argtypes = [
            ctypes.c_char_p,
            ctypes.POINTER(MessageSummaryStruct),
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t),
        ]
        self.lib.hermes_api_store_list_inventory.restype = ctypes.c_int
        self.lib.hermes_api_store_decrypt_message.argtypes = [
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_char),
            ctypes.POINTER(ctypes.c_size_t),
            ctypes.POINTER(MessageSummaryStruct),
        ]
        self.lib.hermes_api_store_decrypt_message.restype = ctypes.c_int
        self.lib.hermes_api_store_cleanup.argtypes = [ctypes.c_char_p, ctypes.c_uint64]
        self.lib.hermes_api_store_cleanup.restype = ctypes.c_int

        self.lib.hermes_api_relay_init.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
        self.lib.hermes_api_relay_init.restype = ctypes.c_int
        self.lib.hermes_api_relay_add_peer.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]
        self.lib.hermes_api_relay_add_peer.restype = ctypes.c_int
        self.lib.hermes_api_relay_list_peers.argtypes = [
            ctypes.c_char_p,
            ctypes.POINTER(PeerStruct),
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t),
        ]
        self.lib.hermes_api_relay_list_peers.restype = ctypes.c_int
        self.lib.hermes_api_relay_process_imports.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_size_t)]
        self.lib.hermes_api_relay_process_imports.restype = ctypes.c_int
        self.lib.hermes_api_relay_export_latest.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
        self.lib.hermes_api_relay_export_latest.restype = ctypes.c_int
        self.lib.hermes_api_relay_sync_once.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_size_t)]
        self.lib.hermes_api_relay_sync_once.restype = ctypes.c_int
        self.lib.hermes_api_relay_read_status.argtypes = [
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_char),
            ctypes.POINTER(ctypes.c_size_t),
        ]
        self.lib.hermes_api_relay_read_status.restype = ctypes.c_int

    def status_text(self, status_code: int) -> str:
        raw = self.lib.hermes_api_status_string(int(status_code))
        return raw.decode("utf-8", errors="replace") if raw else f"status {status_code}"

    def check(self, status_code: int) -> None:
        if int(status_code) != 0:
            raise HermesError(int(status_code), self.status_text(int(status_code)))

    def version(self) -> str:
        raw = self.lib.hermes_api_version_string()
        return raw.decode("utf-8", errors="replace") if raw else "Hermes Relay"

    def close(self) -> None:
        if not self._closed:
            self.lib.hermes_api_shutdown()
            self._closed = True


_binding: Optional[HermesBinding] = None


def get_binding() -> HermesBinding:
    global _binding
    if _binding is None:
        _binding = HermesBinding()
    return _binding
