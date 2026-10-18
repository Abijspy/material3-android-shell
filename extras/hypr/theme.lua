-- ─────────────────────────────────────────────────────────────────────────────
--  Material3UI Shell: Theme & Environment Configuration
--  This file provides the necessary environment variables for Qt/GTK apps
--  to follow the shell's dynamic colors.
-- ─────────────────────────────────────────────────────────────────────────────

-- --- Theme Engines ---
hl.env("QT_QPA_PLATFORMTHEME", "kde")
hl.env("QT_QPA_PLATFORM", "wayland;xcb")
hl.env("XDG_MENU_PREFIX", "plasma-")

-- --- Toolkit Backends ---
hl.env("GDK_BACKEND", "wayland,x11,*")
hl.env("SDL_VIDEODRIVER", "wayland")
hl.env("CLUTTER_BACKEND", "wayland")

-- --- XDG Desktop Portal ---
hl.env("XDG_CURRENT_DESKTOP", "Hyprland")
hl.env("XDG_SESSION_TYPE", "wayland")
hl.env("XDG_SESSION_DESKTOP", "Hyprland")

-- --- Initial Theming Hook ---
hl.on("hyprland.start", function()
    hl.exec_cmd("gsettings set org.gnome.desktop.interface gtk-theme 'adw-gtk3'")
    hl.exec_cmd("gsettings set org.gnome.desktop.interface color-scheme 'prefer-dark'")
    
    -- --- Dbus Activation Sync ---
    hl.exec_cmd("dbus-update-activation-environment --all")
    hl.exec_cmd("sleep 1 && dbus-update-activation-environment --systemd WAYLAND_DISPLAY XDG_CURRENT_DESKTOP")
end)