ardour {
	["type"]    = "EditorAction",
	name        = "PT : Heal Separation",
	license     = "MIT",
	author      = "BourrinAudio",
	description = [[Ressoude les régions sélectionnées issues d'un même split (même source, alignement source intact, bords adjacents ou en recouvrement) — équivalent du "Heal Separation" de Pro Tools (Ctrl+H).]]
}

function factory () return function ()
	local sel = Editor:get_selection ()

	-- regroupe les régions sélectionnées par playlist
	local groups = {}
	for r in sel.regions:regionlist ():iter () do
		local pl = r:playlist ()
		if not pl:isnil () then
			local key = pl:name ()
			if not groups[key] then groups[key] = { pl = pl, regions = {} } end
			table.insert (groups[key].regions, r)
		end
	end

	local healed = 0
	Session:begin_reversible_command ("Heal Separation")

	for _, grp in pairs (groups) do
		table.sort (grp.regions, function (a, b)
			return a:position ():samples () < b:position ():samples ()
		end)

		grp.pl:to_stateful ():clear_changes ()
		local pl_dirty = false

		local cur = nil
		for _, r in ipairs (grp.regions) do
			if cur == nil then
				cur = r
			else
				local cpos, clen, cstart = cur:position ():samples (), cur:length ():samples (), cur:start ():samples ()
				local rpos, rlen, rstart = r:position ():samples (), r:length ():samples (), r:start ():samples ()
				local same_src  = (cur:source (0):name () == r:source (0):name ())
				local aligned   = ((rpos - cpos) == (rstart - cstart))
				local touching  = (rpos <= cpos + clen)
				if same_src and aligned and touching then
					if rpos + rlen > cpos + clen then
						-- étend la 1re région jusqu'à la fin de la 2e, puis supprime la 2e
						cur:to_stateful ():clear_changes ()
						cur:set_length (Temporal.timecnt_t ((rpos + rlen) - cpos))
						Session:add_stateful_diff_command (cur:to_statefuldestructible ())
					end
					-- (si r est entièrement couverte, on la supprime simplement)
					grp.pl:remove_region (r)
					pl_dirty = true
					healed = healed + 1
				else
					cur = r
				end
			end
		end

		if pl_dirty then
			Session:add_stateful_diff_command (grp.pl:to_statefuldestructible ())
		end
	end

	if not Session:abort_empty_reversible_command () then
		Session:commit_reversible_command (nil)
	end

	if healed == 0 then
		LuaDialog.Message ("Heal Separation",
			"Rien à ressouder : il faut sélectionner des régions adjacentes issues du même split\n(même fichier source, sans déplacement relatif).",
			LuaDialog.MessageType.Info, LuaDialog.ButtonType.Close):run ()
	end
end end
