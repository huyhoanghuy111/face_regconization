#!/usr/bin/env bash
# Cai phim tat Control + Option + K va bieu tuong tren Desktop de mo may cham cong.
#   bash scripts/hotkey.sh install
#   bash scripts/hotkey.sh uninstall
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LABEL=local.face.hotkey
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
DOMAIN="gui/$(id -u)"
DESKTOP_LINK="$HOME/Desktop/Máy chấm công.app"

stop_agent() {
    launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
}

case "${1:-}" in
    install)
        stop_agent
        mkdir -p "$(dirname "$PLIST")"
        cat >"$PLIST" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>$LABEL</string>
    <key>ProgramArguments</key>
    <array>
        <string>$ROOT/build/face_hotkey</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>StandardErrorPath</key>
    <string>/tmp/face_hotkey.log</string>
</dict>
</plist>
PLIST
        launchctl bootstrap "$DOMAIN" "$PLIST"
        ln -sfn "$ROOT/build/face_kiosk.app" "$DESKTOP_LINK"
        echo "Da cai: Control + Option + K, va bieu tuong tren Desktop"
        ;;
    uninstall)
        stop_agent
        rm -f "$PLIST" "$DESKTOP_LINK"
        echo "Da go phim tat va bieu tuong tren Desktop"
        ;;
    *)
        echo "Cach dung: bash scripts/hotkey.sh install|uninstall" >&2
        exit 1
        ;;
esac
