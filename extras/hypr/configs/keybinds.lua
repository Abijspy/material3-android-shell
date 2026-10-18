-- ─────────────────────────────────────────────────────────────────────────────
--  Material3UI Shell: Basic Hyprland Keybinds
--  Standard window management and navigation.
-- ─────────────────────────────────────────────────────────────────────────────

-- --- Window Focus ---
hl.bind("SUPER + Left", hl.dsp.focus({ direction = "l" }))
hl.bind("SUPER + Right", hl.dsp.focus({ direction = "r" }))
hl.bind("SUPER + Up", hl.dsp.focus({ direction = "u" }))
hl.bind("SUPER + Down", hl.dsp.focus({ direction = "d" }))

-- --- Move Window ---
hl.bind("SUPER + SHIFT + Left", hl.dsp.window.move({ direction = "l" }))
hl.bind("SUPER + SHIFT + Right", hl.dsp.window.move({ direction = "r" }))
hl.bind("SUPER + SHIFT + Up", hl.dsp.window.move({ direction = "u" }))
hl.bind("SUPER + SHIFT + Down", hl.dsp.window.move({ direction = "d" }))

-- --- Workspaces ---
-- Switch workspaces
for i = 1, 9 do
    hl.bind("SUPER + " .. tostring(i), hl.dsp.focus({ workspace = tostring(i) }))
    -- Move active window to workspace
    hl.bind("SUPER + SHIFT + " .. tostring(i), hl.dsp.window.move({ workspace = tostring(i) }))
end
hl.bind("SUPER + 0", hl.dsp.focus({ workspace = "10" }))
hl.bind("SUPER + SHIFT + 0", hl.dsp.window.move({ workspace = "10" }))

-- --- Window Layout ---
hl.bind("SUPER + F", hl.dsp.window.fullscreen("fullscreen"))
hl.bind("SUPER + S", hl.dsp.window.float({ action = "toggle" }))
hl.bind("SUPER + P", hl.dsp.window.pseudo({ action = "toggle" })) -- dwindle
hl.bind("SUPER + J", hl.dsp.layout("togglesplit")) -- dwindle

-- --- Mouse Binds ---
-- Move/resize windows with mainMod + LMB/RMB and dragging
hl.bind("SUPER + mouse:272", hl.dsp.window.drag(), { mouse = true })
hl.bind("SUPER + mouse:273", hl.dsp.window.resize(), { mouse = true })