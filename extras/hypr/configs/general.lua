-- --- General Appearance ---
-- Modularized and standardized following user examples

-- --- Gestures (Standard Syntax) ---
hl.config({
    gestures = {
        workspace_swipe_distance = 700,
        workspace_swipe_cancel_ratio = 0.2,
        workspace_swipe_min_speed_to_force = 5,
        workspace_swipe_direction_lock = true,
        workspace_swipe_direction_lock_threshold = 10,
        workspace_swipe_create_new = true
    }
})

-- --- Cursor ---
hl.config({
    cursor = {
        inactive_timeout = 3
    }
})

-- --- General ---
hl.config({
    general = {
        gaps_in = 7,
        gaps_out = 14,
        gaps_workspaces = 50,
        border_size = 1,
        ["col.active_border"] = "rgba(0DB7D455)",
        ["col.inactive_border"] = "rgba(31313600)",
        resize_on_border = true,
        no_focus_fallback = true,
        allow_tearing = true,
        layout = "dwindle",
        snap = {
            enabled = true,
            window_gap = 4,
            monitor_gap = 5,
            respect_gaps = true
        }
    }
})

hl.config({
    dwindle = {
        preserve_split = true,
        smart_split = false,
        smart_resizing = false
    }
})

-- --- Decoration ---
hl.config({
    decoration = {
        rounding_power = 2.4,
        rounding = 18,
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
            input_methods_ignorealpha = 0.8
        },
        shadow = {
            enabled = true,
            ignore_window = true,
            range = 50,
            offset = "0 4",
            render_power = 10,
            color = "rgba(00000027)"
        },
        dim_inactive = true,
        dim_strength = 0.05,
        dim_special = 0.07
    }
})

-- --- Animation Curves ---
hl.curve("expressiveFastSpatial", { type = "bezier", points = {{0.42, 1.67}, {0.21, 0.90}} })
hl.curve("expressiveDefaultSpatial", { type = "bezier", points = {{0.38, 1.21}, {0.22, 1.00}} })
hl.curve("expressiveSlowSpatial", { type = "bezier", points = {{0.39, 1.29}, {0.35, 0.98}} })
hl.curve("expressiveEffects", { type = "bezier", points = {{0.34, 0.80}, {0.34, 1.00}} })
hl.curve("emphasized", { type = "bezier", points = {{0.05, 0}, {0.133, 0.06}} })
hl.curve("emphasizedAccel", { type = "bezier", points = {{0.3, 0}, {0.8, 0.15}} })
hl.curve("emphasizedDecel", { type = "bezier", points = {{0.05, 0.7}, {0.1, 1}} })
hl.curve("standard", { type = "bezier", points = {{0.2, 0}, {0, 1}} })
hl.curve("standardAccel", { type = "bezier", points = {{0.3, 0}, {1, 1}} })
hl.curve("standardDecel", { type = "bezier", points = {{0, 0}, {0, 1}} })

-- New anim curves
hl.curve("subtleBounce", { type = "bezier", points = {{0.14, 1.1}, {0.2, 1.0}} })
hl.curve("easeOutQuint", { type = "bezier", points = {{0.23, 1}, {0.32, 1}} })
hl.curve("easeOutExpo", { type = "bezier", points = {{0.16, 1}, {0.3, 1}} })

-- --- Animations ---
hl.animation({ leaf = "windows",     enabled = true, speed = 4, bezier = "subtleBounce", style = "popin 85%" })
hl.animation({ leaf = "windowsIn",   enabled = true, speed = 4, bezier = "emphasizedDecel", style = "popin 85%" })
hl.animation({ leaf = "windowsOut",  enabled = true, speed = 3, bezier = "emphasizedAccel", style = "popin 80%" })
hl.animation({ leaf = "windowsMove", enabled = true, speed = 4, bezier = "easeOutQuint", style = "slide" })
hl.animation({ leaf = "border",      enabled = true, speed = 10, bezier = "default" })
hl.animation({ leaf = "borderangle", enabled = true, speed = 8, bezier = "default" })
hl.animation({ leaf = "fade",        enabled = true, speed = 5, bezier = "standard" })
hl.animation({ leaf = "workspaces",  enabled = true, speed = 5, bezier = "easeOutExpo", style = "slidevert" })

-- --- Input ---
hl.config({
    input = {
        kb_layout = "us",
        numlock_by_default = true,
        repeat_delay = 250,
        repeat_rate = 35,
        follow_mouse = 1,
        touchpad = {
            natural_scroll = true,
            disable_while_typing = true,
            clickfinger_behavior = true,
            scroll_factor = 0.7
        }
    }
})

-- --- Misc ---
hl.config({
    misc = {
        disable_hyprland_logo = true,
        disable_splash_rendering = true,
        vfr = true,
        vrr = true,
        mouse_move_enables_dpms = false,
        key_press_enables_dpms = false,
        initial_workspace_tracking = false,
        focus_on_activate = true
    }
})
