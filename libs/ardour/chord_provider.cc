/*
 * Copyright (C) 2026 Paul Davis <paul@linuxaudiosystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <string>

#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include "pbd/file_utils.h"
#include "pbd/strsplit.h"

#include "ardour/chord_provider.h"
#include "ardour/filesystem_paths.h"
#include "ardour/libardour_visibility.h"
#include "ardour/parameter_descriptor.h"
#include "ardour/search_paths.h"

#include "pbd/i18n.h"

using namespace ARDOUR;

std::vector<ChordProvider::ChordInfo> ChordProvider::chord_info;

int64_t
ChordProvider::hash_intervals (ChordProvider::Intervals const & intervals)
{
	assert (!intervals.empty());

	const int64_t max_interval = 25; /* one larger than max interval in semitones */
	int64_t mult = max_interval;
	int64_t ret = intervals[0];

	for (auto n = 1U; n < intervals.size(); ++n) {
		assert (intervals[n] < max_interval);

		ret += mult * intervals[n];
		mult *= max_interval;
	}

	return ret;
}

void
ChordProvider::load_12tet_chords ()
{
	std::string path;

	if (!find_file (ardour_data_search_path(), "chords.txt", path)) {
		std::cerr << "Chord definitions not found!\n";
		return;
	}

	load (path);
}

bool
ChordProvider::add_chord (ChordInfo const & new_chord)
{
	for (auto & ci : chord_info) {
		if (new_chord.hashed == ci.hashed ||
		    new_chord.canonical_name == ci.canonical_name ||
		    new_chord.short_name == ci.short_name) {
			return false;
		}
	}

	chord_info.push_back (new_chord);
	save ();
	return true;
}

static inline
int
pitch_to_pitch_class (int pitch)
{
	return pitch % 12;
}

static inline
int
canonical_interval (int interval)
{
	return (((interval % 12) + 12) % 12);
}

static std::vector<int>
to_pitch_class (std::vector<int> const & pitches)
{
	/* It migbt seem obvious to use a std::set<> here, but the pitch
	   classes we return must be in the same order as the pitches we are
	   provided, which std::set<> makes hard to do.
	*/
	std::vector<int> v;
	int mask = 0;
	for (int n : pitches) {
		int pc = pitch_to_pitch_class (n);
		if (!(mask & (1<<pc))) {
			v.push_back (pc);
			mask |= (1<<pc);
		}
	}
	return v;
}

static ChordProvider::Intervals
to_intervals (std::vector<int> const & pcs, int root)
{
	ChordProvider::Intervals iv;
	for (int pc : pcs) {
		int d = canonical_interval (pc - root);
		iv.push_back (d);
	}
	return iv;
}

std::string
ChordProvider::identify_chord (std::vector<int> const & pitches)
{
	if (pitches.empty()) {
		return "";
	}

	if (pitches.size() < 2) {
		return _("Not a chord");
		return "";
	}

	if (chord_info.empty() ){
		load_12tet_chords ();
	}

	int bass = *std::min_element (pitches.begin(), pitches.end());
	int bass_class = pitch_to_pitch_class (bass);
	std::vector<int> pcs = to_pitch_class (pitches);

	for (int root : pcs) {
		auto intervals = to_intervals (pcs, root);
		int64_t hashed = hash_intervals (intervals);;

		for (auto const & ci : chord_info) {

			if (hashed == ci.hashed) {
				std::string ret;
				/* translate note names but no enharmonics */
				ret = ParameterDescriptor::midi_note_name (root, true, false, false) + ' ';
				ret += ci.canonical_name;

				if (bass_class != root) {
					/* slash chord */
					ret += '/';
					ret += ParameterDescriptor::midi_note_name (bass_class, true, false, false);
				}
				return ret;
			}
		}
	}

	return _("Unknown");
}

std::string
ChordProvider::canonical_name (Intervals const & intervals)
{
	int64_t hashed = hash_intervals (intervals);

	for (auto const & ci : chord_info) {
		if (ci.hashed == hashed) {
			return ci.canonical_name;
		}
	}

	return std::string ();
}


std::string
ChordProvider::short_name (Intervals const & intervals)
{
	int64_t hashed = hash_intervals (intervals);

	for (auto const & ci : chord_info) {
		if (ci.hashed == hashed) {
			return ci.short_name;
		}
	}

	return std::string ();
}

std::vector<std::string>
ChordProvider::other_names (Intervals const & intervals)
{
	int64_t hashed = hash_intervals (intervals);

	for (auto const & ci : chord_info) {
		if (ci.hashed == hashed) {
			return ci.other_names;
		}
	}

	return std::vector<std::string> ();
}

ChordProvider::ChordInfo const *
ChordProvider::by_short_name (std::string const & sn) const
{
	for (auto const & ci : chord_info) {
		if (ci.short_name == sn) {
			return &ci;
		}
	}

	return nullptr;
}

ChordProvider::ChordInfo const *
ChordProvider::by_canonical_name (std::string const & sn) const
{
	for (auto const & ci : chord_info) {
		if (ci.canonical_name == sn) {
			return &ci;
		}
	}

	return nullptr;
}

ChordProvider::ChordInfo const *
ChordProvider::by_any_name (std::string const & sn) const
{
	for (auto const & ci : chord_info) {
		if (ci.canonical_name == sn ||
		    ci.short_name == sn) {
			return &ci;
		}
		for (auto const & on : ci.other_names) {
			if (on == sn) {
				return &ci;
			}
		}
	}

	return nullptr;
}


int
ChordProvider::load (std::string const & path)
{
	using namespace std;

	ifstream f (path);

	if (!f.good()) {
		return -1;
	}

	string line;

	while (1) {

		getline (f, line);

		if (!f.good()) {
			return -1;
		}
		if (line.empty() || line[0] == '#') {
			continue;
		}

		vector<string> parts;
		split (line, parts, ';');

		if (parts.size() < 3) {
			continue;
		}

		vector<string> intervals;
		split (parts[0], intervals, ',');

		Intervals ints;
		for (auto const & i : intervals) {
			int n = atoi (i.c_str());
			ints.push_back (n);
		}

		vector<string> others;
		for (size_t on = 3; on < parts.size(); ++on) {
			others.push_back (parts[on]);
		}

		chord_info.push_back (ChordInfo (ints, hash_intervals (ints), parts[1], parts[2], others));
	}

	return 0;
}

int
ChordProvider::save ()
{
	std::string path = Glib::build_filename (user_config_directory(), "chords.txt");
	std::ofstream f (path, std::ofstream::trunc);

	if (!f) {
		return -1;
	}

	for (auto const & ci : chord_info) {
		for (auto n : ci.intervals) {
			f << n << ',';
		}
		f << ';'
		  << ci.canonical_name
		  << ';'
		  << ci.short_name
		  << ';';
		for (auto const & o : ci.other_names) {
			f << o << ';';
		}
		f << '\n';
	}

	f.close ();

	return 0;
}
