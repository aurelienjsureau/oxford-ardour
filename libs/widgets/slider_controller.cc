/*
 * Copyright (C) 1998 Paul Davis <paul@linuxaudiosystems.com>
 * Copyright (C) 2017 Robin Gareus <robin@gareus.org>
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

#include "pbd/controllable.h"

#include "widgets/ardour_fader.h"
#include "widgets/slider_controller.h"

#include "pbd/i18n.h"

using namespace PBD;
using namespace ArdourWidgets;

SliderController::SliderController (Gtk::Adjustment *adj, std::shared_ptr<PBD::Controllable> mc, int orien)
	: FaderWidget (*adj, orien)
	, _ctrl (mc)
	, _ctrl_adj (adj)
	, _spin_adj (0, 0, 1.0, .1, .01)
	, _spin (_spin_adj, 0, 2)
	, _ctrl_ignore (false)
	, _spin_ignore (false)
{
	if (_ctrl) {
		_spin_adj.set_lower (_ctrl->numeric_entry_convert (_ctrl->internal_to_interface (_ctrl->lower ()), true));
		_spin_adj.set_upper (_ctrl->numeric_entry_convert (_ctrl->internal_to_interface (_ctrl->upper ()), true));
		if (_ctrl->is_gain_like()) {
			_spin_adj.set_step_increment (0.1);
			_spin_adj.set_page_increment (1.0);
		} else {
			// TODO this may need work for some future numeric_entry_convert()
			_spin_adj.set_step_increment(_ctrl->interface_to_internal(adj->get_step_increment()) - _ctrl->lower ());
			_spin_adj.set_page_increment(_ctrl->interface_to_internal(adj->get_page_increment()) - _ctrl->lower ());
		}

		ctrl_adjusted ();

		adj->signal_value_changed().connect (sigc::mem_fun(*this, &SliderController::ctrl_adjusted));
		_spin_adj.signal_value_changed().connect (sigc::mem_fun(*this, &SliderController::spin_adjusted));

		_binding_proxy.set_controllable (_ctrl);
	}

	_spin.set_name ("SliderControllerValue");
	_spin.set_numeric (true);
	_spin.set_snap_to_ticks (false);
}

bool
SliderController::on_button_press_event (GdkEventButton *ev)
{
	if (_binding_proxy.button_press_handler (ev)) {
		return true;
	}

	return FaderWidget::on_button_press_event (ev);
}

bool
SliderController::on_enter_notify_event (GdkEventCrossing* ev)
{
	std::shared_ptr<PBD::Controllable> c (_binding_proxy.get_controllable ());
	if (c) {
		PBD::Controllable::GUIFocusChanged (std::weak_ptr<PBD::Controllable> (c));
	}
	return FaderWidget::on_enter_notify_event (ev);
}

bool
SliderController::on_leave_notify_event (GdkEventCrossing* ev)
{
	if (_binding_proxy.get_controllable()) {
		PBD::Controllable::GUIFocusChanged (std::weak_ptr<PBD::Controllable> ());
	}
	return FaderWidget::on_leave_notify_event (ev);
}

void
SliderController::ctrl_adjusted ()
{
	assert (_ctrl); // only used w/BarController
	if (_spin_ignore) return;
	_ctrl_ignore = true;

	_spin_adj.set_value (_ctrl->numeric_entry_convert (_ctrl_adj->get_value(), true));
	_ctrl_ignore = false;
}

void
SliderController::spin_adjusted ()
{
	assert (_ctrl); // only used w/BarController
	if (_ctrl_ignore) return;
	_spin_ignore = true;
	_ctrl_adj->set_value (_ctrl->numeric_entry_convert (_spin_adj.get_value (), false));
	_spin_ignore = false;
}



VSliderController::VSliderController (Gtk::Adjustment *adj, std::shared_ptr<PBD::Controllable> mc, int fader_length, int fader_girth)
	: FaderWidget (*adj, VERT)
	, SliderController (adj, mc, VERT)
	, ArdourFader (*adj, VERT, fader_length, fader_girth)
{
}

HSliderController::HSliderController (Gtk::Adjustment *adj, std::shared_ptr<PBD::Controllable> mc, int fader_length, int fader_girth)
	: FaderWidget (*adj, HORIZ)
	, SliderController (adj, mc, HORIZ)
	, ArdourFader (*adj, HORIZ, fader_length, fader_girth)
{
}
