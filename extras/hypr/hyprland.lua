-- ─────────────────────────────────────────────────────────────────────────────
--  Material3UI Shell: Main Hyprland Configuration
-- ─────────────────────────────────────────────────────────────────────────────

-- --- Source Modular Configs ---
require("configs/env")
require("configs/execs")
require("configs/general")
require("configs/rules")
require("configs/keybinds")

-- --- Source Material3UI Shell Keybinds & Rules ---
require("material3ui/material3ui")
require("material3ui/user_persistence")

-- --- Monitors ---
hl.monitor({
    output = "",
    mode = "preferred",
    position = "auto",
    scale = "1"
})