/*
 * OXFORD : fond acrylique translucide (Windows 11).
 */

#ifndef __gtk_ardour_oxford_acrylic_h__
#define __gtk_ardour_oxford_acrylic_h__

namespace Gtk {
	class Widget;
	class Window;
}

namespace OxfordAcrylic {

bool enabled ();

/** Allume l'acrylique sur une fenêtre de premier niveau. */
void attach (Gtk::Window&);

/** Exclut un widget (et ses descendants) de la transparence. */
void keep_opaque (Gtk::Widget&);

} /* namespace OxfordAcrylic */

#endif /* __gtk_ardour_oxford_acrylic_h__ */
