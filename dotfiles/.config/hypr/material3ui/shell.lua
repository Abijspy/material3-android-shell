-- ─────────────────────────────────────────────────────────────────────────────
--  Material3UI Shell: Shell launch, panel shortcuts & layer rules
--  Self-contained: "inject" installs require only this file and
--  user_persistence.lua from an existing hyprland.lua.
-- ─────────────────────────────────────────────────────────────────────────────

-- The native shell (C++, Wayland) and its IPC client
local m3 = "material3ui msg"
local mod = "SUPER"

hl.on("hyprland.start", function()
	hl.exec_cmd("material3ui")
end)

local B = require("material3ui.bindings")

local function run(keys, cmd, label, flags)
	B.bind(keys, hl.dsp.exec_cmd(cmd), flags, label)
end

-- Panels
run(mod .. " + Super_L", m3 .. " launcher", "Shell: Spotlight (tap)", { release = true })
run(mod .. " + Space", m3 .. " launcher", "Shell: App launcher")
run(mod .. " + A", m3 .. " popup notifications", "Shell: Notifications")
run(mod .. " + N", m3 .. " popup quicksettings", "Shell: Quick Settings")
run(mod .. " + G", m3 .. " screenshot", "Shell: Screenshot tool")
run(mod .. " + D", m3 .. " popup dashboard", "Shell: Dashboard")
run(mod .. " + I", m3 .. " settings", "Shell: Settings")
run(mod .. " + Tab", m3 .. " popup overview", "Shell: Overview")
run(mod .. " + Escape", m3 .. " popup power", "Shell: Power menu")
run(mod .. " + L", m3 .. " lock", "Shell: Lock")
run("CTRL + " .. mod .. " + T", m3 .. " wallpaper pick", "Shell: Wallpapers")
run("CTRL + ALT + Delete", m3 .. " popup sysmon", "Shell: System monitor")
run("CTRL + " .. mod .. " + R", "sh -c 'pkill -x material3ui; sleep 0.3; material3ui'", "Shell: Restart shell")

-- Clipboard and emoji
run(mod .. " + V", m3 .. " clipboard", "Shell: Clipboard history")
run(mod .. " + Period", m3 .. " launcher :", "Shell: Emoji picker")

-- Region tools
run(mod .. " + SHIFT + S", m3 .. " screenshot region", "Tools: Region screenshot")
run(mod .. " + SHIFT + X", m3 .. " screenshot ocr", "Tools: Copy text (OCR)")
run(mod .. " + SHIFT + R", m3 .. " record region", "Tools: Record region", { locked = true })
run("Print", m3 .. " screenshot region", "Tools: Screenshot (Print)")

-- Brightness through the shell so its OSD shows the level
run("XF86MonBrightnessUp", m3 .. " brightness up", "Media: Brightness up", { locked = true, repeating = true })
run("XF86MonBrightnessDown", m3 .. " brightness down", "Media: Brightness down", { locked = true, repeating = true })

-- Binds added in Settings → System → Keybinds
B.apply_custom()

-- Layer rules: the shell's layers and blur follow Settings → Look & feel,
-- written to effects_custom.lua
local ok_fx, fx = pcall(require, "material3ui.effects_custom")
if not ok_fx or type(fx) ~= "table" then fx = {} end
for _, ns in ipairs({ "bar", "popup", "toasts", "osd", "dock" }) do
	hl.layer_rule({ match = { namespace = "material3ui-" .. ns }, blur = fx.blur ~= false, ignore_alpha = fx.ignore_alpha or 0.6 })
end
hl.layer_rule({ match = { namespace = "material3ui-wallpaper" }, blur = false, no_anim = true })

-- The Settings app is a window: floating and centred, at a comfortable size
hl.window_rule({
	match = { class = "^(material3ui-settings)$" },
	float = true,
	center = true,
	size = { "monitor_w*0.8", "monitor_h*0.85" },
})
if fx.size then
	hl.config({ decoration = { blur = { size = fx.size, passes = fx.passes or 3 } } })
end
