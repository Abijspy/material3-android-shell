-- ─────────────────────────────────────────────────────────────────────────────
--  Material3UI Shell: Appearance
--  Layout, decoration and Material 3 expressive motion.
-- ─────────────────────────────────────────────────────────────────────────────

hl.config({
	general = {
		gaps_in = 7,
		gaps_out = 14,
		gaps_workspaces = 50,
		border_size = 1,
		resize_on_border = true,
		no_focus_fallback = true,
		allow_tearing = true,
		layout = "dwindle",
		snap = {
			enabled = true,
			window_gap = 4,
			monitor_gap = 5,
			respect_gaps = true,
		},
	},

	dwindle = {
		preserve_split = true,
		smart_split = false,
		smart_resizing = false,
	},

	decoration = {
		rounding = 18,
		rounding_power = 2.4,
		dim_inactive = true,
		dim_strength = 0.05,
		dim_special = 0.07,
		blur = {
			enabled = true,
			xray = true,
			special = false,
			new_optimizations = true,
			size = 10,
			passes = 3,
			brightness = 1,
			noise = 0.05,
			contrast = 0.89,
			vibrancy = 0.5,
			vibrancy_darkness = 0.5,
			popups = false,
			popups_ignorealpha = 0.6,
			input_methods = true,
			input_methods_ignorealpha = 0.8,
		},
		shadow = {
			enabled = true,
			range = 50,
			offset = "0 4",
			render_power = 10,
			color = "rgba(00000027)",
		},
	},

	cursor = {
		inactive_timeout = 3,
	},

	misc = {
		disable_hyprland_logo = true,
		disable_splash_rendering = true,
		vrr = true,
		mouse_move_enables_dpms = false,
		key_press_enables_dpms = false,
		initial_workspace_tracking = false,
		focus_on_activate = true,
	},
})

-- Material 3 motion curves
local curves = {
	emphasizedAccel = { {0.3, 0}, {0.8, 0.15} },
	emphasizedDecel = { {0.05, 0.7}, {0.1, 1} },
	standard        = { {0.2, 0}, {0, 1} },
	subtleBounce    = { {0.14, 1.1}, {0.2, 1.0} },
	easeOutQuint    = { {0.23, 1}, {0.32, 1} },
	easeOutExpo     = { {0.16, 1}, {0.3, 1} },
}
for name, points in pairs(curves) do
	hl.curve(name, { type = "bezier", points = points })
end

hl.animation({ leaf = "windows",     enabled = true, speed = 4,  bezier = "subtleBounce",    style = "popin 85%" })
hl.animation({ leaf = "windowsIn",   enabled = true, speed = 4,  bezier = "emphasizedDecel", style = "popin 85%" })
hl.animation({ leaf = "windowsOut",  enabled = true, speed = 3,  bezier = "emphasizedAccel", style = "popin 80%" })
hl.animation({ leaf = "windowsMove", enabled = true, speed = 4,  bezier = "easeOutQuint",    style = "slide" })
hl.animation({ leaf = "border",      enabled = true, speed = 10, bezier = "default" })
hl.animation({ leaf = "borderangle", enabled = true, speed = 8,  bezier = "default" })
hl.animation({ leaf = "fade",        enabled = true, speed = 5,  bezier = "standard" })
hl.animation({ leaf = "workspaces",  enabled = true, speed = 5,  bezier = "easeOutExpo",     style = "slidevert" })
