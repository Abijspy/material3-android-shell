-- ─────────────────────────────────────────────────────────────────────────────
--  Material3UI Shell: Session autostart
--  The shell itself is started from shell.lua.
-- ─────────────────────────────────────────────────────────────────────────────

hl.on("hyprland.start", function()
	hl.exec_cmd("dbus-update-activation-environment --systemd --all")
	hl.exec_cmd("systemctl --user import-environment WAYLAND_DISPLAY XDG_CURRENT_DESKTOP")
	hl.exec_cmd("gnome-keyring-daemon --start --components=secrets")
	hl.exec_cmd("hypridle")

	-- Clipboard history for Spotlight (SUPER+V)
	hl.exec_cmd("wl-paste --type text --watch cliphist store")
	hl.exec_cmd("wl-paste --type image --watch cliphist store")

	-- GTK follows the generated palette through adw-gtk3
	hl.exec_cmd("gsettings set org.gnome.desktop.interface gtk-theme 'adw-gtk3'")
	hl.exec_cmd("gsettings set org.gnome.desktop.interface color-scheme 'prefer-dark'")
end)
