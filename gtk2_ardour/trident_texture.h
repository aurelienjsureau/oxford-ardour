/*
 * trident_texture.h — remplissage de panneau Trident avec TEXTURE procédurale
 * (métal brossé : dégradé vertical de relief + fines striations horizontales +
 *  micro-grain déterministe). Cairo pur, aucun asset. Header-only.
 */
#pragma once

#include <cmath>
#include <ytkmm/drawingarea.h>
#include "ui_config.h"

/* facteur d'échelle DPI d'Ardour, ADOUCI : on ne prend que 60% du sur-scale
 * (compromis entre la taille d'origine et le plein 4K, qui était trop gros). */
static inline double trident_ui_scale ()
{
	const float s = UIConfiguration::instance ().get_ui_scale ();
	const double d = s < 1.0f ? 1.0 : (double) s;
	return 1.0 + (d - 1.0) * 0.6;
}

static inline double trident_tex_clamp (double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

/* grain pseudo-aléatoire STABLE (fonction des coordonnées, pas de scintillement) */
static inline double trident_tex_hash (int x, int y)
{
	unsigned int h = (unsigned int) (x * 374761393 + y * 668265263);
	h = (h ^ (h >> 13)) * 1274126177u;
	return ((h ^ (h >> 16)) & 0xffff) / 65535.0;  // 0..1
}

/* Remplit (x,y,w,h) en ALUMINIUM BROSSÉ très discret : dégradé vertical de
 * relief (±2,5 % de luminance) + striations horizontales à 1,5 % d'alpha.
 * Volontairement à la limite du visible — une version plus marquée avait déjà
 * été essayée et rendait mal autour des potards à grande taille. */
static inline void
trident_fill_panel (Cairo::RefPtr<Cairo::Context>& cr,
                    double x, double y, double w, double h,
                    double r, double g, double b)
{
	if (w <= 0.0 || h <= 0.0) { return; }
	/* 1) galbe vertical : légèrement plus clair en haut, plus sombre en bas.
	 * Même réserve que le reflet ci-dessous : sur un petit widget il est
	 * recalculé sur sa propre hauteur et le fait ressortir en carré. */
	if (h < 160.0) {
		cr->set_source_rgb (r, g, b);
		cr->rectangle (x, y, w, h);
		cr->fill ();
	} else {
		Cairo::RefPtr<Cairo::LinearGradient> lg = Cairo::LinearGradient::create (x, y, x, y + h);
		lg->add_color_stop_rgb (0.0, trident_tex_clamp (r * 1.025), trident_tex_clamp (g * 1.025), trident_tex_clamp (b * 1.025));
		lg->add_color_stop_rgb (0.5, r, g, b);
		lg->add_color_stop_rgb (1.0, trident_tex_clamp (r * 0.975), trident_tex_clamp (g * 0.975), trident_tex_clamp (b * 0.975));
		cr->set_source (lg);
		cr->rectangle (x, y, w, h);
		cr->fill ();
	}
	cr->save ();
	cr->rectangle (x, y, w, h);
	cr->clip ();

	/* 2) REFLET DIFFUS à grande échelle : c'est lui qui fait lire « métal ».
	 * Sans cette composante lente, les seules rayures fines donnent un rendu
	 * de neige télé.
	 * ⚠ Il n'est peint QUE sur les grandes surfaces (châssis, pages, tranches).
	 * Sur un petit widget — le fond d'un potard — le dégradé est recalculé sur
	 * SA hauteur à lui : sa luminance moyenne ne correspond alors plus à celle
	 * du fond au même endroit, et le widget se détache en carré plus clair.
	 * Amplitude aussi réduite (±3,75 % -> ±2 %) pour que le raccord reste
	 * invisible là où un widget opaque se pose sur le châssis. */
	if (h >= 160.0) {
		Cairo::RefPtr<Cairo::LinearGradient> sheen = Cairo::LinearGradient::create (x, y, x, y + h);
		const int nb = 7;
		for (int i = 0; i <= nb; ++i) {
			const double t = (double) i / (double) nb;
			const double n = trident_tex_hash (7, i * 37) - 0.5;   // -0,5..0,5 stable
			const double a = n * 0.040;                            // ±2 %
			if (a >= 0.0) sheen->add_color_stop_rgba (t, 1.0, 1.0, 1.0, a);
			else          sheen->add_color_stop_rgba (t, 0.0, 0.0, 0.0, -a);
		}
		cr->set_source (sheen);
		cr->rectangle (x, y, w, h);
		cr->fill ();
	}

	/* 3) brossage : striations horizontales, une par ligne, intensité stable.
	 * Les lignes sont GROUPÉES en 4 paliers d'alpha et tracées en 4 stroke()
	 * au lieu d'un par ligne : sur une tranche de mixer haute de 1000 px et
	 * quinze tranches à l'écran, un stroke par ligne coûtait très cher. */
	cr->set_line_width (1.0);
	{
		const int y0 = (int) y, y1 = (int) (y + h);
		static const double lightA[2] = { 0.018, 0.048 };
		static const double darkA[2]  = { 0.016, 0.043 };
		for (int band = 0; band < 4; ++band) {
			bool any = false;
			for (int yy = y0; yy < y1; ++yy) {
				const double n = trident_tex_hash (0, yy);      // 0..1, stable
				int b;
				if      (n > 0.80) b = 3;                       // clair marqué
				else if (n > 0.58) b = 2;                       // clair léger
				else if (n < 0.20) b = 1;                       // sombre marqué
				else if (n < 0.42) b = 0;                       // sombre léger
				else continue;
				if (b != band) { continue; }
				cr->move_to (x, yy + 0.5);
				cr->line_to (x + w, yy + 0.5);
				any = true;
			}
			if (!any) { continue; }
			if (band >= 2) cr->set_source_rgba (1.0, 1.0, 1.0, lightA[band - 2]);
			else           cr->set_source_rgba (0.0, 0.0, 0.0, darkA[band]);
			cr->stroke ();
		}
	}
	cr->restore ();
}

/* Bord de PLAQUE BOULONNÉE : jeu sombre en périphérie + liseré clair juste en
 * dessous (lumière en haut-gauche) + têtes de vis aux angles. C'est ce qui fait
 * lire « tôle vissée » plutôt que « aplat de couleur ». */
static inline void
trident_plate_edge (Cairo::RefPtr<Cairo::Context>& cr,
                    double x, double y, double w, double h,
                    bool screws = true, double inset = 3.0)
{
	if (w < 12.0 || h < 12.0) { return; }
	/* jeu du joint */
	cr->set_line_width (1.0);
	cr->set_source_rgba (0.0, 0.0, 0.0, 0.30);
	cr->rectangle (x + inset + 0.5, y + inset + 0.5, w - 2.0 * inset - 1.0, h - 2.0 * inset - 1.0);
	cr->stroke ();
	/* arête éclairée sous le joint (haut) et à gauche */
	cr->set_source_rgba (1.0, 1.0, 1.0, 0.20);
	cr->move_to (x + inset + 1.5, y + inset + 1.5);
	cr->line_to (x + w - inset - 1.5, y + inset + 1.5);
	cr->stroke ();
	if (!screws) { return; }
	const double sx[4] = { x + inset + 7.0, x + w - inset - 7.0, x + inset + 7.0, x + w - inset - 7.0 };
	const double sy[4] = { y + inset + 7.0, y + inset + 7.0, y + h - inset - 7.0, y + h - inset - 7.0 };
	for (int i = 0; i < 4; ++i) {
		const double r = 2.6;
		/* ombre portée de la tête */
		cr->set_source_rgba (0, 0, 0, 0.28);
		cr->arc (sx[i] + 0.7, sy[i] + 0.9, r, 0, 2 * M_PI); cr->fill ();
		/* tête : dégradé haut-gauche clair -> bas-droite sombre */
		Cairo::RefPtr<Cairo::RadialGradient> hd =
			Cairo::RadialGradient::create (sx[i] - r * 0.4, sy[i] - r * 0.4, 0.0, sx[i], sy[i], r);
		hd->add_color_stop_rgb (0.0, 0.86, 0.87, 0.89);
		hd->add_color_stop_rgb (1.0, 0.44, 0.46, 0.49);
		cr->set_source (hd);
		cr->arc (sx[i], sy[i], r, 0, 2 * M_PI); cr->fill ();
		/* fente cruciforme */
		cr->set_line_width (0.9);
		cr->set_source_rgba (0.20, 0.21, 0.23, 0.85);
		cr->move_to (sx[i] - r * 0.62, sy[i]); cr->line_to (sx[i] + r * 0.62, sy[i]);
		cr->move_to (sx[i], sy[i] - r * 0.62); cr->line_to (sx[i], sy[i] + r * 0.62);
		cr->stroke ();
	}
}
