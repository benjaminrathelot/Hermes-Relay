from __future__ import annotations

import os
import signal
import subprocess
import time
from dataclasses import dataclass
from typing import Optional

from .core import HermesClient, HermesError


@dataclass
class RelayHandle:
    process: subprocess.Popen
    root: str


class RelaySupervisor:
    def __init__(self, client: Optional[HermesClient] = None):
        self.client = client or HermesClient()
        self._handles: dict[str, RelayHandle] = {}

    @property
    def cli_path(self) -> str:
        cli_path = self.client.native_cli_path
        if not cli_path:
            raise FileNotFoundError("unable to locate hermes-cli; set HERMESRELAY_CLI explicitly")
        return cli_path

    def start(self, root: str, *, log_stderr: bool = False) -> RelayHandle:
        if self.running(root):
            raise HermesError(-1, f"relay already running for {root}")
        args = [self.cli_path, "relay-run", "--root", root]
        if log_stderr:
            args.append("--log-stderr")
        process = subprocess.Popen(
            args,
            stdout=subprocess.DEVNULL,
            stderr=None if log_stderr else subprocess.DEVNULL,
        )
        time.sleep(0.35)
        if process.poll() is not None:
            raise HermesError(-1, f"relay failed to start for {root}")
        handle = RelayHandle(process=process, root=root)
        self._handles[root] = handle
        return handle

    def stop(self, root: str) -> None:
        handle = self._handles.get(root)
        if handle:
            if handle.process.poll() is None:
                handle.process.terminate()
                handle.process.wait(timeout=15)
            self._handles.pop(root, None)
            return
        status = self.status(root)
        if not status or "pid" not in status:
            raise HermesError(-1, "relay status does not expose a pid")
        pid = int(status["pid"])
        os.kill(pid, signal.SIGTERM)
        for _ in range(20):
            time.sleep(0.25)
            fresh = self.status(root)
            if not fresh or fresh.get("state") == "stopped":
                return

    def running(self, root: str) -> bool:
        handle = self._handles.get(root)
        if handle:
            return handle.process.poll() is None
        status = self.status(root)
        return bool(status and status.get("state") == "running")

    def status(self, root: str) -> dict | None:
        try:
            import json

            return json.loads(self.client.relay_status(root))
        except Exception:
            return None
