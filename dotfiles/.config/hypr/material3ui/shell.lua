-- ─────────────────────────────────────────────────────────────────────────────
--  Material3UI Shell: Shell launch, panel shortcuts & layer rules
--  Self-contained: "inject" installs require only this file and
--  user_persistence.lua from an existing hyprland.lua.
-- ─────────────────────────────────────────────────────────────────────────────

local ipc = "quickshell -c material3ui ipc call"
local m3 = ipc .. " material3ui"
local scripts = "~/.config/quickshell/material3ui/scripts"
local mod = "SUPER"

hl.on("hyprland.start", function()
	hl.exec_cmd("quickshell -c material3ui")
end)

local B = require("material3ui.bindings")

local function run(keys, cmd, label, flags)
	B.bind(keys, hl.dsp.exec_cmd(cmd), flags, label)
end

local function global(keys, name, label, flags)
	B.bind(keys, hl.dsp.global("quickshell:" .. name), flags, label)
end

-- Panels
run(mod .. " + Super_L", m3 .. " spotlight", "Shell: Spotlight (tap)", { release = true })
run(mod .. " + Space", m3 .. " launcher", "Shell: App launcher")
run(mod .. " + A", m3 .. " notifications", "Shell: Notifications")
run(mod .. " + N", m3 .. " controlCenter", "Shell: Quick Settings")
run(mod .. " + G", m3 .. " quickActions", "Shell: Quick actions")
run(mod .. " + D", m3 .. " dashboard", "Shell: Dashboard")
run(mod .. " + I", m3 .. " settings", "Shell: Settings")
run(mod .. " + Tab", m3 .. " overview", "Shell: Overview")
run(mod .. " + Escape", m3 .. " power", "Shell: Power menu")
run(mod .. " + L", m3 .. " lock", "Shell: Lock")
run("CTRL + " .. mod .. " + T", m3 .. " wallpaperPicker", "Shell: Wallpapers")
run("CTRL + ALT + Delete", m3 .. " systemMonitor", "Shell: System monitor")
run("CTRL + " .. mod .. " + R", scripts .. "/restartshell.sh", "Shell: Restart shell")

-- Spotlight modes
global(mod .. " + V", "spotlightClipboard", "Shell: Clipboard history")
global(mod .. " + Period", "spotlightEmoji", "Shell: Emoji picker")

-- Region tools
global(mod .. " + SHIFT + S", "regionScreenshot", "Tools: Region screenshot")
global(mod .. " + SHIFT + X", "regionOcr", "Tools: Copy text (OCR)")
global(mod .. " + SHIFT + R", "regionRecord", "Tools: Record region", { locked = true })
global("Print", "regionScreenshot", "Tools: Screenshot (Print)")

-- Brightness through the shell so its OSD shows the level
run("XF86MonBrightnessUp", ipc .. " brightness increment", "Media: Brightness up", { locked = true, repeating = true })
run("XF86MonBrightnessDown", ipc .. " brightness decrement", "Media: Brightness down", { locked = true, repeating = true })

-- Binds added in Settings → System → Keybinds
B.apply_custom()

-- Layer rules: blur translucent panels, but not full-screen or fading layers
-- (blurred content bleeds colour mid-fade; full-screen blur recomputes every frame).
hl.layer_rule({ match = { namespace = "quickshell:.*" }, blur = true, ignore_alpha = 0.79 })
hl.layer_rule({ match = { namespace = "notifications" }, blur = true, ignore_alpha = 0.69 })
hl.layer_rule({ match = { namespace = "launcher" }, blur = true, ignore_alpha = 0.5 })
hl.layer_rule({ match = { namespace = "session" }, blur = true })
hl.layer_rule({ match = { namespace = "overview" }, no_anim = true })

for _, ns in ipairs({ "spotlight", "launcher", "background", "desktop-widgets" }) do
	hl.layer_rule({ match = { namespace = "quickshell:" .. ns }, blur = false })
end
for _, ns in ipairs({ "regionSelector", "recordingMarker" }) do
	hl.layer_rule({ match = { namespace = "quickshell:" .. ns }, blur = false, no_anim = true })
end

-- The native shell (material3ui): its layers and blur follow
-- Settings → Appearance → Effects, written to effects_custom.lua
local ok_fx, fx = pcall(require, "material3ui.effects_custom")
if not ok_fx or type(fx) ~= "table" then fx = {} end
for _, ns in ipairs({ "bar", "popup", "toasts", "osd" }) do
	hl.layer_rule({ match = { namespace = "material3ui-" .. ns }, blur = fx.blur ~= false, ignore_alpha = fx.ignore_alpha or 0.6 })
end
hl.layer_rule({ match = { namespace = "material3ui-wallpaper" }, blur = false, no_anim = true })
if fx.size then
	hl.config({ decoration = { blur = { size = fx.size, passes = fx.passes or 3 } } })
end
