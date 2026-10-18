-- ─────────────────────────────────────────────────────────────────────────────
--  Material3UI Shell: Window rules
--  Shell layer rules live in shell.lua so they also apply in inject mode.
-- ─────────────────────────────────────────────────────────────────────────────

local function float_centered(match, extra)
	local rule = { match = match, float = true, center = true }
	for k, v in pairs(extra or {}) do rule[k] = v end
	hl.window_rule(rule)
end

-- Dialogs & file pickers
for _, title in ipairs({
	"Open File", "Open Folder", "Select a File", "Save As", "File Upload",
	"Choose wallpaper", "Library",
}) do
	float_centered({ title = "^(" .. title .. ")(.*)$" })
end

-- Nemo's own dialogs (copy progress, properties, connect to server)
float_centered({ class = "^(nemo)$", title = "^(.*Properties|File Operations|Connect to Server)$" })

-- System tools
local tool_size = { size = { "monitor_w*0.45", "monitor_h*0.45" } }
float_centered({ class = "^(pavucontrol|org.pulseaudio.pavucontrol)$" }, tool_size)
float_centered({ class = "^(nm-connection-editor)$" }, tool_size)
float_centered({ class = "^(blueman-manager)$" }, tool_size)

-- Portals
hl.window_rule({ match = { class = "^(xdg-desktop-portal-gtk)$" }, float = true })
hl.window_rule({ match = { class = "^(xdg-desktop-portal-hyprland)$" }, float = true })
hl.window_rule({ match = { class = "^(org.freedesktop.impl.portal.desktop.kde)$" }, float = true })

-- Picture-in-picture (LibreWolf / Firefox)
hl.window_rule({ match = { title = "^([Pp]icture[-\\s]?[Ii]n[-\\s]?[Pp]icture)(.*)$" }, float = true, pin = true })

-- Material3UI windows
float_centered({ title = "^(Settings)$" }, { border_size = 0 })
float_centered({ title = "^(System Monitor)$" }, { border_size = 0 })
float_centered({ title = "^(Welcome to Material3UI)$" }, { border_size = 0 })
