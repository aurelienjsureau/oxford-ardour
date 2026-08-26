/*
 * OxfordConsolePanel — implémentation. Voir oxford_console_panel.h.
 * Panneau ancré, knobs ronds, sous-vues EQ / Dynamics. Skin maison (inspiré de la
 * DISPOSITION des channel strips Oxford : bandes EQ groupées + code couleur par
 * fréquence ; dynamique en sections), pas une copie d'assets tiers.
 */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <ytkmm/alignment.h>
#include <ytkmm/separator.h>
#include <ytkmm/frame.h>
#include <ytkmm/comboboxtext.h>
#include <ytkmm/eventbox.h>
#include <pangomm/fontdescription.h>

#include "pbd/controllable.h"
#include "pbd/file_utils.h"
#include "pbd/search_path.h"

#include "ardour/filesystem_paths.h"
#include "ardour/rc_configuration.h"
#include "ardour/route.h"
#include "ardour/oxford_channel.h"

#include "widgets/ardour_fader.h"
#include "widgets/tooltips.h"

#include "mixer_ui.h"
#include "mixer_strip.h"
#include "ui_config.h"
#include "oxford_console_panel.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace ArdourWidgets;

/* bornes de fréquence EQ par bande (LF,LMF,MF,HMF,HF) — specs OXF-R3 */
/* plages OXF-R3 (manuel, table Full Channels EQ) */
static const double oxFmin[5] = {  20, 30, 100,  600,  2000 };
static const double oxFmax[5] = { 400, 600, 6000, 18000, 20000 };
/* code couleur par bande (convention fréquence : grave->aigu) */
static const double bandRGB[5][3] = {
	{ 0.357, 0.608, 0.835 },  // LF  #5b9bd5 bleu clair
	{ 0.373, 0.659, 0.416 },  // LMF #5fa86a vert
	{ 0.753, 0.224, 0.169 },  // MF  #c0392b rouge
	{ 0.831, 0.439, 0.165 },  // HMF #d4702a orange
	{ 0.831, 0.690, 0.125 },  // HF  #d4b020 jaune
};
static const double kFilterCap[3] = { 0.847, 0.816, 0.722 };  // #d8d0b8 crème (caps HP/LP)
/* châssis du panneau : recalé sur le THÈME ACTIF au démarrage (theme:bg1/bg2
 * du .colors — suit « Oxford Warm » etc. ; changer de thème -> relancer pour
 * que le panel suive). Valeurs ci-dessous = fallback (bleu-gris OXF-R3). */
static double kPanelBg[3]   = { 0.706, 0.753, 0.796 };  // #b4bfcb bleu-gris (châssis panneau = strips DAW)
static double kSubBg[3]     = { 0.800, 0.835, 0.871 };  // #ccd5de bleu-gris CLAIR = sous-panneaux + fond des knobs
static const double kGraphBg[3]   = { 0.055, 0.094, 0.220 };  // #0e1838 bleu marine (écran graphe réel)
static double kLabelDk[3]   = { 0.102, 0.125, 0.188 };  // texte du châssis — recalé sur neutral:foreground du thème (sombre sur clair, crème sur Oxford Night)
static const double kUI   = 1.5;   // agrandissement du PANNEAU (largeur, graphes, polices)
static const double kKnob = 1.2;   // agrandissement des KNOBS (plus petit que le panneau -> place pour les boutons)

static std::string f_db  (double v){ char s[24]; std::snprintf (s,sizeof s,"%+.1f",v); return s; }
static std::string f_hz  (double v){ char s[24]; if (v>=1000) std::snprintf (s,sizeof s,"%.1fk",v/1000.0); else std::snprintf (s,sizeof s,"%.0f",v); return s; }
static std::string f_q   (double v){ char s[24]; std::snprintf (s,sizeof s,"%.2f",v); return s; }
static std::string f_ms  (double v){ char s[24]; if (v>=1000.0) std::snprintf (s,sizeof s,"%.1fs",v/1000.0); else if (v>=10.0) std::snprintf (s,sizeof s,"%.0f",v); else if (v>=1.0) std::snprintf (s,sizeof s,"%.1f",v); else std::snprintf (s,sizeof s,"%.3f",v); return s; }
static std::string f_rat (double v){ char s[24]; std::snprintf (s,sizeof s,"%.2f",v); return s; }
/* Ratio compresseur : la commande est 0..1 (loi 1/Ratio Sonnox), on AFFICHE le vrai ratio. */
static std::string f_compratio (double c){
	double r;
	if      (c <= 0.5)  r = 1.0 + (c / 0.5) * 1.0;
	else if (c <= 0.75) r = 2.0 + ((c - 0.5) / 0.25) * 2.0;
	else                { double t = (c - 0.75) / 0.25; r = 4.0 / (1.0 - (t > 0.999 ? 0.999 : t)); }
	char s[24];
	if      (r >= 100.0) std::snprintf (s,sizeof s,"Lim");
	else if (r >= 10.0)  std::snprintf (s,sizeof s,"%.0f:1", r);
	else                 std::snprintf (s,sizeof s,"%.1f:1", r);
	return s;
}
static const char* curve_name (int t){ const char* n[]={"Type 1","Type 2","Type 3","Type 4","GML"}; return n[t<0?0:(t>4?4:t)]; }
static const char* timing_name(int t){ const char* n[]={"Normal","Classic","Linear"}; return n[t<0?0:(t>2?2:t)]; }
/* (l'écran EQ trace désormais la VRAIE réponse via OxfordChannel::eqCurveDb —
 * coefficients réels, lois de Q des Types comprises ; plus d'approximation ici) */

/* ---- helpers de rendu "écran console" (bezel arrondi + verre) ---- */
static void rrect (const Cairo::RefPtr<Cairo::Context>& cr, double x, double y, double w, double h, double r)
{
	cr->begin_new_sub_path ();
	cr->arc (x + w - r, y + r,     r, -M_PI/2.0, 0.0);
	cr->arc (x + w - r, y + h - r, r,  0.0,      M_PI/2.0);
	cr->arc (x + r,     y + h - r, r,  M_PI/2.0, M_PI);
	cr->arc (x + r,     y + r,     r,  M_PI,     3.0*M_PI/2.0);
	cr->close_path ();
}

/* peint châssis + écran navy (dégradé) + bezel, puis CLIPPE sur l'écran :
 * tout ce qui est dessiné ensuite reste dans la vitre. */
/* épaisseur du cadre noir autour du tube (le CRT de l'OXF-R3 est ENCASTRÉ
 * derrière un cadre, il ne remplit pas l'ouverture du châssis) */
static const double kBezel = 5.0;

static void screen_begin (const Cairo::RefPtr<Cairo::Context>& cr, double w, double h)
{
	cr->set_source_rgb (kPanelBg[0], kPanelBg[1], kPanelBg[2]); cr->paint ();
	/* ombre portée du bloc écran sur le châssis */
	rrect (cr, 2.0, 3.0, w - 4.0, h - 4.0, 8.0);
	cr->set_source_rgba (0.0, 0.0, 0.0, 0.25); cr->fill ();
	/* CADRE : plastique noir mat, très légèrement dégradé */
	rrect (cr, 2.0, 2.0, w - 4.0, h - 5.0, 8.0);
	{
		Cairo::RefPtr<Cairo::LinearGradient> bz = Cairo::LinearGradient::create (0, 0, 0, h);
		bz->add_color_stop_rgb (0.0, 0.115, 0.125, 0.145);
		bz->add_color_stop_rgb (1.0, 0.055, 0.060, 0.075);
		cr->set_source (bz);
		cr->fill_preserve ();
	}
	cr->set_source_rgba (0.0, 0.0, 0.0, 0.85); cr->set_line_width (1.2); cr->stroke ();
	/* filet clair sur l'arête haute du cadre (lumière rasante d'atelier) */
	rrect (cr, 3.0, 3.0, w - 6.0, h - 7.0, 7.0);
	cr->set_source_rgba (1.0, 1.0, 1.0, 0.055); cr->set_line_width (1.0); cr->stroke ();

	/* OUVERTURE DU TUBE, encastrée de kBezel dans le cadre */
	const double sx = 2.0 + kBezel, sy = 2.0 + kBezel;
	const double sw = w - 4.0 - 2.0 * kBezel, sh = h - 5.0 - 2.0 * kBezel;
	rrect (cr, sx, sy, sw, sh, 4.0);
	Cairo::RefPtr<Cairo::LinearGradient> g = Cairo::LinearGradient::create (0, sy, 0, sy + sh);
	g->add_color_stop_rgb (0.00, kGraphBg[0]*1.9, kGraphBg[1]*1.9, kGraphBg[2]*1.45);
	g->add_color_stop_rgb (0.30, kGraphBg[0],     kGraphBg[1],     kGraphBg[2]);
	g->add_color_stop_rgb (1.00, kGraphBg[0]*0.55, kGraphBg[1]*0.60, kGraphBg[2]*0.85);
	cr->set_source (g);
	cr->fill_preserve ();
	/* ombre interne : le tube est en retrait derrière le cadre */
	cr->set_source_rgba (0.0, 0.0, 0.0, 0.55); cr->set_line_width (2.0); cr->stroke ();
	/* clip vitre pour la suite (grille, courbes, reflets) */
	rrect (cr, sx, sy, sw, sh, 4.0);
	cr->clip ();
}

/* Traitement CRT — à dessiner en DERNIER (par-dessus la courbe), le clip de
 * screen_begin le confine au tube :
 *   1) lignes de balayage    2) vignettage des coins    3) reflet spéculaire. */
static void screen_glass (const Cairo::RefPtr<Cairo::Context>& cr, double w, double h)
{
	/* 1) balayage : une ligne sombre toutes les 3 px, très faible */
	cr->set_line_width (1.0);
	cr->set_source_rgba (0.0, 0.0, 0.0, 0.055);
	for (double y = 1.5; y < h; y += 3.0) {
		cr->move_to (0, y); cr->line_to (w, y);
	}
	cr->stroke ();

	/* 2) vignettage : les bords du tube s'assombrissent (courbure du verre) */
	{
		const double cx = w * 0.5, cy = h * 0.5;
		const double rad = std::sqrt (cx*cx + cy*cy);
		Cairo::RefPtr<Cairo::RadialGradient> v = Cairo::RadialGradient::create (cx, cy, rad * 0.42, cx, cy, rad);
		v->add_color_stop_rgba (0.0, 0.0, 0.0, 0.0, 0.0);
		v->add_color_stop_rgba (1.0, 0.0, 0.0, 0.0, 0.38);
		cr->set_source (v);
		cr->rectangle (0, 0, w, h);
		cr->fill ();
	}

	/* 3) reflet spéculaire : tache oblique en haut-gauche, comme sur la photo */
	{
		cr->save ();
		cr->translate (w * 0.30, h * 0.10);
		cr->rotate (-0.42);
		cr->scale (w * 0.42, h * 0.34);
		Cairo::RefPtr<Cairo::RadialGradient> s = Cairo::RadialGradient::create (0, 0, 0.0, 0, 0, 1.0);
		s->add_color_stop_rgba (0.0, 1.0, 1.0, 1.0, 0.085);
		s->add_color_stop_rgba (0.6, 1.0, 1.0, 1.0, 0.030);
		s->add_color_stop_rgba (1.0, 1.0, 1.0, 1.0, 0.0);
		cr->set_source (s);
		cr->arc (0, 0, 1.0, 0, 2*M_PI);
		cr->fill ();
		cr->restore ();
	}
	/* voile général en haut de dalle */
	Cairo::RefPtr<Cairo::LinearGradient> g = Cairo::LinearGradient::create (0, 0, 0, h * 0.38);
	g->add_color_stop_rgba (0.0, 1.0, 1.0, 1.0, 0.045);
	g->add_color_stop_rgba (1.0, 1.0, 1.0, 1.0, 0.0);
	cr->set_source (g);
	cr->rectangle (0, 0, w, h * 0.38);
	cr->fill ();
}

Cairo::RefPtr<Cairo::ImageSurface>
OxfordConsolePanel::load_knob_image (const char* file)
{
	PBD::Searchpath rc (ARDOUR::ardour_data_search_path ());
	rc.add_subdirectory_to_paths ("resources");
	std::string path;
	if (PBD::find_file (rc, file, path)) {
		try { return Cairo::ImageSurface::create_from_png (path); } catch (...) {}
	}
	return Cairo::RefPtr<Cairo::ImageSurface> ();
}

OxfordConsolePanel::OxfordConsolePanel ()
{
	/* châssis aux couleurs du THÈME ACTIF (le panel suit « Oxford Warm » etc.) */
	fetch_theme_chrome ();
	/* skins knobs (chargés AVANT la construction des pages) */
	{
		const char* kf[6] = { "Oxford-knob-blue.png","Oxford-knob-green.png","Oxford-knob-red.png",
		                      "Oxford-knob-orange.png","Oxford-knob-yellow.png","Oxford-knob-neutral.png" };
		for (int i = 0; i < 6; ++i) { _kimg[i] = load_knob_image (kf[i]); }
		/* skin de poignée de fader (statique -> tous les faders du DAW) */
		Cairo::RefPtr<Cairo::ImageSurface> fimg = load_knob_image ("Oxford-fader.png");
		if (fimg) { ArdourWidgets::ArdourFader::set_handle_image (fimg); }
	}
	set_size_request ((int) (320 * kUI), -1);
	/* fond CLAIR global du panel : l'EventBox peint, le VBox interne porte tout.
	 * set_app_paintable(true) EMPÊCHE GTK de peindre l'aplat par-dessus notre
	 * texture ; le handler expose est branché AVANT le défaut et retourne false,
	 * pour que la propagation aux enfants ait quand même lieu. */
	Gdk::Color bg; bg.set_rgb_p (kPanelBg[0], kPanelBg[1], kPanelBg[2]);
	modify_bg (Gtk::STATE_NORMAL, bg);
	set_app_paintable (true);
	signal_expose_event ().connect (sigc::mem_fun (*this, &OxfordConsolePanel::on_panel_expose), false);
	/* texte CLAIR par défaut (hérité par les labels enfants + titres de cadres) sur fond ardoise */
	Gdk::Color fgc; fgc.set_rgb_p (kLabelDk[0], kLabelDk[1], kLabelDk[2]);
	modify_fg (Gtk::STATE_NORMAL, fgc);
	_box.modify_fg (Gtk::STATE_NORMAL, fgc);
	/* filet bleu Oxford sur le bord GAUCHE : démarque la section console du reste du mixer */
	Gtk::HBox* outer = Gtk::manage (new Gtk::HBox ());
	Gtk::EventBox* edge = Gtk::manage (new Gtk::EventBox ());
	edge->set_size_request (3, -1);
	Gdk::Color ec; ec.set ("#243a5e"); edge->modify_bg (Gtk::STATE_NORMAL, ec);
	outer->pack_start (*edge, false, false);
	outer->pack_start (_box, true, true);
	add (*outer);
	_box.set_spacing (3);
	_box.set_border_width (4);

	/* En-tête : wordmark "OXFORD" + sous-titre gravé + motif de pastilles colorées */
	Gtk::HBox* hdr = Gtk::manage (new Gtk::HBox ()); hdr->set_spacing (4);
	Gtk::VBox* wmb = Gtk::manage (new Gtk::VBox ());
	Gtk::Label* wm = Gtk::manage (new Gtk::Label ());
	wm->set_markup ("<b><span size=\"x-large\" foreground=\"#11151c\" letter_spacing=\"3072\">OXFORD</span></b>");
	wm->set_alignment (0.0, 1.0);
	Gtk::Label* wsub = Gtk::manage (new Gtk::Label ());
	wsub->set_markup ("<span size=\"x-small\" foreground=\"#46536a\" letter_spacing=\"2048\">REFERENCE CONSOLE · OXF-R3</span>");
	wsub->set_alignment (0.0, 0.0);
	wmb->pack_start (*wm, false, false);
	wmb->pack_start (*wsub, false, false);
	hdr->pack_start (*wmb, false, false);
	const char* dotc[5] = { "#f0d000", "#f08000", "#e02020", "#20a040", "#2050d0" };
	for (int d = 0; d < 5; ++d) {
		Gtk::EventBox* dot = Gtk::manage (new Gtk::EventBox ());
		dot->set_size_request (9, 9);
		Gdk::Color c; c.set (dotc[d]); dot->modify_bg (Gtk::STATE_NORMAL, c);
		Gtk::Alignment* al = Gtk::manage (new Gtk::Alignment (0.5, 0.5, 0, 0));
		al->add (*dot);
		hdr->pack_start (*al, false, false);
	}
	_box.pack_start (*hdr, Gtk::PACK_SHRINK);

	_sel_label.set_markup (_("<b>—</b>"));
	_sel_label.set_ellipsize (Pango::ELLIPSIZE_END);
	_sel_label.set_alignment (0.5, 0.5);
	_box.pack_start (_sel_label, Gtk::PACK_SHRINK);

	/* PAN + WIDTH (route) : pilotent le panner Ardour, pas le DSP Oxford.
	 * Width n'est montré que pour les pistes stéréo. */
	{
		Gtk::HBox* pw = Gtk::manage (new Gtk::HBox ()); pw->set_spacing (4);
		_pan_knob = Gtk::manage (new TridentKnob (0, 1, 0.5, _("Pan"), 0.45, 0.50, 0.58));
		_pan_knob->set_bg (kPanelBg[0], kPanelBg[1], kPanelBg[2]);   // crème (entourage châssis)
		_knobsMain.push_back (_pan_knob);
		_pan_knob->set_ui_scale (kKnob);
		_pan_knob->set_image (_kimg[5]);   // skin neutre
		_pan_knob->on_format ([](double v){
			int p = (int) ((v - 0.5) * 200.0 + (v >= 0.5 ? 0.5 : -0.5));
			int ap = p < 0 ? -p : p;
			char b[16]; std::snprintf (b, sizeof b, "%s%d", p > 0 ? "R" : (p < 0 ? "L" : "C"), ap);
			return std::string (b);
		});
		_pan_knob->on_changed ([this](double v){
			if (_updating) return;
			if (_route && _route->pan_azimuth_control ()) {
				_route->pan_azimuth_control ()->set_value (v, PBD::Controllable::NoGroup);
			}
		});
		/* Alt + glisser : MÊME delta de pan sur les autres pistes sélectionnées
		 * (le pan vit sur la Route, pas sur l'OxfordChannel -> apply_linked ne
		 * le couvrait pas). */
		_pan_knob->on_linked ([this](double delta){
			Mixer_UI* mx = Mixer_UI::instance ();
			if (!mx) { return; }
			for (MixerStrip* s : mx->mixer_strips ()) {
				std::shared_ptr<ARDOUR::Route> r = s->route ();
				if (!r || !r->is_selected () || r->is_master () || r == _route) { continue; }
				std::shared_ptr<ARDOUR::AutomationControl> pc = r->pan_azimuth_control ();
				if (!pc) { continue; }
				double v = pc->get_value () + delta;
				pc->set_value (v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v), PBD::Controllable::NoGroup);
			}
		});
		_width_knob = Gtk::manage (new TridentKnob (-1, 1, 0, _("Width"), 0.45, 0.50, 0.58));
		_width_knob->set_bg (kPanelBg[0], kPanelBg[1], kPanelBg[2]);   // crème (entourage châssis)
		_knobsMain.push_back (_width_knob);
		_width_knob->set_ui_scale (kKnob);
		_width_knob->set_image (_kimg[5]);   // skin neutre
		_width_knob->on_format ([](double v){ char b[16]; std::snprintf (b, sizeof b, "%.0f%%", (1.0 + v) * 100.0); return std::string (b); });
		_width_knob->on_changed ([this](double v){
			if (_updating) return;
			if (_chan) { _chan->setWidth ((float) (1.0 + v)); }
		});
		ArdourWidgets::set_tooltip (*_pan_knob,   _("Track panning. Alt + drag applies the same offset to the selected tracks."));
		ArdourWidgets::set_tooltip (*_width_knob, _("Stereo width of the strip (M/S processing). Hidden on mono tracks."));
		Gtk::Alignment* cen = Gtk::manage (new Gtk::Alignment (0.5, 0.5, 0, 0));
		pw->pack_start (*_pan_knob, false, false);
		pw->pack_start (*_width_knob, false, false);
		cen->add (*pw);
		/* masqué sur la tranche master : un panoramique n'y a pas de sens */
		cen->set_no_show_all (true);
		_panwid_box = cen;
		_box.pack_start (*cen, Gtk::PACK_SHRINK);
		/* ATTENTION : show_all() SORT immédiatement sur un widget no_show_all,
		 * sans descendre dans ses enfants -> il faut montrer le contenu à la
		 * main, sinon les potards restent invisibles à vie (même après un
		 * cen->show(), qui ne montre que l'alignement).
		 * Et l'état de départ est "piste" : refresh() ne rebascule que sur
		 * CHANGEMENT de sélection, il ne montrerait donc jamais rien au
		 * démarrage tant que le master n'a pas été sélectionné puis quitté. */
		pw->show_all ();
		cen->show ();
	}

	Gtk::HBox* navb = Gtk::manage (new Gtk::HBox ());
	navb->set_spacing (2);
	navb->set_homogeneous (true);   // EQ / DYNAMICS largeur égale
	_btn_eq.set_text (_("EQ"));
	_btn_dyn.set_text (_("DYN"));
	_btn_master.set_text (_("MASTER"));
	_btn_eq.set_fixed_colors     (0x9aa5b0ff, 0x454a52ff);   // actif gris clair / inactif gris
	_btn_dyn.set_fixed_colors    (0x9aa5b0ff, 0x454a52ff);
	_btn_master.set_fixed_colors (0x9aa5b0ff, 0x454a52ff);
	{ Pango::FontDescription nf ("ArdourSans bold 10");
	  _btn_eq.set_layout_font (nf); _btn_dyn.set_layout_font (nf); _btn_master.set_layout_font (nf); }
	_btn_eq.signal_clicked.connect     ([this](){ show_view (_isBus ? 4 : 0); });
	_btn_dyn.signal_clicked.connect    ([this](){ show_view (_isBus ? 3 : 1); });
	_btn_master.signal_clicked.connect (sigc::bind (sigc::mem_fun (*this, &OxfordConsolePanel::show_view), 2));
	navb->pack_start (_btn_eq, true, true);
	navb->pack_start (_btn_dyn, true, true);
	navb->pack_start (_btn_master, true, true);
	ArdourWidgets::set_tooltip (_btn_eq,     _("Five-band equaliser and filters of the selected strip."));
	ArdourWidgets::set_tooltip (_btn_dyn,    _("Gate, expander, compressor and limiter of the selected strip."));
	ArdourWidgets::set_tooltip (_btn_master, _("Control room: monitor section, PCM-1630 model and output limiter."));
	_box.pack_start (*navb, Gtk::PACK_SHRINK);
	/* La vue MASTER (régie MONITOR + PCM-1630) n'appartient QU'À la tranche
	 * master : depuis une piste, le bouton n'a pas lieu d'être. On l'affiche /
	 * masque avec la sélection (set_no_show_all : un show_all() du parent ne
	 * doit pas le ressortir). Inversement EQ/DYN disparaissent sur le master. */
	_btn_eq.set_no_show_all (true);
	_btn_dyn.set_no_show_all (true);
	_btn_master.set_no_show_all (true);

	_nb.set_show_tabs (false);
	_nb.set_show_border (false);
	{ Gdk::Color nc; nc.set_rgb_p (kPanelBg[0], kPanelBg[1], kPanelBg[2]); _nb.modify_bg (Gtk::STATE_NORMAL, nc); }
	_chromeMain.push_back (&_nb);
	_nb.append_page (*wrap_scroll (build_eq (false)));
	_nb.append_page (*build_dyn (false));   // page 1 : la vue dynamique porte son propre défilement (graphe épinglé)
	_nb.append_page (*wrap_scroll (build_master ()));
	_nb.append_page (*build_dyn (true));    // page 3 : dynamique des BUS
	_nb.append_page (*wrap_scroll (build_eq (true)));    // page 4 : EQ des BUS
	_box.pack_start (_nb, Gtk::PACK_EXPAND_WIDGET);

	show_view (0);
	show_all ();
	_btn_eq.show ();
	_btn_dyn.show ();
	_btn_master.hide ();   // apparaît uniquement quand le master est sélectionné

	/* suivi LIVE du thème (Oxford <-> Oxford Warm) : recolore tout le châssis */
	UIConfiguration::instance ().ColorsChanged.connect (sigc::mem_fun (*this, &OxfordConsolePanel::theme_colors_changed));

	_timer = Glib::signal_timeout ().connect (sigc::mem_fun (*this, &OxfordConsolePanel::on_timer), 150);
}

OxfordConsolePanel::~OxfordConsolePanel () { _timer.disconnect (); }

void
OxfordConsolePanel::fetch_theme_chrome ()
{
	bool failed = false;
	const uint32_t c1 = UIConfiguration::instance ().color ("theme:bg1", &failed);
	if (!failed) {
		kPanelBg[0] = ((c1 >> 24) & 0xff) / 255.0;
		kPanelBg[1] = ((c1 >> 16) & 0xff) / 255.0;
		kPanelBg[2] = ((c1 >>  8) & 0xff) / 255.0;
	}
	failed = false;
	const uint32_t c2 = UIConfiguration::instance ().color ("theme:bg2", &failed);
	if (!failed) {
		kSubBg[0] = ((c2 >> 24) & 0xff) / 255.0;
		kSubBg[1] = ((c2 >> 16) & 0xff) / 255.0;
		kSubBg[2] = ((c2 >>  8) & 0xff) / 255.0;
	}
	/* texte du châssis : suit le foreground du thème (lisible sur Night) */
	failed = false;
	const uint32_t c3 = UIConfiguration::instance ().color ("neutral:foreground", &failed);
	if (!failed) {
		kLabelDk[0] = ((c3 >> 24) & 0xff) / 255.0;
		kLabelDk[1] = ((c3 >> 16) & 0xff) / 255.0;
		kLabelDk[2] = ((c3 >>  8) & 0xff) / 255.0;
	}
}

/* Texte d'infobulle d'un contrôle, à partir du contexte de section posé par le
 * constructeur de la vue (_tipctx) et du libellé du potard. Un seul point de
 * vérité : mk() et mkToggle() y passent tous. L'extinction générale est celle
 * d'Ardour (Préférences -> Apparence -> infobulles), rien à coder ici. */
static const char*
oxford_tip (const char* ctx, const char* lab)
{
	if (!ctx || !lab) { return 0; }
	const std::string c (ctx), l (lab);

	if (c == "EQ") {
		if (l == "FREQ")  { return _("Band frequency. Drag to set; the matching dot moves along the curve."); }
		if (l == "Q")     { return _("Bell width: 0.5 = very wide, 16 = surgical. With the band in Shelf mode, this knob becomes the OVERSHOOT (corner resonance)."); }
		if (l == "GAIN")  { return _("Band boost or cut, ±20 dB. The width follows the selected curve type."); }
		if (l == "In")    { return _("Enables the band. When off it is removed from the computation (no residual colouration)."); }
		if (l == "Shelf") { return _("Switches the band from a bell to a shelf. The Q knob then drives the overshoot."); }
	}
	if (c == "HP")  { return _("High-pass frequency (20–400 Hz). The slope is set with the button on the left, or by dragging the dot up and down on the curve."); }
	if (c == "LP")  { return _("Low-pass frequency (1–20 kHz). The slope is set with the button on the left, or by dragging the dot up and down on the curve."); }

	if (c == "GATE") {
		if (l == "On")  { return _("Noise gate: cuts below the threshold, with 4 dB of hysteresis to avoid chattering."); }
		if (l == "Thr") { return _("Level at which the gate opens."); }
		if (l == "Rng") { return _("Attenuation applied while the gate is closed. −80 dB = hard cut, −20 dB = mere darkening."); }
		if (l == "Att") { return _("Opening time. Very short for transients (drums), longer to avoid clicks."); }
		if (l == "Rel") { return _("Closing time once the signal falls below the threshold."); }
	}
	if (c == "EXPANDER") {
		if (l == "On")  { return _("Expander: instead of cutting abruptly like the gate, it gradually pushes down whatever sits below the threshold."); }
		if (l == "Thr") { return _("Level below which expansion acts."); }
		if (l == "Rat") { return _("Expansion ratio. 2 = 2 dB down for every dB below the threshold."); }
		if (l == "Rng") { return _("Maximum attenuation the expander may apply."); }
		if (l == "Att") { return _("Time to return to unity gain once the signal rises back above the threshold."); }
		if (l == "Rel") { return _("Time to pull down once the signal falls below the threshold."); }
	}
	if (c == "COMPRESSOR") {
		if (l == "On")   { return _("Feed-forward compressor, detector in dB, with shared look-ahead."); }
		if (l == "Thr")  { return _("Compression threshold: above it, gain reduction applies."); }
		if (l == "Rat")  { return _("Ratio, using the console's 1/Ratio law: fully up = limiter. The displayed value is the real X:1."); }
		if (l == "Att")  { return _("Time for the gain reduction to build up."); }
		if (l == "Hold") { return _("Freezes the gain reduction for this long before releasing: avoids pumping on uneven material."); }
		if (l == "Rel")  { return _("Time to return to unity gain once the signal has come back down."); }
		if (l == "Mkp")  { return _("Make-up gain, up to +24 dB, to recover the level lost to compression."); }
		if (l == "Soft") { return _("Knee width: 0 = hard corner, 20 = very progressive compression around the threshold."); }
	}
	if (c == "LIMITER") {
		if (l == "On")   { return _("Channel limiter, referenced to the OUTPUT of the strip."); }
		if (l == "Thr")  { return _("Limiter ceiling."); }
		if (l == "Att")  { return _("Reaction time. Very short = more headroom held, but more distortion on low frequencies."); }
		if (l == "Hold") { return _("Freezes the gain reduction before releasing."); }
		if (l == "Rel")  { return _("Release time of the limiting."); }
	}

	if (c == "PCM") {
		if (l == "In")  { return _("Input level into the PCM-1630 model: this is what decides how hard the capture is driven."); }
		if (l == "Out") { return _("Output level trim after the model."); }
		if (l == "On")  { return _("Runs the master through the PCM-1630 converter model. Off = direct path."); }
	}
	if (c == "OUTLIM") {
		if (l == "Ceil") { return _("Ceiling of the output limiter, placed AFTER the PCM-1630 to catch the peaks the capture brings back up."); }
		if (l == "On")   { return _("Output brickwall limiter, last link before the physical outputs."); }
	}
	return 0;
}

void
OxfordConsolePanel::apply_chrome ()
{
	Gdk::Color cm; cm.set_rgb_p (kPanelBg[0], kPanelBg[1], kPanelBg[2]);
	Gdk::Color cs; cs.set_rgb_p (kSubBg[0],   kSubBg[1],   kSubBg[2]);
	Gdk::Color cf; cf.set_rgb_p (kLabelDk[0], kLabelDk[1], kLabelDk[2]);
	modify_bg (Gtk::STATE_NORMAL, cm);
	modify_fg (Gtk::STATE_NORMAL, cf);
	_box.modify_fg (Gtk::STATE_NORMAL, cf);
	for (Gtk::Widget* w : _chromeMain) { w->modify_bg (Gtk::STATE_NORMAL, cm); }
	for (Gtk::Widget* w : _chromeSub)  { w->modify_bg (Gtk::STATE_NORMAL, cs); }
	for (TridentKnob* k : _knobsMain)  { k->set_bg (kPanelBg[0], kPanelBg[1], kPanelBg[2]); k->queue_draw (); }
	for (TridentKnob* k : _knobsSub)   { k->set_bg (kSubBg[0],   kSubBg[1],   kSubBg[2]);   k->queue_draw (); }
	queue_draw ();
}

void
OxfordConsolePanel::theme_colors_changed ()
{
	fetch_theme_chrome ();
	apply_chrome ();
}

TridentKnob*
OxfordConsolePanel::mk (double lo, double hi, double def, const char* lab,
                        std::function<std::string(double)> fmt, bool master, int band,
                        std::function<void(OxfordChannel&, double)> set,
                        std::function<double(OxfordChannel&)> get,
                        double cr, double cg, double cb)
{
	TridentKnob* k = Gtk::manage (new TridentKnob (lo, hi, def, lab, cr, cg, cb));
	k->set_bg (kSubBg[0], kSubBg[1], kSubBg[2]);   // fond clair console
	_knobsSub.push_back (k);
	if (const char* tip = oxford_tip (_tipctx, lab)) { ArdourWidgets::set_tooltip (*k, tip); }
	k->set_ui_scale (kKnob);                                // agrandissement ×1.5
	k->set_image (_kimg[5]);   // skin neutre par défaut (les bandes EQ écrasent avec leur couleur)
	k->on_format (fmt);
	const size_t idx = _params.size ();
	Param p; p.knob = k; p.master = master; p.band = band; p.set = set; p.get = get;
	_params.push_back (p);
	k->on_changed ([this, idx] (double v) {
		if (_updating) { return; }
		std::shared_ptr<OxfordChannel> c = _params[idx].master ? _master : _chan;
		if (c) { _params[idx].set (*c, v); }
	});
	/* Alt + glisser : diffuse le MÊME delta aux AUTRES pistes Oxford sélectionnées (façon Trident). */
	k->on_linked ([this, idx] (double delta) {
		if (_params[idx].master) { return; }   // pas de link sur les paramètres master
		Mixer_UI* mx = Mixer_UI::instance ();
		if (!mx) { return; }
		for (MixerStrip* s : mx->mixer_strips ()) {
			std::shared_ptr<ARDOUR::Route> r = s->route ();
			if (!r || !r->is_selected () || r->is_master ()) { continue; }
			std::shared_ptr<OxfordChannel> c2 = r->oxford_channel ();
			if (!c2 || c2->kind () != OxfordChannel::Strip || c2 == _chan) { continue; }
			_params[idx].set (*c2, _params[idx].get (*c2) + delta);
		}
	});
	return k;
}

ArdourButton*
OxfordConsolePanel::mkToggle (Gtk::Box& box, const char* lab, bool master,
                              std::function<void(OxfordChannel&, bool)> set,
                              std::function<bool(OxfordChannel&)> get,
                              const char* capcol)
{
	/* petit CARRÉ coloré (sans texte) + libellé DESSOUS (jamais tronqué) */
	ArdourButton* b = Gtk::manage (new ArdourButton (""));
	b->set_fixed_colors (0xc8e030ff, 0xc8c4b8ff);   // actif vert-jaune / inactif crème
	b->set_corner_radius (3.0);
	if (const char* tip = oxford_tip (_tipctx, lab)) { ArdourWidgets::set_tooltip (*b, tip); }
	b->set_size_request ((int)(16*kKnob), (int)(16*kKnob));
	const size_t idx = _toggles.size ();
	Toggle t; t.btn = b; t.master = master; t.set = set; t.get = get;
	_toggles.push_back (t);
	b->signal_clicked.connect ([this, idx] () {
		if (_toggles[idx].master) {
			if (!_master) { return; }                       // param master : local (pas de link)
			const bool nv = !_toggles[idx].get (*_master);
			_toggles[idx].set (*_master, nv);
			_toggles[idx].btn->set_active_state (nv ? Gtkmm2ext::ExplicitActive : Gtkmm2ext::Off);
		} else {
			if (!_chan) { return; }
			const bool nv = !_toggles[idx].get (*_chan);
			auto setfn = _toggles[idx].set;
			apply_linked ([setfn, nv] (OxfordChannel& c) { setfn (c, nv); });   // Alt -> sélection
			_toggles[idx].btn->set_active_state (nv ? Gtkmm2ext::ExplicitActive : Gtkmm2ext::Off);
		}
		eqc ().queue_draw ();   // refléter immédiatement (In/Shelf) sur le visualiseur
	});
	/* carré coloré + libellé À DROITE (toujours lisible, jamais collé) */
	Gtk::HBox* row = Gtk::manage (new Gtk::HBox ()); row->set_spacing (3);
	Gtk::Alignment* al = Gtk::manage (new Gtk::Alignment (0.5, 0.5, 0, 0)); al->add (*b);
	row->pack_start (*al, false, false);
	Gtk::Label* cap = Gtk::manage (new Gtk::Label ());
	/* couleur par défaut des libellés = foreground du THÈME (lisible sur
	 * Oxford Night ; le sentinel "#1a2433" historique est remplacé) */
	char themed[8];
	if (std::strcmp (capcol, "#1a2433") == 0) {
		std::snprintf (themed, sizeof themed, "#%02x%02x%02x",
		               (int)(kLabelDk[0]*255.0+0.5), (int)(kLabelDk[1]*255.0+0.5), (int)(kLabelDk[2]*255.0+0.5));
		capcol = themed;
	}
	char m[64]; std::snprintf (m, sizeof m, "<span size=\"small\" foreground=\"%s\">%s</span>", capcol, lab);
	cap->set_markup (m); cap->set_alignment (0.0, 0.5);
	row->pack_start (*cap, false, false);
	box.pack_start (*row, false, false);
	return b;
}

/* Vue EQ. Même principe que la vue dynamique : DEUX conteneurs distincts, un
 * pour les pistes (5 bandes + filtres à pente) et un pour les bus (LF/MF/HF
 * seules, spec Return), au lieu de détacher des blocs d'un conteneur commun. */
Gtk::Widget*
OxfordConsolePanel::build_eq (bool bus)
{
	Gtk::VBox* b = Gtk::manage (new Gtk::VBox ());
	b->set_spacing (1);
	Gtk::DrawingArea& curve = bus ? _eq_curveB : _eq_curve;

	/* courbe de réponse en tête — pastilles draggables (freq/gain) + molette (Q) */
	curve.set_size_request (-1, (int)(120*kUI));
	ArdourWidgets::set_tooltip (curve, _("Equaliser response. Drag a dot to set the frequency and gain of its band, use the wheel for Q (overshoot in Shelf mode). The cream dots are the filters: dragging one engages it, sideways sets the frequency, up and down the slope (the wheel does it too). Alt: the selection follows."));
	curve.signal_expose_event ().connect (sigc::mem_fun (*this, &OxfordConsolePanel::on_eq_expose));
	curve.add_events (Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK
	                    | Gdk::POINTER_MOTION_MASK | Gdk::SCROLL_MASK);
	curve.signal_button_press_event ().connect (sigc::mem_fun (*this, &OxfordConsolePanel::on_eq_press));
	curve.signal_button_release_event ().connect (sigc::mem_fun (*this, &OxfordConsolePanel::on_eq_release));
	curve.signal_motion_notify_event ().connect (sigc::mem_fun (*this, &OxfordConsolePanel::on_eq_motion));
	curve.signal_scroll_event ().connect (sigc::mem_fun (*this, &OxfordConsolePanel::on_eq_scroll));
	b->pack_start (curve, Gtk::PACK_SHRINK);

	/* type de courbe + bascules shelf. UN dropdown par page. */
	ArdourWidgets::ArdourDropdown& curve_btn = bus ? _curve_btnB : _curve_btn;
	Gtk::HBox* opt = Gtk::manage (new Gtk::HBox ()); opt->set_spacing (2);
	curve_btn.set_text (curve_name (2));
	curve_btn.set_fixed_colors (0xc8e030ff, 0xc8c4b8ff);
	curve_btn.set_layout_font (Pango::FontDescription ("ArdourSans 10"));
	/* menu déroulant : on voit les 5 types d'un coup au lieu de les parcourir */
	for (int t = 0; t < 5; ++t) {
		ArdourWidgets::ArdourDropdown* cb = &curve_btn;
		curve_btn.add_menu_elem (Gtk::Menu_Helpers::MenuElem (curve_name (t), [this, t, cb] () {
			if (!_chan) { return; }
			apply_linked ([t] (OxfordChannel& c) { c.setCurveType (t); });   // Alt -> sélection
			cb->set_text (curve_name (t));
		}));
	}
	Gtk::Label* cl = Gtk::manage (new Gtk::Label (_("Curve"))); cl->set_alignment (0,0.5);
	opt->pack_start (*cl, false, false);
	ArdourWidgets::set_tooltip (curve_btn, _("Curve type: how bell width reacts to gain. 1 = constant Q, 2 = symmetric when boosting and pinched when cutting, 3 = moderate (factory setting), 4 = strongly proportional."));
	opt->pack_start (curve_btn, true, true);
	b->pack_start (*opt, Gtk::PACK_SHRINK);

	/* HPF — pistes seulement (les Returns n'ont pas de filtres à pente). */
	Gtk::Frame* hpf = 0;
	if (!bus) {
	_tipctx = "HP";
	Gtk::HBox* hp = Gtk::manage (new Gtk::HBox ()); hp->set_spacing (2);
	Gtk::Label* hpl = Gtk::manage (new Gtk::Label (_("HP"))); hpl->set_size_request (40,-1); hpl->set_alignment (0,0.5);
	hp->pack_start (*hpl, false, false);
	_hp_slope_btn.set_text (_("off"));
	_hp_slope_btn.set_fixed_colors (0xc8e030ff, 0xc8c4b8ff);
	_hp_slope_btn.set_layout_font (Pango::FontDescription ("ArdourSans 9")); _hp_slope_btn.set_size_request ((int)(28*kKnob), (int)(15*kKnob));
	for (int s = 0; s <= 36; s += 6) {
		char sl[8]; std::snprintf (sl, sizeof sl, "%d", s);
		const std::string lbl = s ? std::string (sl) : std::string (_("off"));
		_hp_slope_btn.add_menu_elem (Gtk::Menu_Helpers::MenuElem (lbl, [this, s] () {
			if (!_chan) { return; }
			const bool  on    = (s > 0);
			const float slope = on ? (float) s : 12.f;
			apply_linked ([on, slope] (OxfordChannel& c) { c.setHPF (on, c.hpfHz (), slope); });   // Alt -> sélection
		}));
	}
	ArdourWidgets::set_tooltip (_hp_slope_btn, _("High-pass slope: off, 6, 12 … 36 dB/oct."));
	hp->pack_start (_hp_slope_btn, false, false);
	hp->pack_start (*mk (20,400,80,_("Freq"),f_hz,false,-1,
	    [](OxfordChannel& c,double v){ c.setHPF (c.hpfOn (), (float)v, c.hpfSlope ()); },
	    [](OxfordChannel& c){ return (double) c.hpfHz (); }, kFilterCap[0],kFilterCap[1],kFilterCap[2]), true, false);
	hpf = Gtk::manage (new Gtk::Frame (_("HP FILTER")));
	hpf->add (*subpanel (hp));
	}

	/* 5 bandes empilées. Chaque bloc : label crème à gauche | (FREQ+Q) au-dessus,
	 * GAIN dessous | (LF/HF : shelf toggle). Resserré, séparateur fin entre blocs. */
	_tipctx = "EQ";
	const char* bn[5] = { "LF","LMF","MF","HMF","HF" };
	Gtk::VBox* bandbox = Gtk::manage (new Gtk::VBox ()); bandbox->set_spacing (10);   // bandes plus aérées
	for (int i = 0; i < 5; ++i) {
		if (bus && (i == 1 || i == 3)) { continue; }   // Return : LF/MF/HF seules
		const int band = i; const double R=bandRGB[i][0], G=bandRGB[i][1], B=bandRGB[i][2];
		Gtk::HBox* block = Gtk::manage (new Gtk::HBox ()); block->set_spacing (2); block->set_border_width (3);
		Gtk::Label* l = Gtk::manage (new Gtk::Label ());
		char m[64]; std::snprintf (m,sizeof m,"<b><span foreground=\"#1a2433\">%s</span></b>",bn[i]);
		l->set_markup (m); l->set_size_request (34,-1); l->set_alignment (0.5,0.5);
		block->pack_start (*l, false, false);

		/* UNE seule ligne par bande (profite de la largeur ×1.5) : FREQ · Q · GAIN · In/Shelf
		 * Les 3 knobs portent la COULEUR de la bande (skin _kimg[i]). */
		TridentKnob* fk = mk (oxFmin[i],oxFmax[i],oxFmin[i],_("FREQ"),f_hz,false,band,
		    [band](OxfordChannel& c,double v){ c.setBandFreq (band,(float)v); },
		    [band](OxfordChannel& c){ return (double) c.bandFreq (band); }, R,G,B);
		fk->set_image (_kimg[i]);
		block->pack_start (*fk, true, false);
		TridentKnob* qk = mk (0.5,16,2.83,_("Q"),f_q,false,-1,   // défaut 2.83 (centre log), plage 0.5..16
		    [band](OxfordChannel& c,double v){
		        /* En mode shelf, le knob Q pilote l'OVERSHOOT (0-50%), cf. manuel OXF-R3 */
		        const float o = (float)((v - 0.5) / 15.5 * 0.5);
		        if      (band == 0 && c.lfShelf ()) c.setLFOvershoot (o);
		        else if (band == 4 && c.hfShelf ()) c.setHFOvershoot (o);
		        else                                c.setBandQ (band, (float)v);
		    },
		    [band](OxfordChannel& c){ return (double) c.bandQ (band); }, R,G,B);
		qk->set_log (true);   // potard Q logarithmique (2.83 au centre, comme le plugin)
		qk->set_image (_kimg[i]);
		block->pack_start (*qk, true, false);
		TridentKnob* gk = mk (-20,20,0,_("GAIN"),f_db,false,-1,
		    [band](OxfordChannel& c,double v){ c.setBandGain (band,(float)v); },
		    [band](OxfordChannel& c){ return (double) c.bandGain (band); }, R,G,B);
		gk->set_image (_kimg[i]);
		block->pack_start (*gk, true, false);
		Gtk::VBox* tgl = Gtk::manage (new Gtk::VBox ()); tgl->set_spacing (3);
		mkToggle (*tgl, _("In"), false,
		    [band](OxfordChannel& c,bool v){ c.setBandEnabled (band, v); },
		    [band](OxfordChannel& c){ return c.bandEnabled (band); });
		if (i == 0) mkToggle (*tgl, _("Shelf"), false, [](OxfordChannel& c,bool v){ c.setLFShelf (v); }, [](OxfordChannel& c){ return c.lfShelf (); });
		if (i == 4) mkToggle (*tgl, _("Shelf"), false, [](OxfordChannel& c,bool v){ c.setHFShelf (v); }, [](OxfordChannel& c){ return c.hfShelf (); });
		Gtk::Alignment* tgal = Gtk::manage (new Gtk::Alignment (0.0, 0.5, 0, 0)); tgal->add (*tgl);
		block->pack_start (*tgal, false, false);
		bandbox->pack_start (*block, Gtk::PACK_SHRINK);
		if (i < 4) {
			Gtk::HSeparator* sep = Gtk::manage (new Gtk::HSeparator ());
			bandbox->pack_start (*sep, Gtk::PACK_SHRINK);
		}
	}
	Gtk::Frame* bandframe = Gtk::manage (new Gtk::Frame (_("EQUALISER")));
	bandframe->add (*subpanel (bandbox));
	b->pack_start (*bandframe, Gtk::PACK_SHRINK);

	/* LPF — pistes seulement. */
	if (!bus) {
	_tipctx = "LP";
	Gtk::HBox* lp = Gtk::manage (new Gtk::HBox ()); lp->set_spacing (2);
	Gtk::Label* lpl = Gtk::manage (new Gtk::Label (_("LP"))); lpl->set_size_request (40,-1); lpl->set_alignment (0,0.5);
	lp->pack_start (*lpl, false, false);
	_lp_slope_btn.set_text (_("off"));
	_lp_slope_btn.set_fixed_colors (0xc8e030ff, 0xc8c4b8ff);
	_lp_slope_btn.set_layout_font (Pango::FontDescription ("ArdourSans 9")); _lp_slope_btn.set_size_request ((int)(28*kKnob), (int)(15*kKnob));
	for (int s = 0; s <= 36; s += 6) {
		char sl[8]; std::snprintf (sl, sizeof sl, "%d", s);
		const std::string lbl = s ? std::string (sl) : std::string (_("off"));
		_lp_slope_btn.add_menu_elem (Gtk::Menu_Helpers::MenuElem (lbl, [this, s] () {
			if (!_chan) { return; }
			const bool  on    = (s > 0);
			const float slope = on ? (float) s : 12.f;
			apply_linked ([on, slope] (OxfordChannel& c) { c.setLPF (on, c.lpfHz (), slope); });   // Alt -> sélection
		}));
	}
	ArdourWidgets::set_tooltip (_lp_slope_btn, _("Low-pass slope: off, 6, 12 … 36 dB/oct."));
	lp->pack_start (_lp_slope_btn, false, false);
	lp->pack_start (*mk (1000,20000,18000,_("Freq"),f_hz,false,-1,
	    [](OxfordChannel& c,double v){ c.setLPF (c.lpfOn (), (float)v, c.lpfSlope ()); },
	    [](OxfordChannel& c){ return (double) c.lpfHz (); }, kFilterCap[0],kFilterCap[1],kFilterCap[2]), true, false);
	Gtk::Frame* lpf = Gtk::manage (new Gtk::Frame (_("LP FILTER")));
	lpf->add (*subpanel (lp));
	/* HP et LP côte à côte (gagne une rangée de hauteur) */
	Gtk::HBox* filt = Gtk::manage (new Gtk::HBox ()); filt->set_spacing (4);
	filt->pack_start (*hpf, true, true);
	filt->pack_start (*lpf, true, true);
	b->pack_start (*filt, Gtk::PACK_SHRINK);
	}

	return b;
}

/* Vue dynamique. DEUX vues DISTINCTES sont construites : une pour les pistes
 * (Gate/Exp/Comp/Lim) et une pour les bus (Comp/Lim seulement). On BASCULE de
 * l'une à l'autre au lieu de détacher des sections d'un conteneur commun :
 * détacher laissait GTK conserver la hauteur libérée, d'où le grand vide en
 * mode bus. Ici chaque conteneur naît avec exactement ce qu'il doit montrer,
 * il n'y a donc plus rien à récupérer. */
Gtk::Widget*
OxfordConsolePanel::build_dyn (bool bus)
{
	Gtk::VBox* b = Gtk::manage (new Gtk::VBox ());
	b->set_spacing (1);
	Gtk::DrawingArea& curve  = bus ? _dyn_curveB  : _dyn_curve;
	Gtk::DrawingArea& meters = bus ? _dyn_metersB : _dyn_meters;

	/* Graphe + VU : ÉPINGLÉS en haut de la page, hors du défilement. Empilés avec
	 * les quatre sections, ils sortaient de l'écran dès qu'on descendait régler le
	 * limiteur — or c'est justement là qu'on veut les voir. */
	Gtk::VBox* head = Gtk::manage (new Gtk::VBox ());
	head->set_spacing (1);
	/* graphe transfert IN/OUT PLEINE LARGEUR */
	curve.set_size_request (-1, (int)(105*kUI));
	ArdourWidgets::set_tooltip (curve, _("Input/output transfer curve of the four sections combined. The moving dot is the detector level; the dotted lines mark the thresholds."));
	curve.signal_expose_event ().connect (sigc::mem_fun (*this, bus ? &OxfordConsolePanel::on_dyn_exposeB : &OxfordConsolePanel::on_dyn_expose));
	head->pack_start (curve, Gtk::PACK_SHRINK);
	/* VU de GR HORIZONTAUX sous le graphe, barres fines (précis) */
	meters.set_size_request (-1, (int)((bus ? 34 : 64)*kUI));
	ArdourWidgets::set_tooltip (meters, _("Gain reduction of the four sections, 0 to 20 dB, with peak hold."));
	meters.signal_expose_event ().connect (sigc::mem_fun (*this, bus ? &OxfordConsolePanel::on_dyn_metersB : &OxfordConsolePanel::on_dyn_meters_expose));
	head->pack_start (meters, Gtk::PACK_SHRINK);

	/* (Timing retiré de la GUI — non utilisé ; le DSP reste en loi Normal par défaut.) */

	/* Les 4 sections sont EMPILÉES (comme l'écran réel OXF-R3), en-têtes colorés,
	 * chacune avec son On + ses knobs -> remplit la colonne, pas de vide.
	 * Nombre de potards par rangée calculé sur la largeur RÉELLE (le panneau ne
	 * défile pas horizontalement : à fort scale DPI, 4 ne rentrent plus). */
	const double knobW  = 56.0 * kKnob * trident_ui_scale ();
	const int    perRow = (knobW * 4.0 <= 320.0 * kUI - 32.0) ? 4 : 3;
	auto flow = [&](Gtk::VBox* col, std::vector<TridentKnob*> ks) {
		Gtk::HBox* r=0; int n=0;
		for (auto k : ks) { if (n%perRow==0){ r=Gtk::manage(new Gtk::HBox()); r->set_spacing(2); col->pack_start(*r,Gtk::PACK_SHRINK);} r->pack_start(*k,true,false); ++n; }
	};
	auto sec_title = [](Gtk::Label* l, const char* name, const char* color) {
		char m[128];
		std::snprintf (m, sizeof m, "<b><span foreground=\"%s\">%s</span></b>", color, name);
		l->set_markup (m);   /* pas de "large" : le titre poussait le bouton On hors du bord */
	};
	/* Marqueur de repli DESSINÉ, pas écrit : les triangles Unicode (U+25BE/U+25B8)
	 * sortaient en tofu — aucune police de la chaîne de repli ne les porte ici. */
	auto make_tri = [](const char* color, std::shared_ptr<bool> open) -> Gtk::DrawingArea* {
		double r = 0.8, g = 0.8, b = 0.8;
		unsigned int rr = 0, gg = 0, bb = 0;
		if (std::sscanf (color, "#%2x%2x%2x", &rr, &gg, &bb) == 3) {
			r = rr / 255.0; g = gg / 255.0; b = bb / 255.0;
		}
		Gtk::DrawingArea* d = Gtk::manage (new Gtk::DrawingArea ());
		const int s = (int) (11 * kUI);
		d->set_size_request (s, s);
		d->signal_expose_event ().connect ([d, r, g, b, open] (GdkEventExpose*) -> bool {
			Glib::RefPtr<Gdk::Window> win = d->get_window ();
			if (!win) { return false; }
			Cairo::RefPtr<Cairo::Context> cr = win->create_cairo_context ();
			Gtk::Allocation a = d->get_allocation ();
			const double w = a.get_width (), h = a.get_height ();
			const double cx = w * 0.5, cy = h * 0.5, k = std::min (w, h) * 0.30;
			cr->set_source_rgb (r, g, b);
			if (*open) {   /* déplié : pointe vers le bas */
				cr->move_to (cx - k, cy - k * 0.6);
				cr->line_to (cx + k, cy - k * 0.6);
				cr->line_to (cx,     cy + k * 0.9);
			} else {       /* replié : pointe vers la droite */
				cr->move_to (cx - k * 0.6, cy - k);
				cr->line_to (cx + k * 0.9, cy);
				cr->line_to (cx - k * 0.6, cy + k);
			}
			cr->close_path ();
			cr->fill ();
			return true;
		}, false);
		return d;
	};
	auto section = [&](const char* name, const char* color,
	                   std::function<void(OxfordChannel&,bool)> son,
	                   std::function<bool(OxfordChannel&)> gon,
	                   bool trackOnly) -> Gtk::VBox* {
		/* écart vertical UNIFORME entre les 4 modules */
		Gtk::Label* gap = Gtk::manage (new Gtk::Label ()); gap->set_size_request (1, 6);
		b->pack_start (*gap, Gtk::PACK_SHRINK);
		/* micro-panneau sombre sous le titre : délimite chaque module d'un coup d'œil */
		Gtk::EventBox* bar = Gtk::manage (new Gtk::EventBox ());
		Gdk::Color barbg; barbg.set_rgb_p (kSubBg[0], kSubBg[1], kSubBg[2]); bar->modify_bg (Gtk::STATE_NORMAL, barbg); _chromeSub.push_back (bar);
		bar->set_border_width (3);
		Gtk::HBox* hd = Gtk::manage (new Gtk::HBox ()); hd->set_spacing (4);
		std::shared_ptr<bool> open = std::make_shared<bool> (true);
		Gtk::DrawingArea* tri = make_tri (color, open);
		hd->pack_start (*tri, false, false);
		Gtk::Label* l = Gtk::manage (new Gtk::Label ());
		sec_title (l, name, color);
		l->set_alignment (0,0.5);
		hd->pack_start (*l, true, true);
		_tipctx = name;   /* les knobs de la section qui suit héritent du contexte */
		mkToggle (*hd, _("On"), false, son, gon, "#1a2433");   // libellé clair sur barre sombre
		bar->add (*hd);
		b->pack_start (*bar, Gtk::PACK_SHRINK);
		Gtk::VBox* col = Gtk::manage (new Gtk::VBox ()); col->set_spacing (1);
		Gtk::Widget* sp = subpanel (col);
		b->pack_start (*sp, Gtk::PACK_SHRINK);
		/* clic sur le titre = replier / déplier les potards de la section (le
		 * bouton On, lui, mange ses propres clics) */
		bar->add_events (Gdk::BUTTON_PRESS_MASK);
		ArdourWidgets::set_tooltip (*bar, _("Click the name to fold this section away."));
		bar->signal_button_press_event ().connect ([sp, tri, open] (GdkEventButton* ev) -> bool {
			if (ev->type != GDK_BUTTON_PRESS || ev->button != 1) { return false; }
			*open = !sp->get_visible ();
			if (*open) { sp->show (); } else { sp->hide (); }
			tri->queue_draw ();
			return true;
		}, false);
		/* section absente des Returns (bus) : DÉTACHÉE quand un bus est sélectionné */
		if (trackOnly) { reg_track_only (b, gap); reg_track_only (b, bar); reg_track_only (b, sp); }
		return col;
	};

	/* GATE et EXPANDER : pistes seulement (un gate sur une somme de groupe n'a
	 * pas de sens) ; COMPRESSOR et LIMITER sont exposés aussi sur les bus. */
	if (!bus) {
	{ Gtk::VBox* c = section (_("GATE"), "#2e7d32", [](OxfordChannel& c,bool v){c.setGateOn(v);}, [](OxfordChannel& c){return c.gateOn();}, false);
	  flow (c, {
	    mk(-80,0,-80,_("Thr"),f_db,false,-1,[](OxfordChannel& c,double v){c.setGateThreshDb((float)v);},[](OxfordChannel& c){return (double)c.gateThreshDb();}),
	    mk(-80,0,-80,_("Rng"),f_db,false,-1,[](OxfordChannel& c,double v){c.setGateRangeDb((float)v);},[](OxfordChannel& c){return (double)c.gateRangeDb();}),
	    mk(0.005,26,0.005,_("Att"),f_ms,false,-1,[](OxfordChannel& c,double v){c.setGateAttackMs((float)v);},[](OxfordChannel& c){return (double)c.gateAttackMs();}),
	    mk(7.8,519,7.8,_("Rel"),f_ms,false,-1,[](OxfordChannel& c,double v){c.setGateReleaseMs((float)v);},[](OxfordChannel& c){return (double)c.gateReleaseMs();}) }); }

	{ Gtk::VBox* c = section (_("EXPANDER"), "#1565c0", [](OxfordChannel& c,bool v){c.setExpOn(v);}, [](OxfordChannel& c){return c.expOn();}, false);
	  flow (c, {
	    mk(-60,0,0,_("Thr"),f_db,false,-1,[](OxfordChannel& c,double v){c.setExpThreshDb((float)v);},[](OxfordChannel& c){return (double)c.expThreshDb();}),
	    mk(1,16,2,_("Rat"),f_rat,false,-1,[](OxfordChannel& c,double v){c.setExpRatio((float)v);},[](OxfordChannel& c){return (double)c.expRatio();}),
	    mk(-80,0,-40,_("Rng"),f_db,false,-1,[](OxfordChannel& c,double v){c.setExpRangeDb((float)v);},[](OxfordChannel& c){return (double)c.expRangeDb();}),
	    mk(0.26,104,5.2,_("Att"),f_ms,false,-1,[](OxfordChannel& c,double v){c.setExpAttackMs((float)v);},[](OxfordChannel& c){return (double)c.expAttackMs();}),
	    mk(7.8,519,51.9,_("Rel"),f_ms,false,-1,[](OxfordChannel& c,double v){c.setExpReleaseMs((float)v);},[](OxfordChannel& c){return (double)c.expReleaseMs();}) }); }
	}   /* fin des sections réservées aux pistes */

	{ Gtk::VBox* c = section (_("COMPRESSOR"), "#b8860b", [](OxfordChannel& c,bool v){c.setCompOn(v);}, [](OxfordChannel& c){return c.compOn();}, false);
	  /* Hold : gel de la réduction avant le release (spec OXF-R3 : 10 ms .. 30 s) */
	  TridentKnob* chold = mk(10,30000,10,_("Hold"),f_ms,false,-1,[](OxfordChannel& c,double v){c.setCompHoldMs((float)v);},[](OxfordChannel& c){return (double)c.compHoldMs();});
	  chold->set_log (true);
	  flow (c, {
	    mk(-60,0,0,_("Thr"),f_db,false,-1,[](OxfordChannel& c,double v){c.setCompThreshDb((float)v);},[](OxfordChannel& c){return (double)c.compThreshDb();}),
	    mk(0,1,0.5,_("Rat"),f_compratio,false,-1,[](OxfordChannel& c,double v){c.setCompRatioCtrl((float)v);},[](OxfordChannel& c){return (double)c.compRatioCtrl();}),
	    mk(0.519,52,5.2,_("Att"),f_ms,false,-1,[](OxfordChannel& c,double v){c.setCompAttackMs((float)v);},[](OxfordChannel& c){return (double)c.compAttackMs();}),
	    chold,
	    mk(52,3100,127,_("Rel"),f_ms,false,-1,[](OxfordChannel& c,double v){c.setCompReleaseMs((float)v);},[](OxfordChannel& c){return (double)c.compReleaseMs();}),
	    mk(0,24,0,_("Mkp"),f_db,false,-1,[](OxfordChannel& c,double v){c.setCompMakeupDb((float)v);},[](OxfordChannel& c){return (double)c.compMakeupDb();}),
	    mk(0,20,0,_("Soft"),f_db,false,-1,[](OxfordChannel& c,double v){c.setCompSoftDb((float)v);},[](OxfordChannel& c){return (double)c.compSoftDb();}) }); }

	{ Gtk::VBox* c = section (_("LIMITER"), "#c62828", [](OxfordChannel& c,bool v){c.setLimOn(v);}, [](OxfordChannel& c){return c.limOn();}, false);
	  /* Hold : spec OXF-R3 : 50 ms .. 30 s */
	  TridentKnob* lhold = mk(50,30000,50,_("Hold"),f_ms,false,-1,[](OxfordChannel& c,double v){c.setLimHoldMs((float)v);},[](OxfordChannel& c){return (double)c.limHoldMs();});
	  lhold->set_log (true);
	  flow (c, {
	    mk(-60,0,-0.3,_("Thr"),f_db,false,-1,[](OxfordChannel& c,double v){c.setLimThreshDb((float)v);},[](OxfordChannel& c){return (double)c.limThreshDb();}),
	    mk(0.1,500,0.3,_("Att"),f_ms,false,-1,[](OxfordChannel& c,double v){c.setLimAttackMs((float)v);},[](OxfordChannel& c){return (double)c.limAttackMs();}),
	    lhold,
	    mk(100,10000,150,_("Rel"),f_ms,false,-1,[](OxfordChannel& c,double v){c.setLimReleaseMs((float)v);},[](OxfordChannel& c){return (double)c.limReleaseMs();}) }); }

	/* respiration en bas : le LIMITER ne touche pas le bord inférieur */
	Gtk::Label* botpad = Gtk::manage (new Gtk::Label ()); botpad->set_size_request (1, 10);
	b->pack_start (*botpad, Gtk::PACK_SHRINK);

	/* page = bandeau fixe (graphe + VU) au-dessus de la partie qui défile */
	Gtk::VBox* page = Gtk::manage (new Gtk::VBox ());
	page->pack_start (*chrome_box (head, 10, 2, 10), Gtk::PACK_SHRINK);
	page->pack_start (*wrap_scroll (b, false, 2, 10), Gtk::PACK_EXPAND_WIDGET);
	return page;
}

Gtk::Widget*
OxfordConsolePanel::build_master ()
{
	/* Vue MASTER — le panneau EQ/DYN ne sert à rien sur le master : on y met la
	 * RÉGIE. Contenu : section MONITOR d'Ardour (réimplantée ici depuis le bord
	 * du mixer) + trims du PCM-1630 + limiteur de sortie POST PCM-1630. */
	Gtk::VBox* b = Gtk::manage (new Gtk::VBox ());
	b->set_spacing (1);

	/* mêmes helpers visuels que la vue Dynamics (barre de titre + rangées de 3) */
	auto flow = [&](Gtk::VBox* col, std::vector<TridentKnob*> ks) {
		Gtk::HBox* r=0; int n=0;
		for (auto k : ks) { if (n%3==0){ r=Gtk::manage(new Gtk::HBox()); r->set_spacing(2); col->pack_start(*r,Gtk::PACK_SHRINK);} r->pack_start(*k,true,false); ++n; }
	};
	/* en-tête de section ; toggle optionnel (son/gon nuls = pas de bouton On) */
	auto section = [&](const char* name, const char* color,
	                   std::function<void(OxfordChannel&,bool)> son,
	                   std::function<bool(OxfordChannel&)> gon) -> Gtk::VBox* {
		Gtk::Label* gap = Gtk::manage (new Gtk::Label ()); gap->set_size_request (1, 6);
		b->pack_start (*gap, Gtk::PACK_SHRINK);
		Gtk::EventBox* bar = Gtk::manage (new Gtk::EventBox ());
		Gdk::Color barbg; barbg.set_rgb_p (kSubBg[0], kSubBg[1], kSubBg[2]); bar->modify_bg (Gtk::STATE_NORMAL, barbg); _chromeSub.push_back (bar);
		bar->set_border_width (3);
		Gtk::HBox* hd = Gtk::manage (new Gtk::HBox ()); hd->set_spacing (4);
		Gtk::Label* l = Gtk::manage (new Gtk::Label ());
		char m[96]; std::snprintf (m,sizeof m,"<b><span foreground=\"%s\">%s</span></b>",color,name);   /* pas de "large" : le titre poussait le bouton On hors du bord */
		l->set_markup (m); l->set_alignment (0,0.5);
		hd->pack_start (*l, true, true);
		if (son && gon) { mkToggle (*hd, _("On"), true, son, gon, "#1a2433"); }
		bar->add (*hd);
		b->pack_start (*bar, Gtk::PACK_SHRINK);
		Gtk::VBox* col = Gtk::manage (new Gtk::VBox ()); col->set_spacing (1);
		b->pack_start (*subpanel (col), Gtk::PACK_SHRINK);
		return col;
	};

	/* ---- MONITOR : emplacement d'accueil du tearoff de MonitorSection ---- */
	{
		Gtk::VBox* c = section (_("MONITOR"), "#3a5a8c", nullptr, nullptr);
		_mon_note.set_markup (_("<i>Pas de bus Monitor dans cette session.</i>"));
		_mon_note.set_line_wrap (true);
		_mon_note.set_alignment (0.0, 0.5);
		c->pack_start (_mon_note, Gtk::PACK_SHRINK);
		_mon_create_btn.set_text (_("Créer le bus Monitor"));
		_mon_create_btn.set_fixed_colors (0xc8e030ff, 0xc8c4b8ff);
		_mon_create_btn.set_layout_font (Pango::FontDescription ("ArdourSans 9"));
		_mon_create_btn.signal_clicked.connect ([] () {
			/* déclenche Session::config_changed("use-monitor-bus") -> bus créé */
			ARDOUR::Config->set_use_monitor_bus (true);
		});
		c->pack_start (_mon_create_btn, Gtk::PACK_SHRINK);
		c->pack_start (_mon_slot, Gtk::PACK_SHRINK);
	}

	/* ---- PCM-1630 : trims autour du réseau NAM (capture non-linéaire) ----
	 * "In" recule l'attaque du modèle (désature), "Out" compense le niveau. */
	{
		_tipctx = "PCM";
		Gtk::VBox* c = section (_("PCM-1630"), "#3a5a8c", nullptr, nullptr);
		flow (c, {
		  mk(-24,24,0,_("In"),f_db,true,-1,[](OxfordChannel& ch,double v){ch.setMtInDb((float)v);},[](OxfordChannel& ch){return (double)ch.mtInDb();}, 0.35,0.48,0.66),
		  mk(-24,24,0,_("Out"),f_db,true,-1,[](OxfordChannel& ch,double v){ch.setMtOutDb((float)v);},[](OxfordChannel& ch){return (double)ch.mtOutDb();}, 0.35,0.48,0.66) });
	}

	/* ---- LIMITEUR DE SORTIE, placé APRÈS le PCM-1630 ----
	 * le modèle NAM restitue les crêtes du convertisseur du 1630 : c'est le seul
	 * point de la chaîne où on peut les rattraper. Des plugins peuvent aussi être
	 * insérés après le PCM-1630 dans la processor-box du master. */
	{
		_tipctx = "OUTLIM";
		Gtk::VBox* c = section (_("OUT LIMITER"), "#c62828",
		    [](OxfordChannel& ch,bool v){ch.setBusLimOn(v);},
		    [](OxfordChannel& ch){return ch.busLimOn();});
		TridentKnob* lrel = mk(0.05,3000,7.34,_("Rel"),f_ms,true,-1,
		     [](OxfordChannel& ch,double v){ch.setBusLimRelMs((float)v);},
		     [](OxfordChannel& ch){return (double)ch.busLimRelMs();}, 0.78,0.30,0.28);
		lrel->set_log (true);
		flow (c, {
		  mk(-12,0,-0.3,_("Thresh"),f_db,true,-1,
		     [](OxfordChannel& ch,double v){ch.setBusLimCeilDb((float)v);},
		     [](OxfordChannel& ch){return (double)ch.busLimCeilDb();}, 0.78,0.30,0.28),
		  mk(0.05,5,0.052,_("Att"),f_ms,true,-1,
		     [](OxfordChannel& ch,double v){ch.setBusLimAttMs((float)v);},
		     [](OxfordChannel& ch){return (double)ch.busLimAttMs();}, 0.78,0.30,0.28),
		  lrel,
		  mk(0,10,0,_("Knee"),f_db,true,-1,
		     [](OxfordChannel& ch,double v){ch.setBusLimKneeDb((float)v);},
		     [](OxfordChannel& ch){return (double)ch.busLimKneeDb();}, 0.78,0.30,0.28),
		  mk(0,125,0,_("Enh"),f_rat,true,-1,
		     [](OxfordChannel& ch,double v){ch.setBusLimEnhance((float)v);},
		     [](OxfordChannel& ch){return (double)ch.busLimEnhance();}, 0.78,0.30,0.28) });
		mkToggle (*c, _("Safe"), true,
		     [](OxfordChannel& ch,bool v){ch.setBusLimSafe(v);},
		     [](OxfordChannel& ch){return ch.busLimSafe();});
		mkToggle (*c, _("Auto-Comp"), true,
		     [](OxfordChannel& ch,bool v){ch.setBusLimAutoComp(v);},
		     [](OxfordChannel& ch){return ch.busLimAutoComp();});
		/* Réduction et dépassement inter-échantillon en BARGRAPHS, du même type
		 * que ceux de la section Dynamics : un chiffre écrit ne se lit pas d'un
		 * coup d'œil pendant qu'on mixe. */
		_outlim_bars.set_size_request (-1, (int)(34*kUI));
		_outlim_bars.signal_expose_event ().connect (sigc::mem_fun (*this, &OxfordConsolePanel::on_outlim_expose));
		c->pack_start (_outlim_bars, Gtk::PACK_SHRINK);
	}

	Gtk::Label* botpad = Gtk::manage (new Gtk::Label ()); botpad->set_size_request (1, 10);
	b->pack_start (*botpad, Gtk::PACK_SHRINK);
	return b;
}

void
OxfordConsolePanel::set_monitor_widget (Gtk::Widget* w)
{
	if (_mon_widget == w) { return; }
	if (_mon_widget) { _mon_slot.remove (*_mon_widget); }
	_mon_widget = w;
	if (_mon_widget) {
		_mon_slot.pack_start (*_mon_widget, Gtk::PACK_SHRINK);
		_mon_widget->show ();
		_mon_note.hide ();
		_mon_create_btn.hide ();
	} else {
		_mon_note.show ();
		_mon_create_btn.show ();
	}
}

void
OxfordConsolePanel::show_view (int page)
{
	_nb.set_current_page (page);
	_btn_eq.set_active_state     ((page==0 || page==4) ? Gtkmm2ext::ExplicitActive : Gtkmm2ext::Off);
	_btn_dyn.set_active_state    ((page==1 || page==3) ? Gtkmm2ext::ExplicitActive : Gtkmm2ext::Off);
	_btn_master.set_active_state (page==2 ? Gtkmm2ext::ExplicitActive : Gtkmm2ext::Off);
	if (page != 2) { _lastTrackPage = page; }   // page à restaurer en quittant le master
}

bool
OxfordConsolePanel::alt_down () const
{
	/* état LIVE des modificateurs (Alt encore enfoncé au moment du clic) */
	GdkModifierType mask = (GdkModifierType) 0;
	gdk_display_get_pointer (gdk_display_get_default (), 0, 0, 0, &mask);
	return (mask & GDK_MOD1_MASK) != 0;
}

void
OxfordConsolePanel::apply_linked (std::function<void(OxfordChannel&)> fn)
{
	if (_chan) { fn (*_chan); }                 // canal courant
	if (!alt_down ()) { return; }               // sans Alt : pas de diffusion
	Mixer_UI* mx = Mixer_UI::instance ();
	if (!mx) { return; }
	for (MixerStrip* s : mx->mixer_strips ()) {
		std::shared_ptr<ARDOUR::Route> r = s->route ();
		if (!r || !r->is_selected () || r->is_master ()) { continue; }
		std::shared_ptr<OxfordChannel> c2 = r->oxford_channel ();
		if (!c2 || c2 == _chan) { continue; }   // pistes ET bus sélectionnés
		fn (*c2);
	}
}

std::shared_ptr<OxfordChannel>
OxfordConsolePanel::selected_channel ()
{
	Mixer_UI* mx = Mixer_UI::instance ();
	if (!mx) { return std::shared_ptr<OxfordChannel> (); }
	for (MixerStrip* s : mx->mixer_strips ()) {
		std::shared_ptr<ARDOUR::Route> r = s->route ();
		if (r && r->is_selected () && !r->is_master ()) {
			/* pistes (Strip) ET bus (Bus = canal RETURN OXF-R3, panel réduit) */
			std::shared_ptr<OxfordChannel> tc = r->oxford_channel ();
			if (tc) { return tc; }
		}
	}
	return std::shared_ptr<OxfordChannel> ();
}

std::shared_ptr<OxfordChannel>
OxfordConsolePanel::master_channel ()
{
	Mixer_UI* mx = Mixer_UI::instance ();
	if (!mx) { return std::shared_ptr<OxfordChannel> (); }
	for (MixerStrip* s : mx->mixer_strips ()) {
		std::shared_ptr<ARDOUR::Route> r = s->route ();
		if (r && r->is_master ()) {
			/* les seuls params "master" du panel = trims In/Out du PCM-1630,
			 * portés par le processor stage Dac (oxford_dac), pas le Front. */
			if (r->oxford_dac ()) { return r->oxford_dac (); }
			return r->oxford_channel ();
		}
	}
	return std::shared_ptr<OxfordChannel> ();
}

void
OxfordConsolePanel::refresh ()
{
	_chan   = selected_channel ();
	_master = master_channel ();

	/* MASTER sélectionné -> le panneau bascule sur la vue régie (monitor +
	 * PCM-1630 + limiteur de sortie) ; retour piste -> on restaure la vue. */
	bool msel = false;
	if (Mixer_UI* mx = Mixer_UI::instance ()) {
		for (MixerStrip* s : mx->mixer_strips ()) {
			std::shared_ptr<ARDOUR::Route> r = s->route ();
			if (r && r->is_master () && r->is_selected ()) { msel = true; break; }
		}
	}
	if (msel != _masterSelected) {
		_masterSelected = msel;
		/* le bouton MASTER n'existe QUE sur la tranche master (la régie n'a rien
		 * à faire depuis une piste), et EQ/DYN n'ont rien à piloter sur elle */
		if (msel) { _btn_eq.hide (); _btn_dyn.hide (); _btn_master.show (); if (_panwid_box) _panwid_box->hide (); }
		else      { _btn_master.hide (); _btn_eq.show (); _btn_dyn.show (); if (_panwid_box) _panwid_box->show (); }
		show_view (msel ? 2 : _lastTrackPage);
	}

	/* mode RETURN : un bus n'expose que LF/MF/HF + Gate/Comp (spec OXF-R3) */
	const bool isBus = _chan && _chan->kind () == OxfordChannel::Bus;
	if (isBus != _isBus) {
		_isBus = isBus;
		set_bus_mode (_isBus);
		/* bascule vers la page dynamique correspondante si on y est */
		const int cur = _nb.get_current_page ();
		if (cur == 1 || cur == 3) { show_view (_isBus ? 3 : 1); }
		else if (cur == 0 || cur == 4) { show_view (_isBus ? 4 : 0); }
		/* ceinture : force le relayout complet du notebook et du châssis
		 * (le viewport gardait parfois l'ancienne allocation) */
		_nb.queue_resize ();
		_box.queue_resize ();
	}

	if (_chan) {
		std::shared_ptr<ARDOUR::Route> r;
		for (MixerStrip* s : Mixer_UI::instance ()->mixer_strips ()) {
			if (s->route () && s->route ()->oxford_channel () == _chan) { r = s->route (); break; }
		}
		_route = r;
		_sel_label.set_markup (std::string ("<b>") + (r ? r->name () : std::string ("?")) + "</b>");
	} else {
		_route.reset ();
		_sel_label.set_markup (_("<b>(aucune piste Oxford)</b>"));
	}

	_updating = true;
	for (Param& p : _params) {
		std::shared_ptr<OxfordChannel> c = p.master ? _master : _chan;
		if (c) { p.knob->set_value (p.get (*c)); p.knob->set_sensitive (true); }
		else   { p.knob->set_sensitive (false); }
	}
	for (Toggle& t : _toggles) {
		std::shared_ptr<OxfordChannel> c = t.master ? _master : _chan;
		if (c) { t.btn->set_active_state (t.get (*c) ? Gtkmm2ext::ExplicitActive : Gtkmm2ext::Off); t.btn->set_sensitive (true); }
		else   { t.btn->set_sensitive (false); }
	}
	if (_chan) {
		_curve_btn.set_text (curve_name (_chan->curveType ()));
		_curve_btnB.set_text (curve_name (_chan->curveType ()));
		_timing_btn.set_text (timing_name (_chan->timingLaw ()));
		char sb[16];
		if (_chan->hpfOn ()) { std::snprintf (sb,sizeof sb,"%.0f",_chan->hpfSlope ()); _hp_slope_btn.set_text (sb); _hp_slope_btn.set_active_state (Gtkmm2ext::ExplicitActive); } else { _hp_slope_btn.set_text (_("off")); _hp_slope_btn.set_active_state (Gtkmm2ext::Off); }
		if (_chan->lpfOn ()) { std::snprintf (sb,sizeof sb,"%.0f",_chan->lpfSlope ()); _lp_slope_btn.set_text (sb); _lp_slope_btn.set_active_state (Gtkmm2ext::ExplicitActive); } else { _lp_slope_btn.set_text (_("off")); _lp_slope_btn.set_active_state (Gtkmm2ext::Off); }
	}
	/* PAN : route panner. WIDTH : seulement pour les pistes STÉRÉO. */
	if (_pan_knob) {
		if (_route && _route->pan_azimuth_control ()) {
			_pan_knob->set_value (_route->pan_azimuth_control ()->get_value ());
			_pan_knob->set_sensitive (true);
		} else {
			_pan_knob->set_sensitive (false);
		}
	}
	if (_width_knob) {
		const bool stereo = _route
			&& _route->n_inputs ().n_audio () >= 2
			&& _route->n_outputs ().n_audio () >= 2;
		if (stereo && _chan) {
			_width_knob->set_value ((double) _chan->width () - 1.0);   // 0..2 -> -1..1
			_width_knob->set_sensitive (true);
			_width_knob->show ();
		} else {
			_width_knob->set_sensitive (false);
			_width_knob->hide ();   // masqué en mono
		}
	}

	_grVals[0] = _chan ? _chan->gateGrDb ()        : 0.f;
	_grVals[1] = _chan ? _chan->expGrDb ()         : 0.f;
	_grVals[2] = _chan ? _chan->gainReductionDb () : 0.f;
	_grVals[3] = _chan ? _chan->dynLimGrDb ()      : 0.f;
	/* peak-hold : tient la crête, redescend ~4 dB/s (tick 150 ms) */
	for (int i = 0; i < 4; ++i) {
		_grPeak[i] = std::max (_grVals[i], _grPeak[i] - 0.6f);
	}
	_dyn_meters.queue_draw ();
	_dyn_metersB.queue_draw ();
	/* limiteur de SORTIE : réduction et dépassement inter-échantillon (bargraphs) */
	_outlimGr    = _master ? _master->busLimGrDb ()    : 0.f;
	_outlimTp    = _master ? _master->busLimTruePeakDb () : -120.f;
	_outlimCeil  = _master ? _master->busLimCeilDb () : -0.3f;
	_outlim_bars.queue_draw ();
	_updating = false;
	eqc ().queue_draw ();
	_dyn_curve.queue_draw ();
	_dyn_curveB.queue_draw ();
}

bool
OxfordConsolePanel::on_timer () { refresh (); return true; }

/* Enregistre un widget "piste seulement" (absent des Returns/bus) avec sa
 * position dans son conteneur, pour pouvoir le détacher/rattacher au bon
 * endroit. hide() ne suffisait pas : l'allocation restait dans le viewport. */
void
OxfordConsolePanel::reg_track_only (Gtk::Box* parent, Gtk::Widget* w)
{
	int pos = 0;
	std::vector<Gtk::Widget*> kids = parent->get_children ();
	for (size_t i = 0; i < kids.size (); ++i) { if (kids[i] == w) { pos = (int) i; break; } }
	_trackOnly.push_back (TrackOnlyW { parent, w, pos, true });
}

void
OxfordConsolePanel::set_bus_mode (bool bus)
{
	for (TrackOnlyW& t : _trackOnly) {
		if (bus && t.attached) {
			t.w->reference ();          // le conteneur détenait la seule ref (Gtk::manage)
			t.parent->remove (*t.w);
			t.attached = false;
		} else if (!bus && !t.attached) {
			t.parent->pack_start (*t.w, Gtk::PACK_SHRINK);
			t.parent->reorder_child (*t.w, t.pos);   // _trackOnly est en ordre de build -> positions restaurées
			t.w->show ();
			t.w->unreference ();        // le conteneur a repris sa ref
			t.attached = true;
		}
	}
}

/* Tôle brossée + marges autour d'un contenu : la page couvre le châssis, elle
 * doit porter la même texture, sinon le fond du panneau est masqué par un aplat.
 * Marges hautes/basses réglables : la vue dynamique empile deux de ces boîtes
 * (le graphe fixe, puis la partie défilante) et le raccord doit rester serré. */
Gtk::Widget*
OxfordConsolePanel::chrome_box (Gtk::Widget* w, int top, int bottom, int side)
{
	Gtk::EventBox* eb = Gtk::manage (new Gtk::EventBox ());
	Gdk::Color c; c.set_rgb_p (kPanelBg[0], kPanelBg[1], kPanelBg[2]);
	eb->modify_bg (Gtk::STATE_NORMAL, c);
	eb->set_app_paintable (true);
	eb->signal_expose_event ().connect ([eb] (GdkEventExpose*) -> bool {
		Glib::RefPtr<Gdk::Window> win = eb->get_window ();
		if (!win) { return false; }
		Cairo::RefPtr<Cairo::Context> cr = win->create_cairo_context ();
		Gtk::Allocation a = eb->get_allocation ();
		trident_fill_panel (cr, 0, 0, a.get_width (), a.get_height (),
		                    kPanelBg[0], kPanelBg[1], kPanelBg[2]);
		return false;
	}, false);
	/* respiration : la première rangée de potards touchait le bord supérieur
	 * du châssis (et son joint) */
	Gtk::Alignment* al = Gtk::manage (new Gtk::Alignment (0.0, 0.0, 1.0, 1.0));
	al->set_padding (top, bottom, side, side);
	al->add (*w);
	eb->add (*al);
	_chromeMain.push_back (eb);
	return eb;
}

Gtk::Widget*
OxfordConsolePanel::wrap_scroll (Gtk::Widget* w, bool hscroll, int top, int bottom)
{
	Gdk::Color c; c.set_rgb_p (kPanelBg[0], kPanelBg[1], kPanelBg[2]);
	Gtk::Widget* eb = chrome_box (w, top, bottom, 10);
	Gtk::ScrolledWindow* sw = Gtk::manage (new Gtk::ScrolledWindow ());
	/* la régie d'Ardour est plus large que le panneau : sans défilement
	 * horizontal, ses commandes (le On du limiteur par ex.) sont COUPÉES. */
	sw->set_policy (hscroll ? Gtk::POLICY_AUTOMATIC : Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
	sw->set_shadow_type (Gtk::SHADOW_NONE);
	sw->add (*eb);
	sw->modify_bg (Gtk::STATE_NORMAL, c);
	_chromeMain.push_back (sw);
	if (Gtk::Widget* vp = sw->get_child ()) { vp->modify_bg (Gtk::STATE_NORMAL, c); _chromeMain.push_back (vp); }  // Viewport
	return sw;
}

/* Châssis du panneau : tôle d'aluminium brossé, boulonnée en périphérie.
 * L'aplat de couleur ne lisait pas comme du métal — il manquait le reflet
 * diffus, les rayures de brossage et surtout le joint + la visserie. */
bool
OxfordConsolePanel::on_panel_expose (GdkEventExpose*)
{
	Glib::RefPtr<Gdk::Window> win = get_window ();
	if (!win) { return false; }
	Cairo::RefPtr<Cairo::Context> cr = win->create_cairo_context ();
	Gtk::Allocation a = get_allocation ();
	const double w = a.get_width (), h = a.get_height ();
	trident_fill_panel (cr, 0, 0, w, h, kPanelBg[0], kPanelBg[1], kPanelBg[2]);
	trident_plate_edge (cr, 0, 0, w, h, true, 2.0);
	return false;   // laisse GTK propager l'expose aux enfants
}

/* Tuile BLEU CLAIR (sous-panneau) sur le châssis crème -> bi-ton façon console.
 * Frame biseauté (relief) + EventBox bleu. */
Gtk::Widget*
OxfordConsolePanel::subpanel (Gtk::Widget* w)
{
	Gtk::EventBox* eb = Gtk::manage (new Gtk::EventBox ());
	Gdk::Color c; c.set_rgb_p (kSubBg[0], kSubBg[1], kSubBg[2]);
	eb->modify_bg (Gtk::STATE_NORMAL, c);
	/* sous-panneau = plaque rapportée, brossée elle aussi, avec son joint */
	eb->set_app_paintable (true);
	eb->signal_expose_event ().connect ([eb] (GdkEventExpose*) -> bool {
		Glib::RefPtr<Gdk::Window> win = eb->get_window ();
		if (!win) { return false; }
		Cairo::RefPtr<Cairo::Context> cr = win->create_cairo_context ();
		Gtk::Allocation a = eb->get_allocation ();
		trident_fill_panel (cr, 0, 0, a.get_width (), a.get_height (),
		                    kSubBg[0], kSubBg[1], kSubBg[2]);
		trident_plate_edge (cr, 0, 0, a.get_width (), a.get_height (), true, 1.0);
		return false;
	}, false);
	eb->set_border_width (3);
	_chromeSub.push_back (eb);
	eb->add (*w);
	Gtk::Frame* fr = Gtk::manage (new Gtk::Frame ());
	fr->set_shadow_type (Gtk::SHADOW_ETCHED_OUT);   // léger relief de sous-panneau
	fr->add (*eb);
	return fr;
}

/* Courbe de réponse EQ ORIGINALE (approx analytique bell/shelf + pentes HP/LP). */
bool
OxfordConsolePanel::on_eq_expose (GdkEventExpose*)
{
	Glib::RefPtr<Gdk::Window> win = eqc ().get_window ();
	if (!win) { return false; }
	Cairo::RefPtr<Cairo::Context> cr = win->create_cairo_context ();
	Gtk::Allocation a = eqc ().get_allocation ();
	const double w = a.get_width (), h = a.get_height ();
	screen_begin (cr, w, h);
	const double fmin = 20.0, fmax = 20000.0;
	auto xf = [&](double f){ return w * (std::log10 (f/fmin) / std::log10 (fmax/fmin)); };
	/* réponse d'abord (l'échelle verticale en dépend), grille ensuite */
	const int N=(int)w;
	std::vector<double> resp ((size_t)(N+1), 0.0);
	for (int i=0;i<=N;++i){
		double f = fmin*std::pow(fmax/fmin,(double)i/(N>0?N:1)); double db=0.0;
		/* VRAIE réponse du filtre (coefficients réels : Orfanidis, lois de Q
		 * des Types, shelf+overshoot, Butterworth HP/LP) — plus d'approximation
		 * de cloche : ce que l'écran montre = ce que l'audio fait. Les bandes
		 * LMF/HMF et HP/LP en mode bus (RETURN) sont déjà désactivées côté DSP. */
		if (_chan){ db = _chan->eqCurveDb (f); }
		if (db>40)db=40; if (db<-40)db=-40;
		resp[(size_t)i]=db;
	}
	/* échelle verticale AUTO (réf. plugin : la vitre contient toujours la courbe) */
	double mxdb = 0.0; for (double d : resp) { const double ad = std::fabs (d); if (ad > mxdb) mxdb = ad; }
	double S = 10.0; if (mxdb > 9.5) S = 20.0; if (mxdb > 19.5) S = 30.0; if (mxdb > 29.5) S = 40.0;
	_eq_scale = S;
	auto yf = [&](double db){ return h * (0.5 - db/(2.0*S)); };
	/* grille : mineures discrètes, décades + labels */
	cr->set_line_width (1.0);
	const double gf[] = {50,100,200,500,1000,2000,5000,10000};
	for (double f : gf) {
		const bool major = (f==100 || f==1000 || f==10000);
		cr->set_source_rgba (0.30,0.44,0.72, major ? 0.55 : 0.22);
		double x=xf(f); cr->move_to(x,0); cr->line_to(x,h); cr->stroke ();
	}
	const int gstep = (S <= 10.0) ? 5 : 10;
	for (int db=-(int)S+gstep; db<=(int)S-gstep; db+=gstep) {
		cr->set_source_rgba (0.30,0.44,0.72, db==0 ? 0.0 : 0.22);
		double y=yf(db); cr->move_to(0,y); cr->line_to(w,y); cr->stroke ();
	}
	cr->set_source_rgba (1,1,1,0.28); cr->move_to(0,yf(0)); cr->line_to(w,yf(0)); cr->stroke ();
	/* labels d'axes (petits, dans la vitre) */
	cr->select_font_face ("ArdourSans", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_NORMAL);
	cr->set_font_size (8.5);
	cr->set_source_rgba (0.62,0.72,0.92,0.65);
	const struct { double f; const char* t; } fl[] = { {100,"100"}, {1000,"1k"}, {10000,"10k"} };
	for (auto& L : fl) { cr->move_to (xf(L.f)+3, h-11); cr->show_text (L.t); }
	{
		const int lab = (S <= 10.0) ? 5 : ((S <= 20.0) ? 10 : 20);
		char lt[8];
		std::snprintf (lt, sizeof lt, "+%d", lab); cr->move_to (9, yf(lab)+3);  cr->show_text (lt);
		std::snprintf (lt, sizeof lt, "-%d", lab); cr->move_to (9, yf(-lab)+3); cr->show_text (lt);
	}
	/* remplissage entre la courbe et 0 dB (dégradé orange translucide) */
	cr->move_to (0, yf(resp[0]));
	for (int i=1;i<=N;++i) cr->line_to ((double)i, yf(resp[(size_t)i]));
	cr->line_to (w, yf(0)); cr->line_to (0, yf(0)); cr->close_path ();
	{
		Cairo::RefPtr<Cairo::LinearGradient> fg = Cairo::LinearGradient::create (0, 0, 0, h);
		fg->add_color_stop_rgba (0.0, 0.941,0.659,0.188, 0.30);
		fg->add_color_stop_rgba (0.5, 0.941,0.659,0.188, 0.10);
		fg->add_color_stop_rgba (1.0, 0.941,0.659,0.188, 0.30);
		cr->set_source (fg); cr->fill ();
	}
	/* glow puis trait net */
	cr->move_to (0, yf(resp[0]));
	for (int i=1;i<=N;++i) cr->line_to ((double)i, yf(resp[(size_t)i]));
	cr->set_source_rgba (0.941,0.659,0.188,0.20); cr->set_line_width (5.5); cr->stroke_preserve ();
	cr->set_source_rgb (0.980,0.760,0.300); cr->set_line_width (2.0); cr->stroke ();
	/* pastilles de bande = HANDLES : point coloré posé au gain PROPRE de la bande
	 * (et non sur la courbe sommée — sinon engager un HPF fait dégringoler la
	 * pastille LF avec la courbe), à la fréquence de la bande ; position
	 * mémorisée pour le hit-test (drag/molette). */
	if (_chan) {
		OxfordChannel& c=*_chan;
		for (int bnd=0;bnd<5;bnd++){
			if (_isBus && (bnd==1 || bnd==3)) { _eq_dotv[bnd]=false; continue; }   // RETURN
			if (!c.bandEnabled(bnd)) { _eq_dotv[bnd]=false; continue; }
			const double x = xf ((double) c.bandFreq(bnd));
			const double y = yf ((double) c.bandGain(bnd));
			_eq_dotx[bnd]=x; _eq_doty[bnd]=y; _eq_dotv[bnd]=true;
			const bool drag = (bnd==_eq_drag);
			if (drag) {   // halo de saisie
				cr->arc (x, y, 8.5, 0, 2*M_PI);
				cr->set_source_rgba (bandRGB[bnd][0],bandRGB[bnd][1],bandRGB[bnd][2],0.30); cr->fill ();
			}
			cr->arc (x, y, drag ? 5.5 : 4.0, 0, 2*M_PI);
			cr->set_source_rgb (bandRGB[bnd][0],bandRGB[bnd][1],bandRGB[bnd][2]); cr->fill_preserve ();
			cr->set_source_rgba (1,1,1,0.85); cr->set_line_width (1.2); cr->stroke ();
		}
		/* handles HP/LP (crème) : posés au coude de LEUR filtre (-3 dB quand
		 * engagé, 0 dB sinon) et non sur la courbe sommée ;
		 * translucides quand le filtre est OFF (le drag l'engage) */
		const bool   fon[2] = { c.hpfOn (), c.lpfOn () };
		const double ffq[2] = { (double) c.hpfHz (), (double) c.lpfHz () };
		if (_isBus) { _eq_dotv[5] = _eq_dotv[6] = false; }   // RETURN : pas de HP/LP
		for (int k = 0; k < 2 && !_isBus; ++k) {
			const int idx = 5 + k;
			const double x = xf (ffq[k]);
			const double y = yf (fon[k] ? -3.0 : 0.0);
			_eq_dotx[idx]=x; _eq_doty[idx]=y; _eq_dotv[idx]=true;
			const bool drag = (idx==_eq_drag);
			if (drag) {
				cr->arc (x, y, 8.5, 0, 2*M_PI);
				cr->set_source_rgba (kFilterCap[0],kFilterCap[1],kFilterCap[2],0.30); cr->fill ();
			}
			cr->arc (x, y, drag ? 5.5 : 4.0, 0, 2*M_PI);
			cr->set_source_rgba (kFilterCap[0],kFilterCap[1],kFilterCap[2], fon[k] ? 1.0 : 0.35);
			cr->fill_preserve ();
			cr->set_source_rgba (1,1,1, fon[k] ? 0.85 : 0.35); cr->set_line_width (1.2); cr->stroke ();
		}
		/* readout pendant le drag : bande (freq/gain/Q) ou filtre (freq/pente) */
		if (_eq_drag >= 0) {
			const int b2 = _eq_drag;
			char t[48]; double rr=1, gg=1, bb=1; bool show = true;
			if (b2 < 5 && c.bandEnabled(b2)) {
				static const char* bn[5] = { "LF","LMF","MF","HMF","HF" };
				std::snprintf (t, sizeof t, "%s  %s Hz  %+.1f dB  Q %.2f", bn[b2],
				               f_hz ((double) c.bandFreq(b2)).c_str (),
				               (double) c.bandGain(b2), (double) c.bandQ(b2));
				rr=bandRGB[b2][0]*0.7+0.3; gg=bandRGB[b2][1]*0.7+0.3; bb=bandRGB[b2][2]*0.7+0.3;
			} else if (b2 == 5) {
				std::snprintf (t, sizeof t, "HP  %s Hz  %.0f dB/oct", f_hz ((double) c.hpfHz()).c_str (), (double) c.hpfSlope());
				rr=kFilterCap[0]; gg=kFilterCap[1]; bb=kFilterCap[2];
			} else if (b2 == 6) {
				std::snprintf (t, sizeof t, "LP  %s Hz  %.0f dB/oct", f_hz ((double) c.lpfHz()).c_str (), (double) c.lpfSlope());
				rr=kFilterCap[0]; gg=kFilterCap[1]; bb=kFilterCap[2];
			} else { show = false; }
			if (show) {
				cr->select_font_face ("monospace", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_BOLD);
				cr->set_font_size (10.0);
				cr->set_source_rgb (rr, gg, bb);
				cr->move_to (11, 18); cr->show_text (t);
			}
		}
	} else {
		for (int bnd=0;bnd<7;bnd++) { _eq_dotv[bnd]=false; }
	}
	screen_glass (cr, w, h);
	return true;
}

/* ---- interaction pastilles EQ (handles façon Sonnox) ---- */
int
OxfordConsolePanel::eq_dot_at (double x, double y) const
{
	int best = -1; double bd = 81.0;   // rayon de saisie 9 px
	for (int b = 0; b < 7; ++b) {
		if (!_eq_dotv[b]) { continue; }
		const double dx = x - _eq_dotx[b], dy = y - _eq_doty[b];
		const double d2 = dx*dx + dy*dy;
		if (d2 <= bd) { bd = d2; best = b; }
	}
	return best;
}

bool
OxfordConsolePanel::on_eq_press (GdkEventButton* ev)
{
	if (!_chan || ev->button != 1) { return false; }
	const int b = eq_dot_at (ev->x, ev->y);
	if (b >= 0) {
		_eq_drag = b;
		if (b == 5 || b == 6) {
			/* origine du geste vertical = pente du filtre au moment du clic */
			const bool on = (b == 5) ? _chan->hpfOn () : _chan->lpfOn ();
			_eq_drag_y0    = ev->y;
			_eq_drag_slope0 = on ? (int) ((b == 5) ? _chan->hpfSlope () : _chan->lpfSlope ()) : 12;
		}
		eqc ().queue_draw ();
		return true;
	}
	return false;
}

bool
OxfordConsolePanel::on_eq_release (GdkEventButton*)
{
	if (_eq_drag >= 0) { _eq_drag = -1; eqc ().queue_draw (); return true; }
	return false;
}

bool
OxfordConsolePanel::on_eq_motion (GdkEventMotion* ev)
{
	if (!_chan) { return false; }
	if (_eq_drag < 0) {
		/* curseur main au survol d'une pastille */
		if (Glib::RefPtr<Gdk::Window> win = eqc ().get_window ()) {
			if (eq_dot_at (ev->x, ev->y) >= 0) { Gdk::Cursor cur (Gdk::HAND2); win->set_cursor (cur); }
			else                               { win->set_cursor (); }
		}
		return false;
	}
	const int b = _eq_drag;
	Gtk::Allocation a = eqc ().get_allocation ();
	const double w = a.get_width (), h = a.get_height ();
	/* x -> fréquence (échelle log de l'écran) */
	const double fmin = 20.0, fmax = 20000.0;
	double f = fmin * std::pow (fmax / fmin, w > 0 ? ev->x / w : 0.0);
	if (b == 5 || b == 6) {
		/* HP/LP : X = fréquence (bornée comme le knob) et ENGAGE le filtre,
		 * Y = PENTE — un cran de 6 dB/oct tous les 22 px, vers le HAUT = plus
		 * raide (la pastille, elle, reste posée sur le coude du filtre). Pas plus
		 * sensible que ça : sinon un tremblement pendant le réglage de fréquence
		 * fait sauter la pente. */
		const double flo = (b == 5) ? 20.0 : 1000.0;
		const double fhi = (b == 5) ? 400.0 : 20000.0;
		if (f < flo) f = flo;
		if (f > fhi) f = fhi;
		int s = _eq_drag_slope0 + 6 * (int) std::lround ((_eq_drag_y0 - ev->y) / (22.0 * kUI));
		if (s < 6)  s = 6;
		if (s > 36) s = 36;
		const float slope = (float) s;
		if (b == 5) { apply_linked ([f, slope] (OxfordChannel& c) { c.setHPF (true, (float) f, slope); }); }   // Alt -> sélection
		else        { apply_linked ([f, slope] (OxfordChannel& c) { c.setLPF (true, (float) f, slope); }); }
		eqc ().queue_draw ();
		return true;
	}
	/* bandes : freq bornée aux plages OXF-R3 de la bande */
	if (f < oxFmin[b]) f = oxFmin[b];
	if (f > oxFmax[b]) f = oxFmax[b];
	/* y -> gain (échelle verticale AUTO de l'écran, cf. _eq_scale) */
	double g = (0.5 - (h > 0 ? ev->y / h : 0.0)) * (2.0 * _eq_scale);
	if (g < -20.0) g = -20.0;
	if (g >  20.0) g =  20.0;
	apply_linked ([b, f, g] (OxfordChannel& c) { c.setBandFreq (b, (float) f); c.setBandGain (b, (float) g); });   // Alt -> sélection
	eqc ().queue_draw ();
	return true;
}

bool
OxfordConsolePanel::on_eq_scroll (GdkEventScroll* ev)
{
	if (!_chan) { return false; }
	const int b = (_eq_drag >= 0) ? _eq_drag : eq_dot_at (ev->x, ev->y);
	if (b < 0) { return false; }
	const bool up = (ev->direction == GDK_SCROLL_UP);
	if (!up && ev->direction != GDK_SCROLL_DOWN) { return false; }
	/* HP/LP : la molette pilote la PENTE (off->6->...->36 ; sous 6 = off), comme le bouton */
	if (b == 5 || b == 6) {
		const bool  on = (b == 5) ? _chan->hpfOn () : _chan->lpfOn ();
		int s = on ? (int) ((b == 5) ? _chan->hpfSlope () : _chan->lpfSlope ()) : 0;
		s += up ? 6 : -6;
		if (s > 36) s = 36;
		const bool  non   = (s >= 6);
		const float slope = non ? (float) s : 12.f;
		if (b == 5) { apply_linked ([non, slope] (OxfordChannel& c) { c.setHPF (non, c.hpfHz (), slope); }); }   // Alt -> sélection
		else        { apply_linked ([non, slope] (OxfordChannel& c) { c.setLPF (non, c.lpfHz (), slope); }); }
		eqc ().queue_draw ();
		return true;
	}
	const double step = up ? 1.12 : 1.0 / 1.12;
	double q = (double) _chan->bandQ (b) * step;
	if (q < 0.5)  q = 0.5;
	if (q > 16.0) q = 16.0;
	/* en mode shelf LF/HF, le geste Q pilote l'OVERSHOOT (même mapping que le knob) */
	const float o = (float) ((q - 0.5) / 15.5 * 0.5);
	apply_linked ([b, q, o] (OxfordChannel& c) {   // Alt -> sélection
		c.setBandQ (b, (float) q);
		if      (b == 0 && c.lfShelf ()) { c.setLFOvershoot (o); }
		else if (b == 4 && c.hfShelf ()) { c.setHFOvershoot (o); }
	});
	eqc ().queue_draw ();
	return true;
}

/* Graphe Dynamics : courbe de transfert IN/OUT des QUATRE sections (Gate,
 * Expander, Compresseur, Limiteur) reconstituée avec les MÊMES lois que le DSP,
 * + un POINT MOBILE posé sur la courbe à la position du signal (niveau du
 * détecteur -> sortie correspondante). Échelle -80..0 dB (le Gate descend à -80). */
/* Les deux pages dynamiques (piste et bus) ont chacune leur graphe et leurs VU :
 * un widget ne peut pas être empaqueté deux fois. Le dessin est commun, seul le
 * widget cible change. */
bool
OxfordConsolePanel::on_dyn_exposeB (GdkEventExpose* e)
{
	return dyn_curve_draw (_dyn_curveB);
}

bool
OxfordConsolePanel::on_dyn_metersB (GdkEventExpose* e)
{
	return dyn_meters_draw (_dyn_metersB);
}

bool
OxfordConsolePanel::on_dyn_expose (GdkEventExpose*)
{
	return dyn_curve_draw (_dyn_curve);
}

bool
OxfordConsolePanel::dyn_curve_draw (Gtk::DrawingArea& area)
{
	Glib::RefPtr<Gdk::Window> win = area.get_window ();
	if (!win) { return false; }
	Cairo::RefPtr<Cairo::Context> cr = win->create_cairo_context ();
	Gtk::Allocation a = area.get_allocation ();
	const double w = a.get_width (), h = a.get_height ();
	screen_begin (cr, w, h);
	const double LO = -80.0;                                   // bas d'échelle des 2 axes
	auto X=[&](double din){ return w*(din-LO)/(-LO); };
	auto Y=[&](double dout){ return h*(1.0-(dout-LO)/(-LO)); };
	cr->set_line_width (1.0); cr->set_source_rgba (0.30,0.44,0.72,0.30);
	for (int d=-70; d<=0; d+=10){ double x=X(d),y=Y(d); cr->move_to(x,0);cr->line_to(x,h); cr->move_to(0,y);cr->line_to(w,y);} cr->stroke();
	/* labels d'axes */
	cr->select_font_face ("ArdourSans", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_NORMAL);
	cr->set_font_size (8.5);
	cr->set_source_rgba (0.62,0.72,0.92,0.65);
	cr->move_to (X(-60)+2, h-10);  cr->show_text ("-60");
	cr->move_to (X(-40)+2, h-10);  cr->show_text ("-40");
	cr->move_to (X(-20)+2, h-10);  cr->show_text ("-20");
	cr->move_to (8, Y(-20)-3);    cr->show_text ("-20");
	cr->move_to (8, Y(-40)-3);    cr->show_text ("-40");
	cr->move_to (8, Y(-60)-3);    cr->show_text ("-60");
	/* diagonale 1:1 de référence */
	cr->set_source_rgba (1,1,1,0.22); cr->move_to(X(LO),Y(LO)); cr->line_to(X(0),Y(0)); cr->stroke();

	auto ratio_of=[](double c){ if(c<=0.5)return 1.0+(c/0.5); if(c<=0.75)return 2.0+((c-0.5)/0.25)*2.0; double t=(c-0.75)/0.25; return 4.0/std::max(1e-3,1.0-t); };

	const bool gOn = _chan && _chan->gateOn ();
	const bool eOn = _chan && _chan->expOn ();
	const bool cOn = _chan && _chan->compOn ();
	const bool lOn = _chan && _chan->limOn ();

	if (_chan && (gOn || eOn || cOn || lOn)) {

		/* mêmes formules que OxfordDynamics::processSample (régime établi :
		 * enveloppes au repos, hystérésis du gate ignorée) */
		OxfordChannel& c = *_chan;
		const double gThr = c.gateThreshDb (), gRng = -std::fabs (c.gateRangeDb ());
		const double eThr = c.expThreshDb (),  eRat = c.expRatio (), eRng = -std::fabs (c.expRangeDb ());
		const double cThr = c.compThreshDb (), cR = ratio_of (c.compRatioCtrl ());
		const double cMk  = c.compMakeupDb (), cKnee = (c.compSoftDb () > 0.0 ? c.compSoftDb () : 3.0);
		const double lThr = c.limThreshDb ();

		auto transfer = [&](double din) {
			double gg = 0.0, ge = 0.0, gc = 0.0, gl = 0.0;
			if (gOn && din < gThr)  { gg = gRng; }
			if (eOn && din < eThr)  { ge = std::max (eRng, (din - eThr) * (eRat - 1.0)); }
			if (cOn) {
				const double over = din - cThr, slope = 1.0 - 1.0 / cR;
				double red;
				if      (over <= -cKnee * 0.5) { red = 0.0; }
				else if (over >=  cKnee * 0.5) { red = over * slope; }
				else { const double t = over + cKnee * 0.5; red = (t * t / (2.0 * cKnee)) * slope; }
				gc = -red;
			}
			const double mk = cOn ? cMk : 0.0;
			if (lOn) { const double o = din + gg + ge + gc + mk - lThr; gl = o > 0.0 ? -o : 0.0; }
			return din + gg + ge + gc + mk + gl;
		};

		const int N=(int)w;
		std::vector<double> tr ((size_t)(N+1), 0.0);
		for (int i=0;i<=N;++i){
			double dout = transfer (LO + (-LO) * i / (w > 0 ? w : 1));
			if (dout < LO - 20.0) { dout = LO - 20.0; }   // le gate plonge : on laisse sortir du cadre
			if (dout > 0.0) { dout = 0.0; }
			tr[(size_t)i]=dout;
		}
		/* aire entre la diagonale 1:1 et la courbe (réduction/expansion) */
		cr->move_to (0, Y(tr[0]));
		for (int i=1;i<=N;++i) cr->line_to ((double)i, Y(tr[(size_t)i]));
		cr->line_to (X(0), Y(0)); cr->line_to (X(LO), Y(LO)); cr->close_path ();
		cr->set_source_rgba (0.941,0.659,0.188,0.12); cr->fill ();

		/* seuils : un pointillé vertical par section active, à sa couleur */
		{
			std::vector<double> dash (1, 3.0);
			const struct { bool on; double thr; double r,g,b; const char* t; } TH[4] = {
				{ gOn, gThr, 0.18,0.49,0.20, "G" },   // #2e7d32
				{ eOn, eThr, 0.08,0.40,0.75, "E" },   // #1565c0
				{ cOn, cThr, 0.72,0.53,0.04, "C" },   // #b8860b
				{ lOn, lThr, 0.78,0.16,0.16, "L" } }; // #c62828
			cr->set_line_width (1.0);
			cr->select_font_face ("ArdourSans", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_BOLD);
			cr->set_font_size (9.0);
			for (int i = 0; i < 4; ++i) {
				if (!TH[i].on) { continue; }
				const double x = X (std::max (LO, TH[i].thr));
				cr->set_dash (dash, 0.0);
				cr->set_source_rgba (TH[i].r, TH[i].g, TH[i].b, 0.75);
				cr->move_to (x, 0); cr->line_to (x, h); cr->stroke ();
				cr->unset_dash ();
				cr->move_to (x + 2, 10 + i * 10); cr->show_text (TH[i].t);
			}
		}

		/* courbe : glow + trait */
		cr->move_to (0, Y(tr[0]));
		for (int i=1;i<=N;++i) cr->line_to ((double)i, Y(tr[(size_t)i]));
		cr->set_source_rgba (0.941,0.659,0.188,0.20); cr->set_line_width (5.5); cr->stroke_preserve ();
		cr->set_source_rgb (0.980,0.760,0.300); cr->set_line_width (2.0); cr->stroke ();

		/* POINT MOBILE : où se situe le signal sur la courbe */
		const double lvl = (double) _chan->dynInputDb ();
		if (lvl > LO - 0.5) {
			const double din = std::min (0.0, lvl);
			double dout = transfer (din); if (dout > 0.0) { dout = 0.0; }
			const double px = X (din), py = Y (std::max (LO, dout));
			/* projections discrètes vers les axes */
			cr->set_line_width (1.0); cr->set_source_rgba (1,1,1,0.25);
			cr->move_to (px, py); cr->line_to (px, h); cr->stroke ();
			cr->move_to (px, py); cr->line_to (0, py);  cr->stroke ();
			/* halo + pastille */
			cr->set_source_rgba (0.98,0.85,0.45,0.28); cr->arc (px, py, 8.0, 0, 2*M_PI); cr->fill ();
			cr->set_source_rgb  (1.0,0.97,0.80);       cr->arc (px, py, 3.6, 0, 2*M_PI); cr->fill ();
			cr->set_source_rgba (0.20,0.14,0.03,0.85); cr->set_line_width (1.2);
			cr->arc (px, py, 3.6, 0, 2*M_PI); cr->stroke ();
			/* lecture chiffrée in -> out */
			char t[48]; std::snprintf (t, sizeof t, "%.0f \xe2\x86\x92 %.0f dB", din, dout);
			cr->select_font_face ("monospace", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_BOLD);
			cr->set_font_size (9.5);
			Cairo::TextExtents te; cr->get_text_extents (t, te);
			cr->set_source_rgba (1.0,0.97,0.80,0.85);
			cr->move_to (w - te.width - 8, h - 6); cr->show_text (t);
		}

		/* GR live en haut à droite (compresseur) */
		if (_grVals[2] > 0.05f) {
			char t[24]; std::snprintf (t, sizeof t, "GR %.1f dB", _grVals[2]);
			cr->select_font_face ("monospace", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_BOLD);
			cr->set_font_size (10.0);
			Cairo::TextExtents te; cr->get_text_extents (t, te);
			cr->set_source_rgb (0.980,0.760,0.300);
			cr->move_to (w - te.width - 10, 15); cr->show_text (t);
		}
	} else {
		cr->set_source_rgba(1,1,1,0.4); cr->select_font_face("ArdourSans",Cairo::FONT_SLANT_NORMAL,Cairo::FONT_WEIGHT_NORMAL); cr->set_font_size(11); cr->move_to(8,18); cr->show_text(_("Dynamics off"));
	}
	screen_glass (cr, w, h);
	return true;
}

/* 4 VU de gain reduction HORIZONTAUX (Gate/Exp/Comp/Lim), barres fines à dégradé
 * (vert->ambre->rouge), pleine largeur sous le graphe. Échelle 0..20 dB GR. */
bool
OxfordConsolePanel::on_dyn_meters_expose (GdkEventExpose*)
{
	return dyn_meters_draw (_dyn_meters);
}

bool
OxfordConsolePanel::dyn_meters_draw (Gtk::DrawingArea& area)
{
	Glib::RefPtr<Gdk::Window> win = area.get_window ();
	if (!win) { return false; }
	Cairo::RefPtr<Cairo::Context> cr = win->create_cairo_context ();
	Gtk::Allocation a = area.get_allocation ();
	const double w = a.get_width (), h = a.get_height ();
	cr->set_source_rgb (kPanelBg[0], kPanelBg[1], kPanelBg[2]); cr->paint ();
	/* Sur un BUS, le gate et l'expandeur n'existent pas : afficher leurs rangées
	 * laissait deux bargraphs morts en permanence. On ne montre alors que les
	 * deux sections réellement présentes. */
	static const char* kAll[4] = { "GATE", "EXP", "COMP", "LIM" };
	const int first = _isBus ? 2 : 0;
	const int nrow  = 4 - first;
	const char* const* lbl = &kAll[first];
	const double tickh = 11.0;                       // bandeau d'échelle en bas
	const double rowh = (h - tickh) / (double) nrow, lblw = 38.0, maxgr = 20.0;
	const int NSEG = 20; const double gap = 1.0;
	double bx = lblw, bw = w - lblw - 34.0;
	cr->select_font_face ("monospace", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_BOLD);
	cr->set_font_size (8.0);
	for (int i = 0; i < nrow; ++i) {
		const double y = i * rowh + 1.0, bh = rowh - 3.0;
		cr->set_source_rgb (kLabelDk[0], kLabelDk[1], kLabelDk[2]);
		cr->move_to (2.0, y + bh - 2.0); cr->show_text (lbl[i]);
		/* puits du bargraph (arrondi, fond profond) */
		rrect (cr, bx - 1.5, y - 1.0, bw + 3.0, bh + 2.0, 2.5);
		cr->set_source_rgb (0.06, 0.08, 0.11); cr->fill ();
		double gr = _grVals[first+i]; if (gr < 0) gr = 0; if (gr > maxgr) gr = maxgr;
		double pk = _grPeak[first+i]; if (pk < 0) pk = 0; if (pk > maxgr) pk = maxgr;
		/* 20 segments discrets (VU OXF-R3) : segments ÉTEINTS visibles en sombre */
		const double segw = (bw - (NSEG - 1) * gap) / NSEG;
		const int lit  = (int) std::ceil ((gr / maxgr) * NSEG);
		const int pseg = (int) std::ceil ((pk / maxgr) * NSEG) - 1;
		for (int s2 = 0; s2 < NSEG; ++s2) {
			const double frac = (double) s2 / (NSEG - 1);
			double r, gg, bb;
			if      (frac < 0.6)  { r = 0.20; gg = 0.80; bb = 0.30; }   // vert
			else if (frac < 0.85) { r = 0.90; gg = 0.80; bb = 0.20; }   // ambre
			else                  { r = 0.90; gg = 0.25; bb = 0.20; }   // rouge
			const bool on = (s2 < lit);
			const bool ph = (s2 == pseg && pseg >= lit);                // segment peak-hold
			if      (on) cr->set_source_rgb (r, gg, bb);
			else if (ph) cr->set_source_rgba (r, gg, bb, 0.75);
			else         cr->set_source_rgba (r, gg, bb, 0.13);         // LED éteinte
			cr->rectangle (bx + s2 * (segw + gap), y, segw, bh); cr->fill ();
		}
		char s[16]; std::snprintf (s, sizeof s, "%.1f", gr);
		cr->set_source_rgb (kLabelDk[0], kLabelDk[1], kLabelDk[2]);
		cr->move_to (bx + bw + 3.0, y + bh - 2.0); cr->show_text (s);
	}
	/* échelle 0..20 dB sous les barres */
	cr->set_font_size (7.0);
	cr->set_source_rgba (kLabelDk[0], kLabelDk[1], kLabelDk[2], 0.75);
	for (int d = 0; d <= 20; d += 5) {
		const double x = bx + bw * d / maxgr;
		cr->move_to (x, 4 * rowh - 1.0); cr->line_to (x, 4 * rowh + 2.0);
		cr->set_line_width (1.0); cr->stroke ();
		char t[8]; std::snprintf (t, sizeof t, "%d", d);
		Cairo::TextExtents te; cr->get_text_extents (t, te);
		cr->move_to (x - te.width * 0.5, 4 * rowh + tickh - 2.0); cr->show_text (t);
	}
	return true;
}

/* Bargraphs du limiteur de sortie : réduction de gain et dépassement des crêtes
 * inter-échantillons (Recon), dessinés comme ceux de la section Dynamics. */
bool
OxfordConsolePanel::on_outlim_expose (GdkEventExpose*)
{
	Glib::RefPtr<Gdk::Window> win = _outlim_bars.get_window ();
	if (!win) { return false; }
	Cairo::RefPtr<Cairo::Context> cr = win->create_cairo_context ();
	Gtk::Allocation a = _outlim_bars.get_allocation ();
	const double w = a.get_width (), h = a.get_height ();
	trident_fill_panel (cr, 0, 0, w, h, kPanelBg[0], kPanelBg[1], kPanelBg[2]);

	/* Rangée 2 = NIVEAU DE SORTIE en crête réelle, repéré par rapport au plafond
	 * (0 = plafond). Un simple « de combien ça dépasse » restait à zéro tant que
	 * tout allait bien et n'apprenait rien ; là on voit en permanence où on est,
	 * et le dépassement devient la zone rouge. */
	const char*  lbl[2] = { "GR", "OUT" };
	const double rel = (double) _outlimTp - (double) _outlimCeil;   // dB par rapport au plafond
	const double val[2] = { (double) _outlimGr, rel + 24.0 };       // -24..+3 -> 0..27
	const double maxv[2] = { 20.0, 27.0 };
	const double lblw = 40.0, rowh = h / 2.0;
	const int    NSEG = 20; const double gap = 1.0;
	const double bx = lblw, bw = w - lblw - 6.0;

	cr->select_font_face ("monospace", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_BOLD);
	cr->set_font_size (8.5);
	for (int i = 0; i < 2; ++i) {
		const double y = i * rowh + 2.0, bh = rowh - 5.0;
		cr->set_source_rgb (kLabelDk[0], kLabelDk[1], kLabelDk[2]);
		cr->move_to (2.0, y + bh - 1.0); cr->show_text (lbl[i]);
		rrect (cr, bx - 1.5, y - 1.0, bw + 3.0, bh + 2.0, 2.5);
		cr->set_source_rgb (0.06, 0.08, 0.11); cr->fill ();
		double v = val[i]; if (v < 0) v = 0; if (v > maxv[i]) v = maxv[i];
		const double segw = (bw - (NSEG - 1) * gap) / NSEG;
		const int lit = (int) std::ceil ((v / maxv[i]) * NSEG);
		for (int s = 0; s < NSEG; ++s) {
			double r, g, b;
			if (i == 1) {
				/* Rangée OUT : la couleur suit la valeur en dB du segment, pas sa
				 * position relative. Le rouge ne s'allume que si le BAS du segment
				 * est AU-DESSUS du plafond — sinon un signal simplement collé au
				 * plafond (ce que fait un limiteur en fonctionnement normal, a
				 * fortiori avec l'Enhance) allumerait le rouge en permanence. */
				const double lowDb = (double) s / NSEG * maxv[1] - 24.0;   // dB par rapport au plafond
				if      (lowDb >= 0.0)  { r = 0.90; g = 0.25; b = 0.20; }  // dépassement
				else if (lowDb >= -6.0) { r = 0.90; g = 0.80; b = 0.20; }  // approche
				else                    { r = 0.20; g = 0.80; b = 0.30; }
			} else {
				const double frac = (double) s / (NSEG - 1);
				if      (frac < 0.60) { r = 0.20; g = 0.80; b = 0.30; }
				else if (frac < 0.85) { r = 0.90; g = 0.80; b = 0.20; }
				else                  { r = 0.90; g = 0.25; b = 0.20; }
			}
			const bool on = (s < lit);
			cr->set_source_rgba (r, g, b, on ? 1.0 : 0.13);
			cr->rectangle (bx + s * (segw + gap), y, segw, bh);
			cr->fill ();
		}
	}
	return true;
}
