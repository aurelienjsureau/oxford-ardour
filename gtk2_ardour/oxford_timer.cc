/*
 * OxfordTimer — compteur de la barre de transport (fork Oxford). Voir oxford_timer.h.
 */

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <glibmm/main.h>
#include <glibmm/markup.h>

#include <ytkmm/menu.h>
#include <ytkmm/menuitem.h>
#include <ytkmm/scrolledwindow.h>
#include <ytkmm/separator.h>
#include <ytkmm/spinbutton.h>
#include <ytkmm/stock.h>

#include "pbd/xml++.h"

#include "ardour/session.h"

#include "gtkmm2ext/colors.h"

#include "widgets/tooltips.h"

#include "ardour_dialog.h"
#include "ardour_message.h"
#include "oxford_timer.h"
#include "ui_config.h"
#include "utils.h"

#include "pbd/i18n.h"

using namespace Gtk;
using namespace ArdourWidgets;

/* ambre OXF-R3 : dépassement du minuteur */
static const char* kOver  = "#d49d2b";
/* compteur en pause : le texte s'efface sans changer de largeur */
static const char* kPause = "#7c7c7c";

/* Noms de widgets repris à l'horloge de transport : le thème et le fichier rc
 * s'occupent de la police, du fond et des couleurs -> même look que l'horloge
 * de gauche et que ses boutons tempo / signature. */
static const char* kClockName = "transport clock";
static const char* kBtnName   = "transport option button";

/* couleur du thème (RGBA) -> "#rrggbb" pour le markup Pango */
static std::string
hexcol (uint32_t rgba)
{
	char b[16];
	std::snprintf (b, sizeof b, "#%06x", (unsigned) ((rgba >> 8) & 0xffffff));
	return std::string (b);
}

OxfordTimer::OxfordTimer ()
	: _mode_btn ("")
	, _run_btn ("")
	, _rst_btn ("")
{
	/* ---- ligne 0 : l'afficheur, calqué sur l'horloge de transport ----
	 * Un Label dans un EventBox ne marche PAS : le thème d'Ardour repeint le
	 * fond par-dessus modify_bg. Un ArdourButton, lui, peint son fond lui-même
	 * (couleurs fixes) et accepte la police d'horloge du thème. */
	const uint32_t clock_bg = UIConfiguration::instance().color (X_("transport clock: background"));
	_disp.set_name (kClockName);
	_disp.set_fixed_colors (clock_bg, clock_bg);
	_disp.set_layout_font (ARDOUR_UI_UTILS::get_font_for_style (kClockName));
	_disp.set_corner_radius (2);
	_disp.signal_clicked.connect (sigc::mem_fun (*this, &OxfordTimer::begin_edit));
	/* connecté AVANT le gestionnaire du bouton pour intercepter le clic droit */
	_disp.signal_button_press_event ().connect (sigc::mem_fun (*this, &OxfordTimer::disp_button_press), false);
	_disp.show ();
	set_tooltip (_disp, _("Clic : saisir la durée (45, 45:00 ou 1:30:00), Entrée pour valider, Échap pour annuler."));

	_entry.set_width_chars (8);
	_entry.set_alignment (0.5);
	_entry.set_has_frame (true);
	/* l'entrée est construite masquée : show_all() du parent ne doit pas la révéler */
	_entry.set_no_show_all (true);
	_entry.signal_activate ().connect (sigc::mem_fun (*this, &OxfordTimer::commit_edit));
	_entry.signal_key_press_event ().connect (sigc::mem_fun (*this, &OxfordTimer::entry_key_press), false);
	_entry.signal_focus_out_event ().connect (sigc::mem_fun (*this, &OxfordTimer::entry_focus_out));

	_disp_box.pack_start (_disp,  true, true);
	_disp_box.pack_start (_entry, true, true);
	_disp_box.show ();

	/* ---- ligne 1 : les boutons, mêmes noms que ceux de l'horloge ----
	 * PAS de set_size_request : ArdourButton tronque son texte si on lui impose
	 * moins que sa taille naturelle, et la hauteur vient des SizeGroup de la
	 * barre de transport (voir ApplicationBar) — c'est ça qui les rend
	 * identiques aux boutons tempo / signature. */
	const Pango::FontDescription btn_font = UIConfiguration::instance().get_ArdourSmallFont ();

	_mode_btn.set_name (kBtnName);
	_mode_btn.set_layout_font (btn_font);
	_mode_btn.signal_clicked.connect (sigc::mem_fun (*this, &OxfordTimer::cycle_mode));
	_mode_btn.show ();
	set_tooltip (_mode_btn, _("Bascule entre le chronomètre (monte) et le minuteur (descend)."));

	_run_btn.set_name (kBtnName);
	_run_btn.set_layout_font (btn_font);
	_run_btn.signal_clicked.connect (sigc::mem_fun (*this, &OxfordTimer::toggle_run));
	_run_btn.show ();
	set_tooltip (_run_btn, _("Démarrer / mettre en pause."));

	_rst_btn.set_text (_("RAZ"));
	_rst_btn.set_name (kBtnName);
	_rst_btn.set_layout_font (btn_font);
	_rst_btn.signal_clicked.connect (sigc::mem_fun (*this, &OxfordTimer::reset_current));
	_rst_btn.show ();
	set_tooltip (_rst_btn, _("Remettre le compteur affiché à zéro (la facturation n'est pas touchée)."));

	/* facturation : compteur autonome, dans la rangée du bas pour ne pas
	 * dépasser la hauteur de la barre */
	_bill_lbl.set_use_markup ();
	_bill_lbl.show ();
	_bill_ev.set_visible_window (false);
	_bill_ev.add_events (Gdk::BUTTON_PRESS_MASK);
	_bill_ev.add (_bill_lbl);
	_bill_ev.show ();
	_bill_ev.signal_button_press_event ().connect (sigc::mem_fun (*this, &OxfordTimer::bill_button_press));
	set_tooltip (_bill_ev, _("Temps facturé sur cette session (sauvegardé avec la session).\nClic : marche / pause."));

	_ctl.set_spacing (2);
	_ctl.pack_start (_mode_btn, true,  true);
	_ctl.pack_start (_run_btn,  false, false);
	_ctl.pack_start (_rst_btn,  true,  true);
	_ctl.pack_start (_bill_ev,  false, false, 4);
	_ctl.show ();

	update_label ();

	/* 500 ms : la seconde affichée ne « saute » pas d'une unité à l'autre */
	_tick_conn = Glib::signal_timeout ().connect (sigc::mem_fun (*this, &OxfordTimer::tick), 500);
}

OxfordTimer::~OxfordTimer ()
{
	_tick_conn.disconnect ();
}

std::string
OxfordTimer::hms (double secs)
{
	const bool neg = secs < 0;
	if (neg) { secs = -secs; }
	const long t = (long) (secs + 0.5);
	const long h = t / 3600;
	const long m = (t % 3600) / 60;
	const long s = t % 60;
	/* toujours h:mm:ss, comme une horloge : la largeur ne bouge pas */
	char b[32];
	std::snprintf (b, sizeof b, "%s%ld:%02ld:%02ld", neg ? "-" : "", h, m, s);
	return std::string (b);
}

double
OxfordTimer::parse_hms (const std::string& raw)
{
	/* on tolère les espaces et les suffixes h/m/s : "45", "45 min", "1h30" */
	std::string s;
	for (std::string::const_iterator i = raw.begin (); i != raw.end (); ++i) {
		const char c = *i;
		if (std::isdigit ((unsigned char) c) || c == ':' || c == '.') { s += c; }
		else if (c == 'h' || c == 'H') { s += ':'; }
		/* 'm', 'i', 'n', 's', espaces : ignorés */
	}
	/* "1h30" devient "1:30" ; "1h" devient "1:" -> on complète */
	while (!s.empty () && s[s.size () - 1] == ':') { s += "0"; }
	if (s.empty ()) { return -1.0; }

	double part[3] = { 0, 0, 0 };
	int n = 0;
	std::string cur;
	for (size_t i = 0; i <= s.size (); ++i) {
		if (i == s.size () || s[i] == ':') {
			if (n > 2) { return -1.0; }
			part[n++] = cur.empty () ? 0.0 : std::atof (cur.c_str ());
			cur.clear ();
		} else {
			cur += s[i];
		}
	}

	switch (n) {
	case 1: return part[0] * 60.0;                                  /* minutes */
	case 2: return part[0] * 60.0 + part[1];                        /* mm:ss */
	case 3: return part[0] * 3600.0 + part[1] * 60.0 + part[2];     /* hh:mm:ss */
	}
	return -1.0;
}

void
OxfordTimer::set_session (ARDOUR::Session* s)
{
	if (_session) { flush (); }

	SessionHandlePtr::set_session (s);

	_billed = 0;
	_log.clear ();
	_billing_on = false;

	if (_session) {
		load_state ();
		_billing_on = true;   // la facturation démarre avec la session
	}
	_last_us  = 0;
	_save_acc = 0;
	update_label ();
}

void
OxfordTimer::session_going_away ()
{
	flush ();
	SessionHandlePtr::session_going_away ();
	_billed = 0;
	_billing_on = false;
	_log.clear ();
	update_label ();
}

void
OxfordTimer::load_state ()
{
	XMLNode* n = _session->instant_xml (X_("OxfordTimer"));
	if (!n) { return; }

	double d = 0;
	if (n->get_property (X_("billed-seconds"), d)) { _billed = d; }
	if (n->get_property (X_("planned-seconds"), d) && d > 0) { _task_planned = d; }

	int32_t m = 0;
	if (n->get_property (X_("mode"), m)) { _mode = (m == 0) ? Stopwatch : Countdown; }

	for (XMLNodeList::const_iterator i = n->children ().begin (); i != n->children ().end (); ++i) {
		if ((*i)->name () != X_("Task")) { continue; }
		TaskLog t;
		std::string nm;
		(*i)->get_property (X_("name"), nm);
		(*i)->get_property (X_("planned"), t.planned);
		(*i)->get_property (X_("actual"),  t.actual);
		t.name = nm;
		_log.push_back (t);
	}
}

void
OxfordTimer::flush ()
{
	if (!_session) { return; }

	/* nœud sur la pile : add_instant_xml en prend une COPIE */
	XMLNode node (X_("OxfordTimer"));
	node.set_property (X_("billed-seconds"),  _billed);
	node.set_property (X_("planned-seconds"), _task_planned);
	node.set_property (X_("mode"), (int32_t) (_mode == Stopwatch ? 0 : 1));

	for (std::vector<TaskLog>::const_iterator i = _log.begin (); i != _log.end (); ++i) {
		XMLNode* t = node.add_child (X_("Task"));
		t->set_property (X_("name"),    i->name);
		t->set_property (X_("planned"), i->planned);
		t->set_property (X_("actual"),  i->actual);
	}

	/* false : état PROPRE À LA SESSION, pas dans la config globale */
	_session->add_instant_xml (node, false);
}

bool
OxfordTimer::tick ()
{
	const gint64 now = g_get_monotonic_time ();
	double dt = _last_us ? (now - _last_us) / 1000000.0 : 0.0;
	_last_us = now;

	/* garde-fou : mise en veille de la machine, gel de l'UI... on ne facture
	 * pas un trou. Au-delà de 5 s d'un tick à l'autre, le temps est perdu. */
	if (dt < 0.0) { dt = 0.0; }
	if (dt > 5.0) { dt = 5.0; }

	if (_session && _billing_on) { _billed += dt; _save_acc += dt; }
	if (_stop_on)                { _stop_elapsed += dt; }
	if (_task_on)                { _task_elapsed += dt; }

	if (_save_acc >= 60.0) { flush (); _save_acc = 0.0; }

	_blink = !_blink;
	update_label ();
	return true;
}

void
OxfordTimer::update_label ()
{
	char buf[256];

	/* ---- facturation ---- */
	{
		const char* col = (_billing_on && _session) ? 0 : kPause;
		if (col) {
			std::snprintf (buf, sizeof buf,
			               "<span size=\"small\" foreground=\"%s\">%s</span> <b><span foreground=\"%s\">%s</span></b>",
			               col, _("FACT"), col, hms (_billed).c_str ());
		} else {
			std::snprintf (buf, sizeof buf, "<span size=\"small\">%s</span> <b>%s</b>",
			               _("FACT"), hms (_billed).c_str ());
		}
		_bill_lbl.set_markup (buf);
	}

	/* ---- chrono / minuteur ---- */
	_mode_btn.set_text (_mode == Stopwatch ? _("CHRONO") : _("MINUT."));

	const bool running = (_mode == Stopwatch) ? _stop_on : _task_on;
	_run_btn.set_text (running ? "‖" : "▶");   /* ‖ / ▶ */
	_run_btn.set_active_state (running ? Gtkmm2ext::ExplicitActive : Gtkmm2ext::Off);

	std::string val, col;
	if (_mode == Stopwatch) {
		val = hms (_stop_elapsed);
	} else {
		const double remain = _task_planned - _task_elapsed;
		val = hms (remain);
		if (remain < 0) {
			/* dépassement : ambre, et clignotement tant que ça tourne */
			col = (!_task_on || _blink) ? std::string (kOver)
			                            : hexcol (UIConfiguration::instance().color (X_("transport clock: text")));
		}
	}
	/* l'encre reste celle de l'horloge de transport même à l'arrêt (c'est le
	 * bouton ▶/‖ qui dit si ça tourne) ; seul le dépassement passe en ambre */
	if (col.empty ()) {
		col = hexcol (UIConfiguration::instance().color (X_("transport clock: text")));
	}

	std::snprintf (buf, sizeof buf, "<span foreground=\"%s\">%s</span>", col.c_str (), val.c_str ());
	if (!_editing) { _disp.set_text (buf, true /* markup */); }
}

/* ------------------------------------------------------------------------
 * saisie directe de la durée
 * --------------------------------------------------------------------- */

bool
OxfordTimer::disp_button_press (GdkEventButton* ev)
{
	/* le clic gauche est traité par ArdourButton (signal_clicked -> begin_edit),
	 * on n'intercepte que le clic droit pour ouvrir le menu */
	if (ev->button == 3) { popup_menu (ev); return true; }
	return false;
}

void
OxfordTimer::begin_edit ()
{
	if (_editing) { return; }
	_editing = true;
	_entry.set_text (hms (_mode == Stopwatch ? _stop_elapsed : _task_planned));
	_disp.hide ();
	_entry.show ();
	_entry.grab_focus ();
	_entry.select_region (0, -1);
}

void
OxfordTimer::commit_edit ()
{
	if (!_editing) { return; }
	const double v = parse_hms (_entry.get_text ());
	cancel_edit ();

	if (v < 0) { return; }

	if (_mode == Stopwatch) {
		_stop_elapsed = v;
	} else {
		/* on arme la nouvelle durée sans démarrer : le bouton ▶ décide */
		commit_task ();
		_task         = "";
		_task_planned = v;
		_task_elapsed = 0;
		_task_on      = false;
	}
	flush ();
	update_label ();
}

void
OxfordTimer::cancel_edit ()
{
	if (!_editing) { return; }
	_editing = false;
	_entry.hide ();
	_disp.show ();
	update_label ();
}

bool
OxfordTimer::entry_key_press (GdkEventKey* ev)
{
	if (ev->keyval == GDK_Escape) { cancel_edit (); return true; }
	/* la barre d'état vit sous les raccourcis globaux d'Ardour : on garde les
	 * touches pour l'entrée tant qu'elle est ouverte (sinon Espace = lecture) */
	return false;
}

bool
OxfordTimer::entry_focus_out (GdkEventFocus*)
{
	commit_edit ();
	return false;
}

/* ------------------------------------------------------------------------
 * clics & menu
 * --------------------------------------------------------------------- */

bool
OxfordTimer::bill_button_press (GdkEventButton* ev)
{
	if (ev->button == 3) { popup_menu (ev); return true; }
	if (ev->button == 1) { toggle_billing (); return true; }
	return false;
}

void
OxfordTimer::show ()
{
	_disp_box.show ();
	_ctl.show ();
}

void
OxfordTimer::hide ()
{
	_disp_box.hide ();
	_ctl.hide ();
}

void
OxfordTimer::popup_menu (GdkEventButton* ev)
{
	using namespace Menu_Helpers;

	Menu* m = manage (new Menu);
	MenuList& items = m->items ();

	/* Décompte en un clic : « 45 minutes sur ce morceau » démarre tout de suite.
	 * Pour une autre durée, on tape directement dans l'afficheur. */
	{
		Menu* cd = manage (new Menu);
		MenuList& ci = cd->items ();
		static const int mins[] = { 5, 10, 15, 20, 30, 45, 60, 90 };
		for (size_t i = 0; i < sizeof (mins) / sizeof (mins[0]); ++i) {
			char lbl[32];
			std::snprintf (lbl, sizeof lbl, _("%d min"), mins[i]);
			ci.push_back (MenuElem (lbl, sigc::bind (sigc::mem_fun (*this, &OxfordTimer::start_countdown), (double) mins[i] * 60.0)));
		}
		items.push_back (MenuElem (_("Minuteur : démarrer"), *cd));
	}
	items.push_back (MenuElem (_("Tâche nommée…"), sigc::mem_fun (*this, &OxfordTimer::new_task_dialog)));
	items.push_back (SeparatorElem ());

	items.push_back (MenuElem (_mode == Stopwatch ? _("Passer au minuteur (descend)") : _("Passer au chronomètre (monte)"),
	                           sigc::mem_fun (*this, &OxfordTimer::cycle_mode)));
	items.push_back (MenuElem (_("Démarrer / Pause"), sigc::mem_fun (*this, &OxfordTimer::toggle_run)));
	items.push_back (MenuElem (_("Remettre à zéro"), sigc::mem_fun (*this, &OxfordTimer::reset_current)));
	items.push_back (SeparatorElem ());

	items.push_back (MenuElem (_billing_on ? _("Facturation : pause") : _("Facturation : reprendre"),
	                           sigc::mem_fun (*this, &OxfordTimer::toggle_billing)));
	items.push_back (MenuElem (_("Historique des tâches…"), sigc::mem_fun (*this, &OxfordTimer::show_history)));
	items.push_back (MenuElem (_("Remettre la facturation à zéro…"), sigc::mem_fun (*this, &OxfordTimer::reset_billing)));

	m->popup (ev->button, ev->time);
}

void
OxfordTimer::toggle_run ()
{
	if (_mode == Stopwatch) { _stop_on = !_stop_on; }
	else                    { _task_on = !_task_on; }
	update_label ();
}

void
OxfordTimer::toggle_billing ()
{
	_billing_on = !_billing_on && _session;
	update_label ();
}

void
OxfordTimer::cycle_mode ()
{
	set_mode (_mode == Stopwatch ? Countdown : Stopwatch);
}

void
OxfordTimer::set_mode (Mode m)
{
	if (_editing) { cancel_edit (); }
	_mode = m;
	update_label ();
	flush ();
}

void
OxfordTimer::set_duration (double secs)
{
	/* régler une durée arme une nouvelle passe : on consigne la précédente */
	commit_task ();
	_task_planned = secs;
	_task_elapsed = 0;
	_task_on      = true;
	_mode         = Countdown;
	update_label ();
}

void
OxfordTimer::start_task (const std::string& name, double planned)
{
	commit_task ();
	_task         = name;
	_task_planned = planned;
	_task_elapsed = 0;
	_task_on      = true;
	_mode         = Countdown;
	update_label ();
}

void
OxfordTimer::commit_task ()
{
	/* moins de 5 s : faux départ, on ne pollue pas le journal */
	if (_task_elapsed < 5.0) { return; }
	TaskLog t;
	t.name    = _task.empty () ? hms (_task_planned) : _task;
	t.planned = _task_planned;
	t.actual  = _task_elapsed;
	_log.push_back (t);
	flush ();
}

void
OxfordTimer::reset_current ()
{
	if (_mode == Stopwatch) {
		_stop_elapsed = 0;
		_stop_on      = false;
	} else {
		commit_task ();
		_task_elapsed = 0;
		_task_on      = false;
	}
	update_label ();
}

void
OxfordTimer::new_task_dialog ()
{
	ArdourDialog d (_("Tâche chronométrée"), true);

	Gtk::HBox rowa, rowb;
	Gtk::Label ln (_("Tâche :"));
	Gtk::Label ld (_("Durée (minutes) :"));
	Gtk::Entry name;
	Gtk::Adjustment adj (_task_planned / 60.0, 1, 480, 1, 5);
	Gtk::SpinButton dur (adj);

	name.set_text (_task);
	name.set_activates_default (true);
	rowa.set_spacing (6); rowb.set_spacing (6);
	rowa.pack_start (ln, false, false); rowa.pack_start (name, true, true);
	rowb.pack_start (ld, false, false); rowb.pack_start (dur, false, false);

	d.get_vbox ()->set_spacing (6);
	d.get_vbox ()->pack_start (rowa, false, false);
	d.get_vbox ()->pack_start (rowb, false, false);
	d.add_button (Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
	d.add_button (_("Démarrer"), Gtk::RESPONSE_OK);
	d.set_default_response (Gtk::RESPONSE_OK);
	d.show_all ();

	if (d.run () == Gtk::RESPONSE_OK) {
		start_task (name.get_text (), dur.get_value () * 60.0);
	}
}

void
OxfordTimer::reset_billing ()
{
	ArdourMessageDialog md (_("Remettre à zéro le compteur de facturation de cette session ?"),
	                        false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_YES_NO, true);
	md.set_secondary_text (string_compose (_("Les %1 déjà comptés seront perdus."), hms (_billed)));
	if (md.run () != Gtk::RESPONSE_YES) { return; }

	_billed = 0;
	flush ();
	update_label ();
}

void
OxfordTimer::show_history ()
{
	ArdourDialog d (_("Historique des tâches"), true);

	Gtk::VBox* box = manage (new Gtk::VBox);
	box->set_spacing (2);
	box->set_border_width (6);

	{
		Gtk::Label* head = manage (new Gtk::Label);
		head->set_markup (string_compose ("<b>%1</b>  %2", _("Facturé sur cette session :"), hms (_billed)));
		head->set_alignment (0, 0.5);
		box->pack_start (*head, false, false);
		box->pack_start (*manage (new Gtk::HSeparator), false, false, 4);
	}

	if (_log.empty ()) {
		Gtk::Label* l = manage (new Gtk::Label (_("Aucune tâche chronométrée pour le moment.")));
		l->set_alignment (0, 0.5);
		box->pack_start (*l, false, false);
	} else {
		double tot_p = 0, tot_a = 0;
		for (std::vector<TaskLog>::const_iterator i = _log.begin (); i != _log.end (); ++i) {
			const double diff = i->actual - i->planned;
			char b[320];
			std::snprintf (b, sizeof b, "%s — %s %s, %s %s <span foreground=\"%s\">(%s%s)</span>",
			               Glib::Markup::escape_text (i->name).c_str (),
			               _("prévu"), hms (i->planned).c_str (),
			               _("réel"),  hms (i->actual).c_str (),
			               diff > 0 ? kOver : "#3a8a3a",
			               diff > 0 ? "+" : "", hms (diff < 0 ? -diff : diff).c_str ());
			Gtk::Label* l = manage (new Gtk::Label);
			l->set_markup (b);
			l->set_alignment (0, 0.5);
			box->pack_start (*l, false, false);
			tot_p += i->planned;
			tot_a += i->actual;
		}
		box->pack_start (*manage (new Gtk::HSeparator), false, false, 4);
		Gtk::Label* t = manage (new Gtk::Label);
		t->set_markup (string_compose ("<b>%1</b>  %2 / %3", _("Total prévu / réel :"), hms (tot_p), hms (tot_a)));
		t->set_alignment (0, 0.5);
		box->pack_start (*t, false, false);
	}

	Gtk::ScrolledWindow* sw = manage (new Gtk::ScrolledWindow);
	sw->set_policy (Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
	sw->add (*box);
	sw->set_size_request (420, 260);

	d.get_vbox ()->pack_start (*sw, true, true);
	d.add_button (Gtk::Stock::CLOSE, Gtk::RESPONSE_CLOSE);
	d.show_all ();
	d.run ();
}

void
OxfordTimer::start_countdown (double seconds)
{
	/* décompte immédiat : pas de nom, pas de dialogue. La passe est nommée
	 * d'après sa durée pour rester lisible dans l'historique de session. */
	char n[32];
	std::snprintf (n, sizeof n, "%.0f min", seconds / 60.0);
	start_task (n, seconds);
}
