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

local function run(keys, cmd, flags)
	hl.bind(keys, hl.dsp.exec_cmd(cmd), flags)
end

local function global(keys, name, flags)
	hl.bind(keys, hl.dsp.global("quickshell:" .. name), flags)
end

-- Panels
run(mod .. " + Super_L", m3 .. " spotlight", { release = true }) -- tap SUPER
run(mod .. " + Space", m3 .. " launcher")
run(mod .. " + A", m3 .. " notifications")
run(mod .. " + N", m3 .. " controlCenter")
run(mod .. " + G", m3 .. " quickActions")
run(mod .. " + D", m3 .. " dashboard")
run(mod .. " + I", m3 .. " settings")
run(mod .. " + Tab", m3 .. " overview")
run(mod .. " + Escape", m3 .. " power")
run(mod .. " + L", m3 .. " lock")
run("CTRL + " .. mod .. " + T", m3 .. " wallpaperPicker")
run("CTRL + ALT + Delete", m3 .. " systemMonitor")
run("CTRL + " .. mod .. " + R", scripts .. "/restartshell.sh")

-- Spotlight modes
global(mod .. " + V", "spotlightClipboard", { description = "Clipboard history" })
global(mod .. " + Period", "spotlightEmoji", { description = "Emoji picker" })

-- Region tools
global(mod .. " + SHIFT + S", "regionScreenshot")
global(mod .. " + SHIFT + X", "regionOcr")
global(mod .. " + SHIFT + R", "regionRecord", { locked = true })
global("Print", "regionScreenshot")

-- Brightness through the shell so its OSD shows the level
run("XF86MonBrightnessUp", ipc .. " brightness increment", { locked = true, repeating = true })
run("XF86MonBrightnessDown", ipc .. " brightness decrement", { locked = true, repeating = true })

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
