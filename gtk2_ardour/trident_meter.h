/*
 * TridentMeter — petit afficheur cairo (gtkmm2) pour la tranche Trident.
 * Trois styles :
 *   BarUp    : barre du bas vers le haut (vert -> jaune -> rouge) — niveau
 *   BarDown  : barre du haut vers le bas (ambre) — gain reduction
 *   NeedleVU : VU à aiguille style analogique — niveau de tape/bus
 * set_fill(0..1) puis queue_draw. Header-only.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <ytkmm/drawingarea.h>
#include "trident_texture.h"

class TridentMeter : public Gtk::DrawingArea
{
public:
	enum Style { BarUp, BarDown, NeedleVU, GRLadder };

	TridentMeter (Style style = BarUp, const std::string& label = "")
		: _style (style), _label (label)
	{
		_s = trident_ui_scale ();
		if (_style == NeedleVU) {
			set_size_request ((int) (78 * _s), (int) (88 * _s));
		} else if (_style == GRLadder) {
			set_size_request ((int) (34 * _s), (int) (66 * _s));   // échelle LED + libellés dB (Harrison)
		} else {
			set_size_request ((int) (16 * _s), (int) (56 * _s));
		}
	}

	// remplissage normalisé 0..1
	void set_fill (double f)
	{
		f = f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
		if (f == _target) { return; }
		_target = f;
		queue_draw ();
	}

	void set_caption (const std::string& s) { _caption = s; queue_draw (); }
	void set_bg (double r, double g, double b) { _bgr = r; _bgg = g; _bgb = b; queue_draw (); }
	// valeur GR (dB) ; <0 = comp OFF. Toujours redessiner : anime la décroissance
	// de la crête tenue (appelé périodiquement par le timer GUI ~60 ms).
	void set_reading (double db) { _reading = db; queue_draw (); }

protected:
	bool on_expose_event (GdkEventExpose*) override
	{
		Glib::RefPtr<Gdk::Window> win = get_window ();
		if (!win) { return true; }
		Cairo::RefPtr<Cairo::Context> cr = win->create_cairo_context ();

		cr->scale (_s, _s);   // échelle DPI (taille + texte + traits)
		Gtk::Allocation a = get_allocation ();
		const double w = a.get_width () / _s;
		const double h = a.get_height () / _s;

		/* fond = panneau texturé (métal brossé, #3a3d42 par défaut) */
		trident_fill_panel (cr, 0, 0, w, h, _bgr, _bgg, _bgb);

		/* ballistique seulement pour l'aiguille VU ; les barres (niveau/GR)
		 * suivent la cible directement, sinon une valeur résiduelle reste figée
		 * quand la cible retombe à 0 (ex. comp bypassé). */
		if (_style == NeedleVU) {
			_disp += (_target - _disp) * 0.35;
		} else {
			_disp = _target;
		}

		if (_style == NeedleVU) {
			draw_needle (cr, w, h);
		} else if (_style == GRLadder) {
			/* maintien de crête : montée immédiate, descente lente (lisible) */
			const double cur = _reading > 0.0 ? _reading : 0.0;
			if (cur > _hold) { _hold = cur; } else { _hold -= 0.35; if (_hold < 0.0) _hold = 0.0; }
			if (_reading < 0.0) { _hold = 0.0; }   // comp OFF
			draw_ladder (cr, w, h, cur);
		} else {
			draw_bar (cr, w, h);
		}
		return true;
	}

private:
	/* Échelle GR à crans fixes façon Harrison/Mixbus : LED 3/6/10/14/20 dB,
	 * vert -> ambre -> rouge. Crête tenue (_hold) pour rester lisible. */
	void draw_ladder (Cairo::RefPtr<Cairo::Context>& cr, double w, double h, double cur)
	{
		struct Seg { double db; double r, g, b; };
		static const Seg segs[5] = {
			{ 20.0, 0.90, 0.15, 0.10 },   // rouge
			{ 14.0, 0.90, 0.15, 0.10 },   // rouge
			{ 10.0, 0.95, 0.70, 0.10 },   // ambre
			{  6.0, 0.20, 0.82, 0.22 },   // vert
			{  3.0, 0.20, 0.82, 0.22 },   // vert
		};
		const double pad   = 3.0;
		const int    n     = 5;
		const double rowH  = (h - 2.0 * pad) / n;
		const double ledW  = 12.0;
		const double ledH  = rowH - 3.0;
		const double ledX  = pad;

		cr->select_font_face ("ArdourSans", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_NORMAL);
		cr->set_font_size (8.0);

		for (int i = 0; i < n; ++i) {
			const double y   = pad + i * rowH;
			const bool   lit = (_hold >= segs[i].db) || (cur >= segs[i].db);
			const double f   = lit ? 1.0 : 0.16;   // éteint = sombre

			/* corps de LED (dégradé léger pour le relief) */
			Cairo::RefPtr<Cairo::LinearGradient> g = Cairo::LinearGradient::create (ledX, y, ledX, y + ledH);
			g->add_color_stop_rgb (0.0, segs[i].r * f * 1.15, segs[i].g * f * 1.15, segs[i].b * f * 1.15);
			g->add_color_stop_rgb (1.0, segs[i].r * f * 0.65, segs[i].g * f * 0.65, segs[i].b * f * 0.65);
			cr->set_source (g);
			cr->rectangle (ledX, y, ledW, ledH);
			cr->fill ();
			/* cerne */
			cr->set_line_width (0.8);
			cr->set_source_rgb (0.04, 0.04, 0.05);
			cr->rectangle (ledX + 0.5, y + 0.5, ledW - 1.0, ledH - 1.0);
			cr->stroke ();
			/* petit reflet quand allumé */
			if (lit) {
				cr->set_source_rgba (1, 1, 1, 0.25);
				cr->rectangle (ledX + 1.5, y + 1.5, ledW - 3.0, ledH * 0.30);
				cr->fill ();
			}

			/* libellé dB à droite */
			char b[8]; std::snprintf (b, sizeof b, "%.0f", segs[i].db);
			cr->set_source_rgb (0.78, 0.82, 0.88);
			Cairo::TextExtents te; cr->get_text_extents (b, te);
			cr->move_to (ledX + ledW + 3.0, y + ledH * 0.5 + te.height * 0.5);
			cr->show_text (b);
		}
	}

	void draw_bar (Cairo::RefPtr<Cairo::Context>& cr, double w, double h)
	{
		const double pad  = 2.0;
		const double barW = w - 2 * pad;
		const double top  = pad;
		const double barH = h - 2 * pad - 10.0; // 10px pour le texte

		/* puits encastré (léger dégradé) */
		Cairo::RefPtr<Cairo::LinearGradient> bg = Cairo::LinearGradient::create (pad, 0, pad + barW, 0);
		bg->add_color_stop_rgb (0.0, 0.05, 0.06, 0.08);
		bg->add_color_stop_rgb (0.5, 0.10, 0.11, 0.13);
		bg->add_color_stop_rgb (1.0, 0.05, 0.06, 0.08);
		cr->set_source (bg);
		cr->rectangle (pad, top, barW, barH);
		cr->fill ();

		const double fillH = _disp * barH;
		if (_style == BarDown) {
			Cairo::RefPtr<Cairo::LinearGradient> grad = Cairo::LinearGradient::create (0, top, 0, top + barH);
			grad->add_color_stop_rgb (0.0, 1.0,  0.78, 0.30);  // GR ambre, haut->bas
			grad->add_color_stop_rgb (1.0, 0.85, 0.45, 0.05);
			cr->set_source (grad);
			cr->rectangle (pad, top, barW, fillH);
			cr->fill ();
		} else {
			const double y0 = top + barH - fillH;             // niveau bas->haut
			Cairo::RefPtr<Cairo::LinearGradient> grad = Cairo::LinearGradient::create (0, top + barH, 0, top);
			grad->add_color_stop_rgb (0.0, 0.10, 0.80, 0.20);
			grad->add_color_stop_rgb (0.7, 0.85, 0.85, 0.10);
			grad->add_color_stop_rgb (1.0, 0.90, 0.15, 0.10);
			cr->set_source (grad);
			cr->rectangle (pad, y0, barW, fillH);
			cr->fill ();
		}

		/* graduations horizontales gravées */
		cr->set_line_width (1.0);
		cr->set_source_rgba (0.0, 0.0, 0.0, 0.45);
		for (int i = 1; i < 5; ++i) {
			const double y = top + (i / 5.0) * barH;
			cr->move_to (pad, y); cr->line_to (pad + barW, y); cr->stroke ();
		}

		cr->set_line_width (1.0);
		cr->set_source_rgb (0.0, 0.0, 0.0);
		cr->rectangle (pad, top, barW, barH);
		cr->stroke ();

		/* valeur chiffrée (dB) en haut de la barre, p.ex. GR "-5" */
		if (_reading >= 0.0) {
			char b[12];
			std::snprintf (b, sizeof b, "%.0f", _reading);
			cr->select_font_face ("ArdourSans", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_BOLD);
			cr->set_font_size (9.0);
			Cairo::TextExtents te; cr->get_text_extents (b, te);
			const double tx = pad + (barW - te.width) * 0.5;
			/* fond sombre pour lisibilité */
			cr->set_source_rgba (0, 0, 0, 0.55);
			cr->rectangle (tx - 1.0, top + 1.0, te.width + 2.0, 11.0); cr->fill ();
			cr->set_source_rgb (1.0, 0.92, 0.55);
			cr->move_to (tx, top + 10.0);
			cr->show_text (b);
		}

		caption (cr, w, h - 1.0);
	}

	void draw_needle (Cairo::RefPtr<Cairo::Context>& cr, double w, double h)
	{
		/* --- boîtier sombre --- */
		cr->set_source_rgb (0.06, 0.07, 0.09);
		cr->rectangle (0, 0, w, h); cr->fill ();

		const double fx = 2.0, fy = 2.0, fw = w - 4.0, fh = h - 4.0;

		/* --- cadran PARCHEMIN chaud #c8b97a (réf VU Trident 80B) --- */
		cr->set_source_rgb (0.784, 0.725, 0.478);   /* #c8b97a */
		cr->rectangle (fx, fy, fw, fh); cr->fill ();

		/* ombre interne (encastrement réaliste) : haut + gauche assombris */
		Cairo::RefPtr<Cairo::LinearGradient> ish = Cairo::LinearGradient::create (0, fy, 0, fy + fh * 0.35);
		ish->add_color_stop_rgba (0.0, 0, 0, 0, 0.28);
		ish->add_color_stop_rgba (1.0, 0, 0, 0, 0.0);
		cr->set_source (ish);
		cr->rectangle (fx, fy, fw, fh * 0.35); cr->fill ();
		Cairo::RefPtr<Cairo::LinearGradient> isl = Cairo::LinearGradient::create (fx, 0, fx + fw * 0.25, 0);
		isl->add_color_stop_rgba (0.0, 0, 0, 0, 0.22);
		isl->add_color_stop_rgba (1.0, 0, 0, 0, 0.0);
		cr->set_source (isl);
		cr->rectangle (fx, fy, fw * 0.25, fh); cr->fill ();

		const double cx = w * 0.5;
		const double cy = h - 8.0;        // pivot près du bas
		const double R  = h - 20.0;       // aiguille/graduations un peu plus courtes
		const double sweep = 50.0 * M_PI / 180.0;

		/* --- arc d'échelle (noir sur parchemin) --- */
		cr->set_line_width (1.2);
		cr->set_source_rgb (0.0, 0.0, 0.0);
		cr->arc (cx, cy, R, (270.0 - 50.0) * M_PI / 180.0, (270.0 + 50.0) * M_PI / 180.0);
		cr->stroke ();
		/* zone rouge (haut de l'échelle) */
		cr->set_line_width (2.4);
		cr->set_source_rgb (0.72, 0.10, 0.08);
		cr->arc (cx, cy, R, (270.0 + 17.0) * M_PI / 180.0, (270.0 + 50.0) * M_PI / 180.0);
		cr->stroke ();

		/* graduations noires */
		for (int i = 0; i <= 4; ++i) {
			const double t  = i / 4.0;
			const double th = -sweep + t * 2.0 * sweep;
			const double x0 = cx + std::sin (th) * (R - 3.0);
			const double y0 = cy - std::cos (th) * (R - 3.0);
			const double x1 = cx + std::sin (th) * R;
			const double y1 = cy - std::cos (th) * R;
			cr->set_line_width (1.0);
			cr->set_source_rgb (t > 0.7 ? 0.72 : 0.0, t > 0.7 ? 0.10 : 0.0, t > 0.7 ? 0.08 : 0.0);
			cr->move_to (x0, y0); cr->line_to (x1, y1); cr->stroke ();
		}

		/* --- aiguille BRUN FONCÉ + contrepoids (sur parchemin) --- */
		const double th = -sweep + _disp * 2.0 * sweep;
		const double nx = cx + std::sin (th) * (R - 1.0);
		const double ny = cy - std::cos (th) * (R - 1.0);
		cr->set_line_width (1.5);
		cr->set_source_rgb (0.25, 0.13, 0.05);   // brun foncé
		cr->move_to (cx - std::sin (th) * 4.0, cy + std::cos (th) * 4.0); // petit contrepoids
		cr->line_to (nx, ny); cr->stroke ();
		/* pivot */
		cr->set_source_rgb (0.20, 0.10, 0.04);
		cr->arc (cx, cy, 2.2, 0, 2 * M_PI); cr->fill ();

		/* --- reflet verre (translucide, haut) --- */
		Cairo::RefPtr<Cairo::LinearGradient> glass = Cairo::LinearGradient::create (0, fy, 0, fy + fh * 0.55);
		glass->add_color_stop_rgba (0.0, 1.0, 1.0, 1.0, 0.22);
		glass->add_color_stop_rgba (1.0, 1.0, 1.0, 1.0, 0.0);
		cr->set_source (glass);
		cr->rectangle (fx, fy, fw, fh * 0.55); cr->fill ();

		/* --- légende "VU" --- */
		const std::string& txt = _caption.empty () ? _label : _caption;
		if (!txt.empty ()) {
			cr->set_source_rgb (0.22, 0.22, 0.25);
			cr->select_font_face ("ArdourSans", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_BOLD);
			cr->set_font_size (8.0);
			Cairo::TextExtents te; cr->get_text_extents (txt, te);
			cr->move_to (cx - te.width * 0.5, h - 4.0);
			cr->show_text (txt);
		}

		/* cadre */
		cr->set_line_width (1.0);
		cr->set_source_rgb (0.0, 0.0, 0.0);
		cr->rectangle (fx, fy, fw, fh); cr->stroke ();
	}

	void caption (Cairo::RefPtr<Cairo::Context>& cr, double w, double y)
	{
		const std::string& txt = _caption.empty () ? _label : _caption;
		if (txt.empty ()) { return; }
		cr->set_source_rgb (0.80, 0.84, 0.90);
		cr->set_font_size (8.0);
		Cairo::TextExtents te; cr->get_text_extents (txt, te);
		cr->move_to (w * 0.5 - te.width * 0.5, y);
		cr->show_text (txt);
	}

	Style       _style;
	double      _bgr { 0.071 }, _bgg { 0.078 }, _bgb { 0.098 };
	std::string _label;
	std::string _caption;
	double      _target { 0.0 };
	double      _disp   { 0.0 };
	double      _reading { -1.0 };   // valeur chiffrée (dB), <0 = masquée
	double      _hold    { 0.0 };    // crête tenue (échelle GR Harrison)
	double      _s       { 1.0 };    // facteur d'échelle DPI
};
