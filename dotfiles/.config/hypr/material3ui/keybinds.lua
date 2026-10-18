-- ─────────────────────────────────────────────────────────────────────────────
--  Material3UI Shell: Window management, apps & media keys
--  Shell panel shortcuts live in shell.lua.
-- ─────────────────────────────────────────────────────────────────────────────

local apps = require("material3ui.apps")
local B = require("material3ui.bindings")
local mod = "SUPER"

local function run(keys, cmd, label, flags)
	B.bind(keys, hl.dsp.exec_cmd(cmd), flags, label)
end

local function bind(keys, dispatcher, label, flags)
	B.bind(keys, dispatcher, flags, label)
end

-- Applications
run(mod .. " + Return", apps.terminal, "Apps: Terminal")
run(mod .. " + T", apps.terminal, "Apps: Terminal (alternate)")
run(mod .. " + E", apps.file_manager, "Apps: File manager")
run(mod .. " + W", apps.browser, "Apps: Browser (alternate)")
run(mod .. " + B", apps.browser, "Apps: Browser")

-- Windows
bind(mod .. " + Q", hl.dsp.window.close(), "Windows: Close")
bind(mod .. " + F", hl.dsp.window.fullscreen("fullscreen"), "Windows: Fullscreen")
bind(mod .. " + S", hl.dsp.window.float({ action = "toggle" }), "Windows: Toggle floating")
bind(mod .. " + P", hl.dsp.window.pseudo({ action = "toggle" }), "Windows: Pseudotile")
bind(mod .. " + J", hl.dsp.layout("togglesplit"), "Windows: Toggle split")
bind(mod .. " + mouse:272", hl.dsp.window.drag(), "Windows: Drag with mouse", { mouse = true })
bind(mod .. " + mouse:273", hl.dsp.window.resize(), "Windows: Resize with mouse", { mouse = true })

for key, dir in pairs({ Left = "l", Right = "r", Up = "u", Down = "d" }) do
	bind(mod .. " + " .. key, hl.dsp.focus({ direction = dir }), "Windows: Focus " .. key:lower())
	bind(mod .. " + SHIFT + " .. key, hl.dsp.window.move({ direction = dir }), "Windows: Move " .. key:lower())
end

-- Workspaces 1–10 (0 is workspace 10)
for i = 1, 10 do
	local key = tostring(i % 10)
	bind(mod .. " + " .. key, hl.dsp.focus({ workspace = tostring(i) }), "Workspaces: Go to " .. i)
	bind(mod .. " + SHIFT + " .. key, hl.dsp.window.move({ workspace = tostring(i) }), "Workspaces: Move window to " .. i)
end
bind(mod .. " + mouse_down", hl.dsp.focus({ workspace = "e+1" }), "Workspaces: Next (scroll)")
bind(mod .. " + mouse_up", hl.dsp.focus({ workspace = "e-1" }), "Workspaces: Previous (scroll)")

-- Audio (the shell's OSD follows PipeWire, so plain wpctl is enough)
run("XF86AudioRaiseVolume", "wpctl set-volume -l 1 @DEFAULT_AUDIO_SINK@ 5%+", "Media: Volume up", { locked = true, repeating = true })
run("XF86AudioLowerVolume", "wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-", "Media: Volume down", { locked = true, repeating = true })
run("XF86AudioMute", "wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle", "Media: Mute", { locked = true })
run("XF86AudioMicMute", "wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle", "Media: Mute microphone", { locked = true })

-- Media
run("XF86AudioPlay", "playerctl play-pause", "Media: Play / pause", { locked = true })
run("XF86AudioPause", "playerctl play-pause", "Media: Pause", { locked = true })
run("XF86AudioNext", "playerctl next", "Media: Next track", { locked = true })
run("XF86AudioPrev", "playerctl previous", "Media: Previous track", { locked = true })
