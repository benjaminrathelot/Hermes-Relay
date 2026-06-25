from __future__ import annotations

import argparse
import json
import sys
import time
import webbrowser

from .core import HermesClient
from .web_local import LocalWebApp
from .webview_app import run_webview
from .workspace import default_workspace_root


def main() -> int:
    default_root = default_workspace_root()
    parser = argparse.ArgumentParser(
        prog="hermes-desktop",
        description=(
            "Hermes Relay Desktop is a local operator console for the Hermes Relay C core.\n"
            "It manages identities, contacts, short encrypted messages, stores, and integrated relays."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("info", help="Show the resolved native library and CLI paths")

    serve = sub.add_parser("serve-ui", help="Start the local-only web interface")
    serve.add_argument("--host", default="127.0.0.1")
    serve.add_argument("--port", default=8765, type=int)
    serve.add_argument("--workspace-root", default=str(default_root))
    serve.add_argument("--open-browser", action="store_true")

    webview = sub.add_parser("open-webview", help="Start the local UI in pywebview")
    webview.add_argument("--host", default="127.0.0.1")
    webview.add_argument("--port", default=8765, type=int)
    webview.add_argument("--title", default="Hermes Relay Desktop")
    webview.add_argument("--workspace-root", default=str(default_root))

    args = parser.parse_args()

    if args.command == "info":
        client = HermesClient()
        payload = client.info()
        payload["default_workspace_root"] = str(default_root)
        print(json.dumps(payload, indent=2))
        return 0

    if args.command == "serve-ui":
        app = LocalWebApp(host=args.host, port=args.port, workspace_root=args.workspace_root)
        app.start()
        print("Hermes Relay Desktop")
        print(f"  url: {app.url}")
        print("  mode: browser shell")
        print(f"  workspace: {args.workspace_root}")
        print("  note: loopback-only control surface for crisis messaging operations")
        if args.open_browser:
            webbrowser.open(app.url)
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            app.stop()
            return 0

    if args.command == "open-webview":
        run_webview(host=args.host, port=args.port, title=args.title, workspace_root=args.workspace_root)
        return 0

    return 1


if __name__ == "__main__":
    sys.exit(main())
