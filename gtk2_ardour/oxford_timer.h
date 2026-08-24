/*
 * OxfordTimer — compteur de la barre de transport (fork Oxford).
 *
 * Il occupe LA PLACE DE LA 2e HORLOGE d'Ardour (celle-ci est retirée de la
 * barre) : c'est là que l'oeil va, la barre d'état tout en haut était trop
 * discrète.
 *
 * Ce n'est PAS un widget : il fournit deux morceaux à attacher séparément dans
 * la table de la barre de transport, exactement comme une horloge — afficheur
 * en ligne 0, rangée de boutons en ligne 1. C'est la seule façon d'être
 * réellement aligné sur l'horloge voisine (les hauteurs de ligne sont
 * partagées par la table).
 *
 *   [        0:45:00        ]     <- ligne 0, même fond/police que l'horloge
 *   [MINUT.] [▶] [RAZ]  FACT 4:00  <- ligne 1, mêmes boutons que tempo / TS
 *
 * Deux compteurs, tous les contrôles VISIBLES (pas de menu caché) :
 *   - FACTURATION : temps passé sur la session, cumulé et PERSISTÉ dans
 *     instant.xml. Il repart de sa valeur à la réouverture de la session et
 *     tourne en arrière-plan quel que soit l'autre compteur. Clic = marche/pause.
 *   - CHRONO / MINUTEUR : un seul afficheur, le bouton de gauche bascule d'un
 *     mode à l'autre. CLIC SUR L'AFFICHEUR = saisie directe de la durée ("45",
 *     "45:00" ou "1:30:00", Entrée valide, Échap annule). En minuteur, à zéro
 *     l'afficheur passe en ambre et continue en négatif ; la passe terminée est
 *     consignée (prévu / réel) dans le journal de la session, consultable par
 *     « Historique des tâches… » (clic droit).
 */
#pragma once

#include <string>
#include <vector>

#include <ytkmm/box.h>
#include <ytkmm/entry.h>
#include <ytkmm/eventbox.h>
#include <ytkmm/label.h>

#include "widgets/ardour_button.h"

#include "ardour/session_handle.h"

class OxfordTimer : public ARDOUR::SessionHandlePtr
{
public:
	OxfordTimer ();
	~OxfordTimer ();

	void set_session (ARDOUR::Session*);

	/* écrit l'état dans instant.xml (appelé aussi périodiquement) */
	void flush ();

	/* les deux morceaux à attacher dans la table de la barre de transport */
	Gtk::Widget& display_widget ()  { return _disp_box; }
	Gtk::Widget& controls_widget () { return _ctl; }

	/* pour que la barre les range dans ses SizeGroup (hauteur/largeur des
	 * boutons d'horloge) — c'est ce qui les rend identiques à tempo / TS */
	ArdourWidgets::ArdourButton& mode_button  () { return _mode_btn; }
	ArdourWidgets::ArdourButton& run_button   () { return _run_btn; }
	ArdourWidgets::ArdourButton& reset_button () { return _rst_btn; }

	void show ();
	void hide ();

private:
	enum Mode { Stopwatch, Countdown };

	struct TaskLog {
		std::string name;
		double      planned = 0;
		double      actual  = 0;
	};

	/* --- ligne 0 : l'afficheur ------------------------------------------
	 * ArdourButton et pas un Label dans un EventBox : le thème d'Ardour
	 * écrase modify_bg, alors qu'un ArdourButton peint son fond lui-même. */
	Gtk::HBox                   _disp_box;
	ArdourWidgets::ArdourButton _disp;
	Gtk::Entry                  _entry;     // visible seulement pendant la saisie

	/* --- ligne 1 : les boutons + la facturation ------------------------ */
	Gtk::HBox                   _ctl;
	ArdourWidgets::ArdourButton _mode_btn;  // CHRONO <-> MINUT.
	ArdourWidgets::ArdourButton _run_btn;   // marche / pause
	ArdourWidgets::ArdourButton _rst_btn;   // remise à zéro
	Gtk::EventBox               _bill_ev;   // facturation : cliquable
	Gtk::Label                  _bill_lbl;

	bool _editing = false;

	/* --- état ---------------------------------------------------------- */
	Mode _mode = Countdown;

	/* facturation : tourne dès qu'une session est chargée, indépendamment de
	 * l'autre compteur (on peut chronométrer une tâche sans arrêter la facture) */
	double _billed     = 0;
	bool   _billing_on = false;

	double _stop_elapsed = 0;
	bool   _stop_on      = false;

	std::string _task         = "";
	double      _task_planned = 900;   // 15 min par défaut
	double      _task_elapsed = 0;
	bool        _task_on      = false;

	std::vector<TaskLog> _log;

	gint64 _last_us  = 0;   // horloge monotone du dernier tick
	double _save_acc = 0;   // secondes écoulées depuis la dernière écriture
	bool   _blink    = false;

	sigc::connection _tick_conn;

	bool tick ();
	void update_label ();

	/* --- saisie directe de la durée ------------------------------------ */
	bool disp_button_press (GdkEventButton*);
	void begin_edit ();
	void commit_edit ();
	void cancel_edit ();
	bool entry_key_press (GdkEventKey*);
	bool entry_focus_out (GdkEventFocus*);

	bool bill_button_press (GdkEventButton*);
	void popup_menu (GdkEventButton*);

	void toggle_run ();
	void toggle_billing ();
	void cycle_mode ();
	void set_mode (Mode);
	void set_duration (double secs);
	void start_task (const std::string& name, double planned);
	void start_countdown (double seconds);   /* décompte direct, sans nommer de tâche */
	void new_task_dialog ();
	void reset_current ();
	void commit_task ();     // range la tâche courante dans le journal
	void reset_billing ();
	void show_history ();

	void load_state ();
	void session_going_away ();

	static std::string hms (double secs);
	/* "45" -> 45 min, "45:00" -> 45 min, "1:30:00" -> 1 h 30. <0 si illisible */
	static double parse_hms (const std::string&);
};
