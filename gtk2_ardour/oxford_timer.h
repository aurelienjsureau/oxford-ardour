/*
 * OxfordTimer — compteur de la barre d'état (fork Oxford).
 *
 * Un seul widget, trois usages :
 *   - FACTURATION : temps passé sur la session, cumulé et PERSISTÉ dans
 *     instant.xml. Il repart de sa valeur à la réouverture de la session, et
 *     tourne en arrière-plan quel que soit le mode affiché.
 *   - CHRONO : compteur manuel qui monte, remis à zéro à la demande.
 *   - MINUTEUR : compte à rebours nommé ("Prep", "EQ", ...) pour s'entraîner à
 *     tenir un temps. À zéro l'afficheur passe en ambre et continue en négatif.
 *     Chaque tâche terminée est consignée (prévu / réel) dans le journal de la
 *     session, consultable par « Historique des tâches… ».
 *
 * Clic gauche = démarrer/pause du mode affiché, clic droit = menu.
 */
#pragma once

#include <string>
#include <vector>

#include <ytkmm/eventbox.h>
#include <ytkmm/label.h>

#include "ardour/session_handle.h"

class OxfordTimer : public Gtk::EventBox, public ARDOUR::SessionHandlePtr
{
public:
	OxfordTimer ();
	~OxfordTimer ();

	void set_session (ARDOUR::Session*);

	/* écrit l'état dans instant.xml (appelé aussi périodiquement) */
	void flush ();

private:
	enum Mode { Billing, Stopwatch, Countdown };

	struct TaskLog {
		std::string name;
		double      planned = 0;
		double      actual  = 0;
	};

	Gtk::Label _label;
	Mode       _mode = Billing;

	/* facturation : tourne dès qu'une session est chargée, indépendamment du
	 * mode affiché (on peut chronométrer une tâche sans arrêter la facture) */
	double _billed     = 0;
	bool   _billing_on = false;

	double _stop_elapsed = 0;
	bool   _stop_on      = false;

	std::string _task         = "";
	double      _task_planned = 600;   // 10 min par défaut
	double      _task_elapsed = 0;
	bool        _task_on      = false;

	std::vector<TaskLog> _log;

	gint64 _last_us  = 0;   // horloge monotone du dernier tick
	double _save_acc = 0;   // secondes écoulées depuis la dernière écriture
	bool   _blink    = false;

	sigc::connection _tick_conn;

	bool tick ();
	void update_label ();

	bool on_button_press_event (GdkEventButton*);
	void popup_menu (GdkEventButton*);

	void toggle_run ();
	void set_mode (Mode);
	void set_duration (double secs);
	void start_task (const std::string& name, double planned);
	void start_countdown (double seconds);   /* decompte direct, sans nommer de tache */
	void new_task_dialog ();
	void custom_duration_dialog ();
	void reset_current ();
	void commit_task ();     // range la tâche courante dans le journal
	void reset_billing ();
	void show_history ();

	void load_state ();
	void session_going_away ();

	static std::string hms (double secs);
};
