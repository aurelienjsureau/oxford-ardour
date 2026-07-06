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

/* Remplit (x,y,w,h) avec la couleur de tranche, PLAT (la texture rendait mal
 * autour des potards à grande taille -> on garde le fond uni, le relief vient
 * des ombres portées des widgets). */
static inline void
trident_fill_panel (Cairo::RefPtr<Cairo::Context>& cr,
                    double x, double y, double w, double h,
                    double r, double g, double b)
{
	(void) trident_tex_hash; // (gardé pour usage éventuel)
	cr->set_source_rgb (r, g, b);
	cr->rectangle (x, y, w, h);
	cr->fill ();
}
