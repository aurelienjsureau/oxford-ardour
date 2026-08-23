/*
 * Copyright (C) 2006 Paul Davis <paul@linuxaudiosystems.com>
 * Copyright (C) 2017-2018 Robin Gareus <robin@gareus.org>
 * Copyright (C) 2017 Julien "_FrnchFrgg_" RIVAUD <frnchfrgg@free.fr>
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


#include <iostream>
#include <assert.h>

#include "gtkmm2ext/cairo_widget.h"
#include "gtkmm2ext/colors.h"
#include "gtkmm2ext/keyboard.h"
#include "gtkmm2ext/rgb_macros.h"
#include "gtkmm2ext/utils.h"

#include "widgets/ardour_fader.h"
#include "gtkmm2ext/ui_config.h"

using namespace Gtk;
using namespace std;
using namespace Gtkmm2ext;
using namespace ArdourWidgets;

#define CORNER_RADIUS 2.5
#define CORNER_SIZE   2
#define CORNER_OFFSET 1
#define FADER_RESERVE 6

std::list<ArdourFader::FaderImage*> ArdourFader::_patterns;
Cairo::RefPtr<Cairo::ImageSurface> ArdourFader::_handle_img;   // skin poignée (Oxford)

ArdourFader::ArdourFader (Gtk::Adjustment& adj, int orientation, int fader_length, int fader_girth)
	: FaderWidget (adj, orientation)
	, _layout (0)
	, _text_width (0)
	, _text_height (0)
	, _span (fader_length)
	, _girth (fader_girth)
	, _min_span (fader_length)
	, _min_girth (fader_girth)
	, _pattern (0)
	, _centered_text (true)
	, _current_parent (0)
	, have_explicit_bg (false)
	, have_explicit_fg (false)
	, outline_color (0)
{
	update_unity_position ();

	add_events (Gdk::TOUCH_UPDATE_MASK);

	if (_orien == VERT) {
		CairoWidget::set_size_request(_girth, _span);
	} else {
		CairoWidget::set_size_request(_span, _girth);
	}

	outline_color = UIConfigurationBase::instance().color ("fader outline");
}

ArdourFader::~ArdourFader ()
{
	if (_parent_style_change) _parent_style_change.disconnect();
	if (_layout) _layout.clear (); // drop reference to existing layout
}

void
ArdourFader::flush_pattern_cache () {
	for (list<FaderImage*>::iterator f = _patterns.begin(); f != _patterns.end(); ++f) {
		cairo_pattern_destroy ((*f)->pattern);
	}
	_patterns.clear();
}

cairo_pattern_t*
ArdourFader::find_pattern (double afr, double afg, double afb,
			double abr, double abg, double abb,
			int w, int h)
{
	for (list<FaderImage*>::iterator f = _patterns.begin(); f != _patterns.end(); ++f) {
		if ((*f)->matches (afr, afg, afb, abr, abg, abb, w, h)) {
			return (*f)->pattern;
		}
	}
	return 0;
}

void
ArdourFader::create_patterns ()
{
	Gdk::Color c = fg_color (get_state());
	float fr, fg, fb;
	float br, bg, bb;

	fr = c.get_red_p ();
	fg = c.get_green_p ();
	fb = c.get_blue_p ();

	c = bg_color (get_state());

	br = c.get_red_p ();
	bg = c.get_green_p ();
	bb = c.get_blue_p ();

	cairo_surface_t* surface;
	cairo_t* tc = 0;

	if (get_width() <= 1 || get_height() <= 1) {
		return;
	}

	if ((_pattern = find_pattern (fr, fg, fb, br, bg, bb, get_width(), get_height())) != 0) {
		/* found it - use it */
		return;
	}

	if (_orien == VERT) {

		surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, get_width(), get_height() * 2.0);
		tc = cairo_create (surface);

		/* paint background + border */

		/* slot : assombri par défaut ; ÉCLAIRCI quand le skin de poignée Oxford est actif */
		const double s0 = _handle_img ? 0.80 : 0.4;
		const double s1 = _handle_img ? 0.88 : 0.6;
		const double s2 = _handle_img ? 0.96 : 0.8;
		cairo_pattern_t* shade_pattern = cairo_pattern_create_linear (0.0, 0.0, get_width(), 0);
		cairo_pattern_add_color_stop_rgba (shade_pattern, 0, br*s0,bg*s0,bb*s0, 1.0);
		cairo_pattern_add_color_stop_rgba (shade_pattern, 0.25, br*s1,bg*s1,bb*s1, 1.0);
		cairo_pattern_add_color_stop_rgba (shade_pattern, 1, br*s2,bg*s2,bb*s2, 1.0);
		cairo_set_source (tc, shade_pattern);
		cairo_rectangle (tc, 0, 0, get_width(), get_height() * 2.0);
		cairo_fill (tc);

		cairo_pattern_destroy (shade_pattern);

		/* paint lower shade */

		shade_pattern = cairo_pattern_create_linear (0.0, 0.0, get_width() - 2 - CORNER_OFFSET , 0);
		cairo_pattern_add_color_stop_rgba (shade_pattern, 0, fr*0.8,fg*0.8,fb*0.8, 1.0);
		cairo_pattern_add_color_stop_rgba (shade_pattern, 1, fr*0.6,fg*0.6,fb*0.6, 1.0);
		cairo_set_source (tc, shade_pattern);
		Gtkmm2ext::rounded_top_half_rectangle (tc, CORNER_OFFSET, get_height() + CORNER_OFFSET,
				get_width() - CORNER_SIZE, get_height(), CORNER_RADIUS);
		cairo_fill (tc);

		cairo_pattern_destroy (shade_pattern);

		_pattern = cairo_pattern_create_for_surface (surface);

	} else {

		surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, get_width() * 2.0, get_height());
		tc = cairo_create (surface);

		/* paint right shade (background section)*/

		cairo_pattern_t* shade_pattern = cairo_pattern_create_linear (0.0, 0.0, 0.0, get_height());
		cairo_pattern_add_color_stop_rgba (shade_pattern, 0, br*0.4,bg*0.4,bb*0.4, 1.0);
		cairo_pattern_add_color_stop_rgba (shade_pattern, 0.25, br*0.6,bg*0.6,bb*0.6, 1.0);
		cairo_pattern_add_color_stop_rgba (shade_pattern, 1, br*0.8,bg*0.8,bb*0.8, 1.0);
		cairo_set_source (tc, shade_pattern);
		cairo_rectangle (tc, 0, 0, get_width() * 2.0, get_height());
		cairo_fill (tc);

		/* paint left shade (active section/foreground) */

		shade_pattern = cairo_pattern_create_linear (0.0, 0.0, 0.0, get_height());
		cairo_pattern_add_color_stop_rgba (shade_pattern, 0, fr*0.8,fg*0.8,fb*0.8, 1.0);
		cairo_pattern_add_color_stop_rgba (shade_pattern, 1, fr*0.6,fg*0.6,fb*0.6, 1.0);
		cairo_set_source (tc, shade_pattern);
		Gtkmm2ext::rounded_right_half_rectangle (tc, CORNER_OFFSET, CORNER_OFFSET,
				get_width() - CORNER_OFFSET, get_height() - CORNER_SIZE, CORNER_RADIUS);
		cairo_fill (tc);
		cairo_pattern_destroy (shade_pattern);

		_pattern = cairo_pattern_create_for_surface (surface);
	}

	/* cache it for others to use */

	_patterns.push_back (new FaderImage (_pattern, fr, fg, fb, br, bg, bb, get_width(), get_height()));

	cairo_destroy (tc);
	cairo_surface_destroy (surface);
}

Gdk::Color
ArdourFader::bg_color (Gtk::StateType s)
{
	if (have_explicit_bg) {
		return gdk_color_from_rgba (explicit_bg);
	}
	return get_style()->get_bg (s);
}

Gdk::Color
ArdourFader::fg_color (Gtk::StateType s)
{
	if (have_explicit_fg) {
		Gtkmm2ext::HSV   hsv;

		if (_hovering || (s == Gtk::STATE_PRELIGHT)) {
			hsv = HSV (explicit_fg);
			hsv.s *= 0.50;
			return gdk_color_from_rgba (hsv.color());
		}

		switch (s) {
		case Gtk::STATE_NORMAL:
			return gdk_color_from_rgba (explicit_fg);
			return gdk_color_from_rgba (hsv.color());
		case Gtk::STATE_SELECTED:
			return gdk_color_from_rgba (explicit_fg);
		case Gtk::STATE_ACTIVE:
			return gdk_color_from_rgba (explicit_fg);
		case Gtk::STATE_INSENSITIVE:
			return get_style()->get_fg (s);
		case Gtk::STATE_PRELIGHT:
			break;
		}
	}

	return get_style()->get_fg (s);
}

void
ArdourFader::render (Cairo::RefPtr<Cairo::Context> const& ctx, cairo_rectangle_t* area)
{
	cairo_t* cr = ctx->cobj();

	if (!_pattern) {
		create_patterns();
	}

	if (!_pattern) {
		/* this isn't supposed to be happen, but some wackiness whereby
		 * the pixfader ends up with a 1xN or Nx1 size allocation
		 * leads to it. the basic wackiness needs fixing but we
		 * shouldn't crash. just fill in the expose area with
		 * our bg color.
		 */

		CairoWidget::set_source_rgb_a (cr, bg_color (get_state()), 1);
		cairo_rectangle (cr, area->x, area->y, area->width, area->height);
		cairo_fill (cr);
		return;
	}

	OnExpose();
	int ds = display_span ();
	const float w = get_width();
	const float h = get_height();

	CairoWidget::set_source_rgb_a (cr, get_parent_bg(), 1);
	cairo_rectangle (cr, 0, 0, w, h);
	cairo_fill(cr);

	cairo_set_line_width (cr, 2);
	Gtkmm2ext::set_source_rgba (cr, outline_color);


	cairo_matrix_t matrix;
	/* slot/bande : rétréci quand un skin de poignée est actif (le cap dépasse sur les côtés) */
	double tx = CORNER_OFFSET, ty = CORNER_OFFSET, tw = w - CORNER_SIZE, th = h - CORNER_SIZE;
	if (_handle_img) {
		if (_orien == VERT) { const double nw = (w - CORNER_SIZE) * 0.42; tx = (w - nw) * 0.5; tw = nw; }
		else                { const double nh = (h - CORNER_SIZE) * 0.42; ty = (h - nh) * 0.5; th = nh; }
	}
	Gtkmm2ext::rounded_rectangle (cr, tx, ty, tw, th, CORNER_RADIUS);
	// we use a 'trick' here: The stoke is off by .5px but filling the interior area
	// after a stroke of 2px width results in an outline of 1px
	cairo_stroke_preserve(cr);

	if (_orien == VERT) {

		if (ds > h - FADER_RESERVE - CORNER_OFFSET) {
			ds = h - FADER_RESERVE - CORNER_OFFSET;
		}

		if (!CairoWidget::flat_buttons()) {
			cairo_set_source (cr, _pattern);
			cairo_matrix_init_translate (&matrix, 0, (h - ds));
			cairo_pattern_set_matrix (_pattern, &matrix);
		} else {
			CairoWidget::set_source_rgb_a (cr, bg_color (get_state()), 1);
			cairo_fill (cr);
			CairoWidget::set_source_rgb_a (cr, fg_color (get_state()), 1);
			Gtkmm2ext::rounded_rectangle (cr, tx, ds + CORNER_OFFSET,
					tw, h - ds - CORNER_SIZE, CORNER_RADIUS);
		}
		cairo_fill (cr);

	} else {

		if (ds < FADER_RESERVE) {
			ds = FADER_RESERVE;
		}
		assert(ds <= w);

		/*
		 * if ds == w, the pattern does not need to be translated
		 * if ds == 0 (or FADER_RESERVE), the pattern needs to be moved
		 * w to the left, which is -w in pattern space, and w in user space
		 * if ds == 10, then the pattern needs to be moved w - 10
		 * to the left, which is -(w-10) in pattern space, which
		 * is (w - 10) in user space
		 * thus: translation = (w - ds)
		 */

		if (!CairoWidget::flat_buttons() ) {
			cairo_set_source (cr, _pattern);
			cairo_matrix_init_translate (&matrix, w - ds, 0);
			cairo_pattern_set_matrix (_pattern, &matrix);
		} else {
			CairoWidget::set_source_rgb_a (cr, bg_color (get_state()), 1);
			cairo_fill (cr);
			CairoWidget::set_source_rgb_a (cr, fg_color (get_state()), 1);
			Gtkmm2ext::rounded_rectangle (cr, CORNER_OFFSET, ty,
					ds - CORNER_SIZE, th, CORNER_RADIUS);
		}
		cairo_fill (cr);
	}

	/* --- Oxford : la glissière est un CANAL FRAISÉ dans la tôle ---
	 * biseau creusé (lumière en haut-gauche comme partout ailleurs) : paroi
	 * sombre du côté éclairé, lèvre claire du côté opposé. Sans ça, la bande
	 * du fader reste un aplat posé sur le panneau. */
	{
		cairo_save (cr);
		Gtkmm2ext::rounded_rectangle (cr, tx, ty, tw, th, CORNER_RADIUS);
		cairo_clip (cr);
		cairo_pattern_t* rec = (_orien == VERT)
			? cairo_pattern_create_linear (tx, ty, tx + tw, ty)
			: cairo_pattern_create_linear (tx, ty, tx, ty + th);
		cairo_pattern_add_color_stop_rgba (rec, 0.00, 0, 0, 0, 0.38);
		cairo_pattern_add_color_stop_rgba (rec, 0.32, 0, 0, 0, 0.0);
		cairo_pattern_add_color_stop_rgba (rec, 0.78, 1, 1, 1, 0.0);
		cairo_pattern_add_color_stop_rgba (rec, 1.00, 1, 1, 1, 0.16);
		cairo_set_source (cr, rec);
		cairo_rectangle (cr, tx, ty, tw, th);
		cairo_fill (cr);
		cairo_pattern_destroy (rec);
		/* ombre portée du bord haut du canal (le métal surplombe la rainure) */
		cairo_pattern_t* lip = (_orien == VERT)
			? cairo_pattern_create_linear (tx, ty, tx, ty + 4.0)
			: cairo_pattern_create_linear (tx, ty, tx + 4.0, ty);
		cairo_pattern_add_color_stop_rgba (lip, 0.0, 0, 0, 0, 0.34);
		cairo_pattern_add_color_stop_rgba (lip, 1.0, 0, 0, 0, 0.0);
		cairo_set_source (cr, lip);
		cairo_rectangle (cr, tx, ty, _orien == VERT ? tw : 4.0, _orien == VERT ? 4.0 : th);
		cairo_fill (cr);
		cairo_pattern_destroy (lip);
		cairo_restore (cr);
	}

	/* --- Trident : poignée de fader RECTANGULAIRE (réf Mixbus 80B) ---
	 *   corps #b0b4b8, filet haut 1px #d0d4d8, filet bas 1px ombre #606468. */
	{
		if (_handle_img && _orien == VERT) {
			/* poignée = IMAGE (skin Oxford) redimensionnée à la largeur du fader + ombre portée */
			const int iw = _handle_img->get_width (), ih = _handle_img->get_height ();
			const double cap_w = w - CORNER_SIZE - 1.0;
			const double sc = cap_w / (double) (iw > 0 ? iw : 1);
			const double cap_h = ih * sc;
			double cyy = ds + CORNER_OFFSET;
			if (cyy < CORNER_OFFSET + cap_h * 0.5)     { cyy = CORNER_OFFSET + cap_h * 0.5; }
			if (cyy > h - CORNER_OFFSET - cap_h * 0.5) { cyy = h - CORNER_OFFSET - cap_h * 0.5; }
			const double x0  = CORNER_OFFSET + 0.5;
			const double top = cyy - cap_h * 0.5;
			/* ombre portée SOUS le cap : dégradé descendant (net puis estompé) */
			{
				cairo_pattern_t* sh = cairo_pattern_create_linear (0, top + cap_h - 2.0, 0, top + cap_h + 9.0);
				cairo_pattern_add_color_stop_rgba (sh, 0.0, 0, 0, 0, 0.45);
				cairo_pattern_add_color_stop_rgba (sh, 1.0, 0, 0, 0, 0.0);
				cairo_set_source (cr, sh);
				cairo_rectangle (cr, x0 - 1.0, top + cap_h - 2.0, cap_w + 2.0, 11.0);
				cairo_fill (cr);
				cairo_pattern_destroy (sh);
			}
			cairo_save (cr);
			cairo_translate (cr, x0, top);
			cairo_scale (cr, sc, sc);
			cairo_set_source_surface (cr, _handle_img->cobj (), 0, 0);
			cairo_paint (cr);
			cairo_restore (cr);
			/* plastique moulé : léger spéculaire en haut, arête basse assombrie.
			 * Dosé bas — l'image du cap porte déjà son propre modelé. */
			cairo_save (cr);
			cairo_rectangle (cr, x0, top, cap_w, cap_h);
			cairo_clip (cr);
			{
				cairo_pattern_t* sp = cairo_pattern_create_linear (x0, top, x0, top + cap_h);
				cairo_pattern_add_color_stop_rgba (sp, 0.00, 1, 1, 1, 0.18);
				cairo_pattern_add_color_stop_rgba (sp, 0.42, 1, 1, 1, 0.02);
				cairo_pattern_add_color_stop_rgba (sp, 1.00, 0, 0, 0, 0.14);
				cairo_set_source (cr, sp);
				cairo_paint (cr);
				cairo_pattern_destroy (sp);
			}
			cairo_restore (cr);
		} else if (_orien == VERT) {
			const double cap_h = 16.0;
			double cy = ds + CORNER_OFFSET;   // centré sur la frontière haute du remplissage
			if (cy < CORNER_OFFSET + cap_h * 0.5)     { cy = CORNER_OFFSET + cap_h * 0.5; }
			if (cy > h - CORNER_OFFSET - cap_h * 0.5) { cy = h - CORNER_OFFSET - cap_h * 0.5; }
			const double x0  = CORNER_OFFSET + 0.5;
			const double cw  = w - CORNER_SIZE - 1.0;
			const double top = floor (cy - cap_h * 0.5) + 0.5;
			/* drop shadow forcée : rect noir flou décalé bas */
			for (int s = 5; s >= 1; --s) {
				cairo_rectangle (cr, x0 - s, top - s + 3.0, cw + 2 * s, cap_h + 2 * s);
				cairo_set_source_rgba (cr, 0, 0, 0, 0.16);
				cairo_fill (cr);
			}
			/* corps plat */
			cairo_rectangle (cr, x0, top, cw, cap_h);
			cairo_set_source_rgb (cr, 0.690, 0.706, 0.722); /* #b0b4b8 */
			cairo_fill (cr);
			/* filet haut clair 1px */
			cairo_set_line_width (cr, 1.0);
			cairo_set_source_rgb (cr, 0.816, 0.831, 0.847);  /* #d0d4d8 */
			cairo_move_to (cr, x0, top + 0.5); cairo_line_to (cr, x0 + cw, top + 0.5); cairo_stroke (cr);
			/* filet bas sombre 1px */
			cairo_set_source_rgb (cr, 0.376, 0.392, 0.408);  /* #606468 */
			cairo_move_to (cr, x0, top + cap_h - 0.5); cairo_line_to (cr, x0 + cw, top + cap_h - 0.5); cairo_stroke (cr);
			/* trait d'indication central */
			cairo_set_source_rgb (cr, 0.376, 0.392, 0.408);
			cairo_move_to (cr, x0 + 1.0, floor (cy) + 0.5); cairo_line_to (cr, x0 + cw - 1.0, floor (cy) + 0.5); cairo_stroke (cr);
		} else {
			const double cap_w = 22.0;
			double cx = ds;
			if (cx < CORNER_OFFSET + cap_w * 0.5)     { cx = CORNER_OFFSET + cap_w * 0.5; }
			if (cx > w - CORNER_OFFSET - cap_w * 0.5) { cx = w - CORNER_OFFSET - cap_w * 0.5; }
			const double y0  = CORNER_OFFSET + 0.5;
			const double ch  = h - CORNER_SIZE - 1.0;
			const double left = floor (cx - cap_w * 0.5) + 0.5;
			/* drop shadow forcée : rect noir flou décalé droite */
			for (int s = 5; s >= 1; --s) {
				cairo_rectangle (cr, left - s + 3.0, y0 - s, cap_w + 2 * s, ch + 2 * s);
				cairo_set_source_rgba (cr, 0, 0, 0, 0.16);
				cairo_fill (cr);
			}
			cairo_rectangle (cr, left, y0, cap_w, ch);
			cairo_set_source_rgb (cr, 0.690, 0.706, 0.722);
			cairo_fill (cr);
			cairo_set_line_width (cr, 1.0);
			cairo_set_source_rgb (cr, 0.816, 0.831, 0.847);
			cairo_move_to (cr, left + 0.5, y0); cairo_line_to (cr, left + 0.5, y0 + ch); cairo_stroke (cr);
			cairo_set_source_rgb (cr, 0.376, 0.392, 0.408);
			cairo_move_to (cr, left + cap_w - 0.5, y0); cairo_line_to (cr, left + cap_w - 0.5, y0 + ch); cairo_stroke (cr);
		}
	}

	/* draw the unity-position line if it's not at either end*/
	if (!(_tweaks & NoShowUnityLine) && _unity_loc > CORNER_RADIUS) {
		cairo_set_line_width(cr, 1);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		Gdk::Color c = fg_color (Gtk::STATE_ACTIVE);
		cairo_set_source_rgba (cr, c.get_red_p() * 1.5, c.get_green_p() * 1.5, c.get_blue_p() * 1.5, 0.85);
		if (_orien == VERT) {
			if (_unity_loc < h - CORNER_RADIUS) {
				cairo_move_to (cr, 1.5, _unity_loc + CORNER_OFFSET + .5);
				cairo_line_to (cr, _girth - 1.5, _unity_loc + CORNER_OFFSET + .5);
				cairo_stroke (cr);
			}
		} else {
			if (_unity_loc < w - CORNER_RADIUS) {
				cairo_move_to (cr, _unity_loc - CORNER_OFFSET + .5, 1.5);
				cairo_line_to (cr, _unity_loc - CORNER_OFFSET + .5, _girth - 1.5);
				cairo_stroke (cr);
			}
		}
	}

	if (_layout && !_text.empty() && _orien == HORIZ) {
		Gdk::Color bg_col;
		cairo_save (cr);
		if (_centered_text) {
			/* center text */
			cairo_move_to (cr, (w - _text_width)/2.0, h/2.0 - _text_height/2.0);
			bg_col = bg_color (get_state());
		} else if (ds > .5 * w) {
			cairo_move_to (cr, CORNER_OFFSET + 3, h/2.0 - _text_height/2.0);
			bg_col = fg_color (get_state());
		} else {
			cairo_move_to (cr, w - _text_width - CORNER_OFFSET - 3, h/2.0 - _text_height/2.0);
			bg_col = bg_color (get_state());
		}

		const uint32_t r = bg_col.get_red_p () * 255.0;
		const uint32_t g = bg_col.get_green_p () * 255.0;
		const uint32_t b = bg_col.get_blue_p () * 255.0;
		const uint32_t a = 0xff;
		uint32_t rgba = RGBA_TO_UINT (r, g, b, a);
		rgba = contrasting_text_color (rgba);
		Gdk::Color text_color;
		text_color.set_rgb ((rgba >> 24)*256, ((rgba & 0xff0000) >> 16)*256, ((rgba & 0xff00) >> 8)*256);
		CairoWidget::set_source_rgb_a (cr, text_color, 1.);
		pango_cairo_show_layout (cr, _layout->gobj());
		cairo_restore (cr);
	}

	if (!get_sensitive()) {
		Gtkmm2ext::rounded_rectangle (cr, CORNER_OFFSET, CORNER_OFFSET, w-CORNER_SIZE, h-CORNER_SIZE, CORNER_RADIUS);
		cairo_set_source_rgba (cr, 0.505, 0.517, 0.525, 0.4);
		cairo_fill (cr);
	} else if (_hovering && CairoWidget::widget_prelight() && !have_explicit_fg) {
		Gtkmm2ext::rounded_rectangle (cr, CORNER_OFFSET, CORNER_OFFSET, w-CORNER_SIZE, h-CORNER_SIZE, CORNER_RADIUS);
		cairo_set_source_rgba (cr, 0.905, 0.917, 0.925, 0.1);
		cairo_fill (cr);
	}
}

void
ArdourFader::on_size_request (GtkRequisition* req)
{
	if (_orien == VERT) {
		req->width = (_min_girth ? _min_girth : -1);
		req->height = (_min_span ? _min_span : -1);
	} else {
		req->height = (_min_girth ? _min_girth : -1);
		req->width = (_min_span ? _min_span : -1);
	}
}

void
ArdourFader::on_size_allocate (Gtk::Allocation& alloc)
{
	int old_girth = _girth;
	int old_span = _span;

	CairoWidget::on_size_allocate(alloc);

	if (_orien == VERT) {
		_girth = alloc.get_width ();
		_span = alloc.get_height ();
	} else {
		_girth = alloc.get_height ();
		_span = alloc.get_width ();
	}

	if (get_realized() && ((old_girth != _girth) || (old_span != _span))) {
		/* recreate patterns in case we've changed size */
		create_patterns ();
	}

	update_unity_position ();
}

bool
ArdourFader::on_motion_notify_event (GdkEventMotion* ev)
{
	if (_dragging) {
		double scale = 1.0;
		double const ev_pos = (_orien == VERT) ? ev->y : ev->x;

		if (ev->window != _grab_window) {
			_grab_loc = ev_pos;
			_grab_window = ev->window;
			return true;
		}

		if (ev->state & Keyboard::GainFineScaleModifier) {
			if (ev->state & Keyboard::GainExtraFineScaleModifier) {
				scale = 0.005;
			} else {
				scale = 0.1;
			}
		}

		double const delta = ev_pos - _grab_loc;
		_grab_loc = ev_pos;

		const double off  = FADER_RESERVE + ((_orien == VERT) ? CORNER_OFFSET : 0);
		const double span = _span - off;
		double fract = (delta / span);

		fract = min (1.0, fract);
		fract = max (-1.0, fract);

		// X Window is top->bottom for 0..Y

		if (_orien == VERT) {
			fract = -fract;
		}

		_adjustment.set_value (_adjustment.get_value() + scale * fract * (_adjustment.get_upper() - _adjustment.get_lower()));
	}

	return true;
}

bool
ArdourFader::on_touch_update_event (GdkEventTouch* ev)
{
  GdkEventMotion mev;
  mev.window = ev->window;
  mev.time   = ev->time;
  mev.x      = ev->x;
  mev.y      = ev->y;
  mev.state  = 0;
  return ArdourFader::on_motion_notify_event (&mev);
}

/** @return pixel offset of the current value from the right or bottom of the fader */
int
ArdourFader::display_span ()
{
	float fract = (_adjustment.get_value () - _adjustment.get_lower()) / ((_adjustment.get_upper() - _adjustment.get_lower()));
	int ds;
	if (_orien == VERT) {
		const double off  = FADER_RESERVE + CORNER_OFFSET;
		const double span = _span - off;
		ds = (int)rint (span * (1.0 - fract));
	} else {
		const double off  = FADER_RESERVE;
		const double span = _span - off;
		ds = (int)rint (span * fract + off);
	}

	return ds;
}

void
ArdourFader::update_unity_position ()
{
	if (_orien == VERT) {
		const double span = _span - FADER_RESERVE - CORNER_OFFSET;
		_unity_loc = (int) rint (span * (1 - ((_default_value - _adjustment.get_lower()) / (_adjustment.get_upper() - _adjustment.get_lower())))) - 1;
	} else {
		const double span = _span - FADER_RESERVE;
		_unity_loc = (int) rint (FADER_RESERVE + (_default_value - _adjustment.get_lower()) * span / (_adjustment.get_upper() - _adjustment.get_lower()));
	}

	queue_draw ();
}

void
ArdourFader::set_adjustment_from_event (GdkEventButton* ev)
{
	const double off  = FADER_RESERVE + ((_orien == VERT) ? CORNER_OFFSET : 0);
	const double span = _span - off;
	double fract = (_orien == VERT) ? (1.0 - ((ev->y - off) / span)) : ((ev->x - off) / span);

	fract = min (1.0, fract);
	fract = max (0.0, fract);

	_adjustment.set_value (fract * (_adjustment.get_upper () - _adjustment.get_lower ()));
}

void
ArdourFader::set_default_value (float d)
{
	_default_value = d;
	update_unity_position ();
}

void
ArdourFader::set_text (const std::string& str, bool centered, bool expose)
{
	if (_layout && _text == str) {
		return;
	}
	if (!_layout && !str.empty()) {
		_layout = Pango::Layout::create (get_pango_context());
	}

	_text = str;
	_centered_text = centered;
	if (_layout) {
		_layout->set_text (str);
		_layout->get_pixel_size (_text_width, _text_height);
		// queue_resize ();
		if (expose) queue_draw ();
	}
}

void
ArdourFader::on_state_changed (Gtk::StateType old_state)
{
	Widget::on_state_changed (old_state);
	create_patterns ();
	queue_draw ();
}

void
ArdourFader::on_style_changed (const Glib::RefPtr<Gtk::Style>& style)
{
	CairoWidget::on_style_changed (style);
	if (_layout) {
		std::string txt = _layout->get_text();
		_layout.clear (); // drop reference to existing layout
		_text = "";
		set_text (txt, _centered_text, false);
		queue_resize ();
	}
	/* patterns are cached and re-created as needed
	 * during 'expose' in the GUI thread */
	_pattern = 0;
	queue_draw ();
}

void
ArdourFader::update_min_size (int span, int girth)
{
	_min_span = span;
	_min_girth = girth;
	_pattern = 0;

	if (_orien == VERT) {
		CairoWidget::set_size_request(girth, span);
	} else {
		CairoWidget::set_size_request(span, girth);
	}
	queue_resize ();
}

Gdk::Color
ArdourFader::get_parent_bg ()
{
	Widget* parent = get_parent ();

	while (parent) {
		if (parent->get_has_window()) {
			break;
		}
		parent = parent->get_parent();
	}

	if (parent && parent->get_has_window()) {
		if (_current_parent != parent) {
			if (_parent_style_change) _parent_style_change.disconnect();
			_current_parent = parent;
			_parent_style_change = parent->signal_style_changed().connect (mem_fun (*this, &ArdourFader::on_style_changed));
		}
		return parent->get_style ()->get_bg (parent->get_state());
	}

	return get_style ()->get_bg (get_state());
}

void
ArdourFader::set_bg (Gtkmm2ext::Color c)
{
	explicit_bg = c;
	have_explicit_bg = true;
	_pattern = 0;
	queue_draw ();
}

void
ArdourFader::set_fg (Gtkmm2ext::Color c)
{
	explicit_fg = c;
	have_explicit_fg = true;
	_pattern = 0;
	queue_draw ();
}

void
ArdourFader::unset_bg ()
{
	if (have_explicit_bg) {
		have_explicit_bg = false;
		_pattern = 0;
		queue_draw ();
	}
}

void
ArdourFader::unset_fg ()
{
	if (have_explicit_fg) {
		have_explicit_fg = false;
		_pattern = 0;
		queue_draw ();
	}
}
