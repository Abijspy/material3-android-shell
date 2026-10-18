-- ─────────────────────────────────────────────────────────────────────────────
--  Material3UI Shell: Monitors
--  Fallback for every output. Settings → Display writes per-monitor layouts
--  to user_persistence.lua, which is loaded afterwards and takes precedence.
-- ─────────────────────────────────────────────────────────────────────────────

hl.monitor({
	output = "",
	mode = "preferred",
	position = "auto",
	scale = "auto",
})
