#!/bin/bash
# setup-mac-shortcuts.sh — macOS-like shortcuts via keyd + globe key via hidraw
# Run with: sudo bash ~/setup-mac-shortcuts.sh

set -euo pipefail

if [[ $EUID -ne 0 ]]; then
    echo "Run with: sudo bash $0"
    exit 1
fi

REAL_USER="${SUDO_USER:-$USER}"
REAL_HOME=$(eval echo "~$REAL_USER")
REAL_UID=$(id -u "$REAL_USER")
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

run_as_user() {
    sudo -u "$REAL_USER" "$@"
}

run_user_systemctl() {
    sudo -u "$REAL_USER" \
        XDG_RUNTIME_DIR="/run/user/$REAL_UID" \
        DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$REAL_UID/bus" \
        systemctl --user "$@"
}

# Stop xremap if present
run_user_systemctl stop xremap 2>/dev/null || true
run_user_systemctl disable xremap 2>/dev/null || true

# ==========================================================
# 1. keyd config
# ==========================================================
echo "=== keyd ==="

cat > /etc/keyd/default.conf << 'EOF'
[ids]
*

[main]
leftmeta = overload(cmd, leftmeta)
rightmeta = overload(cmd, rightmeta)
leftalt = layer(option)
rightalt = layer(option)

[cmd:C]
# Copy/Paste/Cut: CUA shortcuts (Ctrl+Insert, Shift+Insert, Shift+Delete)
# These work universally in ALL apps (KDE, GTK, Firefox, VS Code, terminals).
# Unlike Ctrl+Shift+C which opens DevTools in Firefox, CUA has no conflicts.
# The custom macOS.keytab ensures Konsole doesn't intercept Insert+modifier.
c = C-insert
v = S-insert
x = S-delete

# Select All, Find, Close Tab, New Tab, New Window: Ctrl+Shift (2-modifier)
# In Konsole, 2-modifier shortcuts bypass the terminal's ShortcutOverride handler.
# For non-Konsole KDE apps, kdeglobals adds Ctrl+Shift+X as alternatives.
a = C-S-a
f = C-S-f
w = C-S-w
t = C-S-t
n = C-S-n

# App switching
tab = A-tab
grave = A-grave

# KRunner
space = A-space

# Quit
q = A-f4

# Cursor movement
left = home
right = end
up = C-home
down = C-end

# Cmd+Delete (Mac "delete" = backspace) → forward delete (trash files in Dolphin)
backspace = delete

# Tab switching
1 = A-1
2 = A-2
3 = A-3
4 = A-4
5 = A-5
6 = A-6
7 = A-7
8 = A-8
9 = A-9

# Everything else: Cmd+key → Ctrl+key via :C inheritance
# (e.g. Cmd+S → Ctrl+S = Save, Cmd+Z → Ctrl+Z = Undo)

[option:A]
left = C-left
right = C-right
backspace = C-backspace
delete = C-delete
EOF

systemctl enable --now keyd
systemctl restart keyd
echo "keyd configured."

# ==========================================================
# 2. udev rule for hidraw access (Globe key needs it)
# ==========================================================
echo "=== hidraw permissions ==="

cat > /etc/udev/rules.d/99-m5stack-hidraw.rules << 'EOF'
KERNEL=="hidraw*", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="1001", GROUP="input", MODE="0660"
EOF
udevadm control --reload-rules
udevadm trigger
chmod 660 /dev/hidraw0 2>/dev/null || true
chgrp input /dev/hidraw0 2>/dev/null || true
echo "hidraw access granted to input group."

# ==========================================================
# 3. Globe key handler (reads from hidraw, bypasses keyd grab)
# ==========================================================
echo "=== Globe key ==="

run_as_user mkdir -p "$REAL_HOME/.local/bin"

run_as_user tee "$REAL_HOME/.local/bin/globe-layout-switch" > /dev/null << 'PYEOF'
#!/usr/bin/env python3
"""Listen for Globe key via hidraw (bypasses keyd's exclusive grab) and switch KDE layout."""
import os, struct, subprocess, sys, glob, time

def find_m5stack_hidraw():
    for hidraw in sorted(glob.glob('/dev/hidraw*')):
        name_path = f'/sys/class/hidraw/{os.path.basename(hidraw)}/device/uevent'
        try:
            with open(name_path) as f:
                if 'M5Stack' in f.read():
                    return hidraw
        except OSError:
            pass
    return None

def switch_layout():
    subprocess.Popen([
        'busctl', '--user', 'call',
        'org.kde.keyboard', '/Layouts',
        'org.kde.KeyboardLayouts', 'switchToNextLayout'
    ])

def main():
    hidraw = find_m5stack_hidraw()
    if not hidraw:
        for _ in range(30):
            time.sleep(2)
            hidraw = find_m5stack_hidraw()
            if hidraw:
                break
        if not hidraw:
            print("M5Stack hidraw not found", file=sys.stderr, flush=True)
            sys.exit(1)

    fd = os.open(hidraw, os.O_RDONLY)
    prev_report5 = 0

    while True:
        try:
            data = os.read(fd, 64)
        except OSError:
            break
        if not data:
            break
        if data[0] == 5 and len(data) >= 2:
            val = data[1]
            if val != 0 and prev_report5 == 0:
                switch_layout()
            prev_report5 = val

if __name__ == '__main__':
    main()
PYEOF

chmod +x "$REAL_HOME/.local/bin/globe-layout-switch"

run_as_user mkdir -p "$REAL_HOME/.config/systemd/user"

run_as_user tee "$REAL_HOME/.config/systemd/user/globe-layout-switch.service" > /dev/null << SVCEOF
[Unit]
Description=Globe key layout switcher (hidraw)
After=graphical-session.target

[Service]
ExecStart=$REAL_HOME/.local/bin/globe-layout-switch
Restart=always
RestartSec=2

[Install]
WantedBy=default.target
SVCEOF

run_user_systemctl daemon-reload
run_user_systemctl enable --now globe-layout-switch
echo "Globe key service started."

# ==========================================================
# 4. Konsole profile + keytab
# ==========================================================
echo "=== Konsole profile ==="

run_as_user mkdir -p "$REAL_HOME/.local/share/konsole"

# Set default profile
run_as_user kwriteconfig6 --file "$REAL_HOME/.config/konsolerc" --group "Desktop Entry" --key "DefaultProfile" "macOS.profile"

# Remove stale [Shortcuts] section — KXMLGUI ignores it for session actions.
# Session shortcuts are stored in sessionui.rc ActionProperties (see step 5).
run_as_user kwriteconfig6 --file "$REAL_HOME/.config/konsolerc" --group "Shortcuts" --key "edit_copy" --delete 2>/dev/null || true
run_as_user kwriteconfig6 --file "$REAL_HOME/.config/konsolerc" --group "Shortcuts" --key "edit_paste" --delete 2>/dev/null || true
run_as_user kwriteconfig6 --file "$REAL_HOME/.config/konsolerc" --group "Shortcuts" --key "select-all" --delete 2>/dev/null || true
run_as_user kwriteconfig6 --file "$REAL_HOME/.config/konsolerc" --group "Shortcuts" --key "edit_find" --delete 2>/dev/null || true
run_as_user kwriteconfig6 --file "$REAL_HOME/.config/konsolerc" --group "Shortcuts" --key "close-session" --delete 2>/dev/null || true
run_as_user kwriteconfig6 --file "$REAL_HOME/.config/konsolerc" --group "Shortcuts" --key "new-tab" --delete 2>/dev/null || true
run_as_user kwriteconfig6 --file "$REAL_HOME/.config/konsolerc" --group "Shortcuts" --key "new-window" --delete 2>/dev/null || true
run_as_user kwriteconfig6 --file "$REAL_HOME/.config/konsolerc" --group "Shortcuts" --key "edit_select_all" --delete 2>/dev/null || true
run_as_user kwriteconfig6 --file "$REAL_HOME/.config/konsolerc" --group "Shortcuts" --key "close-window" --delete 2>/dev/null || true
run_as_user kwriteconfig6 --file "$REAL_HOME/.config/konsolerc" --group "Shortcut Schemes" --key "Current Scheme" --delete 2>/dev/null || true

# Custom keytab: Insert+AnyMod removed so CUA shortcuts (Ctrl+Insert, Shift+Insert)
# reach Qt's shortcut system instead of being consumed by terminal emulation.
if [[ -f "$SCRIPT_DIR/macOS.keytab" ]]; then
    run_as_user cp "$SCRIPT_DIR/macOS.keytab" "$REAL_HOME/.local/share/konsole/macOS.keytab"
    echo "Installed macOS.keytab from repo."
else
    echo "WARNING: macOS.keytab not found next to setup.sh — Konsole copy/paste may not work."
fi

# macOS profile pointing to custom keytab
run_as_user tee "$REAL_HOME/.local/share/konsole/macOS.profile" > /dev/null << 'PROFEOF'
[General]
Name=macOS
Parent=FALLBACK/

[Keyboard]
KeyBindings=macOS
PROFEOF

echo "Konsole profile configured."

# ==========================================================
# 5. Konsole KXMLGUI: sessionui.rc (ActionProperties)
#    This is the ONLY mechanism that works for session-level shortcuts.
#    The factory reads ActionProperties from here, NOT from konsolerc.
# ==========================================================
echo "=== Konsole sessionui.rc ==="

run_as_user mkdir -p "$REAL_HOME/.local/share/kxmlgui5/konsole"

# Version MUST match the embedded version in Konsole binary (36 for v25.12.2).
# ActionProperties:
#   - select-all: has NO default shortcut in code, so we assign Ctrl+Shift+A
#   - monitor-activity: defaults to Ctrl+Shift+A, must be unmapped to avoid conflict
# All other actions (copy, paste, find, close-session) keep their code defaults:
#   - copy: Ctrl+Shift+C; Ctrl+Insert  (CUA Ctrl+Insert from keyd matches this)
#   - paste: Ctrl+Shift+V; Shift+Insert (CUA Shift+Insert from keyd matches this)
#   - find: Ctrl+Shift+F
#   - close-session: Ctrl+Shift+W
# IMPORTANT: Do NOT add ActionProperties for copy/paste/find/close — that would
# REPLACE the defaults and remove the CUA alternatives (Ctrl+Insert, Shift+Insert).
# Full menu structure is required or context menu breaks.
run_as_user tee "$REAL_HOME/.local/share/kxmlgui5/konsole/sessionui.rc" > /dev/null << 'XMLEOF'
<?xml version="1.0"?>
<!DOCTYPE gui SYSTEM "kpartgui.dtd">

<gui name="session" version="36">
    <ActionProperties scheme="Default">
        <Action name="select-all" shortcut="Ctrl+Shift+A"/>
        <Action name="monitor-activity" shortcut=""/>
    </ActionProperties>
    <MenuBar>
        <Menu name="file">
            <Action name="file_save_as" group="session-operations"/>
			<Action name="file-autosave" group="session-operations"/>
			<Action name="stop-autosave" group="session-operations"/>
            <Separator group="session-operations"/>
            <Action name="file_print" group="session-operations"/>
            <Separator group="session-operations"/>
            <Action name="open-browser" group="session-operations"/>
            <Action name="close-session" group="session-tab-operations"/>
        </Menu>
        <Menu name="edit">
            <Action name="edit_copy" group="session-edit-operations"/>
            <Action name="edit_paste" group="session-edit-operations"/>
            <Separator group="session-edit-operations"/>
            <Action name="select-all" group="session-edit-operations"/>
            <Action name="select-mode" group="session-edit-operations"/>
            <Separator group="session-edit-operations"/>
            <Action name="copy-input-to" group="session-edit-operations"/>
            <Action name="send-signal" group="session-edit-operations"/>
            <Action name="rename-session" group="session-edit-operations"/>
            <Action name="zmodem-upload" group="session-edit-operations"/>
            <Separator group="session-edit-operations"/>
            <Action name="edit_find" group="session-edit-operations"/>
            <Action name="edit_find_next" group="session-edit-operations"/>
            <Action name="edit_find_prev" group="session-edit-operations"/>
        </Menu>
        <Menu name="view">
            <Action name="monitor-once" group="session-view-operations"/>
            <Action name="monitor-prompt" group="session-view-operations"/>
            <Action name="monitor-silence" group="session-view-operations"/>
            <Action name="monitor-activity" group="session-view-operations"/>
            <Action name="monitor-process-finish" group="session-view-operations"/>
            <Separator group="session-view-operations"/>
            <Action name="view-readonly" group="session-view-operations"/>
            <Action name="allow-mouse-tracking" group="session-view-operations"/>
            <Separator group="session-view-operations"/>
            <Action name="enlarge-font" group="session-view-operations"/>
            <Action name="reset-font-size" group="session-view-operations"/>
            <Action name="shrink-font" group="session-view-operations"/>
            <Action name="set-encoding" group="session-view-operations"/>
            <Separator group="session-view-operations"/>
            <Action name="clear-history" group="session-view-operations"/>
            <Action name="clear-history-and-reset" group="session-view-operations"/>
        </Menu>
        <Menu name="settings">
            <Action name="edit-current-profile" group="session-settings"/>
            <Action name="switch-profile" group="session-settings"/>
        </Menu>
    </MenuBar>
    <Menu name="session-popup-menu">
        <Action name="edit_copy_contextmenu"/>
        <Action name="edit_copy_contextmenu_in"/>
        <Action name="edit_copy_contextmenu_out"/>
        <Action name="edit_copy_contextmenu_in_out"/>
        <Action name="edit_paste"/>
        <Action name="web-search"/>
        <Action name="open-browser"/>
        <Separator/>
        <Menu name="view-split"><text>Split View</text>
            <Action name="split-view-left-right"/>
            <Action name="split-view-top-bottom"/>
        </Menu>
        <Separator/>
        <Action name="set-encoding"/>
        <Action name="clear-history"/>
        <Action name="adjust-history"/>
        <Separator/>
        <Action name="view-readonly" />
        <Action name="allow-mouse-tracking" />
        <Separator/>
        <Action name="switch-profile"/>
        <Action name="edit-current-profile"/>
    </Menu>
    <ToolBar name="sessionToolbar">
        <text>Session Toolbar</text>
        <index>1</index>
        <Spacer/>
        <Action name="edit_copy" />
        <Action name="edit_paste" />
        <Action name="edit_find" />
        <Action name="hamburger_menu"/>
    </ToolBar>
</gui>
XMLEOF

echo "sessionui.rc configured."

# ==========================================================
# 6. KDE global StandardShortcuts
#    Adds Ctrl+Shift variants for non-Konsole KDE apps (Kate, Dolphin, etc.)
#    Copy/Paste/Cut are NOT needed here — CUA (Ctrl+Insert etc.) works natively.
# ==========================================================
echo "=== KDE global shortcuts ==="

run_as_user kwriteconfig6 --file "$REAL_HOME/.config/kdeglobals" --group "StandardShortcuts" --key "SelectAll" "Ctrl+A; Ctrl+Shift+A"
run_as_user kwriteconfig6 --file "$REAL_HOME/.config/kdeglobals" --group "StandardShortcuts" --key "Find" "Ctrl+F; Ctrl+Shift+F"
run_as_user kwriteconfig6 --file "$REAL_HOME/.config/kdeglobals" --group "StandardShortcuts" --key "New" "Ctrl+N; Ctrl+Shift+N"
run_as_user kwriteconfig6 --file "$REAL_HOME/.config/kdeglobals" --group "StandardShortcuts" --key "Close" "Ctrl+W; Ctrl+Shift+W"
run_as_user kwriteconfig6 --file "$REAL_HOME/.config/kdeglobals" --group "StandardShortcuts" --key "Undo" "Ctrl+Z"
run_as_user kwriteconfig6 --file "$REAL_HOME/.config/kdeglobals" --group "StandardShortcuts" --key "Redo" "Ctrl+Shift+Z"
run_as_user kwriteconfig6 --file "$REAL_HOME/.config/kdeglobals" --group "StandardShortcuts" --key "Save" "Ctrl+S"

# Remove Copy/Paste/Cut overrides (CUA handles these universally)
run_as_user kwriteconfig6 --file "$REAL_HOME/.config/kdeglobals" --group "StandardShortcuts" --key "Copy" --delete 2>/dev/null || true
run_as_user kwriteconfig6 --file "$REAL_HOME/.config/kdeglobals" --group "StandardShortcuts" --key "Paste" --delete 2>/dev/null || true
run_as_user kwriteconfig6 --file "$REAL_HOME/.config/kdeglobals" --group "StandardShortcuts" --key "Cut" --delete 2>/dev/null || true

echo "KDE global shortcuts configured."

# ==========================================================
# Done
# ==========================================================
echo ""
echo "=== DONE ==="
echo ""
echo "IMPORTANT: Quit ALL Konsole windows (Cmd+Q) and reopen."
echo ""
echo "Shortcuts in Konsole (Cmd on Mac keyboard):"
echo "  Cmd+C = Copy (select text first!)    Cmd+V = Paste"
echo "  Cmd+A = Select All   Cmd+F = Find"
echo "  Cmd+T = New Tab   Cmd+W = Close Tab   Cmd+N = New Window"
echo ""
echo "Physical Ctrl key still sends raw Ctrl to terminal:"
echo "  Ctrl+C = SIGINT   Ctrl+D = EOF   Ctrl+Z = Suspend"
