ardour {
	["type"] = "EditorAction",
	name    = "Router vers Mix Bus",
	license = "MIT",
	author  = "Aurelien Sureau",
	description = [[Route les pistes sélectionnées vers un bus Mix 1-8]]
}

function factory () return function ()

	-- Demande le numéro de bus (1-8)
	local rv = LuaDialog.Dialog ("Router vers quel bus ?", {
		{ type = "number", key = "bus", title = "Numéro (1-8)", min = 1, max = 8, step = 1, digits = 0, default = 1 }
	}):run ()

	if not rv then return end -- annulé

	-- le widget "number" renvoie un float -> on force un entier (sinon "Mix 1.0")
	local n = math.floor (tonumber (rv["bus"]) + 0.5)
	local bus_name = "Mix " .. n
	local dest = Session:route_by_name (bus_name)

	if dest:isnil () then
		LuaDialog.Message ("Erreur", "Bus '" .. bus_name .. "' introuvable.",
			LuaDialog.MessageType.Error, LuaDialog.ButtonType.Close):run ()
		return
	end

	-- Itère sur les pistes sélectionnées
	local sel = Editor:get_selection ()
	local count = 0
	for route in sel.tracks:routelist ():iter () do
		-- Ignore les bus (on ne route que les pistes)
		if not route:to_track ():isnil () then
			local n_out = route:n_outputs ():n_audio ()
			for i = 1, n_out do
				local src_port = route:output ():audio (i - 1)
				local dst_port = dest:input ():audio (i - 1)
				if not src_port:isnil () and not dst_port:isnil () then
					src_port:disconnect_all ()              -- redirige la sortie vers le bus
					src_port:connect (dst_port:name ())
				end
			end
			count = count + 1
		end
	end

	LuaDialog.Message ("Routage terminé",
		count .. " piste(s) routée(s) vers " .. bus_name .. ".",
		LuaDialog.MessageType.Info, LuaDialog.ButtonType.Close):run ()

end end
