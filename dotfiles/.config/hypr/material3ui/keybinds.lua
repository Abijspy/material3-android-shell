-- ─────────────────────────────────────────────────────────────────────────────
--  Material3UI Shell: Window management, apps & media keys
--  Shell panel shortcuts live in shell.lua.
-- ─────────────────────────────────────────────────────────────────────────────

local apps = require("material3ui.apps")
local mod = "SUPER"

local function run(keys, cmd, flags)
	hl.bind(keys, hl.dsp.exec_cmd(cmd), flags)
end

-- Applications
run(mod .. " + Return", apps.terminal)
run(mod .. " + T", apps.terminal)
run(mod .. " + E", apps.file_manager)
run(mod .. " + W", apps.browser)
run(mod .. " + B", apps.browser)

-- Windows
hl.bind(mod .. " + Q", hl.dsp.window.close())
hl.bind(mod .. " + F", hl.dsp.window.fullscreen("fullscreen"))
hl.bind(mod .. " + S", hl.dsp.window.float({ action = "toggle" }))
hl.bind(mod .. " + P", hl.dsp.window.pseudo({ action = "toggle" }))
hl.bind(mod .. " + J", hl.dsp.layout("togglesplit"))
hl.bind(mod .. " + mouse:272", hl.dsp.window.drag(), { mouse = true })
hl.bind(mod .. " + mouse:273", hl.dsp.window.resize(), { mouse = true })

for key, dir in pairs({ Left = "l", Right = "r", Up = "u", Down = "d" }) do
	hl.bind(mod .. " + " .. key, hl.dsp.focus({ direction = dir }))
	hl.bind(mod .. " + SHIFT + " .. key, hl.dsp.window.move({ direction = dir }))
end

-- Workspaces 1–10 (0 is workspace 10)
for i = 1, 10 do
	local key = tostring(i % 10)
	hl.bind(mod .. " + " .. key, hl.dsp.focus({ workspace = tostring(i) }))
	hl.bind(mod .. " + SHIFT + " .. key, hl.dsp.window.move({ workspace = tostring(i) }))
end
hl.bind(mod .. " + mouse_down", hl.dsp.focus({ workspace = "e+1" }))
hl.bind(mod .. " + mouse_up", hl.dsp.focus({ workspace = "e-1" }))

-- Audio (the shell's OSD follows PipeWire, so plain wpctl is enough)
run("XF86AudioRaiseVolume", "wpctl set-volume -l 1 @DEFAULT_AUDIO_SINK@ 5%+", { locked = true, repeating = true })
run("XF86AudioLowerVolume", "wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-", { locked = true, repeating = true })
run("XF86AudioMute", "wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle", { locked = true })
run("XF86AudioMicMute", "wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle", { locked = true })

-- Media
run("XF86AudioPlay", "playerctl play-pause", { locked = true })
run("XF86AudioPause", "playerctl play-pause", { locked = true })
run("XF86AudioNext", "playerctl next", { locked = true })
run("XF86AudioPrev", "playerctl previous", { locked = true })
