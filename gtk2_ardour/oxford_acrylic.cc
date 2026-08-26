/*
 * OXFORD : fond acrylique translucide (Windows 11).
 */

#include <vector>

#include <ytkmm/container.h>
#include <ytkmm/widget.h>
#include <ytkmm/window.h>

#include "ui_config.h"

#include "oxford_acrylic.h"

#ifdef PLATFORM_WINDOWS
#include <ydk/gdkwin32.h>
#include <windows.h>
#include <dwmapi.h>
#endif

using namespace OxfordAcrylic;

namespace {

#ifdef PLATFORM_WINDOWS

/* Teinte de la couche acrylique, 0xAABBGGRR. À alpha nul c'était du flou pur :
 * les fonds traversants viraient au décor du bureau. Un VOILE FAIBLE aux
 * couleurs du thème actif (une teinte fixe ne convenait pas aux quatre) les
 * raccroche à la fenêtre sans rien perdre du flou. */
static const double kTintAlpha = 0.08;   // 8 % : « un peu plus opaque », pas un aplat

static DWORD
tint ()
{
	bool failed = false;
	const uint32_t c = UIConfiguration::instance ().color ("theme:bg1", &failed);   // 0xRRGGBBAA
	const DWORD r = failed ? 0x20 : ((c >> 24) & 0xff);
	const DWORD g = failed ? 0x20 : ((c >> 16) & 0xff);
	const DWORD b = failed ? 0x20 : ((c >>  8) & 0xff);
	const DWORD a = (DWORD) (kTintAlpha * 255.0 + 0.5);
	return (a << 24) | (b << 16) | (g << 8) | r;
}

enum AccentState {
	ACCENT_DISABLED                 = 0,
	ACCENT_ENABLE_ACRYLICBLURBEHIND = 4
};

struct AccentPolicy {
	int AccentState;
	int AccentFlags;
	int GradientColor;
	int AnimationId;
};

struct WinCompAttrData {
	DWORD  Attrib;
	PVOID  pvData;
	SIZE_T cbData;
};

static const DWORD WCA_ACCENT_POLICY = 19;

typedef BOOL (WINAPI * SetWindowCompositionAttribute_t) (HWND, WinCompAttrData*);

static SetWindowCompositionAttribute_t
swca ()
{
	static SetWindowCompositionAttribute_t fn = (SetWindowCompositionAttribute_t)
		GetProcAddress (GetModuleHandleA ("user32.dll"), "SetWindowCompositionAttribute");
	return fn;
}

static void
apply_to_hwnd (HWND hwnd, bool on)
{
	if (!hwnd) {
		return;
	}

	DWM_BLURBEHIND bb;
	memset (&bb, 0, sizeof (bb));
	bb.dwFlags  = DWM_BB_ENABLE | DWM_BB_BLURREGION;
	bb.fEnable  = on ? TRUE : FALSE;
	bb.hRgnBlur = on ? CreateRectRgn (0, 0, -1, -1) : 0;
	DwmEnableBlurBehindWindow (hwnd, &bb);
	if (bb.hRgnBlur) {
		DeleteObject (bb.hRgnBlur);
	}

	SetWindowCompositionAttribute_t fn = swca ();
	if (!fn) {
		return;
	}

	AccentPolicy ap;
	memset (&ap, 0, sizeof (ap));
	ap.AccentState   = on ? ACCENT_ENABLE_ACRYLICBLURBEHIND : ACCENT_DISABLED;
	ap.GradientColor = on ? (int) tint () : 0;

	WinCompAttrData data;
	data.Attrib = WCA_ACCENT_POLICY;
	data.pvData = &ap;
	data.cbData = sizeof (ap);

	fn (hwnd, &data);
}

static HWND
hwnd_of (Gtk::Window& w)
{
	GtkWidget* gw = GTK_WIDGET (w.gobj ());
	if (!gw->window) {
		return 0;
	}
	return (HWND) gdk_win32_drawable_get_handle (gw->window);
}

#endif /* PLATFORM_WINDOWS */

static std::vector<Gtk::Window*> s_windows;

static void
apply_to_window (Gtk::Window* w)
{
#ifdef PLATFORM_WINDOWS
	apply_to_hwnd (hwnd_of (*w), enabled ());
#else
	(void) w;
#endif
}

static void
window_realized (Gtk::Window* w)
{
	apply_to_window (w);
}

/* DEST_OVER ne remplit que les pixels laissés à alpha 0 par les vidages GDI. */
static bool
island_drawn (GdkEventExpose* ev, Gtk::Widget* w)
{
	if (!enabled ()) {
		return false;
	}

	Glib::RefPtr<Gdk::Window> win = w->get_window ();
	if (!win) {
		return false;
	}

	GdkRectangle r = ev->area;

	if (!w->get_has_window ()) {
		Gtk::Allocation a = w->get_allocation ();
		GdkRectangle alloc = { a.get_x (), a.get_y (), a.get_width (), a.get_height () };
		GdkRectangle inter;
		if (!gdk_rectangle_intersect (&r, &alloc, &inter)) {
			return false;
		}
		r = inter;
	}

	Cairo::RefPtr<Cairo::Context> cr = win->create_cairo_context ();
	cr->rectangle (r.x, r.y, r.width, r.height);
	cr->clip ();

	Gdk::Color bg = w->get_style ()->get_bg (Gtk::STATE_NORMAL);
	cr->set_source_rgb (bg.get_red_p (), bg.get_green_p (), bg.get_blue_p ());
	cr->set_operator (Cairo::OPERATOR_DEST_OVER);
	cr->paint ();

	return false;
}

static void attach_island (Gtk::Widget&, bool root);

static void
child_added (Gtk::Widget* w)
{
	attach_island (*w, false);
}

/* Un widget à fenêtre propre reçoit son expose après celui de son parent : il
 * faut donc brancher chacun d'eux, pas seulement la racine. On suit aussi les
 * ajouts ultérieurs (section Monitor, tear-offs). */
static void
attach_island (Gtk::Widget& w, bool root)
{
	static const char* tag = "oxford-acrylic-attached";
	if (g_object_get_data (G_OBJECT (w.gobj ()), tag)) {
		return;
	}
	g_object_set_data (G_OBJECT (w.gobj ()), tag, GINT_TO_POINTER (1));

	if (root || w.get_has_window ()) {
		w.signal_expose_event ().connect (
			sigc::bind (sigc::ptr_fun (&island_drawn), &w), true);
	}

	Gtk::Container* c = dynamic_cast<Gtk::Container*> (&w);
	if (!c) {
		return;
	}

	c->signal_add ().connect (sigc::ptr_fun (&child_added));

	std::vector<Gtk::Widget*> kids = c->get_children ();
	for (std::vector<Gtk::Widget*>::iterator i = kids.begin (); i != kids.end (); ++i) {
		attach_island (**i, false);
	}
}

static void
reapply_all ()
{
	for (std::vector<Gtk::Window*>::iterator i = s_windows.begin (); i != s_windows.end (); ++i) {
		apply_to_window (*i);
		(*i)->queue_draw ();
	}
}

static void
parameter_changed (std::string const& p)
{
	if (p != "oxford-acrylic") {
		return;
	}
	reapply_all ();
}

} /* anonymous namespace */

bool
OxfordAcrylic::enabled ()
{
#ifdef PLATFORM_WINDOWS
	return UIConfiguration::instance ().get_oxford_acrylic ();
#else
	return false;
#endif
}

void
OxfordAcrylic::attach (Gtk::Window& w)
{
	static bool connected = false;
	if (!connected) {
		UIConfiguration::instance ().ParameterChanged.connect (sigc::ptr_fun (&parameter_changed));
		/* le voile est aux couleurs du thème : il se recalcule quand il change */
		UIConfiguration::instance ().ColorsChanged.connect (sigc::ptr_fun (&reapply_all));
		connected = true;
	}

	s_windows.push_back (&w);

	if (w.get_realized ()) {
		apply_to_window (&w);
	} else {
		w.signal_realize ().connect (sigc::bind (sigc::ptr_fun (&window_realized), &w));
	}
}

void
OxfordAcrylic::keep_opaque (Gtk::Widget& w)
{
	attach_island (w, true);
}
