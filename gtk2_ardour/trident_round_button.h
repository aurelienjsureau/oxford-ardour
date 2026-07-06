/*
 * TridentRoundButton — petit bouton-poussoir ROND (toggle) dessiné en Cairo.
 * Pour le HPF 50 Hz : compact, placé entre les potards de Low.
 * Off = corps sombre ; On = corps clair (façon switch blanc engagé du 80B).
 * Header-only.
 */
#pragma once

#include <functional>
#include <string>
#include <cmath>
#include <ytkmm/drawingarea.h>
#include "trident_texture.h"

class TridentRoundButton : public Gtk::DrawingArea
{
public:
	TridentRoundButton (const std::string& label = "")
		: _label (label)
	{
		_s = trident_ui_scale ();
		set_size_request ((int) (24 * _s), (int) (34 * _s));   // bouton + légende (scalé DPI)
		add_events (Gdk::BUTTON_PRESS_MASK);
	}

	void on_clicked (std::function<void()> cb) { _cb = cb; }
	void set_label (const std::string& s) { _label = s; queue_draw (); }
	void set_active (bool a) { _active = a; queue_draw (); }
	void set_bg (double r, double g, double b) { _bgr = r; _bgg = g; _bgb = b; queue_draw (); }
	guint last_state () const { return _lastState; }   // modificateurs du dernier clic (Alt...)

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

		trident_fill_panel (cr, 0, 0, w, h, _bgr, _bgg, _bgb);

		const double cx = w * 0.5;
		const double cy = 11.0;
		const double R  = 8.0;

		/* drop shadow */
		Cairo::RefPtr<Cairo::RadialGradient> sh =
			Cairo::RadialGradient::create (cx + 1.0, cy + 2.0, R * 0.2, cx + 1.0, cy + 2.0, R + 3.0);
		sh->add_color_stop_rgba (0.0, 0, 0, 0, 0.55);
		sh->add_color_stop_rgba (0.7, 0, 0, 0, 0.30);
		sh->add_color_stop_rgba (1.0, 0, 0, 0, 0.0);
		cr->set_source (sh);
		cr->arc (cx + 1.0, cy + 2.0, R + 3.0, 0, 2 * M_PI); cr->fill ();

		/* corps rond : clair si engagé (switch blanc 80B), sombre sinon */
		Cairo::RefPtr<Cairo::RadialGradient> body =
			Cairo::RadialGradient::create (cx - R * 0.4, cy - R * 0.4, R * 0.1, cx, cy, R * 1.15);
		if (_active) {
			body->add_color_stop_rgb (0.0, 0.95, 0.96, 0.98);
			body->add_color_stop_rgb (0.55, 0.80, 0.82, 0.85);
			body->add_color_stop_rgb (1.0, 0.45, 0.47, 0.50);
		} else {
			body->add_color_stop_rgb (0.0, 0.34, 0.36, 0.39);
			body->add_color_stop_rgb (0.55, 0.165, 0.176, 0.188);
			body->add_color_stop_rgb (1.0, 0.07, 0.08, 0.09);
		}
		cr->set_source (body);
		cr->arc (cx, cy, R, 0, 2 * M_PI); cr->fill ();
		cr->set_line_width (1.0);
		cr->set_source_rgb (0.0, 0.0, 0.0);
		cr->arc (cx, cy, R, 0, 2 * M_PI); cr->stroke ();

		/* légende sous le bouton */
		if (!_label.empty ()) {
			cr->set_source_rgb (_active ? 0.92 : 0.70, _active ? 0.94 : 0.74, _active ? 0.97 : 0.80);
			cr->set_font_size (8.0);
			Cairo::TextExtents te; cr->get_text_extents (_label, te);
			cr->move_to (cx - te.width * 0.5, h - 2.0);
			cr->show_text (_label);
		}
		return true;
	}

	bool on_button_press_event (GdkEventButton* e) override
	{
		_lastState = e ? e->state : 0;   // mémorise Alt/Ctrl... (pour le link sélection)
		if (_cb) { _cb (); }
		return true;
	}

private:
	std::string _label;
	guint  _lastState { 0 };
	bool   _active { false };
	double _bgr { 0.227 }, _bgg { 0.239 }, _bgb { 0.259 };
	double _s { 1.0 };   // facteur d'échelle DPI
	std::function<void()> _cb;
};
