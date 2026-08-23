/*
 * OxfordTimer — compteur de la barre d'état (fork Oxford). Voir oxford_timer.h.
 */

#include <cstdio>
#include <cstdlib>

#include <glibmm/main.h>
#include <glibmm/markup.h>

#include <ytkmm/box.h>
#include <ytkmm/entry.h>
#include <ytkmm/menu.h>
#include <ytkmm/menuitem.h>
#include <ytkmm/scrolledwindow.h>
#include <ytkmm/separator.h>
#include <ytkmm/spinbutton.h>
#include <ytkmm/stock.h>

#include "pbd/xml++.h"

#include "ardour/session.h"

#include "ardour_dialog.h"
#include "ardour_message.h"
#include "oxford_timer.h"

#include "pbd/i18n.h"

using namespace Gtk;

/* ambre OXF-R3 : dépassement du minuteur */
static const char* kOver  = "#d49d2b";
/* compteur en pause : le texte s'efface sans changer de largeur */
static const char* kPause = "#7c7c7c";

OxfordTimer::OxfordTimer ()
{
	set_name ("MainMenuBar");   // même fond que le reste de la barre d'état
	_label.set_use_markup ();
	_label.show ();
	add (_label);

	add_events (Gdk::BUTTON_PRESS_MASK);

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
	char b[32];
	if (h > 0) { std::snprintf (b, sizeof b, "%s%ld:%02ld:%02ld", neg ? "-" : "", h, m, s); }
	else       { std::snprintf (b, sizeof b, "%s%ld:%02ld",       neg ? "-" : "", m, s); }
	return std::string (b);
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
	node.set_property (X_("billed-seconds"), _billed);

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
	std::string tag, val, col;
	bool running = false;

	switch (_mode) {
	case Billing:
		tag     = _("FACT");
		val     = hms (_billed);
		running = _billing_on && _session;
		break;
	case Stopwatch:
		tag     = _("CHRONO");
		val     = hms (_stop_elapsed);
		running = _stop_on;
		break;
	case Countdown: {
		tag = _task.empty () ? _("MINUT.") : _task;
		const double remain = _task_planned - _task_elapsed;
		val     = hms (remain);
		running = _task_on;
		if (remain < 0) {
			/* dépassement : ambre, et clignotement tant que ça tourne */
			col = (!_task_on || _blink) ? kOver : kPause;
		}
		break;
	}
	}

	if (col.empty () && !running) { col = kPause; }

	const std::string etag = Glib::Markup::escape_text (tag);
	if (col.empty ()) {
		std::snprintf (buf, sizeof buf, "<span size=\"small\">%s</span> <b>%s</b>", etag.c_str (), val.c_str ());
	} else {
		std::snprintf (buf, sizeof buf, "<span size=\"small\" foreground=\"%s\">%s</span> <b><span foreground=\"%s\">%s</span></b>",
		               col.c_str (), etag.c_str (), col.c_str (), val.c_str ());
	}
	_label.set_markup (buf);
}

bool
OxfordTimer::on_button_press_event (GdkEventButton* ev)
{
	if (ev->button == 3) {
		popup_menu (ev);
		return true;   // ne pas laisser le menu de la barre d'état s'ouvrir
	}
	if (ev->button == 1 && ev->type == GDK_2BUTTON_PRESS) {
		new_task_dialog ();
		return true;
	}
	if (ev->button == 1) {
		toggle_run ();
		return true;
	}
	return false;
}

void
OxfordTimer::popup_menu (GdkEventButton* ev)
{
	using namespace Menu_Helpers;

	Menu* m = manage (new Menu);
	MenuList& items = m->items ();

	/* Décompte en un clic : c'est l'usage courant (« 45 minutes sur ce morceau »),
	 * il ne doit pas obliger à passer par une boîte de dialogue ni à nommer une
	 * tâche. La tâche nommée reste disponible juste en dessous. */
	{
		Menu* cd = manage (new Menu);
		MenuList& ci = cd->items ();
		static const int mins[] = { 5, 10, 15, 20, 30, 45, 60, 90 };
		for (int m : mins) {
			char lbl[32];
			std::snprintf (lbl, sizeof lbl, m < 60 ? _("%d min") : _("%d min"), m);
			ci.push_back (MenuElem (lbl, sigc::bind (sigc::mem_fun (*this, &OxfordTimer::start_countdown), (double) m * 60.0)));
		}
		ci.push_back (SeparatorElem ());
		ci.push_back (MenuElem (_("Durée personnalisée…"), sigc::mem_fun (*this, &OxfordTimer::new_task_dialog)));
		items.push_back (MenuElem (_("Minuteur"), *cd));
	}
	items.push_back (MenuElem (_("Nouvelle tâche…"), sigc::mem_fun (*this, &OxfordTimer::new_task_dialog)));
	items.push_back (SeparatorElem ());
	items.push_back (MenuElem (_("Démarrer / Pause"), sigc::mem_fun (*this, &OxfordTimer::toggle_run)));
	items.push_back (MenuElem (_("Remettre à zéro"), sigc::mem_fun (*this, &OxfordTimer::reset_current)));
	items.push_back (SeparatorElem ());

	{
		Menu* mm = manage (new Menu);
		MenuList& mi = mm->items ();
		mi.push_back (MenuElem (std::string (_mode == Billing   ? "• " : "  ") + _("Facturation (temps sur la session)"),
		                        sigc::bind (sigc::mem_fun (*this, &OxfordTimer::set_mode), Billing)));
		mi.push_back (MenuElem (std::string (_mode == Stopwatch ? "• " : "  ") + _("Chronomètre (monte)"),
		                        sigc::bind (sigc::mem_fun (*this, &OxfordTimer::set_mode), Stopwatch)));
		mi.push_back (MenuElem (std::string (_mode == Countdown ? "• " : "  ") + _("Minuteur (descend)"),
		                        sigc::bind (sigc::mem_fun (*this, &OxfordTimer::set_mode), Countdown)));
		items.push_back (MenuElem (_("Mode"), *mm));
	}

	{
		static const int mins[] = { 5, 10, 15, 20, 30, 45, 60, 90 };
		Menu* dm = manage (new Menu);
		MenuList& di = dm->items ();
		for (size_t i = 0; i < sizeof (mins) / sizeof (mins[0]); ++i) {
			char b[32];
			std::snprintf (b, sizeof b, "%d min", mins[i]);
			di.push_back (MenuElem (b, sigc::bind (sigc::mem_fun (*this, &OxfordTimer::set_duration), (double) mins[i] * 60.0)));
		}
		di.push_back (SeparatorElem ());
		di.push_back (MenuElem (_("Durée personnalisée…"), sigc::mem_fun (*this, &OxfordTimer::custom_duration_dialog)));
		items.push_back (MenuElem (_("Durée du minuteur"), *dm));
	}

	items.push_back (SeparatorElem ());
	items.push_back (MenuElem (_("Historique des tâches…"), sigc::mem_fun (*this, &OxfordTimer::show_history)));
	items.push_back (MenuElem (_("Remettre la facturation à zéro…"), sigc::mem_fun (*this, &OxfordTimer::reset_billing)));

	m->popup (ev->button, ev->time);
}

void
OxfordTimer::toggle_run ()
{
	switch (_mode) {
	case Billing:   _billing_on = !_billing_on && _session; break;
	case Stopwatch: _stop_on    = !_stop_on;                break;
	case Countdown: _task_on    = !_task_on;                break;
	}
	update_label ();
}

void
OxfordTimer::set_mode (Mode m)
{
	_mode = m;
	update_label ();
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
	t.name    = _task.empty () ? std::string (_("Tâche")) : _task;
	t.planned = _task_planned;
	t.actual  = _task_elapsed;
	_log.push_back (t);
	flush ();
}

void
OxfordTimer::reset_current ()
{
	switch (_mode) {
	case Billing:
		/* la facture ne se remet pas à zéro par mégarde : menu dédié */
		break;
	case Stopwatch:
		_stop_elapsed = 0;
		break;
	case Countdown:
		commit_task ();
		_task_elapsed = 0;
		_task_on      = false;
		break;
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
OxfordTimer::custom_duration_dialog ()
{
	ArdourDialog d (_("Durée du minuteur"), true);
	Gtk::HBox row;
	Gtk::Label l (_("Minutes :"));
	Gtk::Adjustment adj (_task_planned / 60.0, 1, 480, 1, 5);
	Gtk::SpinButton dur (adj);
	row.set_spacing (6);
	row.pack_start (l, false, false);
	row.pack_start (dur, false, false);
	d.get_vbox ()->pack_start (row, false, false);
	d.add_button (Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
	d.add_button (Gtk::Stock::OK, Gtk::RESPONSE_OK);
	d.set_default_response (Gtk::RESPONSE_OK);
	d.show_all ();

	if (d.run () == Gtk::RESPONSE_OK) {
		set_duration (dur.get_value () * 60.0);
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
	ArdourDialog d (_("Task history"), true);

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
	/* décompte immédiat : pas de nom, pas de dialogue. La tâche est nommée
	 * d'après sa durée pour rester lisible dans l'historique de session. */
	char n[32];
	std::snprintf (n, sizeof n, "%.0f min", seconds / 60.0);
	start_task (n, seconds);
}
