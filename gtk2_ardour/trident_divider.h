/*
 * TridentDivider — séparateur de section 1px (#555a60), dessiné en Cairo.
 * Header-only. Hauteur fixe (1px + marge verticale optionnelle).
 */
#pragma once

#include <cmath>
#include <ytkmm/drawingarea.h>
#include "trident_texture.h"

class TridentDivider : public Gtk::DrawingArea
{
public:
	TridentDivider (double r = 0.333, double g = 0.353, double b = 0.376)  /* #555a60 */
		: _r (r), _g (g), _b (b)
	{
		_s = trident_ui_scale ();
		set_size_request (-1, (int) (3 * _s));   // 1px ligne + marge (scalé DPI)
	}

	void set_bg (double r, double g, double b) { _bgr = r; _bgg = g; _bgb = b; queue_draw (); }

protected:
	bool on_expose_event (GdkEventExpose*) override
	{
		Glib::RefPtr<Gdk::Window> win = get_window ();
		if (!win) { return true; }
		Cairo::RefPtr<Cairo::Context> cr = win->create_cairo_context ();

		cr->scale (_s, _s);
		Gtk::Allocation a = get_allocation ();
		const double w = a.get_width () / _s;
		const double h = a.get_height () / _s;

		/* fond = panneau texturé */
		trident_fill_panel (cr, 0, 0, w, h, _bgr, _bgg, _bgb);

		/* ligne 1px centrée + ombre douce dessous (relief gravé) */
		const double y = std::floor (h * 0.5) + 0.5;
		cr->set_line_width (1.0);
		cr->set_source_rgba (0, 0, 0, 0.5);
		cr->move_to (0, y + 1.0); cr->line_to (w, y + 1.0); cr->stroke ();
		cr->set_source_rgb (_r, _g, _b);
		cr->move_to (0, y); cr->line_to (w, y); cr->stroke ();
		return true;
	}

private:
	double _r, _g, _b;
	double _bgr { 0.227 }, _bgg { 0.239 }, _bgb { 0.259 };  // #3a3d42
	double _s { 1.0 };   // facteur d'échelle DPI
};
