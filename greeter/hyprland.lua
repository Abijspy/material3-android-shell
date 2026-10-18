-- ─────────────────────────────────────────────────────────────────────────────
--  Material3UI greeter: minimal Hyprland session for greetd
--  Runs the greeter; when it quits (after login or in an error) the session
--  exits and greetd starts the chosen desktop.
-- ─────────────────────────────────────────────────────────────────────────────

hl.monitor({ output = "", mode = "preferred", position = "auto", scale = "auto" })

hl.config({
	misc = {
		disable_hyprland_logo = true,
		disable_splash_rendering = true,
		force_default_wallpaper = 0,
		background_color = "rgb(0f0d13)",
	},
	input = {
		kb_layout = "us",
		numlock_by_default = true,
	},
	cursor = {
		inactive_timeout = 0,
	},
	animations = {
		enabled = true,
	},
})

-- Synced keyboard layout and monitor rules (written by `material3ui-greeter sync`)
pcall(dofile, "/var/lib/material3ui-greeter/hyprland-local.lua")

hl.layer_rule({ match = { namespace = "material3ui-greeter" }, no_anim = true })

hl.on("hyprland.start", function()
	hl.exec_cmd([[sh -c "/usr/share/material3ui-greeter/greet-run.sh; hyprctl dispatch 'hl.dsp.exit()'"]])
end)
