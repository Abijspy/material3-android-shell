-- ─────────────────────────────────────────────────────────────────────────────
--  Material3UI Shell: Hyprland configuration (Hyprland 0.55+ Lua API)
--
--  Modules live in ~/.config/hypr/material3ui/. Put personal tweaks in
--  material3ui/user.lua; it is never overwritten by the installer or updates.
--  material3ui/user_persistence.lua is written by the shell itself (monitors,
--  game mode) — do not edit it by hand.
-- ─────────────────────────────────────────────────────────────────────────────

require("material3ui.env")
require("material3ui.monitors")
require("material3ui.appearance")
require("material3ui.colors")
require("material3ui.input")
require("material3ui.rules")
require("material3ui.autostart")
require("material3ui.keybinds")
require("material3ui.shell")

-- Loaded last so they win over everything above.
require("material3ui.user_persistence")
require("material3ui.user")
