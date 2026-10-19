-- ─────────────────────────────────────────────────────────────────────────────
--  Material3UI Shell: Keybind registry
--  Every default bind goes through bind() so Settings → System → Keybinds can
--  remap or disable it. Those edits live in keybinds_custom.lua, which the
--  shell writes; it is never shipped or overwritten by updates.
-- ─────────────────────────────────────────────────────────────────────────────

local M = {}

local ok, custom = pcall(require, "material3ui.keybinds_custom")
if not ok or type(custom) ~= "table" then
	custom = {}
end
local overrides = custom.overrides or {}

local MODS = { SUPER = true, CTRL = true, ALT = true, SHIFT = true }
local ALIASES = { CONTROL = "CTRL", WIN = "SUPER", META = "SUPER", MOD4 = "SUPER", MOD1 = "ALT" }
local ORDER = { "SUPER", "CTRL", "ALT", "SHIFT" }

-- "CTRL + SUPER + t" → "SUPER + CTRL + t"
function M.normalize(keys)
	local mods, key = {}, ""
	for part in keys:gmatch("[^+]+") do
		local p = part:match("^%s*(.-)%s*$")
		local up = ALIASES[p:upper()] or p:upper()
		if MODS[up] then
			mods[up] = true
		else
			key = p
		end
	end
	local out = {}
	for _, m in ipairs(ORDER) do
		if mods[m] then
			out[#out + 1] = m
		end
	end
	out[#out + 1] = key
	return table.concat(out, " + ")
end

-- The id the shell uses for a bind; key names compare case-insensitively
function M.id(keys)
	return M.normalize(keys):lower()
end

-- label is "Group: Action"; the shell shows it in the editor
function M.bind(keys, dispatcher, flags, label)
	local target = overrides[M.id(keys)]
	if target == false then
		return
	end
	flags = flags or {}
	if label then
		flags.description = label
	end
	hl.bind(target or keys, dispatcher, flags)
end

-- Binds added in the editor: a command, or any action (a dispatcher written
-- as Lua, e.g. "hl.dsp.window.pin()"); mouse ones drag (bindm)
function M.apply_custom()
	for _, c in ipairs(custom.custom or {}) do
		local dispatcher
		if c.action and c.action ~= "" then
			local make = load("return " .. c.action)
			if make then
				local ok, d = pcall(make)
				if ok then dispatcher = d end
			end
		elseif c.command and c.command ~= "" then
			dispatcher = hl.dsp.exec_cmd(c.command)
		end
		if c.keys and dispatcher then
			local flags = { description = "Custom: " .. (c.label or c.command or c.action) }
			if c.mouse then flags.mouse = true end
			pcall(hl.bind, c.keys, dispatcher, flags)
		end
	end
end

return M
