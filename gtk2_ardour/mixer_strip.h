/*
 * Copyright (C) 2005-2006 Taybin Rutkin <taybin@taybin.com>
 * Copyright (C) 2005-2017 Paul Davis <paul@linuxaudiosystems.com>
 * Copyright (C) 2006-2011 David Robillard <d@drobilla.net>
 * Copyright (C) 2006 Nick Mainsbridge <mainsbridge@gmail.com>
 * Copyright (C) 2007-2011 Carl Hetherington <carl@carlh.net>
 * Copyright (C) 2013-2019 Robin Gareus <robin@gareus.org>
 * Copyright (C) 2014-2017 Ben Loftis <ben@harrisonconsoles.com>
 * Copyright (C) 2016-2017 Julien "_FrnchFrgg_" RIVAUD <frnchfrgg@free.fr>
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

#ifndef __ardour_mixer_strip__
#define __ardour_mixer_strip__

#include <vector>

#include <cmath>

#include <ytkmm/adjustment.h>
#include <ytkmm/button.h>
#include <ytkmm/box.h>
#include <ytkmm/eventbox.h>
#include <ytkmm/frame.h>
#include <ytkmm/label.h>
#include <ytkmm/menu.h>
#include <ytkmm/sizegroup.h>
#include <ytkmm/textview.h>
#include <ytkmm/togglebutton.h>

#include "pbd/stateful.h"

#include "ardour/types.h"
#include "ardour/ardour.h"
#include "ardour/processor.h"
#include "ardour/oxford_channel.h"

#include "trident_knob.h"
#include "trident_meter.h"
#include "trident_divider.h"
#include "trident_round_button.h"

#include "pbd/fastlog.h"

#include "widgets/ardour_button.h"
#include "widgets/ardour_knob.h"

#include "axis_view.h"
#include "control_slave_ui.h"
#include "io_button.h"
#include "route_ui.h"
#include "gain_meter.h"
#include "panner_ui.h"
#include "enums.h"
#include "processor_box.h"
#include "triggerbox_ui.h"
#include "trigger_master.h"
#include "visibility_group.h"

namespace ARDOUR {
	class Route;
	class Send;
	class Processor;
	class Session;
	class PortInsert;
	class Bundle;
	class Plugin;
	class TriggerBox;
}
namespace Gtk {
	class Window;
	class Style;
}

class Mixer_UI;
class MotionController;
class RouteGroupMenu;
class ArdourWindow;
class AutomationController;
class TriggerBoxWidget;

class MixerStrip : public AxisView, public RouteUI, public Gtk::EventBox
{
public:
	MixerStrip (Mixer_UI&, ARDOUR::Session*, std::shared_ptr<ARDOUR::Route>, bool in_mixer = true);
	MixerStrip (Mixer_UI&, ARDOUR::Session*, bool in_mixer = true);
	~MixerStrip ();

	std::string name()  const;
	Gdk::Color color () const;
	bool marked_for_display () const;
	bool set_marked_for_display (bool);

	std::shared_ptr<ARDOUR::Stripable> stripable() const { return RouteUI::stripable(); }

	void set_width_enum (Width, void* owner);
	Width get_width_enum () const { return _width; }
	void* width_owner () const { return _width_owner; }

	GainMeter&      gain_meter()      { return gpm; }
	PannerUI&       panner_ui()       { return panners; }
	PluginSelector* plugin_selector();

	void fast_update ();
	void set_embedded (bool);

	void set_route (std::shared_ptr<ARDOUR::Route>);
	void set_button_names ();
	void show_send (std::shared_ptr<ARDOUR::Send>);
	void revert_to_default_display ();

	/** @return the delivery that is being edited using our fader; it will be the
	 *  last send passed to \ref show_send() , or our route's main out delivery.
	 */
	std::shared_ptr<ARDOUR::Delivery> current_delivery () const {
		return _current_delivery;
	}

	bool mixer_owned () const {
		return _mixer_owned;
	}

	/* used for screenshots */
	void hide_master_spacer (bool);

	void hide_things ();

	sigc::signal<void> WidthChanged;

	/** The delivery that we are handling the level for with our fader has changed */
	PBD::Signal<void(std::weak_ptr<ARDOUR::Delivery> )> DeliveryChanged;

	static PBD::Signal<void(MixerStrip*)> CatchDeletion;

	std::string state_id() const;

	void parameter_changed (std::string);
	void route_active_changed ();

	void copy_processors ();
	void cut_processors ();
	void paste_processors ();
	void select_all_processors ();
	void deselect_all_processors ();
	bool delete_processors ();  //note: returns false if nothing was deleted
	void toggle_processors ();
	void ab_plugins ();

	void set_selected (bool yn);

	void set_trigger_display (std::shared_ptr<ARDOUR::TriggerBox>);

	static MixerStrip* entered_mixer_strip() { return _entered_mixer_strip; }

protected:
	friend class Mixer_UI;
	void set_packed (bool yn);
	bool packed () { return _packed; }

	void set_stuff_from_route ();

private:
	Mixer_UI& _mixer;

	bool on_strip_expose (GdkEventExpose*);
	void init ();

	bool  _embedded;
	bool  _packed;
	bool  _mixer_owned;
	Width _width;
	void*  _width_owner;

	static int _scrollbar_spacer_height;

	ArdourWidgets::ArdourButton hide_button;
	ArdourWidgets::ArdourButton width_button;
	ArdourWidgets::ArdourButton number_label;
	Gtk::HBox                   width_hide_box;
	Gtk::EventBox               spacer;

	void hide_clicked();
	bool width_button_pressed (GdkEventButton *);

	Gtk::Frame          global_frame;
	Gtk::VBox           global_vpacker;

	ProcessorBox processor_box;
	GainMeter    gpm;
	PannerUI     panners;

	Glib::RefPtr<Gtk::SizeGroup> button_size_group;

	Gtk::Table rec_mon_table;
	Gtk::Table solo_iso_table;
	Gtk::Table mute_solo_table;
	Gtk::Table master_volume_table;
	Gtk::Table bottom_button_table;

	void vca_assign (std::shared_ptr<ARDOUR::VCA>);
	void vca_unassign (std::shared_ptr<ARDOUR::VCA>);

	void meter_changed ();
	void monitor_changed ();
	void monitor_section_added_or_removed ();

	IOButton input_button;
	IOButton output_button;

	ArdourWidgets::ArdourButton* monitor_section_button;

	void comment_button_resized (Gtk::Allocation&);

	ArdourWidgets::ArdourButton midi_input_enable_button;
	Gtk::HBox input_button_box;

	std::string longest_label;

	void midi_input_status_changed ();
	bool input_active_button_press (GdkEventButton*);
	bool input_active_button_release (GdkEventButton*);

	gint    mark_update_safe ();
	guint32 mode_switch_in_progress;

	/*Trigger widget*/
	FittedCanvasWidget _tmaster_widget;
	TriggerMaster*     _tmaster;

	ArdourWidgets::ArdourButton name_button;
	ArdourWidgets::ArdourButton _comment_button;
	Gtk::EventBox               _color_band;   // bande fine couleur de piste, pied de tranche (identité Oxford)
	ArdourWidgets::ArdourKnob   trim_control;

	Gtk::Menu* _master_volume_menu;
	ArdourWidgets::ArdourButton* _loudess_analysis_button;
	std::shared_ptr<AutomationController> _volume_controller;

	void setup_comment_button ();

	void loudess_analysis_button_clicked ();
	bool volume_controller_button_pressed (GdkEventButton*);

	ArdourWidgets::ArdourButton group_button;
	RouteGroupMenu*             group_menu;

	/* --- TRIDENT : section inline, sélecteur 3 panneaux (EQ / Comp / Gate) --- */
	Gtk::EventBox                      _trident_bg;   // fond de panneau (couleur console)
	Gtk::VBox                          _trident_box;
	TridentDivider                     _div_ge, _div_ec;       // pistes : Gate|EQ, EQ|Comp
	TridentDivider                     _div_bc, _div_ct;       // bus : Comp|Tape (+ Tape|Lim)
	Gtk::HBox                          _trident_sel_box;   // sélecteur (radio)
	ArdourWidgets::ArdourButton        _trident_gate_button;
	ArdourWidgets::ArdourButton        _trident_eq_button;
	ArdourWidgets::ArdourButton        _trident_comp_button;
	/* panneau EQ : gain + fréquence par bande */
	Gtk::VBox                          _trident_eq_panel;
	ArdourWidgets::ArdourButton        _eq_on_button;
	TridentRoundButton                 _hpf_round;         // passe-haut 50 Hz (petit bouton rond, entre les Low)
	Gtk::Table                         _eq_knob_table;
	Gtk::HBox                          _eq_opt_box;        // type de courbe + bascule shelf
	ArdourWidgets::ArdourButton        _eq_curve_btn;      // 4 types de courbe (cycle)
	ArdourWidgets::ArdourButton        _eq_lf_shelf_btn, _eq_hf_shelf_btn;  // LF/HF bell<->shelf
	/* EQ OXFORD : 5 bandes (LF/LMF/MF/HMF/HF), chacune Gain + Freq(continue) + Q */
	TridentKnob *_tk_eq_lf{0},  *_tk_eq_lmf{0},  *_tk_eq_mf{0},  *_tk_eq_hmf{0},  *_tk_eq_hf{0};   // gains
	TridentKnob *_tk_eqf_lf{0}, *_tk_eqf_lmf{0}, *_tk_eqf_mf{0}, *_tk_eqf_hmf{0}, *_tk_eqf_hf{0};  // freqs
	TridentKnob *_tk_eqq_lf{0}, *_tk_eqq_lmf{0}, *_tk_eqq_mf{0}, *_tk_eqq_hmf{0}, *_tk_eqq_hf{0};  // Q
	/* panneau Comp */
	Gtk::VBox                          _trident_comp_panel;
	ArdourWidgets::ArdourButton        _comp_on_button;
	Gtk::Table                         _comp_knobs;
	TridentKnob *_tk_cmp_thr{0}, *_tk_cmp_ratio{0}, *_tk_cmp_makeup{0};
	/* panneau Gate */
	Gtk::VBox                          _trident_gate_panel;
	ArdourWidgets::ArdourButton        _gate_on_button;
	Gtk::HBox                          _gate_knobs;
	TridentKnob *_tk_gate_thr{0}, *_tk_gate_range{0};
	/* GR meter (mode Strip, à côté des potards comp) */
	TridentMeter *_tk_gr{0};
	/* panneau Bus/Master : VU (haut) + Comp (att/rel) + Drive + Tape */
	Gtk::VBox                          _trident_bus_panel;
	Gtk::Label                         _trident_logo;          // "TRIDENT 80B" (master)
	TridentMeter *_tk_tape_vu{0};                 // VU à aiguille, en haut
	ArdourWidgets::ArdourButton        _bus_comp_button;   // enable comp bus/master
	Gtk::Table                         _bus_comp_knobs;
	TridentKnob *_tk_bcmp_thr{0}, *_tk_bcmp_ratio{0}, *_tk_bcmp_att{0}, *_tk_bcmp_rel{0}, *_tk_bcmp_mkp{0};
	TridentMeter *_tk_bus_gr{0};                   // GR du comp bus
	ArdourWidgets::ArdourButton        _tape_on_button;
	ArdourWidgets::ArdourButton        _tape_speed_button;   // sélecteur 15/30 ips
	Gtk::HBox                          _bus_knobs;
	TridentKnob *_tk_bus_drive{0};
	/* Limiter brickwall (master uniquement) */
	ArdourWidgets::ArdourButton        _limiter_button;
	TridentKnob *_tk_lim_ceil{0};
	TridentMeter *_tk_lim_gr{0};        // GR du limiteur (master)
	/* Trim (pré-tout) + Pan en potards (à la place du panner natif) */
	Gtk::EventBox                      _trident_pan_bg;  // fond assorti au bloc Trident
	Gtk::HBox                          _trident_pan_box;
	TridentKnob *_tk_pan{0};
	TridentKnob *_tk_trim{0};
	TridentKnob *_tk_width{0};   // largeur stéréo (pistes stéréo uniquement)

	/* --- Link sur sélection (Alt) : registre id->potard pour diffuser une modif
	 *     aux tranches sélectionnées (relatif sur potards, absolu sur toggles). --- */
	TridentKnob *_tk_link[32] {};
	void trident_link_setup ();                 // branche on_linked sur tous les potards
	void trident_link_knob (int id, double delta);
	void trident_apply_power (int which, bool on);
	bool trident_get_power (int which);
	bool trident_link_alt () const;             // Alt enfoncé au dernier press de bouton Trident ?
	bool trident_btn_press (GdkEventButton*);   // capte l'état modificateur au press du bouton
	guint _trident_click_state { 0 };

	/* --- En-têtes de section : le bouton-nom REPLIE (visuel), la LED = ON/OFF.
	 *     Le repli ne coupe jamais le DSP. --- */
	Gtk::HBox                          _gate_hdr, _eq_hdr, _comp_hdr, _tape_hdr, _bus_comp_hdr, _lim_hdr;
	ArdourWidgets::ArdourButton        _gate_pwr, _eq_pwr, _comp_pwr, _tape_pwr, _bus_comp_pwr, _limiter_pwr;
	bool _gate_coll{false}, _eq_coll{false}, _comp_coll{false}, _tape_coll{false}, _buscomp_coll{false}, _lim_coll{false};
	void trident_collapse_toggle (int which); // replie/déplie l'affichage (pas de bypass)
	void trident_power_toggle (int which);    // on/off DSP (LED)
	void trident_hpf_toggle ();               // passe-haut 50 Hz on/off
	void trident_eq_curve_cycle ();           // cycle les 4 types de courbe EQ Oxford
	void trident_eq_shelf_toggle (int which); // 0=LF 1=HF : bascule bell<->shelf
	static const char* eq_curve_name (int t); // nom court d'un type de courbe
	void trident_tape_speed_toggle ();        // bascule vitesse bande 15 <-> 30 ips

	std::shared_ptr<ARDOUR::OxfordChannel> _oxford;
	std::shared_ptr<ARDOUR::OxfordChannel> _oxford_tail; // master : tape+limiter en aval
	std::shared_ptr<ARDOUR::OxfordChannel> trident_tail_target () { return _oxford_tail ? _oxford_tail : _oxford; }
	int  _trident_panel { 1 };          // 0=gate 1=eq 2=comp (défaut EQ)
	void trident_build ();
	void trident_setup ();
	void trident_pan_setup ();              // pan en potard (lecture/affichage)
	void trident_apply_bg (double r, double g, double b); // couleur de fond (master vs piste/bus)
	void trident_show_panel (int which);    // sélection (radio) du panneau visible
	void trident_enable_toggle (int which); // on/off DSP d'une section
	void trident_update_buttons ();
	void trident_update_section_visibility ();  // replie les potards des sections éteintes
	bool trident_meter_update ();            // poll GR / VU (timer)
	sigc::connection _trident_meter_conn;

	void io_changed_proxy ();

	Gtk::Menu *send_action_menu;
	void build_send_action_menu ();

	PBD::ScopedConnection panstate_connection;
	PBD::ScopedConnection panstyle_connection;
	void connect_to_pan ();
	void update_panner_choices ();
	void update_trim_control ();

	void update_diskstream_display ();
	void update_input_display ();
	void update_output_display ();

	void set_automated_controls_sensitivity (bool yn);

	Gtk::Menu* route_ops_menu;
	void build_route_ops_menu ();
	gboolean name_button_button_press (GdkEventButton*);
	gboolean number_button_button_press (GdkEventButton*);

	/* OXFORD : glisser la tranche par son bouton de nom pour la déplacer
	 * (le déplacement effectif est piloté par Mixer_UI::*_strip_drag). */
	bool     name_button_motion (GdkEventMotion*);
	gboolean name_button_button_release (GdkEventButton*);
	double   _name_drag_x0 = 0;
	bool     _name_drag_armed = false;
	void list_route_operations ();

	bool select_route_group (GdkEventButton *);
	void route_group_changed ();

	Gtk::Style *passthru_style;

	void route_color_changed ();
	void show_passthru_color ();

	void route_property_changed (const PBD::PropertyChange&);
	void name_button_resized (Gtk::Allocation&);
	void name_changed ();
	void dpi_reset ();
	void update_spacer ();
	void update_speed_display ();
	void map_frozen ();
	void hide_processor_editor (std::weak_ptr<ARDOUR::Processor> processor);
	void hide_redirect_editors ();

	bool ignore_speed_adjustment;

	static MixerStrip* _entered_mixer_strip;

	virtual void bus_send_display_changed (std::shared_ptr<ARDOUR::Route>);

	void set_current_delivery (std::shared_ptr<ARDOUR::Delivery>);

	void drop_send ();
	PBD::ScopedConnection send_gone_connection;

	void reset_strip_style ();
	void update_sensitivity ();

	bool mixer_strip_enter_event ( GdkEventCrossing * );
	bool mixer_strip_leave_event ( GdkEventCrossing * );

	/** A VisibilityGroup to manage the visibility of some of our controls.
	 *  We fill it with the controls that are being managed, using the same names
	 *  as those used with _mixer_strip_visibility in RCOptionEditor.  Then
	 *  this VisibilityGroup is configured by changes to the RC variable
	 *  mixer-element-visibility, which happen when the user makes changes in
	 *  the RC option editor.
	 */
	VisibilityGroup _visibility;
	std::optional<bool> override_solo_visibility () const;
	std::optional<bool> override_rec_mon_visibility () const;

	PBD::ScopedConnectionList _config_connection;

	void add_input_port (ARDOUR::DataType);
	void add_output_port (ARDOUR::DataType);

	bool _suspend_menu_callbacks;
	bool level_meter_button_press (GdkEventButton *);
	void popup_level_meter_menu (GdkEventButton *);
	void add_level_meter_item_point (Gtk::Menu_Helpers::MenuList &, Gtk::RadioMenuItem::Group &, std::string const &, ARDOUR::MeterPoint);
	void add_level_meter_item_type (Gtk::Menu_Helpers::MenuList &, Gtk::RadioMenuItem::Group &, std::string const &, ARDOUR::MeterType);
	void set_meter_point (ARDOUR::MeterPoint);
	void set_meter_type (ARDOUR::MeterType);
	PBD::ScopedConnection _level_meter_connection;

	std::string meter_point_string (ARDOUR::MeterPoint);

	void update_track_number_visibility ();

	ControlSlaveUI control_slave_ui;
};

#endif /* __ardour_mixer_strip__ */
