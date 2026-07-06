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

#pragma once

#include <ytkmm/listviewtext.h>
#include <ytkmm/scrolledwindow.h>

#include "ardour/chord_provider.h"
#include "widgets/ardour_button.h"

#include "ardour_dialog.h"

class ChordEditor;
class EditingContext;

class ChordDialog : public ArdourDialog
{
  public:
	ChordDialog (EditingContext&, ARDOUR::ChordProvider&, int chord_size);
	ARDOUR::ChordProvider::ChordInfo get_chord () const;
	bool is_protected() const;

 private:
	ChordEditor* editor;
	Gtk::ListViewText chord_list;
	Gtk::ScrolledWindow scroller;
	ArdourWidgets::ArdourButton add_chord_button;

	void chord_selected ();
	void add_chord ();
};
