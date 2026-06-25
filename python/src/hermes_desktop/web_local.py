from __future__ import annotations

import json
import threading
import time
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from importlib import resources
from typing import Any
from urllib.parse import urlparse

from .binding import HermesError
from .core import HermesClient
from .network_status import collect_network_snapshot
from .relay_process import RelaySupervisor
from .workspace import HermesWorkspace, default_workspace_root


class LocalWebApp:
    max_json_body = 262144

    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 8765,
        client: HermesClient | None = None,
        workspace_root: str | None = None,
    ):
        self.host = host
        self.port = port
        self.client = client or HermesClient()
        self.workspace = HermesWorkspace(workspace_root or default_workspace_root(), self.client)
        self.supervisor = RelaySupervisor(self.client)
        self.httpd: ThreadingHTTPServer | None = None
        self.thread: threading.Thread | None = None

    def start(self) -> None:
        self.workspace.bootstrap()
        app = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, format: str, *args: Any) -> None:
                return

            def _send_json(self, status_code: int, payload: dict[str, Any]) -> None:
                raw = json.dumps(payload).encode("utf-8")
                self.send_response(status_code)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Cache-Control", "no-store")
                self.send_header("Content-Length", str(len(raw)))
                self.end_headers()
                self.wfile.write(raw)

            def _send_html(self, html: str) -> None:
                raw = html.encode("utf-8")
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Cache-Control", "no-store")
                self.send_header("Content-Length", str(len(raw)))
                self.end_headers()
                self.wfile.write(raw)

            def _read_json(self) -> dict[str, Any]:
                length = int(self.headers.get("Content-Length", "0"))
                if length < 0 or length > app.max_json_body:
                    raise HermesError(-1, "request body too large")
                raw = self.rfile.read(length) if length > 0 else b"{}"
                return json.loads(raw.decode("utf-8"))

            def do_GET(self) -> None:
                parsed = urlparse(self.path)
                if parsed.path == "/":
                    html = resources.files("hermes_desktop.assets").joinpath("index.html").read_text(encoding="utf-8")
                    self._send_html(html)
                    return
                if parsed.path == "/api/info":
                    self._send_json(HTTPStatus.OK, {"ok": True, "result": app.runtime_info()})
                    return
                if parsed.path == "/api/overview":
                    self._send_json(HTTPStatus.OK, {"ok": True, "result": app.workspace_overview()})
                    return
                if parsed.path == "/api/network":
                    self._send_json(HTTPStatus.OK, {"ok": True, "result": collect_network_snapshot()})
                    return
                self._send_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "not found"})

            def do_POST(self) -> None:
                parsed = urlparse(self.path)
                try:
                    payload = self._read_json()
                    if parsed.path == "/api/workspace/bootstrap":
                        self._send_json(HTTPStatus.OK, {"ok": True, "result": app.workspace_overview()})
                        return
                    if parsed.path == "/api/workspace/identity-generate":
                        self._send_json(
                            HTTPStatus.OK,
                            {"ok": True, "result": app.workspace.generate_identity(payload["alias"])},
                        )
                        return
                    if parsed.path == "/api/workspace/contact-import":
                        self._send_json(
                            HTTPStatus.OK,
                            {
                                "ok": True,
                                "result": app.workspace.import_contact(
                                    payload["source_path"],
                                    payload.get("alias"),
                                ),
                            },
                        )
                        return
                    if parsed.path == "/api/workspace/message-create":
                        self._send_json(
                            HTTPStatus.OK,
                            {
                                "ok": True,
                                "result": app.workspace.create_message(
                                    payload["sender_alias"],
                                    payload["recipient_alias"],
                                    payload["store_name"],
                                    payload["message"],
                                    int(payload.get("ttl_seconds", 0)),
                                    int(payload.get("pow_difficulty", 28)),
                                    payload.get("export_name"),
                                ),
                            },
                        )
                        return
                    if parsed.path == "/api/workspace/store-list":
                        self._send_json(
                            HTTPStatus.OK,
                            {"ok": True, "result": app.workspace.list_store_messages(payload["store_name"])},
                        )
                        return
                    if parsed.path == "/api/workspace/store-read":
                        self._send_json(
                            HTTPStatus.OK,
                            {
                                "ok": True,
                                "result": app.workspace.read_store_message(
                                    payload["store_name"],
                                    payload["identity_alias"],
                                    payload["envelope_id_hex"],
                                ),
                            },
                        )
                        return
                    if parsed.path == "/api/workspace/store-cleanup":
                        self._send_json(
                            HTTPStatus.OK,
                            {"ok": True, "result": app.workspace.cleanup_store(payload["store_name"])},
                        )
                        return
                    if parsed.path == "/api/workspace/bundle-import":
                        self._send_json(
                            HTTPStatus.OK,
                            {
                                "ok": True,
                                "result": app.workspace.import_bundle(
                                    payload["store_name"],
                                    payload["source_path"],
                                ),
                            },
                        )
                        return
                    if parsed.path == "/api/workspace/bundle-export":
                        self._send_json(
                            HTTPStatus.OK,
                            {
                                "ok": True,
                                "result": app.workspace.export_bundle(
                                    payload["store_name"],
                                    payload["output_name"],
                                ),
                            },
                        )
                        return
                    if parsed.path == "/api/workspace/relay-init":
                        self._send_json(
                            HTTPStatus.OK,
                            {
                                "ok": True,
                                "result": app.workspace.ensure_relay(
                                    payload["relay_name"],
                                    payload["listen_addr"],
                                ),
                            },
                        )
                        return
                    if parsed.path == "/api/workspace/relay-status":
                        self._send_json(
                            HTTPStatus.OK,
                            {"ok": True, "result": app.relay_status(payload["relay_name"])},
                        )
                        return
                    if parsed.path == "/api/workspace/relay-peers":
                        self._send_json(
                            HTTPStatus.OK,
                            {"ok": True, "result": app.workspace.relay_peers(payload["relay_name"])},
                        )
                        return
                    if parsed.path == "/api/workspace/relay-add-peer":
                        self._send_json(
                            HTTPStatus.OK,
                            {
                                "ok": True,
                                "result": app.workspace.relay_add_peer(
                                    payload["relay_name"],
                                    payload["peer_addr"],
                                    payload.get("alias"),
                                ),
                            },
                        )
                        return
                    if parsed.path == "/api/workspace/relay-sync":
                        self._send_json(
                            HTTPStatus.OK,
                            {"ok": True, "result": app.workspace.relay_sync_once(payload["relay_name"])},
                        )
                        return
                    if parsed.path == "/api/workspace/relay-imports":
                        self._send_json(
                            HTTPStatus.OK,
                            {"ok": True, "result": app.workspace.relay_process_imports(payload["relay_name"])},
                        )
                        return
                    if parsed.path == "/api/workspace/relay-export":
                        self._send_json(
                            HTTPStatus.OK,
                            {
                                "ok": True,
                                "result": app.workspace.relay_export_latest(
                                    payload["relay_name"],
                                    payload["output_name"],
                                ),
                            },
                        )
                        return
                    if parsed.path == "/api/workspace/relay-drop":
                        self._send_json(
                            HTTPStatus.OK,
                            {
                                "ok": True,
                                "result": app.workspace.relay_drop_file(
                                    payload["relay_name"],
                                    payload["source_path"],
                                ),
                            },
                        )
                        return
                    if parsed.path == "/api/workspace/relay-start":
                        self._send_json(
                            HTTPStatus.OK,
                            {"ok": True, "result": app.start_relay(payload["relay_name"])},
                        )
                        return
                    if parsed.path == "/api/workspace/relay-stop":
                        self._send_json(
                            HTTPStatus.OK,
                            {"ok": True, "result": app.stop_relay(payload["relay_name"])},
                        )
                        return
                except KeyError as exc:
                    self._send_json(HTTPStatus.BAD_REQUEST, {"ok": False, "error": f"missing field: {exc.args[0]}"})
                    return
                except ValueError as exc:
                    self._send_json(HTTPStatus.BAD_REQUEST, {"ok": False, "error": str(exc)})
                    return
                except HermesError as exc:
                    self._send_json(
                        HTTPStatus.BAD_REQUEST,
                        {"ok": False, "error": str(exc), "status_code": exc.status_code},
                    )
                    return
                except json.JSONDecodeError:
                    self._send_json(HTTPStatus.BAD_REQUEST, {"ok": False, "error": "invalid json"})
                    return
                self._send_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "not found"})

        self.httpd = ThreadingHTTPServer((self.host, self.port), Handler)
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    def stop(self) -> None:
        if self.httpd:
            self.httpd.shutdown()
            self.httpd.server_close()
            self.httpd = None
        if self.thread:
            self.thread.join(timeout=5)
            self.thread = None

    @property
    def url(self) -> str:
        return f"http://{self.host}:{self.port}/"

    def runtime_info(self) -> dict[str, Any]:
        result = self.client.info()
        result["workspace_root"] = str(self.workspace.root)
        result["ui_url"] = self.url
        return result

    def workspace_overview(self) -> dict[str, Any]:
        overview = self.workspace.overview()
        overview["runtime"] = self.runtime_info()
        overview["network"] = collect_network_snapshot()
        relays: list[dict[str, Any]] = []
        for relay in overview["relays"]:
            relay_state = self.workspace.relay_status(relay["name"])
            relay_state["running_local_shell"] = self.supervisor.running(str(self.workspace.relay_root(relay["name"])))
            relays.append(relay_state)
        overview["relays"] = relays
        return overview

    def relay_status(self, relay_name: str) -> dict[str, Any]:
        result = self.workspace.relay_status(relay_name)
        result["running_local_shell"] = self.supervisor.running(str(self.workspace.relay_root(relay_name)))
        return result

    def start_relay(self, relay_name: str) -> dict[str, Any]:
        root = str(self.workspace.relay_root(relay_name))
        self.supervisor.start(root)
        for _ in range(12):
            status = self.relay_status(relay_name)
            if status.get("state") == "running":
                return status
            time.sleep(0.25)
        return self.relay_status(relay_name)

    def stop_relay(self, relay_name: str) -> dict[str, Any]:
        root = str(self.workspace.relay_root(relay_name))
        self.supervisor.stop(root)
        for _ in range(12):
            status = self.relay_status(relay_name)
            if status.get("state") != "running":
                return status
            time.sleep(0.25)
        return self.relay_status(relay_name)
