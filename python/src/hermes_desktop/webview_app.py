from __future__ import annotations

from .web_local import LocalWebApp


def run_webview(
    host: str = "127.0.0.1",
    port: int = 8765,
    title: str = "Hermes Relay Desktop",
    workspace_root: str | None = None,
) -> None:
    try:
        import webview
    except ImportError as exc:
        raise RuntimeError("pywebview is not installed; install hermes-desktop[desktop]") from exc

    app = LocalWebApp(host=host, port=port, workspace_root=workspace_root)
    app.start()
    window = webview.create_window(title, app.url)
    try:
        webview.start()
    finally:
        app.stop()
