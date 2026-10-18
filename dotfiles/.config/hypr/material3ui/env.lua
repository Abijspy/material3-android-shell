-- ─────────────────────────────────────────────────────────────────────────────
--  Material3UI Shell: Environment
--  Wayland backends, portals and toolkit theming so Qt/GTK apps follow the
--  shell's generated Material 3 colours.
-- ─────────────────────────────────────────────────────────────────────────────

-- Session / portals
hl.env("XDG_CURRENT_DESKTOP", "Hyprland")
hl.env("XDG_SESSION_TYPE", "wayland")
hl.env("XDG_SESSION_DESKTOP", "Hyprland")

-- Toolkit backends
hl.env("GDK_BACKEND", "wayland,x11,*")
hl.env("QT_QPA_PLATFORM", "wayland;xcb")
hl.env("SDL_VIDEODRIVER", "wayland")
hl.env("CLUTTER_BACKEND", "wayland")
hl.env("ELECTRON_OZONE_PLATFORM_HINT", "auto")
hl.env("MOZ_ENABLE_WAYLAND", "1")

-- Theming (Qt via the KDE platform theme, GTK via adw-gtk3)
hl.env("QT_QPA_PLATFORMTHEME", "kde")
hl.env("XDG_MENU_PREFIX", "plasma-")
hl.env("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1")

-- Preferred applications for scripts that honour these variables
local apps = require("material3ui.apps")
hl.env("TERMINAL", apps.terminal)
hl.env("BROWSER", apps.browser)
