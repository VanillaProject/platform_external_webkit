/*
 *  Copyright (C) 2007, 2009 Holger Hans Peter Freyther zecke@selfish.org
 *  Copyright (C) 2010 Gustavo Noronha Silva <gns@gnome.org>
 *  Copyright (C) 2010 Collabora Ltd.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"
#include "ScrollbarGtk.h"

#include "FrameView.h"
#include "GraphicsContext.h"
#include "GtkVersioning.h"
#include "IntRect.h"
#include "ScrollbarTheme.h"

#include <gtk/gtk.h>

using namespace WebCore;

PassRefPtr<Scrollbar> Scrollbar::createNativeScrollbar(ScrollbarClient* client, ScrollbarOrientation orientation, ScrollbarControlSize size)
{
    return adoptRef(new ScrollbarGtk(client, orientation, size));
}

PassRefPtr<ScrollbarGtk> ScrollbarGtk::createScrollbar(ScrollbarClient* client, ScrollbarOrientation orientation, GtkAdjustment* adj)
{
    return adoptRef(new ScrollbarGtk(client, orientation, adj));
}

static gboolean gtkScrollEventCallback(GtkWidget* widget, GdkEventScroll* event, ScrollbarGtk*)
{
    /* Scroll only if our parent rejects the scroll event. The rationale for
     * this is that we want the main frame to scroll when we move the mouse
     * wheel over a child scrollbar in most cases. */
    return gtk_widget_event(gtk_widget_get_parent(widget), reinterpret_cast<GdkEvent*>(event));
}

ScrollbarGtk::ScrollbarGtk(ScrollbarClient* client, ScrollbarOrientation orientation,
                           ScrollbarControlSize controlSize)
    : Scrollbar(client, orientation, controlSize)
    , m_adjustment(GTK_ADJUSTMENT(gtk_adjustment_new(0.0, 0.0, 0.0, 0.0, 0.0, 0.0)))
{
    GtkWidget* scrollBar = orientation == HorizontalScrollbar ?
                           gtk_hscrollbar_new(m_adjustment):
                           gtk_vscrollbar_new(m_adjustment);
    gtk_widget_show(scrollBar);
    g_object_ref(m_adjustment);
    g_signal_connect(m_adjustment, "value-changed", G_CALLBACK(ScrollbarGtk::gtkValueChanged), this);
    g_signal_connect(scrollBar, "scroll-event", G_CALLBACK(gtkScrollEventCallback), this);

    setPlatformWidget(scrollBar);

    /*
     * assign a sane default width and height to the Scrollbar, otherwise
     * we will end up with a 0 width scrollbar.
     */
    resize(ScrollbarTheme::nativeTheme()->scrollbarThickness(),
           ScrollbarTheme::nativeTheme()->scrollbarThickness());
}

// Create a ScrollbarGtk on top of an existing GtkAdjustment but do not create a
// GtkScrollbar on top of this adjustment. The goal is to have a WebCore::Scrollbar
// that will manipulate the GtkAdjustment properties, will react to the changed
// value but will not consume any space on the screen and will not be painted
// at all. It is achieved by not calling setPlatformWidget.
ScrollbarGtk::ScrollbarGtk(ScrollbarClient* client, ScrollbarOrientation orientation, GtkAdjustment* adjustment)
    : Scrollbar(client, orientation, RegularScrollbar)
    , m_adjustment(adjustment)
{
    g_object_ref(m_adjustment);
    g_signal_connect(m_adjustment, "value-changed", G_CALLBACK(ScrollbarGtk::gtkValueChanged), this);

    // We have nothing to show as we are solely operating on the GtkAdjustment
    resize(0, 0);
}

ScrollbarGtk::~ScrollbarGtk()
{
    if (m_adjustment)
        detachAdjustment();
}

void ScrollbarGtk::attachAdjustment(GtkAdjustment* adjustment)
{
    if (platformWidget())
        return;

    if (m_adjustment)
        detachAdjustment();

    m_adjustment = adjustment;

    g_object_ref(m_adjustment);
    g_signal_connect(m_adjustment, "value-changed", G_CALLBACK(ScrollbarGtk::gtkValueChanged), this);

    updateThumbProportion();
    updateThumbPosition();
}

void ScrollbarGtk::detachAdjustment()
{
    if (!m_adjustment)
        return;

    g_signal_handlers_disconnect_by_func(G_OBJECT(m_adjustment), (gpointer)ScrollbarGtk::gtkValueChanged, this);

    // For the case where we only operate on the GtkAdjustment it is best to
    // reset the values so that the surrounding scrollbar gets updated, or
    // e.g. for a GtkScrolledWindow the scrollbar gets hidden.
    gtk_adjustment_configure(m_adjustment, 0, 0, 0, 0, 0, 0);

    g_object_unref(m_adjustment);
    m_adjustment = 0;
}

IntPoint ScrollbarGtk::getLocationInParentWindow(const IntRect& rect)
{
    IntPoint loc;

    if (parent()->isScrollViewScrollbar(this))
        loc = parent()->convertToContainingWindow(rect.location());
    else
        loc = parent()->contentsToWindow(rect.location());

    return loc;
}

void ScrollbarGtk::frameRectsChanged()
{
    if (!parent() || !platformWidget())
        return;

    IntPoint loc = getLocationInParentWindow(frameRect());

    // Don't allow the allocation size to be negative
    IntSize sz = frameRect().size();
    sz.clampNegativeToZero();

    GtkAllocation allocation = { loc.x(), loc.y(), sz.width(), sz.height() };
    gtk_widget_size_allocate(platformWidget(), &allocation);
}

void ScrollbarGtk::updateThumbPosition()
{
    if (gtk_adjustment_get_value(m_adjustment) != m_currentPos)
        gtk_adjustment_set_value(m_adjustment, m_currentPos);
}

void ScrollbarGtk::updateThumbProportion()
{
    gtk_adjustment_configure(m_adjustment,
                             gtk_adjustment_get_value(m_adjustment),
                             gtk_adjustment_get_lower(m_adjustment),
                             m_totalSize,
                             m_lineStep,
                             m_pageStep,
                             m_visibleSize);
}

void ScrollbarGtk::setFrameRect(const IntRect& rect)
{
    Widget::setFrameRect(rect);
    frameRectsChanged();
}

void ScrollbarGtk::gtkValueChanged(GtkAdjustment*, ScrollbarGtk* that)
{
    that->setValue(static_cast<int>(gtk_adjustment_get_value(that->m_adjustment)));
}

void ScrollbarGtk::setEnabled(bool shouldEnable)
{
    if (enabled() == shouldEnable)
        return;
        
    Scrollbar::setEnabled(shouldEnable);
    if (platformWidget()) 
        gtk_widget_set_sensitive(platformWidget(), shouldEnable);
}

/*
 * Strategy to painting a Widget:
 *  1.) do not paint if there is no GtkWidget set
 *  2.) We assume that the widget has no window and that frameRectsChanged positioned
 *      the widget correctly. ATM we do not honor the GraphicsContext translation.
 */
void ScrollbarGtk::paint(GraphicsContext* context, const IntRect& rect)
{
    if (!platformWidget())
        return;

    if (!context->gdkExposeEvent())
        return;

    GtkWidget* widget = platformWidget();
    ASSERT(!gtk_widget_get_has_window(widget));

    GdkEvent* event = gdk_event_new(GDK_EXPOSE);
    event->expose = *context->gdkExposeEvent();
    event->expose.area = static_cast<GdkRectangle>(rect);

    IntPoint loc = getLocationInParentWindow(rect);

    event->expose.area.x = loc.x();
    event->expose.area.y = loc.y();

#ifdef GTK_API_VERSION_2
    event->expose.region = gdk_region_rectangle(&event->expose.area);
#else
    event->expose.region = cairo_region_create_rectangle(&event->expose.area);
#endif

    /*
     * This will be unref'ed by gdk_event_free.
     */
    g_object_ref(event->expose.window);

    /*
     * If we are going to paint do the translation and GtkAllocation manipulation.
     */
#ifdef GTK_API_VERSION_2
    if (!gdk_region_empty(event->expose.region))
#else
    if (!cairo_region_is_empty(event->expose.region))
#endif
        gtk_widget_send_expose(widget, event);

    gdk_event_free(event);
}
