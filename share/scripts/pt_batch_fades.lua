ardour {
	["type"]    = "EditorAction",
	name        = "PT : Batch Fades",
	license     = "MIT",
	author      = "BourrinAudio",
	description = [[Applique un fade in/out (durée + forme) à toutes les régions sélectionnées d'un coup — équivalent du "Batch Fades" de Pro Tools (Ctrl+F sur une sélection).]]
}

function factory () return function ()
	local sel = Editor:get_selection ()
	local n = 0
	for _ in sel.regions:regionlist ():iter () do n = n + 1 end
	if n == 0 then
		LuaDialog.Message ("Batch Fades", "Aucune région sélectionnée.",
			LuaDialog.MessageType.Info, LuaDialog.ButtonType.Close):run ()
		return
	end

	local shapes = {
		["Linéaire"]             = ARDOUR.FadeShape.FadeLinear,
		["Rapide (log)"]         = ARDOUR.FadeShape.FadeFast,
		["Lent (exp)"]           = ARDOUR.FadeShape.FadeSlow,
		["Puissance constante"]  = ARDOUR.FadeShape.FadeConstantPower,
		["Symétrique"]           = ARDOUR.FadeShape.FadeSymmetric,
	}

	local dlg = LuaDialog.Dialog ("Batch Fades — " .. n .. " région(s)", {
		{ type = "checkbox", key = "doin",  default = true, title = "Fade in" },
		{ type = "number",   key = "fin",   title = "Durée fade in (ms)",  min = 0, max = 60000, step = 1, digits = 1, default = 10 },
		{ type = "checkbox", key = "doout", default = true, title = "Fade out" },
		{ type = "number",   key = "fout",  title = "Durée fade out (ms)", min = 0, max = 60000, step = 1, digits = 1, default = 10 },
		{ type = "dropdown", key = "shape", title = "Forme", values = shapes, default = "Puissance constante" },
	})
	local rv = dlg:run ()
	if not rv then return end

	local sr   = Session:nominal_sample_rate ()
	local sin  = math.floor (rv["fin"]  * sr / 1000)
	local sout = math.floor (rv["fout"] * sr / 1000)

	Session:begin_reversible_command ("Batch Fades")
	for r in sel.regions:regionlist ():iter () do
		local ar = r:to_audioregion ()
		if not ar:isnil () then
			ar:to_stateful ():clear_changes ()
			if rv["doin"] then
				ar:set_fade_in_shape (rv["shape"])
				ar:set_fade_in_length (sin)
				ar:set_fade_in_active (true)
			end
			if rv["doout"] then
				ar:set_fade_out_shape (rv["shape"])
				ar:set_fade_out_length (sout)
				ar:set_fade_out_active (true)
			end
			Session:add_stateful_diff_command (ar:to_statefuldestructible ())
		end
	end
	if not Session:abort_empty_reversible_command () then
		Session:commit_reversible_command (nil)
	end
end end
