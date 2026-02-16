# macOS Shortcuts on Fedora KDE (Apple Keyboard + M5Stack KVM)

Setup guide for macOS-like keyboard shortcuts on Fedora KDE Wayland.
Apple Magic Keyboard connected through M5Stack AtomS3 KVM.

## Quick Setup

```bash
sudo bash ~/setup-mac-shortcuts.sh
```

Then **quit all Konsole windows** and reopen.

## What Works

### Cmd shortcuts (hold Cmd on Apple keyboard)

| Shortcut | Action | Notes |
|----------|--------|-------|
| Cmd+C | Copy | Works in ALL apps including Konsole |
| Cmd+V | Paste | Works in ALL apps including Konsole |
| Cmd+X | Cut | Works in non-terminal apps |
| Cmd+A | Select All | Works in ALL apps including Konsole |
| Cmd+F | Find | |
| Cmd+T | New Tab | |
| Cmd+W | Close Tab | |
| Cmd+N | New Window | |
| Cmd+Q | Quit App | |
| Cmd+S | Save | |
| Cmd+Z | Undo | |
| Cmd+Tab | Switch App | |
| Cmd+\` | Switch Window | |
| Cmd+Space | KRunner | |
| Cmd+Left/Right | Home/End | |
| Cmd+Up/Down | Doc Start/End | |
| Cmd+Delete | Forward Delete | Trash files in Dolphin |
| Cmd+1..9 | Switch Tab | |

### Option shortcuts (hold Option on Apple keyboard)

| Shortcut | Action |
|----------|--------|
| Option+Left/Right | Word navigation |
| Option+Backspace | Delete word back |
| Option+Delete | Delete word forward |

### Globe key

Press Globe key to switch keyboard layout (US <-> Russian).

### Physical Ctrl key in terminal

| Shortcut | Action |
|----------|--------|
| Ctrl+C | SIGINT (interrupt) |
| Ctrl+D | EOF |
| Ctrl+Z | Suspend process |
| Ctrl+L | Clear screen |
| Ctrl+R | Reverse search |

## Architecture

### keyd (kernel-level key remapper)

Config: `/etc/keyd/default.conf`

- `leftmeta = overload(cmd, leftmeta)` — hold for Cmd layer, tap for Meta
- `[cmd:C]` layer inherits Ctrl for unmapped keys (Cmd+S -> Ctrl+S)
- Copy/Paste/Cut use **CUA shortcuts** (Ctrl+Insert, Shift+Insert, Shift+Delete) — universal across all apps, no conflicts with Firefox DevTools or VS Code
- Select All, Find, Close, New Tab use Ctrl+Shift — these are 2-modifier combos that bypass Konsole's terminal ShortcutOverride

### Konsole terminal

Konsole's terminal emulation intercepts ALL single-modifier shortcuts (Ctrl+C, Ctrl+V, etc.) and sends them as control characters. Only 2-modifier shortcuts (Ctrl+Shift+X) pass through to Qt's action system.

CUA shortcuts (Ctrl+Insert, Shift+Insert) also pass through because the custom keytab removes the `Insert+AnyMod` entry that would otherwise convert them to escape sequences.

Key files:
- `~/.local/share/konsole/macOS.keytab` — custom keytab (Insert+AnyMod removed)
- `~/.local/share/konsole/macOS.profile` — profile pointing to custom keytab
- `~/.local/share/kxmlgui5/konsole/sessionui.rc` — KXMLGUI shortcut overrides

**Important:** Session-level shortcuts (copy, paste, select-all) are managed by KXMLGUI ActionProperties in `sessionui.rc`, NOT by `konsolerc [Shortcuts]`. The KXMLGUI factory never calls `readSettings()` — konsolerc entries for session actions are ignored.

The sessionui.rc only overrides:
- `select-all` = Ctrl+Shift+A (has no default shortcut in code)
- `monitor-activity` = unmapped (default Ctrl+Shift+A conflicts with select-all)

All other actions (copy, paste, find, close) use code defaults which include both Ctrl+Shift and CUA alternatives.

### Globe key (layout switching)

- `~/.local/bin/globe-layout-switch` — Python script reading from `/dev/hidraw*`
- Bypasses keyd's EVIOCGRAB by using raw HID interface
- Only triggers on HID Report ID 5 (Apple Globe/fn key)
- Runs as systemd user service: `globe-layout-switch.service`

### KDE global shortcuts

`~/.config/kdeglobals [StandardShortcuts]` adds Ctrl+Shift+A/F/W/N as alternatives for SelectAll/Find/Close/New in non-Konsole KDE apps (Kate, Dolphin, etc.).

## Troubleshooting

### Shortcuts don't work in Konsole

1. Make sure you quit ALL Konsole windows and reopened (KXMLGUI is loaded at startup)
2. Check keyd is running: `systemctl status keyd`
3. Check the profile is active: the tab title area should show "macOS" profile
4. For copy: you must select text first (the copy action is disabled without selection)

### Verify keyd output

```bash
sudo keyd monitor
```

Press Cmd+C — should show Ctrl+Insert being sent.

### Verify Konsole keytab

```bash
# Should show "macOS" (matching the keytab filename)
qdbus6 org.kde.konsole-$(pgrep -n konsole) /Sessions/1 org.kde.konsole.Session.keyBindings
```

### Globe key not working

```bash
systemctl --user status globe-layout-switch
ls -la /dev/hidraw*
```

## Files

| File | Purpose |
|------|---------|
| `~/setup-mac-shortcuts.sh` | Master setup script (run with sudo) |
| `/etc/keyd/default.conf` | keyd key remapping config |
| `~/.local/share/konsole/macOS.keytab` | Custom Konsole keytab |
| `~/.local/share/konsole/macOS.profile` | Konsole profile |
| `~/.local/share/kxmlgui5/konsole/sessionui.rc` | KXMLGUI shortcut overrides |
| `~/.config/kdeglobals` | KDE StandardShortcuts |
| `~/.config/konsolerc` | Konsole config (profile selection only) |
| `~/.local/bin/globe-layout-switch` | Globe key handler |
| `~/.config/systemd/user/globe-layout-switch.service` | Globe key systemd service |
| `/etc/udev/rules.d/99-m5stack-hidraw.rules` | hidraw permissions for Globe key |
