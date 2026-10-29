# Bundled: Soft Brightness Plus (GNOME Shell extension)

This folder is an **unmodified copy** of the Soft Brightness Plus extension,
bundled so ASUS Control Center can offer OLED dimming without a separate
download. The Display page's "Turn on OLED dimming" button copies it to
`~/.local/share/gnome-shell/extensions/soft-brightness-plus@joelkitching.com/`,
compiles its settings schema, and enables it for your user.

- Upstream: https://github.com/jkitching/soft-brightness-plus
- Revision: `f24df9ad743c723abbad19ad81dfc1a12ff0af11` (extension version 52)
- GNOME Shell: 45 – 50
- Authors: Philippe Troin (original "Soft brightness"), Joel Kitching (fork)
- License: GPL-3.0-or-later, see `LICENSE` in this folder (same license as
  ASUS Control Center)

Files: `extension.js`, `prefs.js`, `cursor.js`, `logger.js`, `utils.js`,
`schemas/`, `dbus-interfaces/` as shipped upstream; `metadata.json` is
upstream's `src/metadata.json.in` with the version (52) and revision filled
in, exactly as upstream's own `make` does. Translations are not bundled.

How it dims: GNOME Shell draws a black, partly transparent layer over the
chosen monitors. On an OLED panel this darkens the picture below the
panel's lowest hardware level without lowering the drive current, so it
adds no PWM flicker. ASUS Control Center only writes the extension's
`current-brightness` key (and sets `monitors` to `built-in` on first
install); everything else stays the extension's own defaults.

To update: copy the same files from a newer upstream release and update
the revision above.
