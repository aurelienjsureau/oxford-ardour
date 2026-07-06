ardour {
	["type"]    = "EditorAction",
	name        = "PT : Batch Rename (pistes)",
	license     = "MIT",
	author      = "BourrinAudio",
	description = [[Renomme toutes les pistes sélectionnées d'un coup : chercher/remplacer, préfixe, suffixe, numérotation — équivalent du "Batch Track Rename" de Pro Tools.]]
}

function factory () return function ()
	local sel = Editor:get_selection ()
	local routes = {}
	for route in sel.tracks:routelist ():iter () do
		table.insert (routes, route)
	end
	if #routes == 0 then
		LuaDialog.Message ("Batch Rename", "Aucune piste sélectionnée.",
			LuaDialog.MessageType.Info, LuaDialog.ButtonType.Close):run ()
		return
	end

	local dlg = LuaDialog.Dialog ("Batch Rename — " .. #routes .. " piste(s)", {
		{ type = "entry",    key = "find",    default = "", title = "Chercher" },
		{ type = "entry",    key = "repl",    default = "", title = "Remplacer par" },
		{ type = "entry",    key = "prefix",  default = "", title = "Préfixe" },
		{ type = "entry",    key = "suffix",  default = "", title = "Suffixe" },
		{ type = "checkbox", key = "donum",   default = false, title = "Numéroter" },
		{ type = "number",   key = "numstart", title = "Numérotation : départ", min = 1, max = 999, step = 1, digits = 0, default = 1 },
	})
	local rv = dlg:run ()
	if not rv then return end

	-- échappe les caractères spéciaux Lua pour un chercher/remplacer LITTÉRAL
	local function plain (s)  return (s:gsub ("[%^%$%(%)%%%.%[%]%*%+%-%?]", "%%%0")) end
	local function plainr (s) return (s:gsub ("%%", "%%%%")) end

	local num = math.floor (rv["numstart"])
	for _, route in ipairs (routes) do
		local nm = route:name ()
		if rv["find"] ~= "" then
			nm = nm:gsub (plain (rv["find"]), plainr (rv["repl"]))
		end
		nm = rv["prefix"] .. nm .. rv["suffix"]
		if rv["donum"] then
			nm = nm .. " " .. string.format ("%02d", num)
			num = num + 1
		end
		route:set_name (nm)
	end
end end
