/*
 * TridentKnob — potard cairo autonome (gtkmm2) pour la tranche Trident.
 * Drag vertical pour régler ; callback std::function sur changement.
 * Header-only (pas de modif wscript).
 */
#pragma once

#include <functional>
#include <cmath>
#include <string>
#include <cstdio>
#include <ytkmm/drawingarea.h>
#include <cairomm/surface.h>
#include "trident_texture.h"

class TridentKnob : public Gtk::DrawingArea
{
public:
	TridentKnob (double mn, double mx, double def, const std::string& label,
	             double r = 0.165, double g = 0.176, double b = 0.188)  /* défaut #2a2d30 */
		: _min (mn), _max (mx), _val (def), _def (def), _label (label), _r (r), _g (g), _b (b)
	{
		_s = trident_ui_scale ();
		set_size_request ((int) (56 * _s), (int) (82 * _s));   // corps + label + afficheur LED
		add_events (Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::POINTER_MOTION_MASK);
	}

	void on_changed (std::function<void(double)> cb) { _cb = cb; }
	void on_linked  (std::function<void(double)> cb) { _linkCb = cb; } // diffusion du delta (Alt + sélection)
	void on_format  (std::function<std::string(double)> f) { _fmt = f; } // texte de la valeur
	void set_label  (const std::string& l) { _label = l; queue_draw (); } // relabel (ex. Drive<->Hdrm master)
	void set_range  (double mn, double mx, double def) { _min = mn; _max = mx; _def = def; _val = clamp (def); _raw = _val; queue_draw (); } // recourse (ex. master = Hdrm bipolaire)
	void set_bg (double r, double g, double b) { _bgr = r; _bgg = g; _bgb = b; queue_draw (); }
	void set_ui_scale (double m) { _s = trident_ui_scale () * m; set_size_request ((int) (56 * _s), (int) (82 * _s)); queue_draw (); } // agrandissement (panel Oxford ×1.5)
	void set_log (bool l) { _log = l; queue_draw (); }   // échelle logarithmique (ex. Q de l'EQ Oxford)
	void set_image (Cairo::RefPtr<Cairo::ImageSurface> s) { _img = s; queue_draw (); }   // skin image (rotée selon la valeur)
	void set_detent (double step) { _step = step; _val = snap (clamp (_val)); _raw = _val; } // crans (valeurs fixes)

	void set_value (double v, bool notify = false) { _val = snap (clamp (v)); _raw = _val; queue_draw (); if (notify && _cb) _cb (_val); }
	double get_value () const { return _val; }

protected:
	bool on_expose_event (GdkEventExpose*) override
	{
		Glib::RefPtr<Gdk::Window> win = get_window ();
		if (!win) return true;
		Cairo::RefPtr<Cairo::Context> cr = win->create_cairo_context ();

		/* échelle DPI : on dessine en coordonnées "logiques", Cairo scale -> taille
		 * + texte + traits suivent le 4K comme les widgets natifs d'Ardour. */
		cr->scale (_s, _s);
		Gtk::Allocation a = get_allocation ();
		const double w = a.get_width () / _s;
		const double hgt = a.get_height () / _s;

		auto cl = [](double v){ return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); };

		/* fond = panneau texturé (métal brossé, #3a3d42 par défaut) */
		trident_fill_panel (cr, 0, 0, w, hgt, _bgr, _bgg, _bgb);

		const double cx  = w * 0.5;
		const double cy  = 22.0;
		const double Rsk = 18.0;   // skirt (jupe large)
		const double Rbd = 12.0;   // corps (cap)

		const double t  = norm ();
		const double a0 = 135.0 * M_PI / 180.0;            // début (bas-gauche)
		const double a1 = (135.0 + 270.0) * M_PI / 180.0;  // fin (bas-droite)
		const double ang = a0 + t * (a1 - a0);

		if (_img) {
			/* === KNOB SKIN IMAGE + ombre portée douce (sous le knob, sans toucher le label) === */
			const double Rimg = 21.0;
			const int iw = _img->get_width (), ih = _img->get_height ();
			const double sc = (2.0 * Rimg) / (double) (iw > 0 ? iw : 108);
			Cairo::RefPtr<Cairo::RadialGradient> ish =
				Cairo::RadialGradient::create (cx + 1.5, cy + 4.0, Rimg * 0.3, cx + 1.5, cy + 4.0, Rimg + 2.5);
			ish->add_color_stop_rgba (0.0, 0, 0, 0, 0.45);
			ish->add_color_stop_rgba (0.6, 0, 0, 0, 0.26);
			ish->add_color_stop_rgba (1.0, 0, 0, 0, 0.0);
			cr->set_source (ish);
			cr->arc (cx + 1.5, cy + 4.0, Rimg + 2.5, 0, 2 * M_PI); cr->fill ();
			/* image FIXE (ombrage/lumière gravés -> on ne tourne PAS, sinon la lumière
			 * a l'air de bouger). La valeur est indiquée par un repère dessiné par-dessus. */
			cr->save ();
			cr->translate (cx, cy);
			cr->scale (sc, sc);
			cr->set_source (_img, -iw / 2.0, -ih / 2.0);
			cr->paint ();
			cr->restore ();
			/* repère de valeur (tourne) : BLANC, repoussé vers la collerette du cap */
			const double r1 = Rimg * 0.34, r2 = Rimg * 0.66;
			const double ca = std::cos (ang), sa = std::sin (ang);
			cr->set_line_cap (Cairo::LINE_CAP_ROUND);
			cr->set_line_width (3.4);
			cr->set_source_rgba (0.08, 0.10, 0.16, 0.55);   // fin liseré sombre (lisibilité)
			cr->move_to (cx + ca * r1, cy + sa * r1); cr->line_to (cx + ca * r2, cy + sa * r2); cr->stroke ();
			cr->set_line_width (2.1);
			cr->set_source_rgb (1.0, 1.0, 1.0);             // repère BLANC
			cr->move_to (cx + ca * r1, cy + sa * r1); cr->line_to (cx + ca * r2, cy + sa * r2); cr->stroke ();
		} else {
		/* --- DROP SHADOW sous la jupe (forcée + agrandie, décalée bas-droite) --- */
		Cairo::RefPtr<Cairo::RadialGradient> ksh =
			Cairo::RadialGradient::create (cx + 3.0, cy + 5.0, Rsk * 0.2, cx + 3.0, cy + 5.0, Rsk + 3.0);
		ksh->add_color_stop_rgba (0.0, 0, 0, 0, 0.72);
		ksh->add_color_stop_rgba (0.55, 0, 0, 0, 0.50);
		ksh->add_color_stop_rgba (1.0, 0, 0, 0, 0.0);
		cr->set_source (ksh);
		cr->arc (cx + 3.0, cy + 5.0, Rsk + 3.0, 0, 2 * M_PI); cr->fill ();

		/* --- JUPE (large skirt) : disque sombre dégradé + cerne --- */
		Cairo::RefPtr<Cairo::RadialGradient> skirt =
			Cairo::RadialGradient::create (cx - Rsk * 0.35, cy - Rsk * 0.35, Rsk * 0.15, cx, cy, Rsk);
		skirt->add_color_stop_rgb (0.0, 0.27, 0.28, 0.30);
		skirt->add_color_stop_rgb (0.6, 0.18, 0.19, 0.21);
		skirt->add_color_stop_rgb (1.0, 0.09, 0.10, 0.11);
		cr->set_source (skirt);
		cr->arc (cx, cy, Rsk, 0, 2 * M_PI); cr->fill ();
		cr->set_line_width (1.0);
		cr->set_source_rgb (0.05, 0.05, 0.06);
		cr->arc (cx, cy, Rsk, 0, 2 * M_PI); cr->stroke ();

		/* --- graduations gravées autour de la jupe --- */
		cr->set_line_width (1.0);
		cr->set_source_rgb (0.55, 0.57, 0.60);
		for (int i = 0; i <= 8; ++i) {
			const double th = a0 + (i / 8.0) * (a1 - a0);
			cr->move_to (cx + std::cos (th) * (Rsk - 2.5), cy + std::sin (th) * (Rsk - 2.5));
			cr->line_to (cx + std::cos (th) * (Rsk - 0.5), cy + std::sin (th) * (Rsk - 0.5));
			cr->stroke ();
		}

		/* --- ombre portée du cap sur la jupe (cap en relief, forcée + agrandie) --- */
		Cairo::RefPtr<Cairo::RadialGradient> csh =
			Cairo::RadialGradient::create (cx + 2.0, cy + 3.0, Rbd * 0.3, cx + 2.0, cy + 3.0, Rbd + 5.0);
		csh->add_color_stop_rgba (0.0, 0, 0, 0, 0.65);
		csh->add_color_stop_rgba (0.6, 0, 0, 0, 0.38);
		csh->add_color_stop_rgba (1.0, 0, 0, 0, 0.0);
		cr->set_source (csh);
		cr->arc (cx + 2.0, cy + 3.0, Rbd + 5.0, 0, 2 * M_PI); cr->fill ();

		/* --- CORPS (cap) : dégradé radial avec reflet en HAUT-GAUCHE --- */
		Cairo::RefPtr<Cairo::RadialGradient> body =
			Cairo::RadialGradient::create (cx - Rbd * 0.4, cy - Rbd * 0.4, Rbd * 0.1, cx, cy, Rbd * 1.15);
		body->add_color_stop_rgb (0.0, cl (_r * 1.45 + 0.18), cl (_g * 1.45 + 0.18), cl (_b * 1.45 + 0.18));
		body->add_color_stop_rgb (0.5, _r, _g, _b);
		body->add_color_stop_rgb (1.0, _r * 0.45, _g * 0.45, _b * 0.45);
		cr->set_source (body);
		cr->arc (cx, cy, Rbd, 0, 2 * M_PI); cr->fill ();
		/* cerne du corps */
		cr->set_line_width (1.0);
		cr->set_source_rgb (0.0, 0.0, 0.0);
		cr->arc (cx, cy, Rbd, 0, 2 * M_PI); cr->stroke ();

		/* --- repère 2px : ambre uniforme #e8902a (cohérent sur tous les knobs) --- */
		cr->set_line_width (2.0);
		cr->set_line_cap (Cairo::LINE_CAP_ROUND);
		cr->set_source_rgb (0.910, 0.565, 0.165);
		cr->move_to (cx + std::cos (ang) * (Rbd * 0.20), cy + std::sin (ang) * (Rbd * 0.20));
		cr->line_to (cx + std::cos (ang) * (Rbd - 1.5), cy + std::sin (ang) * (Rbd - 1.5));
		cr->stroke ();
		}  /* fin du rendu procédural (sinon image) */

		/* --- label sérigraphie (mono condensé) : couleur adaptative au fond --- */
		cr->select_font_face ("ArdourMono", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_NORMAL);
		cr->set_font_size (8.0);
		{ const double lum = 0.299 * _bgr + 0.587 * _bgg + 0.114 * _bgb;
		  if (lum > 0.5) cr->set_source_rgb (0.11, 0.14, 0.20);   // fond clair -> texte sombre
		  else           cr->set_source_rgb (0.86, 0.89, 0.93); } // fond foncé -> texte clair
		{
			Cairo::TextExtents te; cr->get_text_extents (_label, te);
			cr->move_to (cx - te.width * 0.5, 51.0);
			cr->show_text (_label);
		}

		/* --- afficheur LED : boîte noire + valeur vert-jaune (#c8e030) mono --- */
		std::string val;
		if (_fmt) { val = _fmt (_val); }
		else { char b[24]; std::snprintf (b, sizeof b, "%.1f", _val); val = b; }
		const double lx = 3.0, ly = 58.0, lw = w - 6.0, lh = 15.0, rr = 3.5;
		cr->set_source_rgb (0.035, 0.035, 0.035);
		cr->move_to (lx + rr, ly);
		cr->line_to (lx + lw - rr, ly);            cr->arc (lx + lw - rr, ly + rr, rr, -M_PI / 2, 0);
		cr->line_to (lx + lw, ly + lh - rr);       cr->arc (lx + lw - rr, ly + lh - rr, rr, 0, M_PI / 2);
		cr->line_to (lx + rr, ly + lh);            cr->arc (lx + rr, ly + lh - rr, rr, M_PI / 2, M_PI);
		cr->line_to (lx, ly + rr);                 cr->arc (lx + rr, ly + rr, rr, M_PI, 3 * M_PI / 2);
		cr->close_path (); cr->fill ();
		cr->set_source_rgb (_dragging ? 1.0 : 0.784, _dragging ? 1.0 : 0.878, _dragging ? 0.4 : 0.188);
		cr->set_font_size (9.0);
		{
			Cairo::TextExtents te; cr->get_text_extents (val, te);
			cr->move_to (cx - te.width * 0.5, ly + lh - 4.0);
			cr->show_text (val);
		}
		return true;
	}

	bool on_button_press_event (GdkEventButton* e) override
	{
		if (e->type == GDK_2BUTTON_PRESS) {       // double-clic = reset au défaut
			_dragging = false;
			_val = snap (clamp (_def)); _raw = _val;
			queue_draw ();
			if (_cb) _cb (_val);
			return true;
		}
		_dragging = true; _lastY = e->y; _raw = _val; return true;
	}
	bool on_button_release_event (GdkEventButton*) override { _dragging = false; return true; }

	bool on_motion_notify_event (GdkEventMotion* e) override
	{
		if (!_dragging) return true;
		_altMod = (e->state & GDK_MOD1_MASK) != 0;   // Alt -> diffusion sur la sélection
		const double dy = _lastY - e->y;     // monter = augmenter
		_lastY = e->y;
		if (_log && _min > 0.0) {
			const double lmin = std::log (_min), lmax = std::log (_max);
			double lr = std::log (_raw <= 0.0 ? _min : _raw) + dy * (lmax - lmin) / (150.0 * _s);
			if (lr < lmin) lr = lmin; if (lr > lmax) lr = lmax;
			_raw = std::exp (lr);
		} else {
			const double span = _max - _min;
			_raw = clamp (_raw + dy * span / (150.0 * _s));   // accumulateur continu (DPI-aware)
		}
		const double nv = snap (_raw);
		if (nv != _val) {
			const double delta = nv - _val;
			_val = nv; queue_draw ();
			if (_cb) _cb (_val);
			if (_altMod && _linkCb) _linkCb (delta);   // même delta sur les tranches sélectionnées
		} else {
			queue_draw ();
		}
		return true;
	}

private:
	double clamp (double v) const { return v < _min ? _min : (v > _max ? _max : v); }
	double snap  (double v) const { return _step > 0.0 ? clamp (std::round (v / _step) * _step) : v; } // crans
	double norm () const {
		if (_max <= _min) return 0.0;
		if (_log && _min > 0.0) return std::log (_val / _min) / std::log (_max / _min);
		return (_val - _min) / (_max - _min);
	}

	double _min, _max, _val, _def;
	double _raw { 0.0 };   // valeur continue interne (drag)
	double _step { 0.0 };  // pas de détente (0 = lisse)
	double _s { 1.0 };     // facteur d'échelle DPI
	double _bgr { 0.071 }, _bgg { 0.078 }, _bgb { 0.098 };
	std::string _label;
	double _r, _g, _b;
	std::function<void(double)> _cb;
	std::function<void(double)> _linkCb;   // diffusion delta (Alt+sélection)
	std::function<std::string(double)> _fmt;
	bool   _dragging { false };
	bool   _altMod { false };
	bool   _log { false };   // échelle logarithmique (Q EQ)
	double _lastY { 0.0 };
	Cairo::RefPtr<Cairo::ImageSurface> _img;   // skin image optionnelle
};
