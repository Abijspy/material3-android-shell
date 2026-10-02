-- Material3UI Shell — Hyprland 0.55+ configuration.
-- Current Hyprland releases load this file from ~/.config/hypr/hyprland.lua.

local mod = "SUPER"
local terminal = "foot"
local browser = "librewolf"
local file_manager = "thunar"

hl.monitor({ output = "", mode = "preferred", position = "auto", scale = "auto" })

hl.config({
    general = {
        gaps_in = 5,
        gaps_out = 10,
        border_size = 2,
        col = {
            active_border = "rgba(c3c6d0ff)",
            inactive_border = "rgba(3d4048cc)",
        },
        layout = "dwindle",
    },
    decoration = {
        rounding = 20,
        blur = { enabled = true, size = 8, passes = 3 },
        shadow = { enabled = true, range = 16, render_power = 3 },
    },
    input = {
        kb_layout = "us",
        follow_mouse = 1,
        touchpad = { natural_scroll = true, tap_to_click = true },
    },
    animations = { enabled = true },
})

hl.curve("material", { type = "bezier", points = { {0.2, 0}, {0, 1} } })
hl.animation({ leaf = "windows", enabled = true, speed = 4, bezier = "material" })
hl.animation({ leaf = "workspaces", enabled = true, speed = 5, bezier = "material" })

-- Quickshell panels share this namespace; transparent surfaces use compositor blur.
hl.layer_rule({ match = { namespace = "^material3ui$" }, blur = true, blur_popups = true, ignore_alpha = 0.18 })

hl.on("hyprland.start", function()
    hl.exec_cmd("dbus-update-activation-environment --systemd WAYLAND_DISPLAY XDG_CURRENT_DESKTOP")
    hl.exec_cmd("systemctl --user import-environment WAYLAND_DISPLAY XDG_CURRENT_DESKTOP")
    hl.exec_cmd("quickshell -c material3ui")
    hl.exec_cmd("material3ui-polkit")
    hl.exec_cmd("material3ui-system clipboard-watch")
    hl.exec_cmd("material3ui-system wallpaper-restore")
    hl.exec_cmd("mako")
end)

local function command(keys, value, flags)
    hl.bind(keys, hl.dsp.exec_cmd(value), flags)
end
local function workspace(number)
    hl.bind(mod .. " + " .. number, hl.dsp.focus({ workspace = number }))
    hl.bind(mod .. " + SHIFT + " .. number, hl.dsp.window.move({ workspace = number }))
end

command(mod .. " + Return", terminal)
command(mod .. " + E", file_manager)
command(mod .. " + B", browser)
hl.bind(mod .. " + Q", hl.dsp.window.close())
hl.bind(mod .. " + F", hl.dsp.window.fullscreen())
command(mod .. " + Space", "material3uictl launcher")
command(mod .. " + C", "material3uictl controlCenter")
command(mod .. " + N", "material3uictl notifications")
command(mod .. " + S", "material3uictl settings")
command(mod .. " + V", "material3uictl clipboard")
command(mod .. " + P", "material3uictl power")
command(mod .. " + L", "hyprlock")
command(mod .. " + SHIFT + M", "material3ui-system hyprmod")
command(mod .. " + SHIFT + R", "hyprctl reload")
command(mod .. " + SHIFT + S", "material3ui-system screenshot")
command(mod .. " + SHIFT + V", "material3ui-system record")

for number = 1, 5 do workspace(number) end
hl.bind(mod .. " + mouse_down", hl.dsp.focus({ workspace = "e+1" }))
hl.bind(mod .. " + mouse_up", hl.dsp.focus({ workspace = "e-1" }))

command("XF86AudioRaiseVolume", "wpctl set-volume -l 1 @DEFAULT_AUDIO_SINK@ 5%+", { locked = true, repeating = true })
command("XF86AudioLowerVolume", "wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-", { locked = true, repeating = true })
command("XF86AudioMute", "wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle", { locked = true })
command("XF86MonBrightnessUp", "brightnessctl set +5%", { locked = true, repeating = true })
command("XF86MonBrightnessDown", "brightnessctl set 5%-", { locked = true, repeating = true })
