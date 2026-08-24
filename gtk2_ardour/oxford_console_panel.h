/*
 * OxfordConsolePanel — panneau de contrôle Oxford ANCRÉ (bord droit du mixer),
 * 100% knobs ronds (TridentKnob), jamais de slider. Style Sonnox Oxford.
 *
 * Trois sous-vues commutées par 3 boutons [EQ] [DYNAMICS] [MASTER] (Notebook sans
 * onglets visibles). EQ + DYNAMICS suivent la piste sélectionnée ; MASTER pilote
 * le bus master. Suivi de sélection par timer (~150 ms). Tous les setters/getters
 * sont ceux de ARDOUR::OxfordChannel.
 */
#pragma once

#include <vector>
#include <functional>
#include <memory>

#include <ytkmm/box.h>
#include <ytkmm/eventbox.h>
#include <ytkmm/notebook.h>
#include <ytkmm/comboboxtext.h>
#include <ytkmm/drawingarea.h>
#include <ytkmm/scrolledwindow.h>
#include <ytkmm/label.h>

#include "widgets/ardour_button.h"
#include "widgets/ardour_dropdown.h"
#include "trident_knob.h"
#include "trident_meter.h"

namespace ARDOUR { class OxfordChannel; class Route; }

class OxfordConsolePanel : public Gtk::EventBox
{
public:
	OxfordConsolePanel ();
	~OxfordConsolePanel ();

	/* La section MONITOR d'Ardour est hébergée par la vue MASTER de ce panneau
	 * (le panneau ne sert à rien sur le master : on y met la régie). Appelé par
	 * Mixer_UI quand le bus Monitor apparaît / disparaît ; 0 = retirer. */
	void set_monitor_widget (Gtk::Widget* w);

private:
	Gtk::VBox _box;   // conteneur interne ; l'EventBox porte le fond clair global

private:
	struct Param {
		TridentKnob* knob = 0;
		bool master = false;                 // master (bus) sinon piste sélectionnée
		int  band   = -1;                    // >=0 : bande EQ (sinon -1)
		std::function<void(ARDOUR::OxfordChannel&, double)> set;
		std::function<double(ARDOUR::OxfordChannel&)>       get;
	};
	struct Toggle {
		ArdourWidgets::ArdourButton* btn = 0;
		bool master = false;
		std::function<void(ARDOUR::OxfordChannel&, bool)> set;
		std::function<bool(ARDOUR::OxfordChannel&)>       get;
	};

	std::vector<Param>  _params;
	std::vector<Toggle> _toggles;
	/* mode RETURN OXF-R3 : un bus sélectionné n'a que EQ 3 bandes (LF/MF/HF)
	 * + Gate/Comp (spec "Return Channels" du manuel). Ces widgets-là sont
	 * DÉTACHÉS du layout quand _isBus (hide() laissait un trou d'allocation
	 * dans le viewport -> on retire carrément du conteneur, ref gardée). */
	bool _isBus = false;
	struct TrackOnlyW { Gtk::Box* parent = 0; Gtk::Widget* w = 0; int pos = 0; bool attached = true; };
	std::vector<TrackOnlyW> _trackOnly;
	void reg_track_only (Gtk::Box* parent, Gtk::Widget* w);
	void set_bus_mode (bool bus);
	/* châssis aux couleurs du thème ACTIF, suivi en LIVE (ColorsChanged) :
	 * widgets enregistrés à la construction puis recolorés d'un bloc. */
	std::vector<Gtk::Widget*> _chromeMain, _chromeSub;
	std::vector<TridentKnob*> _knobsMain, _knobsSub;
	void fetch_theme_chrome ();
	void apply_chrome ();
	void theme_colors_changed ();
	std::shared_ptr<ARDOUR::OxfordChannel> _chan, _master;
	std::shared_ptr<ARDOUR::Route> _route;   // route de la piste sélectionnée (pan/width)
	TridentKnob* _pan_knob   = 0;
	TridentKnob* _width_knob = 0;
	Cairo::RefPtr<Cairo::ImageSurface> _kimg[6];   // skins knobs : 0=LF/bleu 1=LMF/vert 2=MF/rouge 3=HMF/orange 4=HF/jaune 5=neutre
	Cairo::RefPtr<Cairo::ImageSurface> load_knob_image (const char* file);
	bool _updating = false;
	/* contexte de section courant pendant la CONSTRUCTION des vues ("EQ",
	 * "GATE", "COMPRESSOR"...) : sert à choisir l'infobulle dans oxford_tip () */
	const char* _tipctx = 0;
	sigc::connection _timer;

	Gtk::Notebook _nb;
	Gtk::DrawingArea _eq_curve;
	Gtk::DrawingArea _eq_curveB;     // meme courbe, page EQ des BUS
	Gtk::DrawingArea _dyn_curve;
	Gtk::DrawingArea _dyn_curveB;    // meme graphe, page dynamique des BUS
	Gtk::DrawingArea _dyn_metersB;   // VU de GR, page dynamique des BUS
	Gtk::Notebook _dyn_nb;
	Gtk::ComboBoxText _dyn_sel;
	ArdourWidgets::ArdourButton _btn_eq, _btn_dyn, _btn_master;
	ArdourWidgets::ArdourDropdown _curve_btn;   // menu déroulant : 4 types de courbe + GML
	ArdourWidgets::ArdourDropdown _curve_btnB;  // le meme, page EQ des BUS
	ArdourWidgets::ArdourButton _timing_btn;    // cycle 3 lois de timing
	ArdourWidgets::ArdourDropdown _hp_slope_btn, _lp_slope_btn;  // menus : off/6/12/.../36 dB-oct
	TridentMeter* _gr_meter = 0;
	TridentMeter* _gate_meter = 0;
	TridentMeter* _exp_meter = 0;
	TridentMeter* _lim_meter = 0;
	Gtk::DrawingArea _dyn_meters;
	float _grVals[4] = {0,0,0,0};
	float _grPeak[4] = {0,0,0,0};   // peak-hold des VU de GR (décroissance lente)
	bool on_dyn_meters_expose (GdkEventExpose*);
	Gtk::Label _sel_label;

	// helpers de construction
	TridentKnob* mk (double lo, double hi, double def, const char* lab,
	                 std::function<std::string(double)> fmt, bool master, int band,
	                 std::function<void(ARDOUR::OxfordChannel&, double)> set,
	                 std::function<double(ARDOUR::OxfordChannel&)> get,
	                 double cr=0.24, double cg=0.45, double cb=0.60);
	ArdourWidgets::ArdourButton* mkToggle (Gtk::Box& box, const char* lab, bool master,
	                 std::function<void(ARDOUR::OxfordChannel&, bool)> set,
	                 std::function<bool(ARDOUR::OxfordChannel&)> get,
	                 const char* capcol = "#1a2433");
	Gtk::Widget* wrap_scroll (Gtk::Widget* w, bool hscroll = false);
	Gtk::Widget* subpanel (Gtk::Widget* w);   // tuile bleu clair (bi-ton) sur châssis crème
	bool on_panel_expose (GdkEventExpose*);
	bool on_outlim_expose (GdkEventExpose*);
	bool on_dyn_exposeB (GdkEventExpose*);
	bool on_dyn_metersB (GdkEventExpose*);
	bool dyn_curve_draw (Gtk::DrawingArea&);
	bool dyn_meters_draw (Gtk::DrawingArea&);
	/* courbe EQ ACTIVE (piste ou bus) : une seule page est visible a la fois */
	Gtk::DrawingArea& eqc () { return _isBus ? _eq_curveB : _eq_curve; }
	bool on_eq_expose (GdkEventExpose*);

	/* pastilles = HANDLES draggables sur la courbe EQ (façon plugin) :
	 * 0..4 = bandes (drag = freq+gain, molette = Q / overshoot en shelf),
	 * 5 = HPF, 6 = LPF (drag = freq + engage, molette = pente 6..36, <6 = off) */
	int    _eq_drag = -1;                       // handle en cours de drag (-1 = aucun)
	double _eq_scale = 20.0;                    // demi-échelle verticale AUTO de l'écran EQ (dB)
	double _eq_dotx[7] = {0,0,0,0,0,0,0};       // position des pastilles (maj à l'expose)
	double _eq_doty[7] = {0,0,0,0,0,0,0};
	bool   _eq_dotv[7] = {false,false,false,false,false,false,false};
	int  eq_dot_at (double x, double y) const;  // hit-test (-1 si aucune)
	bool on_eq_press   (GdkEventButton*);
	bool on_eq_release (GdkEventButton*);
	bool on_eq_motion  (GdkEventMotion*);
	bool on_eq_scroll  (GdkEventScroll*);
	bool on_dyn_expose (GdkEventExpose*);
	Gtk::Widget* build_eq (bool bus);
	Gtk::Widget* build_dyn (bool bus);
	Gtk::Widget* build_master ();   // MONITOR + PCM-1630 + limiteur post-PCM (vue master)
	void show_view (int page);

	/* vue MASTER : accueille la section monitor d'Ardour + la régie de sortie */
	Gtk::VBox   _mon_slot;            // conteneur d'accueil du tearoff monitor
	Gtk::Label  _mon_note;            // message quand il n'y a pas de bus Monitor
	ArdourWidgets::ArdourButton _mon_create_btn;   // "Créer le bus Monitor"
	Gtk::Widget* _mon_widget = 0;     // tearoff hébergé (non possédé)
	Gtk::Label  _pcmlim_gr;           // réduction du limiteur post-PCM (texte)
	Gtk::Widget* _panwid_box = 0;     // bloc Pan/Width (masque sur le master)
	Gtk::DrawingArea _outlim_bars;    // bargraphs GR + Recon du limiteur de sortie
	float _outlimGr { 0.f }, _outlimTp { -120.f }, _outlimCeil { -0.3f };

	bool _masterSelected = false;     // la tranche master est sélectionnée
	int  _lastTrackPage  = 0;         // page à restaurer en revenant sur une piste

	bool on_timer ();
	void refresh ();
	std::shared_ptr<ARDOUR::OxfordChannel> selected_channel ();
	std::shared_ptr<ARDOUR::OxfordChannel> master_channel ();

	/* Link boutons : applique fn au canal courant ; si Alt enfoncé, aussi à tous les
	 * canaux Oxford des pistes sélectionnées (même logique que le link des potards). */
	bool alt_down () const;
	void apply_linked (std::function<void(ARDOUR::OxfordChannel&)> fn);
};
