/*
 * Copyright (C) 2005-2006 Nick Mainsbridge <mainsbridge@gmail.com>
 * Copyright (C) 2005-2006 Taybin Rutkin <taybin@taybin.com>
 * Copyright (C) 2005-2007 Doug McLain <doug@nostar.net>
 * Copyright (C) 2005-2017 Paul Davis <paul@linuxaudiosystems.com>
 * Copyright (C) 2006-2014 David Robillard <d@drobilla.net>
 * Copyright (C) 2007-2012 Carl Hetherington <carl@carlh.net>
 * Copyright (C) 2008 Hans Baier <hansfbaier@googlemail.com>
 * Copyright (C) 2009-2010 Sakari Bergen <sakari.bergen@beatwaves.net>
 * Copyright (C) 2012-2019 Robin Gareus <robin@gareus.org>
 * Copyright (C) 2013-2016 John Emmas <john@creativepost.co.uk>
 * Copyright (C) 2014-2018 Ben Loftis <ben@harrisonconsoles.com>
 * Copyright (C) 2015-2016 Tim Mayberry <mojofunk@gmail.com>
 * Copyright (C) 2016-2017 Julien "_FrnchFrgg_" RIVAUD <frnchfrgg@free.fr>
 * Copyright (C) 2018 Len Ovens <len@ovenwerks.net>
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

#include <cmath>
#include <list>
#include <algorithm>

#include <sigc++/bind.h>

#include <ytkmm/messagedialog.h>

#include "pbd/convert.h"
#include "pbd/enumwriter.h"
#include "pbd/replace_all.h"
#include "pbd/unwind.h"

#include "ardour/amp.h"
#include "ardour/dB.h"
#include "ardour/async_midi_port.h"
#include "ardour/audio_track.h"
#include "ardour/audioengine.h"
#include "ardour/internal_send.h"
#include "ardour/io.h"
#include "ardour/meter.h"
#include "ardour/midi_track.h"
#include "ardour/pannable.h"
#include "ardour/panner.h"
#include "ardour/panner_shell.h"
#include "ardour/panner_manager.h"
#include "ardour/port.h"
#include "ardour/profile.h"
#include "ardour/route.h"
#include "ardour/route_group.h"
#include "ardour/send.h"
#include "ardour/selection.h"
#include "ardour/session.h"
#include "ardour/types.h"
#include "ardour/user_bundle.h"
#include "ardour/vca.h"
#include "ardour/vca_manager.h"

#include "gtkmm2ext/colors.h"
#include "gtkmm2ext/gtk_ui.h"
#include "gtkmm2ext/menu_elems.h"
#include "gtkmm2ext/utils.h"
#include "gtkmm2ext/doi.h"

#include "widgets/tooltips.h"

#include "ardour_ui.h"
#include "ardour_window.h"
#include "automation_controller.h"
#include "context_menu_helper.h"
#include "enums_convert.h"
#include "mixer_strip.h"
#include "mixer_ui.h"
#include "keyboard.h"
#include "public_editor.h"
#include "send_ui.h"
#include "io_selector.h"
#include "utils.h"
#include "gui_thread.h"
#include "route_group_menu.h"
#include "meter_patterns.h"
#include "rta_manager.h"
#include "ui_config.h"
#include "triggerbox_ui.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace ArdourWidgets;
using namespace PBD;
using namespace Gtk;
using namespace Gtkmm2ext;
using namespace std;
using namespace ArdourMeter;

MixerStrip* MixerStrip::_entered_mixer_strip;
PBD::Signal<void(MixerStrip*)> MixerStrip::CatchDeletion;
int MixerStrip::_scrollbar_spacer_height = 0;

#define PX_SCALE(px) std::max((float)px, rintf((float)px * UIConfiguration::instance().get_ui_scale()))

MixerStrip::MixerStrip (Mixer_UI& mx, Session* sess, bool in_mixer)
	: SessionHandlePtr (sess)
	, RouteUI (sess)
	, _mixer(mx)
	, _mixer_owned (in_mixer)
	, processor_box (sess, std::bind (&MixerStrip::plugin_selector, this), mx.selection(), this, in_mixer)
	, gpm (sess, 250)
	, panners (sess)
	, button_size_group (Gtk::SizeGroup::create (Gtk::SIZE_GROUP_HORIZONTAL))
	, rec_mon_table (2, 2)
	, solo_iso_table (1, 2)
	, mute_solo_table (1, 2)
	, master_volume_table (2, 2)
	, bottom_button_table (2, 2)
	, input_button (true)
	, output_button (false)
	, monitor_section_button (0)
	, _tmaster_widget (-1, 16)
	, _comment_button (_("Comments"))
	, trim_control (ArdourKnob::default_elements, ArdourKnob::Flags (ArdourKnob::Detent | ArdourKnob::ArcToZero))
	, _master_volume_menu (0)
	, _loudess_analysis_button (0)
	, _visibility (X_("mixer-element-visibility"))
	, _suspend_menu_callbacks (false)
	, control_slave_ui (sess)
{
	init ();

	if (!_mixer_owned) {
		/* the editor mixer strip: don't destroy it every time
		   the underlying route goes away.
		*/

		self_destruct = false;
	}
}

MixerStrip::MixerStrip (Mixer_UI& mx, Session* sess, std::shared_ptr<Route> rt, bool in_mixer)
	: SessionHandlePtr (sess)
	, RouteUI (sess)
	, _mixer(mx)
	, _mixer_owned (in_mixer)
	, processor_box (sess, std::bind (&MixerStrip::plugin_selector, this), mx.selection(), this, in_mixer)
	, gpm (sess, 250)
	, panners (sess)
	, button_size_group (Gtk::SizeGroup::create (Gtk::SIZE_GROUP_HORIZONTAL))
	, rec_mon_table (2, 2)
	, solo_iso_table (1, 2)
	, mute_solo_table (1, 2)
	, master_volume_table (1, 2)
	, bottom_button_table (2, 2)
	, input_button (true)
	, output_button (false)
	, monitor_section_button (0)
	, _tmaster_widget (-1, 16)
	, _comment_button (_("Comments"))
	, trim_control (ArdourKnob::default_elements, ArdourKnob::Flags (ArdourKnob::Detent | ArdourKnob::ArcToZero))
	, _master_volume_menu (0)
	, _loudess_analysis_button (0)
	, _visibility (X_("mixer-element-visibility"))
	, _suspend_menu_callbacks (false)
	, control_slave_ui (sess)
{
	init ();
	set_route (rt);

	if (is_master () && !_route->comment().empty () && _session->config.get_show_master_bus_comment_on_load () && self_destruct) {
		open_comment_editor ();
		/* show only once */
		_session->config.set_show_master_bus_comment_on_load (false);
	}
}

void
MixerStrip::init ()
{
	_entered_mixer_strip= 0;
	group_menu = 0;
	route_ops_menu = 0;
	_width_owner = 0;

	_tmaster = new TriggerMaster (_tmaster_widget.root ());

	/* the length of this string determines the width of the mixer strip when it is set to `wide' */
	longest_label = "longest label";

	string t = _("Click to toggle the width of this mixer strip.");
	if (_mixer_owned) {
		t += string_compose (_("\n%1-%2-click to toggle the width of all strips."), Keyboard::primary_modifier_name(), Keyboard::tertiary_modifier_name ());
	}

	width_button.set_icon (ArdourIcon::StripWidth);
	width_button.set_tweaks (ArdourButton::Square);
	set_tooltip (width_button, t);

	hide_button.set_icon (ArdourIcon::HideEye);
	hide_button.set_tweaks (ArdourButton::Square);
	set_tooltip (&hide_button, _("Hide this mixer strip"));

	input_button_box.set_spacing(2);
	input_button_box.pack_start (input_button, true, true);

	bottom_button_table.attach (gpm.meter_point_button, 1, 2, 0, 1);

	hide_button.set_events (hide_button.get_events() & ~(Gdk::ENTER_NOTIFY_MASK|Gdk::LEAVE_NOTIFY_MASK));

	solo_isolated_led = manage (new ArdourButton (ArdourButton::led_default_elements));
	solo_isolated_led->show ();
	solo_isolated_led->set_no_show_all (true);
	solo_isolated_led->set_name (X_("solo isolate"));
	solo_isolated_led->add_events (Gdk::BUTTON_PRESS_MASK|Gdk::BUTTON_RELEASE_MASK);
	solo_isolated_led->signal_button_release_event().connect (sigc::mem_fun (*this, &RouteUI::solo_isolate_button_release), false);
	UI::instance()->set_tip (solo_isolated_led, _("Isolate Solo"), "");

	solo_safe_led = manage (new ArdourButton (ArdourButton::led_default_elements));
	solo_safe_led->show ();
	solo_safe_led->set_no_show_all (true);
	solo_safe_led->set_name (X_("solo safe"));
	solo_safe_led->add_events (Gdk::BUTTON_PRESS_MASK|Gdk::BUTTON_RELEASE_MASK);
	solo_safe_led->signal_button_release_event().connect (sigc::mem_fun (*this, &RouteUI::solo_safe_button_release), false);
	UI::instance()->set_tip (solo_safe_led, _("Lock Solo Status"), "");

	solo_safe_led->set_text (S_("SoloLock|Lock"));
	solo_isolated_led->set_text (_("Iso"));

	solo_iso_table.set_homogeneous (true);
	solo_iso_table.set_spacings (2);
	solo_iso_table.attach (*solo_isolated_led, 0, 1, 0, 1);
	solo_iso_table.attach (*solo_safe_led, 1, 2, 0, 1);
	solo_iso_table.show ();

	rec_mon_table.set_homogeneous (true);
	rec_mon_table.set_row_spacings (2);
	rec_mon_table.set_col_spacings (2);
	if (ARDOUR::Profile->get_mixbus()) {
		rec_mon_table.resize (1, 3);
		rec_mon_table.attach (*monitor_input_button, 1, 2, 0, 1);
		rec_mon_table.attach (*monitor_disk_button, 2, 3, 0, 1);
	}
	rec_mon_table.show ();

	master_volume_table.set_homogeneous (true);
	master_volume_table.set_row_spacings (2);
	master_volume_table.set_col_spacings (2);

	if (solo_isolated_led) {
		button_size_group->add_widget (*solo_isolated_led);
	}
	if (solo_safe_led) {
		button_size_group->add_widget (*solo_safe_led);
	}

	if (!ARDOUR::Profile->get_mixbus()) {
		if (rec_enable_button) {
			button_size_group->add_widget (*rec_enable_button);
		}
		if (monitor_disk_button) {
			button_size_group->add_widget (*monitor_disk_button);
		}
		if (monitor_input_button) {
			button_size_group->add_widget (*monitor_input_button);
		}
	}

	mute_solo_table.set_homogeneous (true);
	mute_solo_table.set_spacings (2);

	bottom_button_table.set_spacings (2);
	bottom_button_table.set_homogeneous (true);
	bottom_button_table.attach (gpm.gain_automation_state_button, 0, 1, 0, 1);
	bottom_button_table.attach (group_button, 0, 1, 1, 2);
	bottom_button_table.attach (*rta_button,   1, 2, 1, 2);

	name_button.set_name ("mixer strip button");
	name_button.set_text_ellipsize (Pango::ELLIPSIZE_END);
	name_button.signal_size_allocate().connect (sigc::mem_fun (*this, &MixerStrip::name_button_resized));

	set_tooltip (&group_button, _("Mix group"));
	group_button.set_name ("mixer strip button");

	set_tooltip (rta_button, _("Realtime Analyzer\nLeft-click to toggle track analysis\nRight-click to toggle RTA window visibility"));
	rta_button->set_name ("mixer strip button");

	Gtk::Requisition mpb_size = gpm.meter_point_button.size_request();
	group_button.set_size_request (mpb_size.width, mpb_size.height);
	rta_button->set_size_request (mpb_size.width, mpb_size.height);

	_comment_button.set_name (X_("mixer strip button"));
	_comment_button.set_text_ellipsize (Pango::ELLIPSIZE_END);
	_comment_button.signal_clicked.connect (sigc::mem_fun (*this, &RouteUI::toggle_comment_editor));
	_comment_button.signal_size_allocate().connect (sigc::mem_fun (*this, &MixerStrip::comment_button_resized));

	trim_control.set_size_request (PX_SCALE(19), PX_SCALE(19));
	trim_control.set_tooltip_prefix (_("Trim: "));
	trim_control.set_name ("trim knob");
	trim_control.set_no_show_all (true);
	input_button_box.pack_start (trim_control, false, false);

	global_vpacker.set_no_show_all ();
	global_vpacker.set_border_width (1);
	global_vpacker.set_spacing (2);

	width_button.set_name ("mixer strip button");
	hide_button.set_name ("mixer strip button");

	width_button.signal_button_press_event().connect (sigc::mem_fun(*this, &MixerStrip::width_button_pressed), false);
	hide_button.signal_clicked.connect (sigc::mem_fun(*this, &MixerStrip::hide_clicked));

	width_hide_box.set_spacing (2);
	width_hide_box.pack_start (width_button, false, true);
	width_hide_box.pack_start (number_label, true, true);
	width_hide_box.pack_end (hide_button, false, true);

	number_label.set_text ("-");
	number_label.set_elements((ArdourButton::Element)(ArdourButton::Edge|ArdourButton::Body|ArdourButton::Text|ArdourButton::Inactive));
	number_label.set_no_show_all ();
	number_label.set_name ("tracknumber label");
	number_label.set_fixed_colors (0x80808080, 0x80808080);
	number_label.set_alignment (.5, .5);
	number_label.set_fallthrough_to_parent (true);
	number_label.set_tweaks (ArdourButton::OccasionalText);
	set_tooltip (&number_label, _("Double-click to edit the route color.\nRight-click to show the route operations context menu."));

	global_vpacker.pack_start (width_hide_box, Gtk::PACK_SHRINK);
	global_vpacker.pack_start (name_button, Gtk::PACK_SHRINK);
	global_vpacker.pack_start (input_button_box, Gtk::PACK_SHRINK);
	global_vpacker.pack_start (invert_button_box, Gtk::PACK_SHRINK);
	global_vpacker.pack_start (_tmaster_widget, Gtk::PACK_SHRINK);
	global_vpacker.pack_start (processor_box, true, true);

	trident_build ();
	_trident_bg.add (_trident_box);
	_trident_bg.set_border_width (3);
	_trident_bg.modify_bg (Gtk::STATE_NORMAL, Gdk::Color ("#3a3d42")); // Trident : fond de tranche (réf 80B)
	_trident_bg.show ();

	/* TRIDENT : on remplace le panner natif (barre L-R) par nos potards Trim+Pan,
	 * placés au même endroit, sur un fond assorti au bloc Trident. */
	_trident_pan_bg.add (_trident_pan_box);
	_trident_pan_bg.set_border_width (3);
	_trident_pan_bg.modify_bg (Gtk::STATE_NORMAL, Gdk::Color ("#3a3d42"));
	_trident_pan_bg.show ();
	global_vpacker.pack_start (rec_mon_table, Gtk::PACK_SHRINK);
	global_vpacker.pack_start (master_volume_table, Gtk::PACK_SHRINK);
	global_vpacker.pack_start (solo_iso_table, Gtk::PACK_SHRINK);
	global_vpacker.pack_start (mute_solo_table, Gtk::PACK_SHRINK);
	global_vpacker.pack_start (gpm, Gtk::PACK_SHRINK);
	global_vpacker.pack_start (control_slave_ui, Gtk::PACK_SHRINK);
	global_vpacker.pack_start (bottom_button_table, Gtk::PACK_SHRINK);
	global_vpacker.pack_start (output_button, Gtk::PACK_SHRINK);
	global_vpacker.pack_start (_comment_button, Gtk::PACK_SHRINK);

	midi_input_enable_button.set_size_request (PX_SCALE(19), PX_SCALE(19));
	midi_input_enable_button.set_name ("midi input button");
	midi_input_enable_button.set_elements ((ArdourButton::Element)(ArdourButton::Edge|ArdourButton::Body|ArdourButton::VectorIcon));
	midi_input_enable_button.set_icon (ArdourIcon::DinMidi);
	midi_input_enable_button.signal_button_press_event().connect (sigc::mem_fun (*this, &MixerStrip::input_active_button_press), false);
	midi_input_enable_button.signal_button_release_event().connect (sigc::mem_fun (*this, &MixerStrip::input_active_button_release), false);
	set_tooltip (midi_input_enable_button, _("Enable/Disable MIDI input"));

	update_spacer ();
	spacer.set_name ("AudioBusStripBase");

	/* pied de tranche : bande fine à la couleur de la piste (repère visuel
	 * façon consoles modernes — la couleur existe déjà, on la montre) */
	_color_band.set_size_request (-1, 6);
	global_vpacker.pack_end (_color_band, false, false);
#ifndef MIXBUS
	global_vpacker.pack_end (spacer, false, false);
#endif

	global_frame.add (global_vpacker);
	global_frame.set_shadow_type (Gtk::SHADOW_IN);
	global_frame.set_name ("BaseFrame");

	add (global_frame);

	/* force setting of visible selected status */

	_selected = true;
	set_selected (false);

	_packed = false;
	_embedded = false;

	number_label.signal_button_press_event().connect (sigc::mem_fun(*this, &MixerStrip::number_button_button_press), false);

	name_button.set_fallthrough_to_parent (true);
	name_button.signal_button_press_event().connect (sigc::mem_fun(*this, &MixerStrip::name_button_button_press), false);
	/* OXFORD : glisser le bouton de nom = déplacer la tranche dans le mixeur */
	name_button.add_events (Gdk::POINTER_MOTION_MASK | Gdk::BUTTON1_MOTION_MASK);
	name_button.signal_motion_notify_event().connect (sigc::mem_fun(*this, &MixerStrip::name_button_motion), false);
	name_button.signal_button_release_event().connect (sigc::mem_fun(*this, &MixerStrip::name_button_button_release), false);

	group_button.signal_button_press_event().connect (sigc::mem_fun(*this, &MixerStrip::select_route_group), false);

	_width = (Width) -1;

	if (is_midi_track()) {
		set_name ("MidiTrackStripBase");
	} else {
		set_name ("AudioTrackStripBase");
	}

	add_events (Gdk::BUTTON_RELEASE_MASK|
		    Gdk::ENTER_NOTIFY_MASK|
		    Gdk::LEAVE_NOTIFY_MASK|
		    Gdk::KEY_PRESS_MASK|
		    Gdk::KEY_RELEASE_MASK);

	set_can_focus ();

	/* Add the widgets under visibility control to the VisibilityGroup; the names used here
	   must be the same as those used in RCOptionEditor so that the configuration changes
	   are recognised when they occur.
	*/
	_visibility.add (&input_button_box, X_("Input"), _("Input"), false);
	_visibility.add (&invert_button_box, X_("PhaseInvert"), _("Phase Invert"), false);
	_visibility.add (&rec_mon_table, X_("RecMon"), _("Record & Monitor"), false, std::bind (&MixerStrip::override_rec_mon_visibility, this));
	_visibility.add (&solo_iso_table, X_("SoloIsoLock"), _("Solo Iso / Lock"), false);
	_visibility.add (&output_button, X_("Output"), _("Output"), false);
	_visibility.add (&_comment_button, X_("Comments"), _("Comments"), false);
	_visibility.add (&control_slave_ui, X_("VCA"), _("VCA Assigns"), false);
	_visibility.add (&_tmaster_widget, X_("TriggerMaster"), _("Trigger Master"), false);

	parameter_changed (X_("mixer-element-visibility"));
	UIConfiguration::instance().ParameterChanged.connect (sigc::mem_fun (*this, &MixerStrip::parameter_changed));
	UIConfiguration::instance().DPIReset.connect (sigc::mem_fun (*this, &MixerStrip::dpi_reset));
	 Config->ParameterChanged.connect (_config_connection, invalidator (*this), std::bind (&MixerStrip::parameter_changed, this, _1), gui_context());
	 _session->config.ParameterChanged.connect (_config_connection, invalidator (*this), std::bind (&MixerStrip::parameter_changed, this, _1), gui_context());

	//watch for mouse enter/exit so we can do some stuff
	signal_enter_notify_event().connect (sigc::mem_fun(*this, &MixerStrip::mixer_strip_enter_event ));
	signal_leave_notify_event().connect (sigc::mem_fun(*this, &MixerStrip::mixer_strip_leave_event ));

	gpm.LevelMeterButtonPress.connect_same_thread (_level_meter_connection, std::bind (&MixerStrip::level_meter_button_press, this, _1));
}

MixerStrip::~MixerStrip ()
{
	CatchDeletion (this);

	if (this ==_entered_mixer_strip) {
		_entered_mixer_strip = NULL;
	}

	delete _master_volume_menu;
	delete route_ops_menu;
}

void
MixerStrip::vca_assign (std::shared_ptr<ARDOUR::VCA> vca)
{
	std::shared_ptr<Slavable> sl = std::dynamic_pointer_cast<Slavable> (route());
	if (sl) {
		sl->assign(vca);
	}
}

void
MixerStrip::vca_unassign (std::shared_ptr<ARDOUR::VCA> vca)
{
	std::shared_ptr<Slavable> sl = std::dynamic_pointer_cast<Slavable> (route());
	if (sl) {
		sl->unassign(vca);
	}
}

bool
MixerStrip::mixer_strip_enter_event (GdkEventCrossing* /*ev*/)
{
	_entered_mixer_strip = this;

	return false;
}

bool
MixerStrip::mixer_strip_leave_event (GdkEventCrossing *ev)
{
	//if we have moved outside our strip, but not into a child view, then deselect ourselves
	if (ev->detail != GDK_NOTIFY_INFERIOR) {
		_entered_mixer_strip= 0;

		//clear keyboard focus in the gain display.  this is cheesy but fixes a longstanding "bug" where the user starts typing in the gain entry, and leaves it active, thereby prohibiting other keybindings from working
		gpm.gain_display.set_sensitive(false);
		gpm.show_gain();
		gpm.gain_display.set_sensitive(true);

		//if we leave this mixer strip we need to clear out any selections
		//processor_box.processor_display.select_none();  //but this doesn't work, because it gets triggered when (for example) you open the menu or start a drag
	}

	return false;
}

string
MixerStrip::name() const
{
	if (_route) {
		return _route->name();
	}
	return string();
}

void
MixerStrip::update_trim_control ()
{
	if (route()->trim() && route()->trim()->active() &&
	    route()->n_inputs().n_audio() > 0) {
		trim_control.show ();
		trim_control.set_controllable (route()->trim()->gain_control());
	} else {
		trim_control.hide ();
		std::shared_ptr<Controllable> none;
		trim_control.set_controllable (none);
	}
}

void
MixerStrip::set_route (std::shared_ptr<Route> rt)
{
	//the rec/monitor stuff only shows up for tracks.
	//the show_sends only shows up for buses.
	//remove them all here, and we may add them back later
	if (show_sends_button->get_parent()) {
		rec_mon_table.remove (*show_sends_button);
	}
	if (rec_enable_button->get_parent()) {
		rec_mon_table.remove (*rec_enable_button);
	}
	if (monitor_input_button->get_parent()) {
		monitor_input_button->get_parent()->remove (*monitor_input_button);
	}
	if (monitor_disk_button->get_parent()) {
		monitor_disk_button->get_parent()->remove (*monitor_disk_button);
	}
	if (group_button.get_parent()) {
		bottom_button_table.remove (group_button);
	}
	if (rta_button->get_parent()) {
		rta_button->get_parent()->remove (*rta_button);
	}

	RouteUI::set_route (rt);

	control_slave_ui.set_stripable (std::dynamic_pointer_cast<Stripable> (rt));

	/* ProcessorBox needs access to _route so that it can read
	   GUI object state.
	*/
	processor_box.set_route (rt);

	revert_to_default_display ();

	/* unpack these from the parent and stuff them into our own
	   table
	*/

	if (gpm.peak_display.get_parent()) {
		gpm.peak_display.get_parent()->remove (gpm.peak_display);
	}
	if (gpm.gain_display.get_parent()) {
		gpm.gain_display.get_parent()->remove (gpm.gain_display);
	}

	mute_solo_table.attach (gpm.gain_display,0,1,1,2, EXPAND|FILL, EXPAND);
	mute_solo_table.attach (gpm.peak_display,1,2,1,2, EXPAND|FILL, EXPAND);

	if (solo_button->get_parent()) {
		mute_solo_table.remove (*solo_button);
	}

	if (mute_button->get_parent()) {
		mute_solo_table.remove (*mute_button);
	}

	if (route()->is_master()) {
		solo_button->hide ();
		mute_button->show ();
		mute_solo_table.attach (*mute_button, 0, 2, 0, 1);
		bottom_button_table.attach (*rta_button,   1, 2, 1, 2);
		if (Config->get_use_master_volume ()) {
			master_volume_table.show ();
		}

		if (monitor_section_button == 0 && _mixer_owned) {
			Glib::RefPtr<Action> act = ActionManager::get_action ("Mixer", "ToggleMonitorSection");
			_session->MonitorChanged.connect (route_connections, invalidator (*this), std::bind (&MixerStrip::monitor_changed, this), gui_context());
			_session->MonitorBusAddedOrRemoved.connect (route_connections, invalidator (*this), std::bind (&MixerStrip::monitor_section_added_or_removed, this), gui_context());

			monitor_section_button = manage (new ArdourButton);
			monitor_changed ();
			monitor_section_button->set_related_action (act);
			set_tooltip (monitor_section_button, _("Show/Hide Monitoring Section"));
			monitor_section_button->set_can_focus (false);
			monitor_section_added_or_removed ();
		}
	} else if (route()->is_surround_master()) {
		mute_solo_table.attach (*mute_button, 0, 2, 0, 1);
		mute_button->show ();
		master_volume_table.hide ();
	} else {
		bottom_button_table.attach (group_button, 0, 1, 1, 2);
		bottom_button_table.attach (*rta_button,   1, 2, 1, 2);
		mute_solo_table.attach (*mute_button, 0, 1, 0, 1);
		mute_solo_table.attach (*solo_button, 1, 2, 0, 1);
		mute_button->show ();
		solo_button->show ();
		master_volume_table.hide ();
	}

	if (route()->is_master() && _volume_controller == 0) {
		assert (_loudess_analysis_button == 0);
		assert (route()->volume_control());
		std::shared_ptr<AutomationControl> ac = route()->volume_control ();

		_volume_controller = AutomationController::create (ac->parameter (), ParameterDescriptor (ac->parameter ()), ac, false);
		_volume_controller->set_name (X_("ProcessorControlSlider"));
		_volume_controller->set_size_request (PX_SCALE(19), -1);
		_volume_controller->disable_vertical_scroll ();

		_loudess_analysis_button = manage (new ArdourButton (S_("Loudness|LAN")));
		_loudess_analysis_button->signal_clicked.connect (mem_fun (*this, &MixerStrip::loudess_analysis_button_clicked));
		_volume_controller->signal_button_press_event().connect (mem_fun (*this, &MixerStrip::volume_controller_button_pressed), false);

		set_tooltip (*_volume_controller, _("Master output volume"));
		set_tooltip (_loudess_analysis_button, _("Measure loudness of the session, normalize master output volume"));

		master_volume_table.attach (*_loudess_analysis_button, 0, 2, 0, 1);
		master_volume_table.attach (*_volume_controller, 0, 2, 1, 2);

		_loudess_analysis_button->show ();
		_volume_controller->show ();
		if (Config->get_use_master_volume ()) {
			master_volume_table.show ();
		}
#ifdef MIXBUS
	} else if (!route()->is_master()) {
		/* mixbus has/had a show_all, empty table still adds some pixel padding */
		master_volume_table.hide ();
#endif
	}

	hide_master_spacer (false);

	if (is_track()) {
		_tmaster_widget.show ();
		monitor_input_button->show ();
		monitor_disk_button->show ();
	} else {
		_tmaster_widget.hide ();
		monitor_input_button->hide();
		monitor_disk_button->hide ();
	}

	update_trim_control();

	if (is_midi_track()) {
		if (!midi_input_enable_button.get_parent()) {
			input_button_box.pack_start (midi_input_enable_button, false, false);
		}

		/* get current state */
		midi_input_status_changed ();

		/* follow changes */
		midi_track()->InputActiveChanged.connect (route_connections, invalidator (*this), std::bind (&MixerStrip::midi_input_status_changed, this), gui_context());
	} else {
		if (midi_input_enable_button.get_parent()) {
			input_button_box.remove (midi_input_enable_button);
		}
	}

	if (is_audio_track()) {
		std::shared_ptr<AudioTrack> at = audio_track();
		at->FreezeChange.connect (route_connections, invalidator (*this), std::bind (&MixerStrip::map_frozen, this), gui_context());
	}

	if (is_track ()) {

		rec_mon_table.attach (*rec_enable_button, 0, 1, 0, ARDOUR::Profile->get_mixbus() ? 1 : 2);
		rec_enable_button->show();

		if (ARDOUR::Profile->get_mixbus()) {
			rec_mon_table.attach (*monitor_input_button, 1, 2, 0, 1);
			rec_mon_table.attach (*monitor_disk_button, 2, 3, 0, 1);
		} else {
			rec_mon_table.attach (*monitor_input_button, 1, 2, 0, 1);
			rec_mon_table.attach (*monitor_disk_button, 1, 2, 1, 2);
		}

	} else {

		/* non-master bus */

		if (!_route->is_main_bus ()) {
			if (ARDOUR::Profile->get_mixbus()) {
				rec_mon_table.attach (*show_sends_button, 0, 3, 0, 2);
			} else {
				rec_mon_table.attach (*show_sends_button, 0, 2, 0, 2);
			}

			if (_mixer_owned) {
				show_sends_button->show();
			} else {
				show_sends_button->hide();
			}
		}
	}

	input_button.set_route (route (), this);
	output_button.set_route (route (), this);

	gpm.meter_point_button.set_text (meter_point_string (_route->meter_point()));

	delete route_ops_menu;
	route_ops_menu = 0;

	_route->meter_change.connect (route_connections, invalidator (*this), bind (&MixerStrip::meter_changed, this), gui_context());
	_route->input()->changed.connect (*this, invalidator (*this), std::bind (&MixerStrip::update_input_display, this), gui_context());
	_route->output()->changed.connect (*this, invalidator (*this), std::bind (&MixerStrip::update_output_display, this), gui_context());
	_route->route_group_changed.connect (route_connections, invalidator (*this), std::bind (&MixerStrip::route_group_changed, this), gui_context());

	_route->io_changed.connect (route_connections, invalidator (*this), std::bind (&MixerStrip::io_changed_proxy, this), gui_context ());

	if (_route->panner_shell()) {
		update_panner_choices();
		_route->panner_shell()->Changed.connect (route_connections, invalidator (*this), std::bind (&MixerStrip::connect_to_pan, this), gui_context());
	}

	_route->comment_changed.connect (route_connections, invalidator (*this), std::bind (&MixerStrip::setup_comment_button, this), gui_context());

	set_stuff_from_route ();

	/* now force an update of all the various elements */

	name_changed ();
	route_group_changed ();
	update_track_number_visibility ();

	connect_to_pan ();
	panners.setup_pan ();

	if (has_audio_outputs () && !_route->is_surround_master ()) {
		panners.show_all ();
	} else {
		panners.hide_all ();
	}

	route_color_changed ();
	update_input_display ();
	update_output_display ();

	add_events (Gdk::BUTTON_RELEASE_MASK);

	processor_box.show ();

	if (!route()->is_master() && !route()->is_monitor()) {
		/* we don't allow master or control routes to be hidden */
		hide_button.show();
		number_label.show();
	}

	gpm.reset_peak_display ();
	gpm.gain_display.show ();
	gpm.peak_display.show ();

	width_button.show();
	width_hide_box.show();
	global_frame.show();
	global_vpacker.show();
	mute_solo_table.show();
	bottom_button_table.show();
	gpm.show_all ();
	gpm.meter_point_button.show();
	input_button_box.show_all();
	output_button.show();
	name_button.show();
	_comment_button.show();
	group_button.show();
	rta_button->show();
	gpm.gain_automation_state_button.show();

	parameter_changed ("mixer-element-visibility");
	map_frozen();

	trident_setup ();

	show ();
	update_sensitivity ();
}

void
MixerStrip::trident_build ()
{
	/* --- Sélecteur de panneau (radio) : Gate / EQ / Comp --- */
	_trident_gate_button.set_text (_("Gate"));
	_trident_eq_button.set_text (_("EQ"));
	_trident_comp_button.set_text (_("Comp"));
	_trident_gate_button.signal_clicked.connect (sigc::bind (sigc::mem_fun (*this, &MixerStrip::trident_show_panel), 0));
	_trident_eq_button.signal_clicked.connect   (sigc::bind (sigc::mem_fun (*this, &MixerStrip::trident_show_panel), 1));
	_trident_comp_button.signal_clicked.connect (sigc::bind (sigc::mem_fun (*this, &MixerStrip::trident_show_panel), 2));
	_trident_sel_box.set_spacing (2);
	_trident_sel_box.pack_start (_trident_gate_button, true, true);
	_trident_sel_box.pack_start (_trident_eq_button,   true, true);
	_trident_sel_box.pack_start (_trident_comp_button, true, true);

	/* helper : petite LED on/off à droite du nom de section */
	auto setup_pwr = [this](ArdourWidgets::ArdourButton& b, int idx){
		b.set_text ("");
		b.set_size_request (15, 15);
		b.signal_button_press_event().connect (sigc::mem_fun (*this, &MixerStrip::trident_btn_press), false); // capte Alt avant le clic
		b.signal_clicked.connect (sigc::bind (sigc::mem_fun (*this, &MixerStrip::trident_power_toggle), idx));
	};

	/* --- Panneau EQ : en-tête (repli) + LED + 4 bandes (gain + fréquence) --- */
	_eq_on_button.set_text (_("EQ"));   // bouton-nom = REPLI (pas de bypass)
	_eq_on_button.signal_clicked.connect (sigc::bind (sigc::mem_fun (*this, &MixerStrip::trident_collapse_toggle), 1));
	setup_pwr (_eq_pwr, 1);
	_eq_hdr.pack_start (_eq_on_button, true, true);
	_eq_hdr.pack_start (_eq_pwr, false, false);
	/* HPF 50 Hz : petit bouton ROND, placé entre les potards de Low (cf. 80B) */
	_hpf_round.set_label (_("HPF"));
	_hpf_round.signal_button_press_event().connect (sigc::mem_fun (*this, &MixerStrip::trident_btn_press), false); // capte Alt avant le clic
	_hpf_round.on_clicked (sigc::mem_fun (*this, &MixerStrip::trident_hpf_toggle));
	/* === EQ OXFORD OXF-R3 : 5 bandes paramétriques (Gain | Freq continue | Q) ===
	 * Teinte froide broadcast Oxford. Bornes de fréquence par bande = specs OXF-R3.
	 * b : 0=LF 1=LMF 2=MF 3=HMF 4=HF */
	const double kOx[3] = { 0.24, 0.45, 0.60 };          // bleu froid (gain/freq/Q)
	/* couleurs/format réutilisés par les panneaux comp/gate/bus plus bas */
	const double kBlack[3] = { 0.165, 0.176, 0.188 };
	const double kKhaki[3] = { 0.35,  0.40,  0.20  };
	const double kRed[3]   = { 0.72,  0.16,  0.14  };
	auto dbf = [](double v){ char b[24]; std::snprintf (b, sizeof b, "%+.1f", v); return std::string (b); };
	(void) kKhaki;
	static const double oxFmin[5] = {   20.0,   30.0,  100.0,  600.0,  2000.0 };
	static const double oxFmax[5] = {  400.0,  600.0, 6000.0,18000.0, 20000.0 };
	static const double oxFdef[5] = {   60.0,  250.0, 1000.0, 4000.0, 12000.0 };
	static const double oxQdef[5] = {    0.7,    1.0,    1.0,    1.0,     0.7 };
	const char* oxName[5] = { "LF", "LM", "MF", "HM", "HF" };
	TridentKnob** oxG[5] = { &_tk_eq_lf, &_tk_eq_lmf, &_tk_eq_mf, &_tk_eq_hmf, &_tk_eq_hf };
	TridentKnob** oxF[5] = { &_tk_eqf_lf, &_tk_eqf_lmf, &_tk_eqf_mf, &_tk_eqf_hmf, &_tk_eqf_hf };
	TridentKnob** oxQ[5] = { &_tk_eqq_lf, &_tk_eqq_lmf, &_tk_eqq_mf, &_tk_eqq_hmf, &_tk_eqq_hf };
	for (int b = 0; b < 5; ++b) {
		const double lo = oxFmin[b], hi = oxFmax[b];
		const double fpos = std::log (oxFdef[b] / lo) / std::log (hi / lo);   // 0..1 (échelle log)
		*oxG[b] = Gtk::manage (new TridentKnob (-20, 20, 0,       oxName[b], kOx[0], kOx[1], kOx[2]));
		*oxF[b] = Gtk::manage (new TridentKnob (0, 1, fpos,       _("Hz"),   kOx[0], kOx[1], kOx[2]));
		*oxQ[b] = Gtk::manage (new TridentKnob (0.5, 16, oxQdef[b], _("Q"),  kOx[0], kOx[1], kOx[2]));
		(*oxG[b])->on_changed ([this,b](double v){ if (_oxford) _oxford->setBandGain (b, (float) v); });
		(*oxF[b])->on_changed ([this,b,lo,hi](double v){ if (_oxford) _oxford->setBandFreq (b, (float)(lo * std::pow (hi/lo, v))); });
		(*oxQ[b])->on_changed ([this,b](double v){ if (_oxford) _oxford->setBandQ (b, (float) v); });
		(*oxG[b])->on_format ([](double v){ char s[16]; std::snprintf (s, sizeof s, "%+.1f", v); return std::string (s); });
		(*oxF[b])->on_format ([lo,hi](double v){ double hz = lo * std::pow (hi/lo, v); char s[16]; if (hz >= 1000.0) std::snprintf (s, sizeof s, "%.1fk", hz/1000.0); else std::snprintf (s, sizeof s, "%.0f", hz); return std::string (s); });
		(*oxQ[b])->on_format ([](double v){ char s[16]; std::snprintf (s, sizeof s, "%.1f", v); return std::string (s); });
	}
	/* type de courbe (4 types, cycle) + bascule shelf LF/HF */
	_eq_curve_btn.set_text (_("Neve-G"));
	_eq_curve_btn.signal_button_press_event().connect (sigc::mem_fun (*this, &MixerStrip::trident_btn_press), false); // capte Alt
	_eq_curve_btn.signal_clicked.connect (sigc::mem_fun (*this, &MixerStrip::trident_eq_curve_cycle));
	_eq_lf_shelf_btn.set_text (_("LF Sh"));
	_eq_lf_shelf_btn.signal_button_press_event().connect (sigc::mem_fun (*this, &MixerStrip::trident_btn_press), false); // capte Alt
	_eq_lf_shelf_btn.signal_clicked.connect (sigc::bind (sigc::mem_fun (*this, &MixerStrip::trident_eq_shelf_toggle), 0));
	_eq_hf_shelf_btn.set_text (_("HF Sh"));
	_eq_hf_shelf_btn.signal_button_press_event().connect (sigc::mem_fun (*this, &MixerStrip::trident_btn_press), false); // capte Alt
	_eq_hf_shelf_btn.signal_clicked.connect (sigc::bind (sigc::mem_fun (*this, &MixerStrip::trident_eq_shelf_toggle), 1));
	_eq_opt_box.set_spacing (2);
	_eq_opt_box.pack_start (_eq_curve_btn,    true, true);
	_eq_opt_box.pack_start (_eq_lf_shelf_btn, true, true);
	_eq_opt_box.pack_start (_eq_hf_shelf_btn, true, true);
	/* table : 5 rangées (HF haut -> LF bas) x 4 colonnes : Gain | Freq | Q | (HPF) */
	_eq_knob_table.resize (5, 4);
	auto eqRow = [this](int r, TridentKnob* g, TridentKnob* f, TridentKnob* q){
		_eq_knob_table.attach (*g, 0,1, r,r+1);
		_eq_knob_table.attach (*f, 1,2, r,r+1);
		_eq_knob_table.attach (*q, 2,3, r,r+1);
	};
	eqRow (0, _tk_eq_hf,  _tk_eqf_hf,  _tk_eqq_hf);
	eqRow (1, _tk_eq_hmf, _tk_eqf_hmf, _tk_eqq_hmf);
	eqRow (2, _tk_eq_mf,  _tk_eqf_mf,  _tk_eqq_mf);
	eqRow (3, _tk_eq_lmf, _tk_eqf_lmf, _tk_eqq_lmf);
	eqRow (4, _tk_eq_lf,  _tk_eqf_lf,  _tk_eqq_lf);
	_eq_knob_table.attach (_hpf_round, 3,4, 4,5, Gtk::SHRINK, Gtk::SHRINK);  // HPF coin bas-droite
	_trident_eq_panel.pack_start (_eq_hdr,        Gtk::PACK_SHRINK);
	_trident_eq_panel.pack_start (_eq_opt_box,    Gtk::PACK_SHRINK);
	_trident_eq_panel.pack_start (_eq_knob_table, Gtk::PACK_SHRINK);

	/* --- Panneau Comp --- */
	_comp_on_button.set_text (_("Comp"));
	_comp_on_button.signal_clicked.connect (sigc::bind (sigc::mem_fun (*this, &MixerStrip::trident_collapse_toggle), 2));
	setup_pwr (_comp_pwr, 2);
	_comp_hdr.pack_start (_comp_on_button, true, true);
	_comp_hdr.pack_start (_comp_pwr, false, false);
	_tk_cmp_thr    = Gtk::manage (new TridentKnob (-40, 0,  -18, _("Thr"), kBlack[0], kBlack[1], kBlack[2]));
	_tk_cmp_ratio  = Gtk::manage (new TridentKnob (1.5, 30, 4,   _("Rat"), kBlack[0], kBlack[1], kBlack[2])); // jusqu'au mode limiteur
	_tk_cmp_makeup = Gtk::manage (new TridentKnob (0,   24, 0,   _("Mkp"), kBlack[0], kBlack[1], kBlack[2]));
	_tk_cmp_thr->on_changed    ([this](double v){ if (_oxford) _oxford->setCompThreshDb (v); });
	_tk_cmp_ratio->on_changed  ([this](double v){ if (_oxford) _oxford->setCompRatioCtrl (v); });
	_tk_cmp_makeup->on_changed ([this](double v){ if (_oxford) _oxford->setCompMakeupDb (v); });
	_tk_cmp_thr->on_format (dbf);
	_tk_cmp_ratio->on_format  ([](double v){ char b[16]; std::snprintf (b, sizeof b, "%.1f:1", v); return std::string (b); });
	_tk_cmp_makeup->on_format ([](double v){ char b[16]; std::snprintf (b, sizeof b, "+%.1f", v);  return std::string (b); });
	_tk_gr = Gtk::manage (new TridentMeter (TridentMeter::GRLadder, _("GR")));  // échelle LED Harrison
	/* 2 colonnes pour aérer : [Thr Rat] / [Mkp GR] */
	_comp_knobs.resize (2, 2);
	_comp_knobs.set_row_spacings (3);
	_comp_knobs.set_col_spacings (4);
	_comp_knobs.attach (*_tk_cmp_thr,    0,1, 0,1); _comp_knobs.attach (*_tk_cmp_ratio, 1,2, 0,1);
	_comp_knobs.attach (*_tk_cmp_makeup, 0,1, 1,2); _comp_knobs.attach (*_tk_gr,        1,2, 1,2);
	_trident_comp_panel.pack_start (_comp_hdr,   Gtk::PACK_SHRINK);
	_trident_comp_panel.pack_start (_comp_knobs, Gtk::PACK_SHRINK);

	/* --- Panneau Gate --- */
	_gate_on_button.set_text (_("Gate"));
	_gate_on_button.signal_clicked.connect (sigc::bind (sigc::mem_fun (*this, &MixerStrip::trident_collapse_toggle), 0));
	setup_pwr (_gate_pwr, 0);
	_gate_hdr.pack_start (_gate_on_button, true, true);
	_gate_hdr.pack_start (_gate_pwr, false, false);
	_tk_gate_thr   = Gtk::manage (new TridentKnob (-80, 0, -30, _("Thr"), kBlack[0], kBlack[1], kBlack[2]));
	_tk_gate_range = Gtk::manage (new TridentKnob (-90, 0, -60, _("Rng"), kBlack[0], kBlack[1], kBlack[2]));
	_tk_gate_thr->on_changed   ([this](double v){ if (_oxford) _oxford->setGateThreshDb (v); });
	_tk_gate_range->on_changed ([this](double v){ if (_oxford) _oxford->setGateRangeDb  (v); });
	_tk_gate_thr->on_format (dbf); _tk_gate_range->on_format (dbf);
	_gate_knobs.set_spacing (1);
	_gate_knobs.pack_start (*_tk_gate_thr,   true, true);
	_gate_knobs.pack_start (*_tk_gate_range, true, true);
	_trident_gate_panel.pack_start (_gate_hdr,   Gtk::PACK_SHRINK);
	_trident_gate_panel.pack_start (_gate_knobs, Gtk::PACK_SHRINK);

	/* --- Panneau Bus/Master : VU (haut) + Comp(att/rel) + Drive/Tape + Limiter(master) --- */
	auto pctf = [](double v){ char b[16]; std::snprintf (b, sizeof b, "%.0f%%", v * 100.0); return std::string (b); };
	auto msf  = [](double v){ char b[16]; std::snprintf (b, sizeof b, "%.0fms", v); return std::string (b); };

	/* logo console (master) + VU à aiguille, tout en haut (lisibilité) */
	_trident_logo.set_markup ("<b>TRIDENT 80B</b>");
	_trident_logo.set_justify (Gtk::JUSTIFY_CENTER);
	_trident_logo.modify_fg (Gtk::STATE_NORMAL, Gdk::Color ("#e8e2c8")); // crème (assorti aux VU)
	_trident_bus_panel.pack_start (_trident_logo, Gtk::PACK_SHRINK);
	_tk_tape_vu = Gtk::manage (new TridentMeter (TridentMeter::NeedleVU, _("VU")));
	_trident_bus_panel.pack_start (*_tk_tape_vu, Gtk::PACK_SHRINK);

	/* Comp glue bus/master : enable + Thr/Ratio/Att/Rel/Mkp + GR */
	_bus_comp_button.set_text (_("Comp"));
	_bus_comp_button.signal_clicked.connect (sigc::bind (sigc::mem_fun (*this, &MixerStrip::trident_collapse_toggle), 4));
	setup_pwr (_bus_comp_pwr, 4);
	_bus_comp_hdr.pack_start (_bus_comp_button, true, true);
	_bus_comp_hdr.pack_start (_bus_comp_pwr, false, false);
	_tk_bcmp_thr   = Gtk::manage (new TridentKnob (-40, 0,    -18, _("Thr"), kBlack[0], kBlack[1], kBlack[2]));
	_tk_bcmp_ratio = Gtk::manage (new TridentKnob (1.5, 30,   2,   _("Rat"), kBlack[0], kBlack[1], kBlack[2])); // 1.5:1 -> limiteur 30:1
	_tk_bcmp_att   = Gtk::manage (new TridentKnob (0.05, 100, 30,  _("Att"), kBlack[0], kBlack[1], kBlack[2])); // FET ultra-rapide (sub-ms)
	_tk_bcmp_rel   = Gtk::manage (new TridentKnob (20,  2500, 200, _("Rel"), kBlack[0], kBlack[1], kBlack[2])); // jusqu'à plusieurs secondes
	_tk_bcmp_mkp   = Gtk::manage (new TridentKnob (0,   24,   0,   _("Mkp"), kBlack[0], kBlack[1], kBlack[2]));
	_tk_bcmp_thr->on_changed   ([this](double v){ if (_oxford) _oxford->setBusCompThreshDb  (v); });
	_tk_bcmp_ratio->on_changed ([this](double v){ if (_oxford) _oxford->setBusCompRatioCtrl (v); });
	_tk_bcmp_att->on_changed   ([this](double v){ if (_oxford) _oxford->setBusCompAttackMs  (v); });
	_tk_bcmp_rel->on_changed   ([this](double v){ if (_oxford) _oxford->setBusCompReleaseMs (v); });
	_tk_bcmp_mkp->on_changed   ([this](double v){ if (_oxford) _oxford->setBusCompMakeupDb  (v); });
	_tk_bcmp_thr->on_format (dbf);
	_tk_bcmp_ratio->on_format ([](double v){ char b[16]; std::snprintf (b, sizeof b, "%.1f:1", v); return std::string (b); });
	_tk_bcmp_att->on_format (msf); _tk_bcmp_rel->on_format (msf);
	_tk_bcmp_mkp->on_format ([](double v){ char b[16]; std::snprintf (b, sizeof b, "+%.1f", v); return std::string (b); });
	_tk_bus_gr = Gtk::manage (new TridentMeter (TridentMeter::GRLadder, _("GR")));
	/* 2 colonnes pour aérer : [Thr Rat] / [Att Rel] / [Mkp GR] */
	_bus_comp_knobs.resize (2, 3);
	_bus_comp_knobs.set_row_spacings (3);
	_bus_comp_knobs.set_col_spacings (4);
	_bus_comp_knobs.attach (*_tk_bcmp_thr, 0,1, 0,1); _bus_comp_knobs.attach (*_tk_bcmp_ratio, 1,2, 0,1);
	_bus_comp_knobs.attach (*_tk_bcmp_att, 0,1, 1,2); _bus_comp_knobs.attach (*_tk_bcmp_rel,   1,2, 1,2);
	_bus_comp_knobs.attach (*_tk_bcmp_mkp, 0,1, 2,3); _bus_comp_knobs.attach (*_tk_bus_gr,     1,2, 2,3);
	_trident_bus_panel.pack_start (_bus_comp_hdr,   Gtk::PACK_SHRINK);
	_trident_bus_panel.pack_start (_bus_comp_knobs, Gtk::PACK_SHRINK);
	_trident_bus_panel.pack_start (_div_bc,         Gtk::PACK_SHRINK); // séparateur Comp|Tape

	/* Tape : header (repli du Drive) + LED on/off ; Drive centré dessous */
	_tape_on_button.set_text (_("Tape"));
	_tape_on_button.signal_clicked.connect (sigc::bind (sigc::mem_fun (*this, &MixerStrip::trident_collapse_toggle), 3));
	setup_pwr (_tape_pwr, 3);
	_tape_hdr.pack_start (_tape_on_button, true, true);
	_tape_hdr.pack_start (_tape_pwr, false, false);
	/* Sélecteur de vitesse bande 15/30 ips (par bus). 30 ips : head bump plus haut
	 * (~80 Hz) + aigus étendus ; 15 ips : bump plus bas (~40 Hz) + plus de poids. */
	_tape_speed_button.set_name ("mixer strip button");
	_tape_speed_button.set_text ("30");
	set_tooltip (_tape_speed_button, _("Vitesse bande : 15 / 30 ips"));
	_tape_speed_button.signal_clicked.connect (sigc::mem_fun (*this, &MixerStrip::trident_tape_speed_toggle));
	_tape_hdr.pack_start (_tape_speed_button, false, false);
	/* Knob du tape (course 0..1). Sur les bus simples = "Drive" (attaque DANS la tape).
	 * Sur le MASTER il devient "Hdrm" (HEADROOM) : relève le SEUIL avant le tanh du
	 * master tape (volume constant, mappé 0..12 dB). route() n'est pas encore connu
	 * ici -> le label/format/valeur (master vs bus) sont fixés dans trident_setup().
	 * Le callback, lui, teste le master à l'usage (route() est valide à ce moment). */
	_tk_bus_drive = Gtk::manage (new TridentKnob (0, 1, 0, _("Drive"), kRed[0], kRed[1], kRed[2]));
	_tk_bus_drive->on_changed ([this](double v){
		auto t = trident_tail_target (); if (!t) return;
		if (route () && route ()->is_master ()) t->setTapeDrive (v); // master : course -9..+9 dB (cf. trident_setup)
		else                                    t->setTapeDrive (v);      // bus : 0..1
	});
	_tk_bus_drive->on_format (pctf);
	/* OXFORD : tape / bus-drive / vitesse RETIRÉS de la GUI (sommation PROPRE +
	 * micro-distorsion convertisseur automatique sur le master, pas de coloration
	 * de bus type Mixbus). Les widgets restent construits (refs conservées dans
	 * update/registry/setup) mais ne sont PAS affichés -> aucun pack ici. */

	/* Limiter brickwall (montré seulement sur le master, cf. trident_setup) */
	_limiter_button.set_text (_("Limiter"));
	_limiter_button.signal_clicked.connect (sigc::bind (sigc::mem_fun (*this, &MixerStrip::trident_collapse_toggle), 5));
	setup_pwr (_limiter_pwr, 5);
	_lim_hdr.pack_start (_limiter_button, true, true);
	_lim_hdr.pack_start (_limiter_pwr, false, false);
	/* Limiteur = maximizer : "Drv" pousse dans le limiteur (plus fort + GR),
	 * plafond de sortie fixe (~-0.3 dBFS). 0..12 dB de drive. */
	_tk_lim_ceil = Gtk::manage (new TridentKnob (0, 24, 0, _("Drv"), kRed[0], kRed[1], kRed[2]));
	_tk_lim_ceil->on_changed ([this](double v){ auto t = trident_tail_target (); if (t) t->setBusLimCeilDb (v); });
	_tk_lim_ceil->on_format ([](double v){ char b[16]; std::snprintf (b, sizeof b, "+%.1f", v); return std::string (b); });
	_tk_lim_gr = Gtk::manage (new TridentMeter (TridentMeter::GRLadder, _("GR")));  // GR limiteur
	/* Ceil + GR limiteur côte à côte */
	Gtk::HBox* lim_row = Gtk::manage (new Gtk::HBox ());
	lim_row->set_spacing (4);
	lim_row->pack_start (*_tk_lim_ceil, true, false);
	lim_row->pack_start (*_tk_lim_gr,   true, false);
	_trident_bus_panel.pack_start (_lim_hdr, Gtk::PACK_SHRINK);
	_trident_bus_panel.pack_start (*lim_row, Gtk::PACK_SHRINK);

	/* --- Trim (pré-tout) + Pan en potards (haut de la tranche Trident) --- */
	_tk_trim = Gtk::manage (new TridentKnob (-20, 20, 0, _("Trim"), kBlack[0], kBlack[1], kBlack[2]));
	_tk_trim->on_changed ([this](double v){
		if (_route && _route->trim_control ()) {
			_route->trim_control ()->set_value (dB_to_coefficient (v), PBD::Controllable::NoGroup);
		}
	});
	_tk_trim->on_format ([](double v){ char b[16]; std::snprintf (b, sizeof b, "%+.1f", v); return std::string (b); });

	_tk_pan = Gtk::manage (new TridentKnob (0, 1, 0.5, _("Pan"), kBlack[0], kBlack[1], kBlack[2]));
	_tk_pan->on_changed ([this](double v){
		if (_route && _route->pan_azimuth_control ()) {
			_route->pan_azimuth_control ()->set_value (v, PBD::Controllable::NoGroup);
		}
	});
	_tk_pan->on_format ([](double v){
		int p = (int) ((v - 0.5) * 200.0 + (v >= 0.5 ? 0.5 : -0.5)); // -100..+100
		int ap = p < 0 ? -p : p;
		char b[16]; std::snprintf (b, sizeof b, "%s%d", p > 0 ? "R" : (p < 0 ? "L" : "C"), ap);
		return std::string (b);
	});
	/* Width = largeur stéréo (pistes stéréo) : -1 (mono) .. 0 .. +1 (large) */
	/* Width = largeur M/S Trident : knob -1 (mono) .. 0 (normal) .. +1 (très large).
	 * Mappé sur _width = 1 + knob (0..2). Effet DANS le DSP Trident (pas le panner). */
	_tk_width = Gtk::manage (new TridentKnob (-1, 1, 0, _("Width"), kBlack[0], kBlack[1], kBlack[2]));
	_tk_width->on_changed ([this](double v){
		if (_oxford) { _oxford->setWidth ((float) (1.0 + v)); }
	});
	_tk_width->on_format ([](double v){ char b[16]; std::snprintf (b, sizeof b, "%.0f%%", (1.0 + v) * 100.0); return std::string (b); });

	_trident_pan_box.set_spacing (3);
	_trident_pan_box.pack_start (*_tk_trim,  true, false);
	_trident_pan_box.pack_start (*_tk_pan,   true, false);
	_trident_pan_box.pack_start (*_tk_width, true, false);

	/* --- Assemblage : panneaux empilés verticalement dans l'ordre du flux
	 *     Gate -> EQ -> Comp (mode Strip), ou panneau Bus (mode Bus). --- */
	_trident_box.set_spacing (3);
	_trident_box.pack_start (_trident_sel_box,    Gtk::PACK_SHRINK); // gardé mais masqué (mode Strip)
	_trident_box.pack_start (_trident_gate_panel, Gtk::PACK_SHRINK);
	_trident_box.pack_start (_div_ge,             Gtk::PACK_SHRINK); // séparateur Gate|EQ
	_trident_box.pack_start (_trident_eq_panel,   Gtk::PACK_SHRINK);
	_trident_box.pack_start (_div_ec,             Gtk::PACK_SHRINK); // séparateur EQ|Comp
	_trident_box.pack_start (_trident_comp_panel, Gtk::PACK_SHRINK);
	_trident_box.pack_start (_trident_bus_panel,  Gtk::PACK_SHRINK);
	_trident_box.show_all ();

	trident_link_setup ();   // branche le link Alt+sélection sur tous les potards
}

void
MixerStrip::trident_setup ()
{
	_oxford = _route ? _route->oxford_channel () : std::shared_ptr<ARDOUR::OxfordChannel> ();
	_oxford_tail = _route ? _route->oxford_tail () : std::shared_ptr<ARDOUR::OxfordChannel> ();

	/* Fond de tranche Trident #3a3d42 (réf Mixbus 80B), uniforme. */
	trident_apply_bg (0.227, 0.239, 0.259);   // #3a3d42

	_trident_meter_conn.disconnect ();

	if (_oxford && _oxford->kind () == ARDOUR::OxfordChannel::Strip) {
		/* --- Pistes : Gate / EQ / Comp + GR meter --- */
		{
			static const double iFmin[5] = {  20.0,  30.0,  100.0,  600.0,  2000.0 };
			static const double iFmax[5] = { 400.0, 600.0, 6000.0,18000.0, 20000.0 };
			TridentKnob* iG[5] = { _tk_eq_lf, _tk_eq_lmf, _tk_eq_mf, _tk_eq_hmf, _tk_eq_hf };
			TridentKnob* iF[5] = { _tk_eqf_lf, _tk_eqf_lmf, _tk_eqf_mf, _tk_eqf_hmf, _tk_eqf_hf };
			TridentKnob* iQ[5] = { _tk_eqq_lf, _tk_eqq_lmf, _tk_eqq_mf, _tk_eqq_hmf, _tk_eqq_hf };
			for (int b = 0; b < 5; ++b) {
				iG[b]->set_value (_oxford->bandGain (b));
				const double hz = _oxford->bandFreq (b);
				iF[b]->set_value (std::log (hz / iFmin[b]) / std::log (iFmax[b] / iFmin[b]));
				iQ[b]->set_value (_oxford->bandQ (b));
			}
			_eq_curve_btn.set_text (eq_curve_name (_oxford->curveType ()));
			_eq_lf_shelf_btn.set_active_state (_oxford->lfShelf () ? Gtkmm2ext::ExplicitActive : Gtkmm2ext::Off);
			_eq_hf_shelf_btn.set_active_state (_oxford->hfShelf () ? Gtkmm2ext::ExplicitActive : Gtkmm2ext::Off);
		}
		_tk_cmp_thr->set_value (_oxford->compThreshDb ());
		_tk_cmp_ratio->set_value (_oxford->compRatioCtrl ());
		_tk_cmp_makeup->set_value (_oxford->compMakeupDb ());
		_tk_gate_thr->set_value (_oxford->gateThreshDb ());
		_tk_gate_range->set_value (_oxford->gateRangeDb ());
		/* empilage vertical des 3 sections (façon Mixbus) ; pas de sélecteur */
		_trident_bus_panel.hide ();
		_trident_sel_box.hide ();
		_trident_gate_panel.show ();
		_trident_eq_panel.show ();
		_trident_comp_panel.show ();
		trident_pan_setup ();
		_trident_box.show ();
		trident_update_buttons ();
		trident_update_section_visibility ();
		_trident_meter_conn = Glib::signal_timeout().connect (sigc::mem_fun (*this, &MixerStrip::trident_meter_update), 60);
	} else if (_oxford && _oxford->kind () == ARDOUR::OxfordChannel::Bus) {
		/* --- Bus / Master : VU + Comp(att/rel) + Drive/Tape + Limiter(master) --- */
		/* Master : knob du tape = "Hdrm" BIPOLAIRE, 0 à MIDI, course -9..+9 dB.
		 *   + = plus de headroom (sature plus tard, aiguille recule) ;
		 *   - = moins de headroom (attaque le tanh plus fort -> SATURE, aiguille monte).
		 * Bus simples : "Drive" 0..1 (%). */
		if (route () && route ()->is_master ()) {
			_tk_bus_drive->set_range (-9.0, 9.0, 0.0);   // 0 centré, 18 dB de course
			_tk_bus_drive->set_label (_("Hdrm"));
			_tk_bus_drive->on_format ([](double v){ char b[16]; std::snprintf (b, sizeof b, "%+.0f dB", v); return std::string (b); });
			_tk_bus_drive->set_value (0.0);
		} else {
			_tk_bus_drive->set_range (0.0, 1.0, 0.0);
			_tk_bus_drive->set_label (_("Drive"));
			_tk_bus_drive->on_format ([](double v){ char b[16]; std::snprintf (b, sizeof b, "%.0f%%", v * 100.0); return std::string (b); });
			_tk_bus_drive->set_value (trident_tail_target ()->tapeDrive ());
		}
		_tape_speed_button.set_text ("30");
		_tk_bcmp_thr->set_value (_oxford->busCompThreshDb ());
		_tk_bcmp_ratio->set_value (_oxford->busCompRatioCtrl ());
		_tk_bcmp_att->set_value (_oxford->busCompAttackMs ());
		_tk_bcmp_rel->set_value (_oxford->busCompReleaseMs ());
		_tk_bcmp_mkp->set_value (_oxford->busCompMakeupDb ());
		_tk_lim_ceil->set_value (trident_tail_target ()->busLimCeilDb ());
		_trident_sel_box.hide ();
		_trident_eq_panel.hide ();
		_trident_comp_panel.hide ();
		_trident_gate_panel.hide ();
		_trident_bus_panel.show ();
		/* bus FX (delay/reverb) : tape seulement, PAS de comp -> on masque la section */
		if (_oxford->isFxBus ()) {
			_bus_comp_hdr.hide (); _bus_comp_knobs.hide ();
		} else {
			_bus_comp_hdr.show ();
		}
		/* le limiter "toute fin" et le logo ne sont montrés que sur le master */
		if (_route && _route->is_master ()) {
			_lim_hdr.show ();
			_trident_logo.show ();
		} else {
			_lim_hdr.hide (); _tk_lim_ceil->hide (); _tk_lim_gr->hide ();
			_trident_logo.hide ();
		}
		trident_pan_setup ();
		_trident_box.show ();
		trident_update_buttons ();
		trident_update_section_visibility ();
		_trident_meter_conn = Glib::signal_timeout().connect (sigc::mem_fun (*this, &MixerStrip::trident_meter_update), 60);
	} else {
		_trident_box.hide ();
	}
}

void
MixerStrip::trident_pan_setup ()
{
	/* Trim + Pan uniquement sur les PISTES (pas les bus ni le master). */
	if (!_oxford || _oxford->kind () != ARDOUR::OxfordChannel::Strip) {
		_trident_pan_bg.hide ();
		return;
	}
	_trident_pan_bg.show ();

	bool any = false;
	if (_route && _route->trim_control ()) {
		_tk_trim->set_value (accurate_coefficient_to_dB (_route->trim_control ()->get_value ()));
		_tk_trim->show (); any = true;
	} else {
		_tk_trim->hide ();
	}
	if (_route && _route->pan_azimuth_control ()) {
		_tk_pan->set_value (_route->pan_azimuth_control ()->get_value ());
		_tk_pan->show (); any = true;
	} else {
		_tk_pan->hide ();
	}
	/* Width : piste STÉRÉO (2 in & 2 out) -> contrôle direct du Pannable.
	 * On se base sur le nombre de canaux (fiable au build) plutôt que sur
	 * pan_width_control() qui dépend de what_can_be_automated()/lock du panner. */
	const bool stereo = _route
		&& _route->n_inputs ().n_audio ()  >= 2
		&& _route->n_outputs ().n_audio () >= 2;
	if (stereo && _oxford) {
		_tk_width->set_value ((double) _oxford->width () - 1.0);   // width 0..2 -> knob -1..1
		_tk_width->show (); any = true;
	} else {
		_tk_width->hide ();
	}
	if (any) { _trident_pan_box.show (); } else { _trident_pan_box.hide (); }
}

void
MixerStrip::trident_apply_bg (double r, double g, double b)
{
	Gdk::Color c; c.set_rgb_p (r, g, b);
	_trident_bg.modify_bg (Gtk::STATE_NORMAL, c);
	_trident_pan_bg.modify_bg (Gtk::STATE_NORMAL, c);
	TridentKnob* knobs[] = { _tk_eq_lf, _tk_eq_lmf, _tk_eq_mf, _tk_eq_hmf, _tk_eq_hf,
		_tk_eqf_lf, _tk_eqf_lmf, _tk_eqf_mf, _tk_eqf_hmf, _tk_eqf_hf,
		_tk_eqq_lf, _tk_eqq_lmf, _tk_eqq_mf, _tk_eqq_hmf, _tk_eqq_hf,
		_tk_cmp_thr, _tk_cmp_ratio, _tk_cmp_makeup, _tk_gate_thr, _tk_gate_range,
		_tk_bus_drive, _tk_bcmp_thr, _tk_bcmp_ratio, _tk_bcmp_att, _tk_bcmp_rel, _tk_bcmp_mkp,
		_tk_lim_ceil, _tk_pan, _tk_trim, _tk_width };
	for (TridentKnob* k : knobs) { if (k) { k->set_bg (r, g, b); } }
	TridentMeter* meters[] = { _tk_gr, _tk_tape_vu, _tk_bus_gr, _tk_lim_gr };
	for (TridentMeter* m : meters) { if (m) { m->set_bg (r, g, b); } }
	TridentDivider* divs[] = { &_div_ge, &_div_ec, &_div_bc, &_div_ct };
	for (TridentDivider* d : divs) { d->set_bg (r, g, b); }
	_hpf_round.set_bg (r, g, b);
}

bool
MixerStrip::trident_meter_update ()
{
	if (!_oxford) {
		return false; /* arrête le timer */
	}
	if (_oxford->kind () == ARDOUR::OxfordChannel::Strip) {
		if (_tk_gr) {
			/* GR : échelle LED Harrison (crête tenue) -> on fournit la valeur dB */
			const double gr = _oxford->compOn () ? _oxford->gainReductionDb () : 0.0;
			_tk_gr->set_reading (_oxford->compOn () ? gr : -1.0);
		}
	} else {
		if (_tk_tape_vu) {
			/* VU = sortie finale (tail = tape+limiter sur le master) */
			const double lv = trident_tail_target ()->busLevel ();
			const double db = lv > 1e-5 ? 20.0 * std::log10 (lv) : -120.0;
			/* HEADROOM : le knob Hdrm décale le seuil de sat du tape À VOLUME CONSTANT
			 * (le DSP fait /headroom puis *headroom). La sortie ne bouge donc pas → le
			 * VU doit refléter le headroom lui-même : on RECULE l'aiguille de Hdrm dB
			 * (0 VU = repère de saturation qui monte) → "je peux aller plus haut dedans".
			 * Calibration de base : fill 1.0 = 0 dBFS, fill 0 = -42 dBFS. */
			const double hr = 0.0;   // 0 (neutre) sur les bus
			_tk_tape_vu->set_fill ((db - hr + 42.0) / 42.0);
		}
		if (_tk_bus_gr) {
			/* GR = comp amont (front) : échelle LED Harrison (crête tenue) */
			const double gr = _oxford->busCompOn () ? _oxford->gainReductionDb () : 0.0;
			_tk_bus_gr->set_reading (_oxford->busCompOn () ? gr : -1.0);
		}
		if (_tk_lim_gr) {
			/* GR du limiteur brickwall (master, sur le module tail) */
			std::shared_ptr<ARDOUR::OxfordChannel> t = trident_tail_target ();
			const bool on = t && t->busLimOn ();
			_tk_lim_gr->set_reading (on ? t->limiterGrDb () : -1.0);
		}
	}
	return true;
}

void
MixerStrip::trident_show_panel (int which)
{
	_trident_panel = which;
	_trident_eq_panel.hide ();
	_trident_comp_panel.hide ();
	_trident_gate_panel.hide ();
	switch (which) {
		case 0: _trident_gate_panel.show (); break;
		case 2: _trident_comp_panel.show (); break;
		default: _trident_eq_panel.show (); break;
	}
	trident_update_buttons ();
}

/* ===== Link sur sélection (Alt) ===== */

namespace {
	enum TkId {
		TK_EQ_LF_G, TK_EQ_LMF_G, TK_EQ_HMF_G, TK_EQ_HF_G,
		TK_EQ_LF_F, TK_EQ_LMF_F, TK_EQ_HMF_F, TK_EQ_HF_F,
		TK_CMP_THR, TK_CMP_RAT, TK_CMP_MKP,
		TK_GATE_THR, TK_GATE_RNG,
		TK_BCMP_THR, TK_BCMP_RAT, TK_BCMP_ATT, TK_BCMP_REL, TK_BCMP_MKP,
		TK_BUS_DRIVE, TK_LIM_CEIL,
		TK_TRIM, TK_PAN, TK_WIDTH
	};
}

bool
MixerStrip::trident_btn_press (GdkEventButton* ev)
{
	_trident_click_state = ev->state;   // mémorise Alt/Ctrl... au press (le clic suit normalement)
	return false;                        // ne consomme pas l'évènement
}

bool
MixerStrip::trident_link_alt () const
{
	/* Lit l'état des modificateurs de l'évènement EN COURS de traitement (le clic/relâche
	 * qui déclenche le toggle) -> fiable pour les ArdourButton, contrairement au state
	 * capturé au press. Fallback sur le state mémorisé si pas d'évènement courant. */
	/* État LIVE des modificateurs : Alt est encore physiquement enfoncé au moment du
	 * toggle. Fiable et indépendant de l'évènement/sa capture (qui, pour une raison
	 * obscure, ne portait pas MOD1 sur le bouton rond ici). */
	GdkModifierType mask = (GdkModifierType) 0;
	gdk_display_get_pointer (gdk_display_get_default (), 0, 0, 0, &mask);
	if ((mask & GDK_MOD1_MASK) != 0) { return true; }
	/* filets de sécurité */
	if ((_trident_click_state & GDK_MOD1_MASK) != 0) { return true; }
	GdkModifierType st = (GdkModifierType) 0;
	if (gtk_get_current_event_state (&st) && (st & GDK_MOD1_MASK) != 0) { return true; }
	return false;
}

void
MixerStrip::trident_link_setup ()
{
	for (int i = 0; i < 32; ++i) { _tk_link[i] = 0; }

	auto reg = [this] (int id, TridentKnob* k) {
		if (!k) { return; }
		_tk_link[id] = k;
		k->on_linked ([this, id] (double d) { trident_link_knob (id, d); });
	};

	reg (TK_EQ_LF_G, _tk_eq_lf);   reg (TK_EQ_LMF_G, _tk_eq_lmf); reg (TK_EQ_HMF_G, _tk_eq_hmf); reg (TK_EQ_HF_G, _tk_eq_hf);
	reg (TK_EQ_LF_F, _tk_eqf_lf);  reg (TK_EQ_LMF_F, _tk_eqf_lmf); reg (TK_EQ_HMF_F, _tk_eqf_hmf); reg (TK_EQ_HF_F, _tk_eqf_hf);
	reg (TK_CMP_THR, _tk_cmp_thr); reg (TK_CMP_RAT, _tk_cmp_ratio); reg (TK_CMP_MKP, _tk_cmp_makeup);
	reg (TK_GATE_THR, _tk_gate_thr); reg (TK_GATE_RNG, _tk_gate_range);
	reg (TK_BCMP_THR, _tk_bcmp_thr); reg (TK_BCMP_RAT, _tk_bcmp_ratio); reg (TK_BCMP_ATT, _tk_bcmp_att);
	reg (TK_BCMP_REL, _tk_bcmp_rel); reg (TK_BCMP_MKP, _tk_bcmp_mkp);
	reg (TK_BUS_DRIVE, _tk_bus_drive); reg (TK_LIM_CEIL, _tk_lim_ceil);
	reg (TK_TRIM, _tk_trim); reg (TK_PAN, _tk_pan); reg (TK_WIDTH, _tk_width);
}

void
MixerStrip::trident_link_knob (int id, double delta)
{
	if (id < 0 || id >= 32) { return; }
	Mixer_UI* mx = Mixer_UI::instance ();
	if (!mx) { return; }
	std::shared_ptr<ARDOUR::Route> me = route ();
	if (!me || !me->is_selected ()) { return; }   // ne cascade que si CETTE tranche est sélectionnée

	for (MixerStrip* s : mx->mixer_strips ()) {
		if (s == this) { continue; }
		std::shared_ptr<ARDOUR::Route> sr = s->route ();
		if (!sr || !sr->is_selected ()) { continue; }
		TridentKnob* k = s->_tk_link[id];
		if (k) { k->set_value (k->get_value () + delta, true); } // notify -> déclenche le setter DSP
	}
}

bool
MixerStrip::trident_get_power (int which)
{
	if (!_oxford) { return false; }
	std::shared_ptr<ARDOUR::OxfordChannel> tail = trident_tail_target ();
	switch (which) {
		case 0: return _oxford->gateOn ();
		case 1: return _oxford->eqEnabled ();
		case 2: return _oxford->compOn ();
		case 3: return tail ? tail->tapeOn () : false;
		case 4: return _oxford->busCompOn ();
		case 5: return tail ? tail->busLimOn () : false;
		default: return false;
	}
}

void
MixerStrip::trident_apply_power (int which, bool on)
{
	if (!_oxford) { return; }
	std::shared_ptr<ARDOUR::OxfordChannel> tail = trident_tail_target ();
	switch (which) {
		case 0: _oxford->setGateOn    (on); break;
		case 1: _oxford->setEQEnabled (on); break;
		case 2: _oxford->setCompOn    (on); break;
		case 3: if (tail) tail->setTapeOn    (on); break;  // tape = aval (tail)
		case 4: _oxford->setBusCompOn (on); break;        // comp = amont (front)
		case 5: if (tail) tail->setBusLimOn (on); break;  // limiter = aval (tail)
		default: break;
	}
	trident_update_buttons ();
}

void
MixerStrip::trident_power_toggle (int which)
{
	/* ON/OFF du DSP (LED). N'affecte PAS le repli. */
	if (!_oxford) {
		return;
	}
	const bool ns = !trident_get_power (which);
	trident_apply_power (which, ns);

	/* Alt + cette tranche sélectionnée -> même état (absolu) sur toute la sélection */
	if (trident_link_alt ()) {
		std::shared_ptr<ARDOUR::Route> me = route ();
		Mixer_UI* mx = Mixer_UI::instance ();
		if (mx && me && me->is_selected ()) {
			for (MixerStrip* s : mx->mixer_strips ()) {
				if (s == this) { continue; }
				std::shared_ptr<ARDOUR::Route> sr = s->route ();
				if (sr && sr->is_selected () && s->_oxford) { s->trident_apply_power (which, ns); }
			}
		}
	}
}

void
MixerStrip::trident_hpf_toggle ()
{
	if (!_oxford) { return; }
	_trident_click_state = _hpf_round.last_state ();   // le bouton rond a capturé Alt au press
	const bool ns = !_oxford->hpfOn ();
	_oxford->setHPF (ns, 50.f, 12.f);
	_hpf_round.set_active (ns);

	/* Alt + cette tranche sélectionnée -> même état HPF sur toute la sélection */
	if (trident_link_alt ()) {
		std::shared_ptr<ARDOUR::Route> me = route ();
		Mixer_UI* mx = Mixer_UI::instance ();
		if (mx && me && me->is_selected ()) {
			for (MixerStrip* s : mx->mixer_strips ()) {
				if (s == this) { continue; }
				std::shared_ptr<ARDOUR::Route> sr = s->route ();
				if (sr && sr->is_selected () && s->_oxford) {
					s->_oxford->setHPF (ns, 50.f, 12.f);
					s->_hpf_round.set_active (ns);
				}
			}
		}
	}
}

/* EQ OXFORD : nom court du type de courbe (4 types) */
const char*
MixerStrip::eq_curve_name (int t)
{
	switch (t) {
		case 0:  return _("SSL-E");
		case 1:  return _("Corr");
		case 2:  return _("Neve-G");
		default: return _("Master");
	}
}

void
MixerStrip::trident_eq_curve_cycle ()
{
	if (!_oxford) { return; }
	const int t = (_oxford->curveType () + 1) % 4;
	_oxford->setCurveType (t);
	_eq_curve_btn.set_text (eq_curve_name (t));

	/* Alt + cette tranche sélectionnée -> même type de courbe sur toute la sélection */
	if (trident_link_alt ()) {
		std::shared_ptr<ARDOUR::Route> me = route ();
		Mixer_UI* mx = Mixer_UI::instance ();
		if (mx && me && me->is_selected ()) {
			for (MixerStrip* s : mx->mixer_strips ()) {
				if (s == this) { continue; }
				std::shared_ptr<ARDOUR::Route> sr = s->route ();
				if (sr && sr->is_selected () && s->_oxford) {
					s->_oxford->setCurveType (t);
					s->_eq_curve_btn.set_text (eq_curve_name (t));
				}
			}
		}
	}
}

void
MixerStrip::trident_eq_shelf_toggle (int which)
{
	if (!_oxford) { return; }
	bool ns;
	if (which == 0) {
		ns = !_oxford->lfShelf ();
		_oxford->setLFShelf (ns);
		_eq_lf_shelf_btn.set_active_state (ns ? Gtkmm2ext::ExplicitActive : Gtkmm2ext::Off);
	} else {
		ns = !_oxford->hfShelf ();
		_oxford->setHFShelf (ns);
		_eq_hf_shelf_btn.set_active_state (ns ? Gtkmm2ext::ExplicitActive : Gtkmm2ext::Off);
	}

	/* Alt + cette tranche sélectionnée -> même état de shelf sur toute la sélection */
	if (trident_link_alt ()) {
		std::shared_ptr<ARDOUR::Route> me = route ();
		Mixer_UI* mx = Mixer_UI::instance ();
		if (mx && me && me->is_selected ()) {
			for (MixerStrip* s : mx->mixer_strips ()) {
				if (s == this) { continue; }
				std::shared_ptr<ARDOUR::Route> sr = s->route ();
				if (sr && sr->is_selected () && s->_oxford) {
					if (which == 0) {
						s->_oxford->setLFShelf (ns);
						s->_eq_lf_shelf_btn.set_active_state (ns ? Gtkmm2ext::ExplicitActive : Gtkmm2ext::Off);
					} else {
						s->_oxford->setHFShelf (ns);
						s->_eq_hf_shelf_btn.set_active_state (ns ? Gtkmm2ext::ExplicitActive : Gtkmm2ext::Off);
					}
				}
			}
		}
	}
}

void
MixerStrip::trident_tape_speed_toggle ()
{
	auto t = trident_tail_target ();
	if (!t) { return; }
	const float ns = (false) ? 15.f : 30.f;
	(void)ns;
	_tape_speed_button.set_text (ns >= 30.f ? "30" : "15");

	/* Alt + tranche sélectionnée -> même vitesse sur toute la sélection */
	if (trident_link_alt ()) {
		std::shared_ptr<ARDOUR::Route> me = route ();
		Mixer_UI* mx = Mixer_UI::instance ();
		if (mx && me && me->is_selected ()) {
			for (MixerStrip* s : mx->mixer_strips ()) {
				if (s == this) { continue; }
				std::shared_ptr<ARDOUR::Route> sr = s->route ();
				if (sr && sr->is_selected ()) {
					auto st = s->trident_tail_target ();
					if (st) {
						(void)ns;
						s->_tape_speed_button.set_text (ns >= 30.f ? "30" : "15");
					}
				}
			}
		}
	}
}

void
MixerStrip::trident_collapse_toggle (int which)
{
	/* REPLI purement visuel (le DSP reste actif), pour gagner de la place. */
	switch (which) {
		case 0: _gate_coll    = !_gate_coll;    break;
		case 1: _eq_coll      = !_eq_coll;      break;
		case 2: _comp_coll    = !_comp_coll;    break;
		case 3: _tape_coll    = !_tape_coll;    break;
		case 4: _buscomp_coll = !_buscomp_coll; break;
		case 5: _lim_coll     = !_lim_coll;     break;
		default: break;
	}
	trident_update_section_visibility ();
	trident_update_buttons ();
}

void
MixerStrip::trident_update_buttons ()
{
	if (!_oxford) { return; }
	std::shared_ptr<ARDOUR::OxfordChannel> tail = trident_tail_target ();
	/* LED on/off = état DSP de chaque section (tape/limiter = tail sur le master) */
	_eq_pwr.set_active_state       (_oxford->eqEnabled () ? Gtkmm2ext::ExplicitActive : Gtkmm2ext::Off);
	_hpf_round.set_active          (_oxford->hpfOn ());
	_eq_lf_shelf_btn.set_active_state (_oxford->lfShelf () ? Gtkmm2ext::ExplicitActive : Gtkmm2ext::Off);
	_eq_hf_shelf_btn.set_active_state (_oxford->hfShelf () ? Gtkmm2ext::ExplicitActive : Gtkmm2ext::Off);
	_eq_curve_btn.set_text         (eq_curve_name (_oxford->curveType ()));
	_comp_pwr.set_active_state     (_oxford->compOn ()    ? Gtkmm2ext::ExplicitActive : Gtkmm2ext::Off);
	_gate_pwr.set_active_state     (_oxford->gateOn ()    ? Gtkmm2ext::ExplicitActive : Gtkmm2ext::Off);
	_tape_pwr.set_active_state     (tail->tapeOn ()        ? Gtkmm2ext::ExplicitActive : Gtkmm2ext::Off);
	_bus_comp_pwr.set_active_state (_oxford->busCompOn () ? Gtkmm2ext::ExplicitActive : Gtkmm2ext::Off);
	_limiter_pwr.set_active_state  (tail->busLimOn ()     ? Gtkmm2ext::ExplicitActive : Gtkmm2ext::Off);

	/* marqueur de repli : ▸ = replié, ▾ = déplié (sans suffixe -/+) */
	_gate_on_button.set_text  (_gate_coll    ? "▸ Gate"    : "▾ Gate");
	_eq_on_button.set_text    (_eq_coll      ? "▸ EQ"      : "▾ EQ");
	_comp_on_button.set_text  (_comp_coll    ? "▸ Comp"    : "▾ Comp");
	_tape_on_button.set_text  (_tape_coll    ? "▸ Tape"    : "▾ Tape");
	_bus_comp_button.set_text (_buscomp_coll ? "▸ Comp"    : "▾ Comp");
	_limiter_button.set_text  (_lim_coll     ? "▸ Limiter" : "▾ Limiter");
}

void
MixerStrip::trident_update_section_visibility ()
{
	/* Le repli est PUREMENT visuel (le DSP reste actif). */
	if (!_oxford) { return; }
	if (_oxford->kind () == ARDOUR::OxfordChannel::Strip) {
		if (_gate_coll) { _gate_knobs.hide (); }    else { _gate_knobs.show (); }
		if (_eq_coll)   { _eq_knob_table.hide (); } else { _eq_knob_table.show (); }
		if (_comp_coll) { _comp_knobs.hide (); }    else { _comp_knobs.show (); }
	} else {
		if (_oxford->isFxBus ()) {                 // bus FX : pas de comp du tout
			_bus_comp_hdr.hide (); _bus_comp_knobs.hide ();
		} else if (_buscomp_coll) { _bus_comp_knobs.hide (); } else { _bus_comp_knobs.show (); }
		if (_tape_coll)    { _bus_knobs.hide (); }      else { _bus_knobs.show (); }
		if (_lim_coll || !(_route && _route->is_master ())) { _tk_lim_ceil->hide (); _tk_lim_gr->hide (); } else { _tk_lim_ceil->show (); _tk_lim_gr->show (); }
	}
}

void
MixerStrip::set_stuff_from_route ()
{
	/* if width is not set, it will be set by the MixerUI or editor */

	Width width;
	if (get_gui_property ("strip-width", width)) {
		set_width_enum (width, this);
	}
}

void
MixerStrip::update_spacer ()
{
	if (_scrollbar_spacer_height == 0) {
		Gtk::Window window (WINDOW_TOPLEVEL);
		Gtk::ScrolledWindow scroller;
		scroller.set_policy (Gtk::POLICY_ALWAYS, Gtk::POLICY_ALWAYS);
		scroller.set_name ("MixerWindow");
		window.add (scroller);
		scroller.ensure_style();

		const Scrollbar* scrollbar = scroller.get_hscrollbar();
		Gtk::Requisition requisition(scrollbar->size_request ());
		_scrollbar_spacer_height = requisition.height;

		gint scrollbar_spacing = 0;
		gtk_widget_style_get (GTK_WIDGET (scroller.gobj()), "scrollbar-spacing", &scrollbar_spacing, NULL);
		_scrollbar_spacer_height += scrollbar_spacing;

		_scrollbar_spacer_height += 6; // track_display_frame border/shadow
	}
	spacer.set_size_request (-1, _scrollbar_spacer_height);
}

void
MixerStrip::dpi_reset ()
{
	set_width_enum (_width, _width_owner);
	_scrollbar_spacer_height = 0;
	update_spacer ();
}

void
MixerStrip::set_width_enum (Width w, void* owner)
{
	/* always set the gpm width again, things may be hidden */

	gpm.set_width (w);
	panners.set_width (w);

	std::shared_ptr<AutomationList> gain_automation = _route->gain_control()->alist();

	_width_owner = owner;

	_width = w;

	if (_width_owner == this) {
		set_gui_property ("strip-width", _width);
	}

	set_button_names ();

	const float scale = std::max(1.f, UIConfiguration::instance().get_ui_scale());

	gpm.gain_automation_state_button.set_text (GainMeterBase::short_astate_string (gain_automation->automation_state()));

	if (_route->panner()) {
		((Gtk::Label*)panners.pan_automation_state_button.get_child())->set_text (GainMeterBase::short_astate_string (_route->pannable()->automation_state()));
	}

	switch (w) {
	case Wide:

		if (show_sends_button)  {
			show_sends_button->set_text (_("Show Sends"));
		}

		{
			// panners expect an even number of horiz. pixels
			int width = rintf (max (110.f * scale, gpm.get_gm_width() + 10.f * scale)) + 1;
			width &= ~1;
			set_size_request (width, -1);
		}
		break;

	case Narrow:

		if (show_sends_button) {
			show_sends_button->set_text (_("Show"));
		}

		gain_meter().setup_meters (); // recalc meter width

		{
			// panners expect an even number of horiz. pixels
			int width = rintf (max (60.f * scale, gpm.get_gm_width() + 10.f * scale)) + 1;
			width &= ~1;
			set_size_request (width, -1);
		}
		break;
	}

	processor_box.set_width (w);

	update_input_display ();
	update_output_display ();
	setup_comment_button ();
	route_group_changed ();
	name_changed ();
	WidthChanged ();
}

void
MixerStrip::set_packed (bool yn)
{
	_packed = yn;
}

void
MixerStrip::connect_to_pan ()
{
	ENSURE_GUI_THREAD (*this, &MixerStrip::connect_to_pan)

	panstate_connection.disconnect ();
	panstyle_connection.disconnect ();

	if (!_route->panner()) {
		return;
	}

	std::shared_ptr<Pannable> p = _route->pannable ();

	p->automation_state_changed.connect (panstate_connection, invalidator (*this), std::bind (&PannerUI::pan_automation_state_changed, &panners), gui_context());

	/* This call reduncant, PannerUI::set_panner() connects to _panshell->Changed itself
	 * However, that only works a panner was previously set.
	 *
	 * PannerUI must remain subscribed to _panshell->Changed() in case
	 * we switch the panner eg. AUX-Send and back
	 * _route->panner_shell()->Changed() vs _panshell->Changed
	 */
	if (panners._panner == 0) {
		panners.panshell_changed ();
	}
	update_panner_choices();

	/* TRIDENT : le panner peut n'être prêt qu'après le build du strip (ex. piste
	 * stéréo neuve) -> on réévalue ici l'affichage du potard Width. */
	trident_pan_setup ();
}

void
MixerStrip::update_panner_choices ()
{
	/* code-dup TriggerStrip::update_panner_choices */
	ENSURE_GUI_THREAD (*this, &MixerStrip::update_panner_choices)
	if (!_route->panner_shell()) { return; }

	uint32_t in = _route->output()->n_ports().n_audio();
	uint32_t out = in;
	if (_route->panner()) {
		in = _route->panner()->in().n_audio();
	}

	panners.set_available_panners(PannerManager::instance().PannerManager::get_available_panners(in, out));
}


void
MixerStrip::update_input_display ()
{
	panners.setup_pan ();

	if (has_audio_outputs () && !_route->is_surround_master ()) {
		panners.show_all ();
	} else {
		panners.hide_all ();
	}

}

void
MixerStrip::update_output_display ()
{
	gpm.setup_meters ();
	panners.setup_pan ();

	if (has_audio_outputs () && !_route->is_surround_master ()) {
		panners.show_all ();
	} else {
		panners.hide_all ();
	}
}

void
MixerStrip::fast_update ()
{
	gpm.update_meters ();
}

void
MixerStrip::io_changed_proxy ()
{
	Glib::signal_idle().connect_once (sigc::mem_fun (*this, &MixerStrip::update_panner_choices));
	Glib::signal_idle().connect_once (sigc::mem_fun (*this, &MixerStrip::update_trim_control));
}

void
MixerStrip::setup_comment_button ()
{
	std::string comment = _route->comment();

	set_tooltip (_comment_button, comment.empty() ? _("Click to add/edit comments") : _route->comment());

	if (comment.empty ()) {
		_comment_button.set_name ("generic button");
		_comment_button.set_text (_width  == Wide ? _("Comments") : _("Cmt"));
		return;
	}

	_comment_button.set_name ("comment button");

	string::size_type pos = comment.find_first_of (" \t\n");
	if (pos != string::npos) {
		comment = comment.substr (0, pos);
	}
	if (comment.empty()) {
		_comment_button.set_text (_width  == Wide ? _("Comments") : _("Cmt"));
	} else {
		_comment_button.set_text (comment);
	}
}

bool
MixerStrip::select_route_group (GdkEventButton *ev)
{
	using namespace Menu_Helpers;

	if (ev->button == 1) {

		if (group_menu == 0) {

			PropertyList* plist = new PropertyList();

			plist->add (Properties::group_gain, true);
			plist->add (Properties::group_mute, true);
			plist->add (Properties::group_solo, true);

			group_menu = new RouteGroupMenu (_session, plist);
		}

		WeakRouteList r;
		r.push_back (route ());
		group_menu->build (r);

		std::shared_ptr<RouteGroup> rg = _route->route_group();

		Gtkmm2ext::anchored_menu_popup(group_menu->menu(), &group_button,
		                               rg ? rg->name() : _("No Group"),
		                               1, ev->time);
	}

	return true;
}

void
MixerStrip::route_group_changed ()
{
	ENSURE_GUI_THREAD (*this, &MixerStrip::route_group_changed);

	std::shared_ptr<RouteGroup> rg (_route->route_group());

	if (rg) {
		group_button.set_text (PBD::short_version (rg->name(), 5));
	} else {
		switch (_width) {
		case Wide:
			group_button.set_text (_("Grp"));
			break;
		case Narrow:
			group_button.set_text (_("~G"));
			break;
		}
	}
}

void
MixerStrip::route_color_changed ()
{
	using namespace ARDOUR_UI_UTILS;
	name_button.modify_bg (STATE_NORMAL, color());
	_color_band.modify_bg (STATE_NORMAL, color());
	Gtkmm2ext::Color c (gdk_color_to_rgba (color()));
	number_label.set_fixed_colors (c, c);
	reset_strip_style ();
}

void
MixerStrip::show_passthru_color ()
{
	reset_strip_style ();
}

void
MixerStrip::build_route_ops_menu ()
{
	using namespace Menu_Helpers;
	route_ops_menu = new Menu;
	route_ops_menu->set_name ("ArdourContextMenu");

	bool active = _route->active () || ARDOUR::Profile->get_mixbus();

	MenuList& items = route_ops_menu->items();

	if (active) {
		Gtk::Window* top = dynamic_cast<Gtk::Window*> (get_toplevel());

		items.push_back (MenuElem (_("Color..."), sigc::bind (sigc::mem_fun (*this, &RouteUI::choose_color), top)));

		items.push_back (MenuElem (_("Comments..."), sigc::mem_fun (*this, &RouteUI::open_comment_editor)));

		items.push_back (MenuElem (_("Inputs..."), sigc::mem_fun (*this, &RouteUI::edit_input_configuration)));

		items.push_back (MenuElem (_("Outputs..."), sigc::mem_fun (*this, &RouteUI::edit_output_configuration)));

		if (!Profile->get_mixbus()) {
			items.push_back (SeparatorElem());
		}

		if (!_route->is_singleton ()
#ifdef MIXBUS
				&& !_route->mixbus()
#endif
		   ) {
			if (Profile->get_mixbus()) {
				items.push_back (SeparatorElem());
			}
			items.push_back (MenuElem (_("Save As Template..."), sigc::mem_fun(*this, &RouteUI::save_as_template)));
		}

		if (!Profile->get_mixbus()) {
			items.push_back (MenuElem (_("Rename..."), sigc::mem_fun(*this, &RouteUI::route_rename)));
			/* do not allow rename if the track is record-enabled */
			items.back().set_sensitive (!is_track() || !track()->rec_enable_control()->get_value());
		}
	}

	if ((!_route->is_singleton () || !active)
#ifdef MIXBUS
			&& !_route->mixbus()
#endif
	   )
	{
		if (active) {
			items.push_back (SeparatorElem());
		}
		items.push_back (CheckMenuElem (_("Active")));
		Gtk::CheckMenuItem* i = dynamic_cast<Gtk::CheckMenuItem *> (&items.back());
		i->set_active (active);
		i->set_sensitive (!_session->transport_rolling());
		i->signal_activate().connect (sigc::bind (sigc::mem_fun (*this, &RouteUI::set_route_active), !_route->active(), false));
	}

	/* Plugin / Processor related */
	if (active) {
		items.push_back (SeparatorElem());
	}

	if (active && !Profile->get_mixbus ()) {
		items.push_back (CheckMenuElem (_("Strict I/O")));
		Gtk::CheckMenuItem* i = dynamic_cast<Gtk::CheckMenuItem *> (&items.back());
		i->set_active (_route->strict_io());
		i->signal_activate().connect (sigc::hide_return (sigc::bind (sigc::mem_fun (*_route, &Route::set_strict_io), !_route->strict_io())));
	}

	uint32_t plugin_insert_cnt = 0;
	_route->foreach_processor (std::bind (RouteUI::help_count_plugins, _1, & plugin_insert_cnt));
	if (active && plugin_insert_cnt > 0) {
		items.push_back (MenuElem (_("Pin Connections..."), sigc::mem_fun (*this, &RouteUI::manage_pins)));
	}

	if (active) {
		items.push_back (CheckMenuElem (_("Protect Against Denormals"), sigc::mem_fun (*this, &RouteUI::toggle_denormal_protection)));
		denormal_menu_item = dynamic_cast<Gtk::CheckMenuItem *> (&items.back());
		denormal_menu_item->set_active (_route->denormal_protection());
	}

	if (active && !_route->presentation_info().special (false)) {
		items.push_back (CheckMenuElem (_("RTA")));
		Gtk::CheckMenuItem* i = dynamic_cast<Gtk::CheckMenuItem *> (&items.back());
		bool attached = RTAManager::instance ()->attached (_route);
		i->set_active (attached);
		i->signal_activate().connect ([this, attached]() {
				if (attached) {
					RTAManager::instance ()->remove (_route);
				} else {
					RTAManager::instance ()->attach (_route);
					ARDOUR_UI::instance()->show_realtime_analyzer ();
				}
			});
	}

	/* Disk I/O */

	if (active && is_track()) {
		items.push_back (SeparatorElem());
		Gtk::Menu* dio_menu = new Menu;
		MenuList& dio_items = dio_menu->items();
		dio_items.push_back (MenuElem (_("Record Pre-Fader"), sigc::bind (sigc::mem_fun (*this, &RouteUI::set_disk_io_point), DiskIOPreFader)));
		dio_items.push_back (MenuElem (_("Record Post-Fader"), sigc::bind (sigc::mem_fun (*this, &RouteUI::set_disk_io_point), DiskIOPostFader)));
		dio_items.push_back (MenuElem (_("Custom Record+Playback Positions"), sigc::bind (sigc::mem_fun (*this, &RouteUI::set_disk_io_point), DiskIOCustom)));
		items.push_back (MenuElem (_("Disk I/O..."), *dio_menu));
	}

	/* MIDI */

	if (active && (std::dynamic_pointer_cast<MidiTrack>(_route) || _route->the_instrument ())) {
		items.push_back (SeparatorElem());
		items.push_back (MenuElem (_("Patch Selector..."),
					sigc::mem_fun(*this, &RouteUI::select_midi_patch)));
	}

	if (active && _route->the_instrument () && _route->the_instrument ()->output_streams().n_audio() > 2) {
		// TODO ..->n_audio() > 1 && separate_output_groups) hard to check here every time.
		items.push_back (MenuElem (_("Fan out to Busses"), sigc::bind (sigc::mem_fun (*this, &RouteUI::fan_out), true, true)));
		items.push_back (MenuElem (_("Fan out to Tracks"), sigc::bind (sigc::mem_fun (*this, &RouteUI::fan_out), false, true)));
	}

	/* note that this relies on selection being shared across editor and
	 * mixer (or global to the backend, in the future), which is the only
	 * sane thing for users anyway.
	 */
	StripableTimeAxisView* stav = PublicEditor::instance().get_stripable_time_axis_by_id (_route->id());
	if (stav) {
		Selection& selection (PublicEditor::instance().get_selection());
		if (!selection.selected (stav)) {
			selection.set (stav);
		}

#ifdef MIXBUS
		if (_route->mixbus()) {
			/* no dup, no remove */
			return;
		}
#endif
		if (_route->is_singleton ()) {
			return;
		}

		if (active) {
			items.push_back (SeparatorElem());
			items.push_back (MenuElem (_("Duplicate..."), sigc::mem_fun (*this, &RouteUI::duplicate_selected_routes)));
		}
		items.push_back (SeparatorElem());
		items.push_back (MenuElem (_("Remove"), sigc::mem_fun(PublicEditor::instance(), &PublicEditor::remove_tracks)));
	}
}

gboolean
MixerStrip::name_button_button_press (GdkEventButton* ev)
{
	if (ev->button == 1 && ev->type == GDK_BUTTON_PRESS) {
		/* OXFORD : on arme le glisser-déplacer (il ne démarre qu'au-delà du
		 * seuil de mouvement, pour ne pas gêner le simple clic de sélection). */
		_name_drag_x0 = ev->x_root;
		_name_drag_armed = true;
		/* fall thru to mixer */
		return false;
	}

	if (ev->button == 1 && ev->type == GDK_2BUTTON_PRESS) {
		route_rename ();
		return true;
	}

	if (ev->button == 3 && ARDOUR::Profile->get_livetrax() && _route && _route->is_singleton ()) {
		return true;
	}

	if (ev->button == 3) {
		list_route_operations ();

		if (ev->button == 1) {
			Gtkmm2ext::anchored_menu_popup(route_ops_menu, &name_button, "",
			                               1, ev->time);
		} else {
			route_ops_menu->popup (ev->button, ev->time);
		}

		return true;
	}

	return false;
}

gboolean
MixerStrip::number_button_button_press (GdkEventButton* ev)
{
	if (ev->type == GDK_2BUTTON_PRESS) {
		choose_color (dynamic_cast<Gtk::Window*> (get_toplevel()));
		return true;
	}

	if (ev->button == 3) {
		list_route_operations ();

		route_ops_menu->popup (ev->button, ev->time);

		return true;
	}

	return false;
}

void
MixerStrip::list_route_operations ()
{
	delete route_ops_menu;
	build_route_ops_menu ();
}

void
MixerStrip::set_selected (bool yn)
{
	AxisView::set_selected (yn);

	if (selected()) {
		global_frame.set_shadow_type (Gtk::SHADOW_ETCHED_OUT);
		global_frame.set_name ("MixerStripSelectedFrame");
	} else {
		global_frame.set_shadow_type (Gtk::SHADOW_IN);
		global_frame.set_name ("MixerStripFrame");
	}

	global_frame.queue_draw ();

//	if (!yn)
//		processor_box.deselect_all_processors();
}

void
MixerStrip::route_property_changed (const PropertyChange& what_changed)
{
	if (what_changed.contains (ARDOUR::Properties::name)) {
		name_changed ();
	}
}

void
MixerStrip::name_changed ()
{
	switch (_width) {
		case Wide:
			name_button.set_text (_route->name());
			break;
		case Narrow:
			name_button.set_text (PBD::short_version (_route->name(), 5));
			break;
	}

	set_tooltip (name_button, Gtkmm2ext::markup_escape_text(_route->name()));

	if (_session->config.get_track_name_number()) {
		const int64_t track_number = _route->track_number ();
		if (track_number == 0) {
			number_label.set_text ("-");
		} else {
			number_label.set_text (PBD::to_string (abs(_route->track_number ())));
		}
	} else {
		number_label.set_text ("");
	}
}

/* OXFORD : glisser-déplacer de tranche. Le seuil évite de transformer un clic
 * de sélection en déplacement ; le relâchement applique l'ordre (Mixer_UI). */
bool
MixerStrip::name_button_motion (GdkEventMotion* ev)
{
	if (_mixer.strip_dragging ()) {
		_mixer.mid_strip_drag ((int) ev->x_root);
		gdk_event_request_motions (ev);
		return true;
	}

	if (!_name_drag_armed || !(ev->state & GDK_BUTTON1_MASK)) {
		return false;
	}

	const double thresh = 6.0 * UIConfiguration::instance().get_ui_scale ();
	if (fabs (ev->x_root - _name_drag_x0) < thresh) {
		return false;
	}

	_mixer.start_strip_drag (this);
	_mixer.mid_strip_drag ((int) ev->x_root);
	gdk_event_request_motions (ev);
	return true;
}

gboolean
MixerStrip::name_button_button_release (GdkEventButton* ev)
{
	_name_drag_armed = false;
	if (ev->button == 1 && _mixer.strip_dragging ()) {
		_mixer.end_strip_drag ();
		return true;   /* pas de clic de sélection après un déplacement */
	}
	return false;
}

void
MixerStrip::name_button_resized (Gtk::Allocation& alloc)
{
	name_button.set_layout_ellipsize_width (alloc.get_width() * PANGO_SCALE);
}

void
MixerStrip::comment_button_resized (Gtk::Allocation& alloc)
{
	_comment_button.set_layout_ellipsize_width (alloc.get_width() * PANGO_SCALE);
}

bool
MixerStrip::width_button_pressed (GdkEventButton* ev)
{
	if (ev->button != 1) {
		return false;
	}

	if (Keyboard::modifier_state_contains (ev->state, Keyboard::ModifierMask (Keyboard::PrimaryModifier | Keyboard::TertiaryModifier)) && _mixer_owned) {
		switch (_width) {
		case Wide:
			_mixer.set_strip_width (Narrow, true);
			break;

		case Narrow:
			_mixer.set_strip_width (Wide, true);
			break;
		}
	} else {
		switch (_width) {
		case Wide:
			set_width_enum (Narrow, this);
			break;
		case Narrow:
			set_width_enum (Wide, this);
			break;
		}
	}

	return true;
}

void
MixerStrip::loudess_analysis_button_clicked ()
{
	PublicEditor::instance().loudness_assistant (false);
}

bool
MixerStrip::volume_controller_button_pressed (GdkEventButton* ev)
{
	using namespace Menu_Helpers;
	if (Keyboard::is_context_menu_event (ev)) {
		delete _master_volume_menu;
		_master_volume_menu = new Menu;
		_master_volume_menu->set_name ("ArdourContextMenu");
		MenuList& items = _master_volume_menu->items();
		items.clear ();
		items.push_back (CheckMenuElem (_("Custom LAN Amp Position")));
		Gtk::CheckMenuItem* cmi = static_cast<Gtk::CheckMenuItem*> (&items.back());
		cmi->set_active (!_route->volume_applies_to_output ());
		cmi->signal_toggled().connect (sigc::bind (sigc::mem_fun (_route.get(), &Route::set_volume_applies_to_output), !_route->volume_applies_to_output ()));

		_master_volume_menu->popup (ev->button, ev->time);
		return true;
	}
	return false;
}

void
MixerStrip::hide_clicked ()
{
	// LAME fix to reset the button status for when it is redisplayed (part 1)
	hide_button.set_sensitive(false);

	if (_embedded) {
		Hiding(); /* EMIT_SIGNAL */
	} else {
		_mixer.hide_strip (this);
	}

	// (part 2)
	hide_button.set_sensitive(true);
}

void
MixerStrip::set_embedded (bool yn)
{
	_embedded = yn;
}

void
MixerStrip::map_frozen ()
{
	ENSURE_GUI_THREAD (*this, &MixerStrip::map_frozen)

	std::shared_ptr<AudioTrack> at = audio_track();

	bool en   = _route->active () || ARDOUR::Profile->get_mixbus();

	if (at) {
		switch (at->freeze_state()) {
		case AudioTrack::Frozen:
			processor_box.set_sensitive (false);
			hide_redirect_editors ();
			break;
		default:
			processor_box.set_sensitive (en);
			break;
		}
	} else {
		processor_box.set_sensitive (en);
	}
	RouteUI::map_frozen ();
}

void
MixerStrip::hide_redirect_editors ()
{
	_route->foreach_processor (sigc::mem_fun (*this, &MixerStrip::hide_processor_editor));
}

void
MixerStrip::hide_processor_editor (std::weak_ptr<Processor> p)
{
	std::shared_ptr<Processor> processor (p.lock ());
	if (!processor) {
		return;
	}

	Gtk::Window* w = processor_box.get_processor_ui (processor);

	if (w) {
		w->hide ();
	}
}

void
MixerStrip::reset_strip_style ()
{
	if (_current_delivery && std::dynamic_pointer_cast<Send>(_current_delivery)) {

		gpm.unset_fader_fg ();
		gpm.set_fader_name ("SendStripBase");

	} else {

		if (is_midi_track()) {
			if (_route->active()) {
				set_name ("MidiTrackStripBase");
			} else {
				set_name ("MidiTrackStripBaseInactive");
			}
			if (UIConfiguration::instance().get_use_route_color_widely()) {
				// gpm.set_fader_bg ();
				gpm.set_fader_fg (gdk_color_to_rgba (route_color_tint()));
			} else {
				gpm.unset_fader_fg ();
			}
			gpm.set_fader_name ("MidiTrackFader");
		} else if (is_audio_track()) {
			if (_route->active()) {
				set_name ("AudioTrackStripBase");
			} else {
				set_name ("AudioTrackStripBaseInactive");
			}
			if (UIConfiguration::instance().get_use_route_color_widely()) {
				// gpm.set_fader_bg ();
				gpm.set_fader_fg (gdk_color_to_rgba (route_color_tint()));
			} else {
				gpm.unset_fader_fg ();
			}
			gpm.set_fader_name ("AudioTrackFader");
		} else {
			if (_route->active()) {
				set_name ("AudioBusStripBase");
			} else {
				set_name ("AudioBusStripBaseInactive");
			}

			if (!is_master() && UIConfiguration::instance().get_use_route_color_widely()) {
				// gpm.set_fader_bg ();
				gpm.set_fader_fg (gdk_color_to_rgba (route_color_tint()));
			} else {
				gpm.unset_fader_fg ();
			}
			gpm.set_fader_name ("AudioBusFader");
		}
	}
}


string
MixerStrip::meter_point_string (MeterPoint mp)
{
	switch (_width) {
	case Wide:
		switch (mp) {
		case MeterInput:
			return S_("MeterWide|In");
			break;

		case MeterPreFader:
			return S_("MeterWide|Pre");
			break;

		case MeterPostFader:
			return S_("MeterWide|Post");
			break;

		case MeterOutput:
			return S_("MeterWide|Out");
			break;

		case MeterCustom:
		default:
			return S_("MeterWide|Custom");
			break;
		}
		break;
	case Narrow:
		switch (mp) {
		case MeterInput:
			return S_("Meter|In");
			break;

		case MeterPreFader:
			return S_("Meter|Pr");
			break;

		case MeterPostFader:
			return S_("Meter|Po");
			break;

		case MeterOutput:
			return S_("Meter|O");
			break;

		case MeterCustom:
		default:
			return S_("Meter|C");
			break;
		}
		break;
	}

	return string();
}

/** Called when the monitor-section state */
void
MixerStrip::monitor_changed ()
{
	assert (monitor_section_button);
	if (_session->monitor_active()) {
		monitor_section_button->set_name ("master monitor section button active");
	} else {
		monitor_section_button->set_name ("master monitor section button normal");
	}
}

void
MixerStrip::monitor_section_added_or_removed ()
{
	assert (monitor_section_button);
	if (mute_button->get_parent()) {
		mute_button->get_parent()->remove(*mute_button);
	}
	if (monitor_section_button->get_parent()) {
		monitor_section_button->get_parent()->remove(*monitor_section_button);
	}
	if (_session && _session->monitor_out ()) {
		mute_solo_table.attach (*mute_button, 0, 1, 0, 1);
		mute_solo_table.attach (*monitor_section_button, 1, 2, 0, 1);
		mute_button->show();
		monitor_section_button->show();
	} else {
		mute_solo_table.attach (*mute_button, 0, 2, 0, 1);
		mute_button->show();
	}
}

/** Called when the metering point has changed */
void
MixerStrip::meter_changed ()
{
	gpm.meter_point_button.set_text (meter_point_string (_route->meter_point()));
	gpm.setup_meters ();
	// reset peak when meter point changes
	gpm.reset_peak_display();
}

/** The bus that we are displaying sends to has changed, or been turned off.
 *  @param send_to New bus that we are displaying sends to, or 0.
 */
void
MixerStrip::bus_send_display_changed (std::shared_ptr<Route> send_to)
{
	RouteUI::bus_send_display_changed (send_to);

	if (send_to) {
		std::shared_ptr<Send> send = _route->internal_send_for (send_to);

		if (send) {
			show_send (send);
		} else {
			revert_to_default_display ();
		}
	} else {
		revert_to_default_display ();
	}
}

void
MixerStrip::drop_send ()
{
	std::shared_ptr<Send> current_send;

	if (_current_delivery && ((current_send = std::dynamic_pointer_cast<Send>(_current_delivery)) != 0)) {
		current_send->set_metering (false);
	}

	send_gone_connection.disconnect ();
	RouteUI::check_rec_enable_sensitivity ();
}

void
MixerStrip::set_current_delivery (std::shared_ptr<Delivery> d)
{
	_current_delivery = d;
	setup_invert_buttons ();
	DeliveryChanged (_current_delivery);
	update_sensitivity ();
}

void
MixerStrip::show_send (std::shared_ptr<Send> send)
{
	assert (send != 0);

	drop_send ();

	set_current_delivery (send);

	send->meter()->set_meter_type (_route->meter_type ());
	send->set_metering (true);
	_current_delivery->DropReferences.connect (send_gone_connection, invalidator (*this), std::bind (&MixerStrip::revert_to_default_display, this), gui_context());

	gain_meter().set_controls (_route, send->meter(), send->amp(), send->gain_control());
	gain_meter().setup_meters ();

	uint32_t const in = _current_delivery->pans_required();
	uint32_t const out = _current_delivery->pan_outs();

	panner_ui().set_panner (_current_delivery->panner_shell(), _current_delivery->panner());
	panner_ui().set_available_panners(PannerManager::instance().PannerManager::get_available_panners(in, out));
	panner_ui().setup_pan ();
	panner_ui().set_send_drawing_mode (true);
	panner_ui().show_all ();

	reset_strip_style ();
}

void
MixerStrip::revert_to_default_display ()
{
	drop_send ();

	set_current_delivery (_route->main_outs ());

	gain_meter().set_controls (_route, _route->shared_peak_meter(), _route->amp(), _route->gain_control());
	gain_meter().setup_meters ();

	panner_ui().set_panner (_route->main_outs()->panner_shell(), _route->main_outs()->panner());
	update_panner_choices();
	panner_ui().setup_pan ();
	panner_ui().set_send_drawing_mode (false);

	if (has_audio_outputs () && !_route->is_surround_master ()) {
		panners.show_all ();
	} else {
		panners.hide_all ();
	}

	reset_strip_style ();
}

void
MixerStrip::set_button_names ()
{
	switch (_width) {
	case Wide:
		mute_button->set_text (_("Mute"));
		monitor_input_button->set_text (S_("Monitor|In"));
		monitor_disk_button->set_text (S_("Monitor|Disk"));
		if (monitor_section_button) {
			monitor_section_button->set_text (_("Mon"));
		}

		if ((_route && _route->solo_safe_control()->solo_safe()) || !solo_button->get_sensitive()) {
			solo_button->set_visual_state (Gtkmm2ext::VisualState (solo_button->visual_state() | Gtkmm2ext::Insensitive));
		} else {
			solo_button->set_visual_state (Gtkmm2ext::VisualState (solo_button->visual_state() & ~Gtkmm2ext::Insensitive));
		}
		if (!Config->get_solo_control_is_listen_control()) {
			solo_button->set_text (_("Solo"));
		} else {
			switch (Config->get_listen_position()) {
			case AfterFaderListen:
				solo_button->set_text (_("AFL"));
				break;
			case PreFaderListen:
				solo_button->set_text (_("PFL"));
				break;
			}
		}
		solo_isolated_led->set_text (_("Iso"));
		solo_safe_led->set_text (S_("SoloLock|Lock"));
		break;

	default:
		if (is_master ()) {
			/* master bus has no solo button, "Mute" fits in narrow mode */
			mute_button->set_text (_("Mute"));
		} else {
			mute_button->set_text (S_("Mute|M"));
		}
		monitor_input_button->set_text (S_("MonitorInput|I"));
		monitor_disk_button->set_text (S_("MonitorDisk|D"));
		if (monitor_section_button) {
			monitor_section_button->set_text (_("Mon"));
		}

		if ((_route && _route->solo_safe_control()->solo_safe()) || !solo_button->get_sensitive()) {
			solo_button->set_visual_state (Gtkmm2ext::VisualState (solo_button->visual_state() | Gtkmm2ext::Insensitive));
		} else {
			solo_button->set_visual_state (Gtkmm2ext::VisualState (solo_button->visual_state() & ~Gtkmm2ext::Insensitive));
		}
		if (!Config->get_solo_control_is_listen_control()) {
			solo_button->set_text (S_("Solo|S"));
		} else {
			switch (Config->get_listen_position()) {
			case AfterFaderListen:
				solo_button->set_text (S_("AfterFader|A"));
				break;
			case PreFaderListen:
				solo_button->set_text (S_("Prefader|P"));
				break;
			}
		}

		solo_isolated_led->set_text (S_("SoloIso|I"));
		solo_safe_led->set_text (S_("SoloLock|L"));
		break;
	}

	if (_route) {
		gpm.meter_point_button.set_text (meter_point_string (_route->meter_point()));
	} else {
		gpm.meter_point_button.set_text ("");
	}
}

PluginSelector*
MixerStrip::plugin_selector()
{
	return _mixer.plugin_selector();
}

void
MixerStrip::hide_things ()
{
	processor_box.hide_things ();
}

bool
MixerStrip::input_active_button_press (GdkEventButton*)
{
	/* nothing happens on press */
	return true;
}

bool
MixerStrip::input_active_button_release (GdkEventButton* ev)
{
	std::shared_ptr<MidiTrack> mt = midi_track ();

	if (!mt) {
		return true;
	}

	std::shared_ptr<RouteList> rl (new RouteList);

	rl->push_back (route());

	_session->set_exclusive_input_active (rl, !mt->input_active(),
					      Keyboard::modifier_state_contains (ev->state, Keyboard::ModifierMask (Keyboard::PrimaryModifier|Keyboard::SecondaryModifier)));

	return true;
}

void
MixerStrip::midi_input_status_changed ()
{
	std::shared_ptr<MidiTrack> mt = midi_track ();
	assert (mt);
	midi_input_enable_button.set_active (mt->input_active ());
}

string
MixerStrip::state_id () const
{
	return string_compose ("strip %1", _route->id().to_s());
}

void
MixerStrip::parameter_changed (string p)
{
	if (p == _visibility.get_state_name()) {
		/* The user has made changes to the mixer strip visibility, so get
		   our VisibilityGroup to reflect these changes in our widgets.
		*/
		_visibility.set_state (UIConfiguration::instance().get_mixer_strip_visibility ());
	} else if (p == "track-name-number") {
		name_changed ();
		update_track_number_visibility();
	} else if (p == "use-master-volume") {
		if (route () && route()->is_master()) {
			if (Config->get_use_master_volume ()) {
				master_volume_table.show ();
			} else {
				master_volume_table.hide ();
			}
		}
	} else if (p == "show-triggers-inline") {
		/* XXX do something or get rid of this parameter */
	} else if (p == "use-route-color-widely") {
		reset_strip_style ();
	}
}

/** Called to decide whether the solo isolate / solo lock button visibility should
 *  be overridden from that configured by the user.  We do this for the master bus.
 *
 *  @return optional value that is present if visibility state should be overridden.
 */
std::optional<bool>
MixerStrip::override_solo_visibility () const
{
	if (is_master ()) {
		return std::optional<bool> (false);
	}

	return std::optional<bool> ();
}

std::optional<bool>
MixerStrip::override_rec_mon_visibility () const
{
	if (is_master ()) {
		return std::optional<bool> (false);
	}

	return std::optional<bool> ();
}


void
MixerStrip::add_input_port (DataType t)
{
	_route->input()->add_port ("", t);
}

void
MixerStrip::add_output_port (DataType t)
{
	_route->output()->add_port ("", t);
}

void
MixerStrip::route_active_changed ()
{
	RouteUI::route_active_changed ();
	reset_strip_style ();
	update_sensitivity ();
}

void
MixerStrip::update_sensitivity ()
{
	bool en   = _route->active () || ARDOUR::Profile->get_mixbus();
	bool send = _current_delivery && std::dynamic_pointer_cast<Send>(_current_delivery) != 0;
	bool aux  = _current_delivery && std::dynamic_pointer_cast<InternalSend>(_current_delivery) != 0;

	if (route()->is_main_bus ()) {
		solo_iso_table.set_sensitive (false);
		control_slave_ui.set_sensitive (false);
	} else {
		solo_iso_table.set_sensitive (en && !send);
		control_slave_ui.set_sensitive (en && !send);
	}

	input_button.set_sensitive (en && !send);
	group_button.set_sensitive (en && !send);
	rta_button->set_sensitive (en && !send);
	gpm.meter_point_button.set_sensitive (en && !send);
	mute_button->set_sensitive (en && !send);
	solo_button->set_sensitive (en && !send);
	solo_isolated_led->set_sensitive (en && !send);
	solo_safe_led->set_sensitive (en && !send);
	monitor_input_button->set_sensitive (en && !send);
	monitor_disk_button->set_sensitive (en && !send);
	_comment_button.set_sensitive (en && !send);
	trim_control.set_sensitive (en && !send);
	control_slave_ui.set_sensitive (en && !send);
	midi_input_enable_button.set_sensitive (en && !send);

	output_button.set_sensitive (en && !aux);

	update_phase_invert_sensitivty ();
	map_frozen ();
	set_button_names (); // update solo button visual state
}

void
MixerStrip::copy_processors ()
{
	processor_box.processor_operation (ProcessorBox::ProcessorsCopy);
}

void
MixerStrip::cut_processors ()
{
	processor_box.processor_operation (ProcessorBox::ProcessorsCut);
}

void
MixerStrip::paste_processors ()
{
	processor_box.processor_operation (ProcessorBox::ProcessorsPaste);
}

void
MixerStrip::select_all_processors ()
{
	processor_box.processor_operation (ProcessorBox::ProcessorsSelectAll);
}

void
MixerStrip::deselect_all_processors ()
{
	processor_box.processor_operation (ProcessorBox::ProcessorsSelectNone);
}

bool
MixerStrip::delete_processors ()
{
	return processor_box.processor_operation (ProcessorBox::ProcessorsDelete);
}

void
MixerStrip::toggle_processors ()
{
	processor_box.processor_operation (ProcessorBox::ProcessorsToggleActive);
}

void
MixerStrip::ab_plugins ()
{
	processor_box.processor_operation (ProcessorBox::ProcessorsAB);
}

bool
MixerStrip::level_meter_button_press (GdkEventButton* ev)
{
	if (_current_delivery && std::dynamic_pointer_cast<Send>(_current_delivery)) {
		return false;
	}
	if (ev->button == 3) {
		popup_level_meter_menu (ev);
		return true;
	}

	return false;
}

void
MixerStrip::popup_level_meter_menu (GdkEventButton* ev)
{
	using namespace Gtk::Menu_Helpers;

	Gtk::Menu* m = ARDOUR_UI_UTILS::shared_popup_menu ();
	MenuList& items = m->items ();

	RadioMenuItem::Group group;

	PBD::Unwinder<bool> uw (_suspend_menu_callbacks, true);
	add_level_meter_item_point (items, group, _("Input"), MeterInput);
	add_level_meter_item_point (items, group, _("Pre Fader"), MeterPreFader);
	add_level_meter_item_point (items, group, _("Post Fader"), MeterPostFader);
	add_level_meter_item_point (items, group, _("Output"), MeterOutput);
	add_level_meter_item_point (items, group, _("Custom"), MeterCustom);

	if (gpm.meter_channels().n_audio() == 0) {
		m->popup (ev->button, ev->time);
		return;
	}

	RadioMenuItem::Group tgroup;
	items.push_back (SeparatorElem());

	add_level_meter_item_type (items, tgroup, ArdourMeter::meter_type_string(MeterPeak), MeterPeak);
	add_level_meter_item_type (items, tgroup, ArdourMeter::meter_type_string(MeterPeak0dB), MeterPeak0dB);
	add_level_meter_item_type (items, tgroup, ArdourMeter::meter_type_string(MeterKrms),  MeterKrms);
	add_level_meter_item_type (items, tgroup, ArdourMeter::meter_type_string(MeterIEC1DIN), MeterIEC1DIN);
	add_level_meter_item_type (items, tgroup, ArdourMeter::meter_type_string(MeterIEC1NOR), MeterIEC1NOR);
	add_level_meter_item_type (items, tgroup, ArdourMeter::meter_type_string(MeterIEC2BBC), MeterIEC2BBC);
	add_level_meter_item_type (items, tgroup, ArdourMeter::meter_type_string(MeterIEC2EBU), MeterIEC2EBU);
	add_level_meter_item_type (items, tgroup, ArdourMeter::meter_type_string(MeterK20), MeterK20);
	add_level_meter_item_type (items, tgroup, ArdourMeter::meter_type_string(MeterK14), MeterK14);
	add_level_meter_item_type (items, tgroup, ArdourMeter::meter_type_string(MeterK12), MeterK12);
	add_level_meter_item_type (items, tgroup, ArdourMeter::meter_type_string(MeterVU),  MeterVU);

	int _strip_type;
	if (_route->is_master()) {
		_strip_type = 4;
	}
	else if (std::dynamic_pointer_cast<AudioTrack>(_route) == 0
			&& std::dynamic_pointer_cast<MidiTrack>(_route) == 0) {
		/* non-master bus */
		_strip_type = 3;
	}
	else if (std::dynamic_pointer_cast<MidiTrack>(_route)) {
		_strip_type = 2;
	}
	else {
		_strip_type = 1;
	}

	MeterType cmt = _route->meter_type();
	const std::string cmn = ArdourMeter::meter_type_string(cmt);

	items.push_back (SeparatorElem());
	items.push_back (MenuElem (string_compose(_("Change all in Group to %1"), cmn),
				sigc::bind (SetMeterTypeMulti, -1, _route->route_group(), cmt)));
	items.push_back (MenuElem (string_compose(_("Change all to %1"), cmn),
				sigc::bind (SetMeterTypeMulti, 0, _route->route_group(), cmt)));
	items.push_back (MenuElem (string_compose(_("Change same track-type to %1"), cmn),
				sigc::bind (SetMeterTypeMulti, _strip_type, _route->route_group(), cmt)));

	m->popup (ev->button, ev->time);
}

void
MixerStrip::add_level_meter_item_point (Menu_Helpers::MenuList& items,
		RadioMenuItem::Group& group, string const & name, MeterPoint point)
{
	using namespace Menu_Helpers;

	items.push_back (RadioMenuElem (group, name, sigc::bind (sigc::mem_fun (*this, &MixerStrip::set_meter_point), point)));
	RadioMenuItem* i = dynamic_cast<RadioMenuItem *> (&items.back ());
	i->set_active (_route->meter_point() == point);
}

void
MixerStrip::set_meter_point (MeterPoint p)
{
	if (_suspend_menu_callbacks) return;
	_route->set_meter_point (p);
}

void
MixerStrip::add_level_meter_item_type (Menu_Helpers::MenuList& items,
		RadioMenuItem::Group& group, string const & name, MeterType type)
{
	using namespace Menu_Helpers;

	items.push_back (RadioMenuElem (group, name, sigc::bind (sigc::mem_fun (*this, &MixerStrip::set_meter_type), type)));
	RadioMenuItem* i = dynamic_cast<RadioMenuItem *> (&items.back ());
	i->set_active (_route->meter_type() == type);
}

void
MixerStrip::set_meter_type (MeterType t)
{
	if (_suspend_menu_callbacks) return;
	_route->set_meter_type (t);
}

void
MixerStrip::update_track_number_visibility ()
{
	DisplaySuspender ds;
	bool show_label = _session->config.get_track_name_number();

	if (_route && _route->is_master()) {
		show_label = false;
	}

	if (show_label) {
		number_label.show ();
		// see ArdourButton::on_size_request(), we should probably use a global size-group here instead.
		// except the width of the number label is subtracted from the name-hbox, so we
		// need to explicitly calculate it anyway until the name-label & entry become ArdourWidgets.
		int tnw = (2 + std::max(2u, _session->track_number_decimals())) * number_label.char_pixel_width();
		if (tnw & 1) --tnw;
		number_label.set_size_request(tnw, -1);
		number_label.show ();
	} else {
		number_label.hide ();
	}
}

Gdk::Color
MixerStrip::color () const
{
	return route_color ();
}

bool
MixerStrip::marked_for_display () const
{
	return !_route->presentation_info().hidden();
}

bool
MixerStrip::set_marked_for_display (bool yn)
{
	return RouteUI::mark_hidden (!yn);
}

void
MixerStrip::hide_master_spacer (bool yn)
{
	if (_mixer_owned && (route()->is_master() || route()->is_surround_master ()) && !yn) {
		spacer.show();
	} else {
		spacer.hide();
	}
}

