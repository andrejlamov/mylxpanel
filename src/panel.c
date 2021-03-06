/**
 * Copyright (c) 2006 LxDE Developers, see the file AUTHORS for details.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <stdlib.h>
#include <glib/gstdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <locale.h>
#include <string.h>
#include <gdk/gdkx.h>
#include <libfm/fm-gtk.h>

#define __LXPANEL_INTERNALS__

#include "private.h"
#include "misc.h"
#include "bg.h"

#include "lxpanelctl.h"
#include "dbg.h"

static gchar *cfgfile = NULL;
static gchar version[] = VERSION;
gchar *cprofile = "default";

static GtkWindowGroup* win_grp; /* window group used to limit the scope of model dialog. */

static int config = 0;
FbEv *fbev = NULL;

GSList* all_panels = NULL;  /* a single-linked list storing all panels */

gboolean is_restarting = FALSE;

static int panel_start(Panel *p);
static void panel_start_gui(Panel *p);

gboolean is_in_lxde = FALSE;

/* A hack used to be compatible with Gnome panel for gtk+ themes.
 * Some gtk+ themes define special styles for desktop panels.
 * http://live.gnome.org/GnomeArt/Tutorials/GtkThemes/GnomePanel
 * So we make a derived class from GtkWindow named PanelToplevel
 * and create the panels with it to be compatible with Gnome themes.
 */
#define PANEL_TOPLEVEL_TYPE				(panel_toplevel_get_type())

typedef struct _PanelToplevel			PanelToplevel;
typedef struct _PanelToplevelClass		PanelToplevelClass;
struct _PanelToplevel
{
	GtkWindow parent;
};
struct _PanelToplevelClass
{
	GtkWindowClass parent_class;
};
G_DEFINE_TYPE(PanelToplevel, panel_toplevel, GTK_TYPE_WINDOW);
static void panel_toplevel_class_init(PanelToplevelClass *klass)
{
}
static void panel_toplevel_init(PanelToplevel *self)
{
}

/* Allocate and initialize new Panel structure. */
static Panel* panel_allocate(void)
{
    Panel* p = g_new0(Panel, 1);
    p->allign = ALLIGN_CENTER;
    p->edge = EDGE_NONE;
    p->widthtype = WIDTH_PERCENT;
    p->width = 100;
    p->heighttype = HEIGHT_PIXEL;
    p->height = PANEL_HEIGHT_DEFAULT;
    p->monitor = 0;
    p->setdocktype = 1;
    p->setstrut = 1;
    p->round_corners = 0;
    p->autohide = 0;
    p->visible = TRUE;
    p->height_when_hidden = 2;
    p->transparent = 0;
    p->alpha = 255;
    gdk_color_parse("white", &p->gtintcolor);
    p->tintcolor = gcolor2rgb24(&p->gtintcolor);
    p->usefontcolor = 0;
    p->fontcolor = 0x00000000;
    p->usefontsize = 0;
    p->fontsize = 10;
    p->spacing = 0;
    p->icon_size = PANEL_ICON_SIZE;
    p->icon_theme = gtk_icon_theme_get_default();
    p->config = config_new();
    return p;
}

/* Normalize panel configuration after load from file or reconfiguration. */
static void panel_normalize_configuration(Panel* p)
{
    panel_set_panel_configuration_changed( p );
    if (p->width < 0)
        p->width = 100;
    if (p->widthtype == WIDTH_PERCENT && p->width > 100)
        p->width = 100;
    p->heighttype = HEIGHT_PIXEL;
    if (p->heighttype == HEIGHT_PIXEL) {
        if (p->height < PANEL_HEIGHT_MIN)
            p->height = PANEL_HEIGHT_MIN;
        else if (p->height > PANEL_HEIGHT_MAX)
            p->height = PANEL_HEIGHT_MAX;
    }
    if (p->monitor < 0)
        p->monitor = 0;
    if (p->background)
        p->transparent = 0;
}

/****************************************************
 *         panel's handlers for WM events           *
 ****************************************************/

void panel_set_wm_strut(Panel *p)
{
    int index;
    gulong strut_size;
    gulong strut_lower;
    gulong strut_upper;

    /* Dispatch on edge to set up strut parameters. */
    switch (p->edge)
    {
        case EDGE_LEFT:
            index = 0;
            strut_size = p->aw;
            strut_lower = p->ay;
            strut_upper = p->ay + p->ah;
            break;
        case EDGE_RIGHT:
            index = 1;
            strut_size = p->aw;
            strut_lower = p->ay;
            strut_upper = p->ay + p->ah;
            break;
        case EDGE_TOP:
            index = 2;
            strut_size = p->ah;
            strut_lower = p->ax;
            strut_upper = p->ax + p->aw;
            break;
        case EDGE_BOTTOM:
            index = 3;
            strut_size = p->ah;
            strut_lower = p->ax;
            strut_upper = p->ax + p->aw;
            break;
        default:
            return;
    }

    /* Handle autohide case.  EWMH recommends having the strut be the minimized size. */
    if (p->autohide)
        strut_size = p->height_when_hidden;

    /* Set up strut value in property format. */
    gulong desired_strut[12];
    memset(desired_strut, 0, sizeof(desired_strut));
    if (p->setstrut)
    {
        desired_strut[index] = strut_size;
        desired_strut[4 + index * 2] = strut_lower;
        desired_strut[5 + index * 2] = strut_upper;
    }
    else
    {
        strut_size = 0;
        strut_lower = 0;
        strut_upper = 0;
    }

    /* If strut value changed, set the property value on the panel window.
     * This avoids property change traffic when the panel layout is recalculated but strut geometry hasn't changed. */
    if ((GTK_WIDGET_MAPPED(p->topgwin))
    && ((p->strut_size != strut_size) || (p->strut_lower != strut_lower) || (p->strut_upper != strut_upper) || (p->strut_edge != p->edge)))
    {
        p->strut_size = strut_size;
        p->strut_lower = strut_lower;
        p->strut_upper = strut_upper;
        p->strut_edge = p->edge;

        /* If window manager supports STRUT_PARTIAL, it will ignore STRUT.
         * Set STRUT also for window managers that do not support STRUT_PARTIAL. */
        if (strut_size != 0)
        {
            XChangeProperty(GDK_DISPLAY(), p->topxwin, a_NET_WM_STRUT_PARTIAL,
                XA_CARDINAL, 32, PropModeReplace,  (unsigned char *) desired_strut, 12);
            XChangeProperty(GDK_DISPLAY(), p->topxwin, a_NET_WM_STRUT,
                XA_CARDINAL, 32, PropModeReplace,  (unsigned char *) desired_strut, 4);
        }
        else
        {
            XDeleteProperty(GDK_DISPLAY(), p->topxwin, a_NET_WM_STRUT);
            XDeleteProperty(GDK_DISPLAY(), p->topxwin, a_NET_WM_STRUT_PARTIAL);
        }
    }
}

/* defined in configurator.c */
void panel_configure(Panel* p, int sel_page );
gboolean panel_edge_available(Panel* p, int edge, gint monitor);

/* built-in commands, defined in configurator.c */
void restart(void);
void gtk_run(void);
void panel_destroy(Panel *p);

static void process_client_msg ( XClientMessageEvent* ev )
{
    int cmd = ev->data.b[0];
    switch( cmd )
    {
#ifndef DISABLE_MENU
        case LXPANEL_CMD_SYS_MENU:
        {
            GSList* l;
            for( l = all_panels; l; l = l->next )
            {
                Panel* p = (Panel*)l->data;
                GList *plugins, *pl;

                plugins = gtk_container_get_children(GTK_CONTAINER(p->box));
                for (pl = plugins; pl; pl = pl->next)
                {
                    LXPanelPluginInit *init = PLUGIN_CLASS(pl->data);
                    if (init->show_system_menu)
                        /* queue to show system menu */
                        init->show_system_menu(pl->data);
                }
                g_list_free(plugins);
            }
            break;
        }
#endif
#ifndef DISABLE_MENU
        case LXPANEL_CMD_RUN:
            gtk_run();
            break;
#endif
        case LXPANEL_CMD_CONFIG:
            {
            Panel * p = ((all_panels != NULL) ? all_panels->data : NULL);
            if (p != NULL)
                panel_configure(p, 0);
            }
            break;
        case LXPANEL_CMD_RESTART:
            restart();
            break;
        case LXPANEL_CMD_EXIT:
            gtk_main_quit();
            break;
    }
}

static GdkFilterReturn
panel_event_filter(GdkXEvent *xevent, GdkEvent *event, gpointer not_used)
{
    Atom at;
    Window win;
    XEvent *ev = (XEvent *) xevent;

    ENTER;
    DBG("win = 0x%x\n", ev->xproperty.window);
    if (ev->type != PropertyNotify )
    {
        /* private client message from lxpanelctl */
        if( ev->type == ClientMessage && ev->xproperty.atom == a_LXPANEL_CMD )
        {
            process_client_msg( (XClientMessageEvent*)ev );
        }
        else if( ev->type == DestroyNotify )
        {
            fb_ev_emit_destroy( fbev, ((XDestroyWindowEvent*)ev)->window );
        }
        RET(GDK_FILTER_CONTINUE);
    }

    at = ev->xproperty.atom;
    win = ev->xproperty.window;
    if (win == GDK_ROOT_WINDOW())
    {
        if (at == a_NET_CLIENT_LIST)
        {
            fb_ev_emit(fbev, EV_CLIENT_LIST);
        }
        else if (at == a_NET_CURRENT_DESKTOP)
        {
            GSList* l;
            for( l = all_panels; l; l = l->next )
                ((Panel*)l->data)->curdesk = get_net_current_desktop();
            fb_ev_emit(fbev, EV_CURRENT_DESKTOP);
        }
        else if (at == a_NET_NUMBER_OF_DESKTOPS)
        {
            GSList* l;
            for( l = all_panels; l; l = l->next )
                ((Panel*)l->data)->desknum = get_net_number_of_desktops();
            fb_ev_emit(fbev, EV_NUMBER_OF_DESKTOPS);
        }
        else if (at == a_NET_DESKTOP_NAMES)
        {
            fb_ev_emit(fbev, EV_DESKTOP_NAMES);
        }
        else if (at == a_NET_ACTIVE_WINDOW)
        {
            fb_ev_emit(fbev, EV_ACTIVE_WINDOW );
        }
        else if (at == a_NET_CLIENT_LIST_STACKING)
        {
            fb_ev_emit(fbev, EV_CLIENT_LIST_STACKING);
        }
        else if (at == a_XROOTPMAP_ID)
        {
            GSList* l;
            for( l = all_panels; l; l = l->next )
            {
                Panel* p = (Panel*)l->data;
                if (p->transparent) {
                    fb_bg_notify_changed_bg(p->bg);
                }
            }
        }
        else if (at == a_NET_WORKAREA)
        {
            GSList* l;
            for( l = all_panels; l; l = l->next )
            {
                Panel* p = (Panel*)l->data;
                g_free( p->workarea );
                p->workarea = get_xaproperty (GDK_ROOT_WINDOW(), a_NET_WORKAREA, XA_CARDINAL, &p->wa_len);
                /* print_wmdata(p); */
            }
        }
        else
            return GDK_FILTER_CONTINUE;

        return GDK_FILTER_REMOVE;
    }
    return GDK_FILTER_CONTINUE;
}

/****************************************************
 *         panel's handlers for GTK events          *
 ****************************************************/


static gint
panel_delete_event(GtkWidget * widget, GdkEvent * event, gpointer data)
{
    ENTER;
    RET(FALSE);
}

static gint
panel_destroy_event(GtkWidget * widget, GdkEvent * event, gpointer data)
{
    //Panel *p = (Panel *) data;
    //if (!p->self_destroy)
    gtk_main_quit();
    RET(FALSE);
}

static void
on_root_bg_changed(FbBg *bg, Panel* p)
{
    panel_update_background( p );
}

void panel_determine_background_pixmap(Panel * p, GtkWidget * widget, GdkWindow * window)
{
    GdkPixmap * pixmap = NULL;

    /* Free p->bg if it is not going to be used. */
    if (( ! p->transparent) && (p->bg != NULL))
    {
        g_signal_handlers_disconnect_by_func(G_OBJECT(p->bg), on_root_bg_changed, p);
        g_object_unref(p->bg);
        p->bg = NULL;
    }

    if (p->background)
    {
        /* User specified background pixmap. */
        if (p->background_file != NULL)
            pixmap = fb_bg_get_pix_from_file(widget, p->background_file);
    }

    else if (p->transparent)
    {
        /* Transparent.  Determine the appropriate value from the root pixmap. */
        if (p->bg == NULL)
        {
            p->bg = fb_bg_get_for_display();
            g_signal_connect(G_OBJECT(p->bg), "changed", G_CALLBACK(on_root_bg_changed), p);
        }
        pixmap = fb_bg_get_xroot_pix_for_win(p->bg, widget);
        if ((pixmap != NULL) && (pixmap != GDK_NO_BG) && (p->alpha != 0))
            fb_bg_composite(pixmap, &p->gtintcolor, p->alpha);
    }

    if (pixmap != NULL)
    {
        gtk_widget_set_app_paintable(widget, TRUE );
        gdk_window_set_back_pixmap(window, pixmap, FALSE);
        g_object_unref(pixmap);
    }
    else
        gtk_widget_set_app_paintable(widget, FALSE);
}

/* Update the background of the entire panel.
 * This function should only be called after the panel has been realized. */
void panel_update_background(Panel * p)
{
    GList *plugins, *l;

    /* Redraw the top level widget. */
    panel_determine_background_pixmap(p, p->topgwin, p->topgwin->window);
    gdk_window_clear(p->topgwin->window);
    gtk_widget_queue_draw(p->topgwin);

    /* Loop over all plugins redrawing each plugin. */
    plugins = gtk_container_get_children(GTK_CONTAINER(p->box));
    for (l = plugins; l != NULL; l = l->next)
        plugin_widget_set_background(l->data, p);
    g_list_free(plugins);
}

static gboolean delay_update_background( Panel* p )
{
    /* Panel could be destroyed while background update scheduled */
    if ( p->topgwin && GTK_WIDGET_REALIZED ( p->topgwin ) ) {
	gdk_display_sync( gtk_widget_get_display(p->topgwin) );
	panel_update_background( p );
    }
    
    return FALSE;
}

static void
panel_realize(GtkWidget *widget, Panel *p)
{
    g_idle_add_full( G_PRIORITY_LOW, 
            (GSourceFunc)delay_update_background, p, NULL );
}

static void
panel_style_set(GtkWidget *widget, GtkStyle* prev, Panel *p)
{
    /* FIXME: This dirty hack is used to fix the background of systray... */
    if( GTK_WIDGET_REALIZED( widget ) )
        g_idle_add_full( G_PRIORITY_LOW, 
                (GSourceFunc)delay_update_background, p, NULL );
}

static gint
panel_size_req(GtkWidget *widget, GtkRequisition *req, Panel *p)
{
    ENTER;

    if (p->autohide && !p->visible)
        /* When the panel is in invisible state, the content box also got hidden, thus always
         * report 0 size.  Ask the content box instead for its size. */
        gtk_widget_size_request(p->box, req);

    if (p->widthtype == WIDTH_REQUEST)
        p->width = (p->orientation == GTK_ORIENTATION_HORIZONTAL) ? req->width : req->height;
    if (p->heighttype == HEIGHT_REQUEST)
        p->height = (p->orientation == GTK_ORIENTATION_HORIZONTAL) ? req->height : req->width;

    calculate_position(p);

    gtk_widget_set_size_request( widget, p->aw, p->ah );

    RET( TRUE );
}

static gint
panel_size_alloc(GtkWidget *widget, GtkAllocation *a, Panel *p)
{
    ENTER;
    if (p->widthtype == WIDTH_REQUEST)
        p->width = (p->orientation == GTK_ORIENTATION_HORIZONTAL) ? a->width : a->height;
    if (p->heighttype == HEIGHT_REQUEST)
        p->height = (p->orientation == GTK_ORIENTATION_HORIZONTAL) ? a->height : a->width;
    calculate_position(p);

    if (a->width == p->aw && a->height == p->ah && a->x == p->ax && a->y == p ->ay) {
        RET(TRUE);
    }

    gtk_window_move(GTK_WINDOW(p->topgwin), p->ax, p->ay);
    panel_set_wm_strut(p);
    RET(TRUE);
}

static  gboolean
panel_configure_event (GtkWidget *widget, GdkEventConfigure *e, Panel *p)
{
    ENTER;
    if (e->width == p->cw && e->height == p->ch && e->x == p->cx && e->y == p->cy)
        RET(TRUE);
    p->cw = e->width;
    p->ch = e->height;
    p->cx = e->x;
    p->cy = e->y;

    if (p->transparent)
        fb_bg_notify_changed_bg(p->bg);

    RET(FALSE);
}

static gint
panel_popupmenu_configure(GtkWidget *widget, gpointer user_data)
{
    panel_configure( (Panel*)user_data, 0 );
    return TRUE;
}

/* Handler for "button_press_event" signal with Panel as parameter. */
static gboolean panel_button_press_event_with_panel(GtkWidget *widget, GdkEventButton *event, Panel *panel)
{
    if (event->button == 3)	 /* right button */
    {
        GtkMenu* popup = (GtkMenu*) lxpanel_get_plugin_menu(panel, NULL, FALSE);
        gtk_menu_popup(popup, NULL, NULL, NULL, NULL, event->button, event->time);
        return TRUE;
    }    
    return FALSE;
}

static void panel_popupmenu_config_plugin( GtkMenuItem* item, GtkWidget* plugin )
{
    Panel *panel = PLUGIN_PANEL(plugin);

    lxpanel_plugin_show_config_dialog(panel, plugin);

    /* FIXME: this should be more elegant */
    panel->config_changed = TRUE;
}

static void panel_popupmenu_add_item( GtkMenuItem* item, Panel* panel )
{
    /* panel_add_plugin( panel, panel->topgwin ); */
    panel_configure( panel, 2 );
}

static void panel_popupmenu_remove_item( GtkMenuItem* item, GtkWidget* plugin )
{
    Panel* panel = PLUGIN_PANEL(plugin);

    /* If the configuration dialog is open, there will certainly be a crash if the
     * user manipulates the Configured Plugins list, after we remove this entry.
     * Close the configuration dialog if it is open. */
    if (panel->pref_dialog != NULL)
    {
        gtk_widget_destroy(panel->pref_dialog);
        panel->pref_dialog = NULL;
    }
    config_setting_destroy(g_object_get_qdata(G_OBJECT(plugin), lxpanel_plugin_qconf));
    /* reset conf pointer because the widget still may be referenced by configurator */
    g_object_set_qdata(G_OBJECT(plugin), lxpanel_plugin_qconf, NULL);

    panel_config_save(panel);
    gtk_widget_destroy(plugin);
}

/* FIXME: Potentially we can support multiple panels at the same edge,
 * but currently this cannot be done due to some positioning problems. */
static char* gen_panel_name( int edge, gint monitor )
{
    const char* edge_str = num2str( edge_pair, edge, "" );
    char* name = NULL;
    char* dir = _user_config_file_name("panels", NULL);
    int i;
    for( i = 0; i < G_MAXINT; ++i )
    {
        char* f;
        if(monitor != 0)
            name = g_strdup_printf( "%s-m%d-%d", edge_str, monitor, i );
        else if( G_LIKELY( i > 0 ) )
            name =  g_strdup_printf( "%s%d", edge_str, i );
        else
            name = g_strdup( edge_str );

        f = g_build_filename( dir, name, NULL );
        if( ! g_file_test( f, G_FILE_TEST_EXISTS ) )
        {
            g_free( f );
            break;
        }
        g_free( name );
        g_free( f );
    }
    g_free( dir );
    return name;
}

/* FIXME: Potentially we can support multiple panels at the same edge,
 * but currently this cannot be done due to some positioning problems. */
static void panel_popupmenu_create_panel( GtkMenuItem* item, Panel* panel )
{
    gint m, e, monitors;
    GdkScreen *screen;
    GtkWidget *err;
    Panel* new_panel = panel_allocate();

    /* Allocate the edge. */
    screen = gdk_screen_get_default();
    g_assert(screen);
    monitors = gdk_screen_get_n_monitors(screen);
    for(m=0; m<monitors; ++m)
    {
        /* try each of the four edges */
        for(e=1; e<5; ++e)
        {
            if(panel_edge_available(new_panel,e,m)) {
                new_panel->edge = e;
                new_panel->monitor = m;
                goto found_edge;
            }
        }
    }

    panel_destroy(new_panel);
    ERR("Error adding panel: There is no room for another panel. All the edges are taken.\n");
    err = gtk_message_dialog_new(NULL,0,GTK_MESSAGE_ERROR,GTK_BUTTONS_OK,_("There is no room for another panel. All the edges are taken."));
    gtk_dialog_run(GTK_DIALOG(err));
    gtk_widget_destroy(err);
    return;

found_edge:
    new_panel->name = gen_panel_name(new_panel->edge,new_panel->monitor);

    /* create new config with first group "Global" */
    config_group_add_subgroup(config_root_setting(new_panel->config), "Global");
    panel_configure(new_panel, 0);
    panel_normalize_configuration(new_panel);
    panel_start_gui(new_panel);
    gtk_widget_show_all(new_panel->topgwin);

    panel_config_save(new_panel);
    all_panels = g_slist_prepend(all_panels, new_panel);
}

static void panel_popupmenu_delete_panel( GtkMenuItem* item, Panel* panel )
{
    GtkWidget* dlg;
    gboolean ok;
    dlg = gtk_message_dialog_new_with_markup( GTK_WINDOW(panel->topgwin),
                                                    GTK_DIALOG_MODAL,
                                                    GTK_MESSAGE_QUESTION,
                                                    GTK_BUTTONS_OK_CANCEL,
                                                    _("Really delete this panel?\n<b>Warning: This can not be recovered.</b>") );
    panel_apply_icon(GTK_WINDOW(dlg));
    gtk_window_set_title( (GtkWindow*)dlg, _("Confirm") );
    ok = ( gtk_dialog_run( (GtkDialog*)dlg ) == GTK_RESPONSE_OK );
    gtk_widget_destroy( dlg );
    if( ok )
    {
        gchar *fname;
        all_panels = g_slist_remove( all_panels, panel );

        /* delete the config file of this panel */
        fname = _user_config_file_name("panels", panel->name);
        g_unlink( fname );
        g_free(fname);
        panel->config_changed = 0;
        panel_destroy( panel );
    }
}

static void panel_popupmenu_about( GtkMenuItem* item, Panel* panel )
{
    GtkWidget *about;
    const gchar* authors[] = {
        "Hong Jen Yee (PCMan) <pcman.tw@gmail.com>",
        "Jim Huang <jserv.tw@gmail.com>",
        "Greg McNew <gmcnew@gmail.com> (battery plugin)",
        "Fred Chien <cfsghost@gmail.com>",
        "Daniel Kesler <kesler.daniel@gmail.com>",
        "Juergen Hoetzel <juergen@archlinux.org>",
        "Marty Jack <martyj19@comcast.net>",
        "Martin Bagge <brother@bsnet.se>",
        NULL
    };
    /* TRANSLATORS: Replace this string with your names, one name per line. */
    gchar *translators = _( "translator-credits" );

    about = gtk_about_dialog_new();
    panel_apply_icon(GTK_WINDOW(about));
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(about), VERSION);
    gtk_about_dialog_set_name(GTK_ABOUT_DIALOG(about), _("LXPanel"));

    if(gtk_icon_theme_has_icon(panel->icon_theme, "video-display"))
    {
         gtk_about_dialog_set_logo( GTK_ABOUT_DIALOG(about),
                                    gtk_icon_theme_load_icon(panel->icon_theme, "video-display", 48, 0, NULL));
    }
    else if (gtk_icon_theme_has_icon(panel->icon_theme, "start-here"))
    {
         gtk_about_dialog_set_logo( GTK_ABOUT_DIALOG(about),
                                    gtk_icon_theme_load_icon(panel->icon_theme, "start-here", 48, 0, NULL));
    }
    else 
    {
        gtk_about_dialog_set_logo(  GTK_ABOUT_DIALOG(about),
                                    gdk_pixbuf_new_from_file(PACKAGE_DATA_DIR "/images/my-computer.png", NULL));
    }

    gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(about), _("Copyright (C) 2008-2013"));
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(about), _( "Desktop panel for LXDE project"));
    gtk_about_dialog_set_license(GTK_ABOUT_DIALOG(about), "This program is free software; you can redistribute it and/or\nmodify it under the terms of the GNU General Public License\nas published by the Free Software Foundation; either version 2\nof the License, or (at your option) any later version.\n\nThis program is distributed in the hope that it will be useful,\nbut WITHOUT ANY WARRANTY; without even the implied warranty of\nMERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\nGNU General Public License for more details.\n\nYou should have received a copy of the GNU General Public License\nalong with this program; if not, write to the Free Software\nFoundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.");
    gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(about), "http://lxde.org/");
    gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(about), authors);
    gtk_about_dialog_set_translator_credits(GTK_ABOUT_DIALOG(about), translators);
    gtk_dialog_run(GTK_DIALOG(about));
    gtk_widget_destroy(about); 
}

void panel_apply_icon( GtkWindow *w )
{
	GdkPixbuf* window_icon;
	
	if(gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), "video-display"))
    {
		window_icon = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), "video-display", 24, 0, NULL);
	}
	else if(gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), "start-here"))
    {
		window_icon = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), "start-here", 24, 0, NULL);
	}
    else
    {
		window_icon = gdk_pixbuf_new_from_file(PACKAGE_DATA_DIR "/images/my-computer.png", NULL);
	}
    gtk_window_set_icon(w, window_icon);
}

GtkMenu* lxpanel_get_plugin_menu( Panel* panel, GtkWidget* plugin, gboolean use_sub_menu )
{
    GtkWidget  *menu_item, *img;
    GtkMenu *ret,*menu;
    LXPanelPluginInit *init;
    char* tmp;
    ret = menu = GTK_MENU(gtk_menu_new());

    img = gtk_image_new_from_stock( GTK_STOCK_EDIT, GTK_ICON_SIZE_MENU );
    menu_item = gtk_image_menu_item_new_with_label(_("Add / Remove Panel Items"));
    gtk_image_menu_item_set_image( (GtkImageMenuItem*)menu_item, img );
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    g_signal_connect( menu_item, "activate", G_CALLBACK(panel_popupmenu_add_item), panel );

    if( plugin )
    {
        init = PLUGIN_CLASS(plugin);
        img = gtk_image_new_from_stock( GTK_STOCK_REMOVE, GTK_ICON_SIZE_MENU );
        tmp = g_strdup_printf( _("Remove \"%s\" From Panel"), _(init->name) );
        menu_item = gtk_image_menu_item_new_with_label( tmp );
        g_free( tmp );
        gtk_image_menu_item_set_image( (GtkImageMenuItem*)menu_item, img );
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
        g_signal_connect( menu_item, "activate", G_CALLBACK(panel_popupmenu_remove_item), plugin );
    }

    menu_item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);

    img = gtk_image_new_from_stock( GTK_STOCK_PREFERENCES, GTK_ICON_SIZE_MENU );
    menu_item = gtk_image_menu_item_new_with_label(_("Panel Settings"));
    gtk_image_menu_item_set_image( (GtkImageMenuItem*)menu_item, img );
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    g_signal_connect(G_OBJECT(menu_item), "activate", G_CALLBACK(panel_popupmenu_configure), panel );

    img = gtk_image_new_from_stock( GTK_STOCK_NEW, GTK_ICON_SIZE_MENU );
    menu_item = gtk_image_menu_item_new_with_label(_("Create New Panel"));
    gtk_image_menu_item_set_image( (GtkImageMenuItem*)menu_item, img );
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    g_signal_connect( menu_item, "activate", G_CALLBACK(panel_popupmenu_create_panel), panel );

    img = gtk_image_new_from_stock( GTK_STOCK_DELETE, GTK_ICON_SIZE_MENU );
    menu_item = gtk_image_menu_item_new_with_label(_("Delete This Panel"));
    gtk_image_menu_item_set_image( (GtkImageMenuItem*)menu_item, img );
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    g_signal_connect( menu_item, "activate", G_CALLBACK(panel_popupmenu_delete_panel), panel );
    if( ! all_panels->next )    /* if this is the only panel */
        gtk_widget_set_sensitive( menu_item, FALSE );

    menu_item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);

    img = gtk_image_new_from_stock( GTK_STOCK_ABOUT, GTK_ICON_SIZE_MENU );
    menu_item = gtk_image_menu_item_new_with_label(_("About"));
    gtk_image_menu_item_set_image( (GtkImageMenuItem*)menu_item, img );
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    g_signal_connect( menu_item, "activate", G_CALLBACK(panel_popupmenu_about), panel );

    if( use_sub_menu )
    {
        ret = GTK_MENU(gtk_menu_new());
        menu_item = gtk_image_menu_item_new_with_label(_("Panel"));
        gtk_menu_shell_append(GTK_MENU_SHELL(ret), menu_item);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), GTK_WIDGET(menu) );

        gtk_widget_show_all(GTK_WIDGET(ret));
    }

    if( plugin )
    {
        menu_item = gtk_separator_menu_item_new();
        gtk_menu_shell_prepend(GTK_MENU_SHELL(ret), menu_item);

        img = gtk_image_new_from_stock( GTK_STOCK_PREFERENCES, GTK_ICON_SIZE_MENU );
        tmp = g_strdup_printf( _("\"%s\" Settings"), _(init->name) );
        menu_item = gtk_image_menu_item_new_with_label( tmp );
        g_free( tmp );
        gtk_image_menu_item_set_image( (GtkImageMenuItem*)menu_item, img );
        gtk_menu_shell_prepend(GTK_MENU_SHELL(ret), menu_item);
        if( init->config )
            g_signal_connect( menu_item, "activate", G_CALLBACK(panel_popupmenu_config_plugin), plugin );
        else
            gtk_widget_set_sensitive( menu_item, FALSE );
    }

    gtk_widget_show_all(GTK_WIDGET(menu));

    g_signal_connect( ret, "selection-done", G_CALLBACK(gtk_widget_destroy), NULL );
    return ret;
}

/* for old plugins compatibility */
GtkMenu* lxpanel_get_panel_menu( Panel* panel, Plugin* plugin, gboolean use_sub_menu )
{
    return lxpanel_get_plugin_menu(panel, plugin->pwid, use_sub_menu);
}

/****************************************************
 *         panel creation                           *
 ****************************************************/

static void
make_round_corners(Panel *p)
{
    /* FIXME: This should be re-written with shape extension of X11 */
    /* gdk_window_shape_combine_mask() can be used */
}

void panel_set_dock_type(Panel *p)
{
    if (p->setdocktype) {
        Atom state = a_NET_WM_WINDOW_TYPE_DOCK;
        XChangeProperty(GDK_DISPLAY(), p->topxwin,
                        a_NET_WM_WINDOW_TYPE, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *) &state, 1);
    }
    else {
        XDeleteProperty( GDK_DISPLAY(), p->topxwin, a_NET_WM_WINDOW_TYPE );
    }
}

static void panel_set_visibility(Panel *p, gboolean visible)
{
    if ( ! visible) gtk_widget_hide(p->box);
    p->visible = visible;
    calculate_position(p);
    gtk_widget_set_size_request(p->topgwin, p->aw, p->ah);
    gdk_window_move(p->topgwin->window, p->ax, p->ay);
    if (visible) gtk_widget_show(p->box);
    panel_set_wm_strut(p);
}

static gboolean panel_leave_real(Panel *p)
{
    /* If the pointer is grabbed by this application, leave the panel displayed.
     * There is no way to determine if it is grabbed by another application, such as an application that has a systray icon. */
    if (gdk_display_pointer_is_grabbed(p->display))
        return TRUE;

    /* If the pointer is inside the panel, leave the panel displayed. */
    gint x, y;
    gdk_display_get_pointer(p->display, NULL, &x, &y, NULL);
    if ((p->cx <= x) && (x <= (p->cx + p->cw)) && (p->cy <= y) && (y <= (p->cy + p->ch)))
        return TRUE;

    /* If the panel is configured to autohide and if it is visible, hide the panel. */
    if ((p->autohide) && (p->visible))
        panel_set_visibility(p, FALSE);

    /* Clear the timer. */
    p->hide_timeout = 0;
    return FALSE;
}

static gboolean panel_enter(GtkImage *widget, GdkEventCrossing *event, Panel *p)
{
    /* We may receive multiple enter-notify events when the pointer crosses into the panel.
     * Do extra tests to make sure this does not cause misoperation such as blinking.
     * If the pointer is inside the panel, unhide it. */
    gint x, y;
    gdk_display_get_pointer(p->display, NULL, &x, &y, NULL);
    if ((p->cx <= x) && (x <= (p->cx + p->cw)) && (p->cy <= y) && (y <= (p->cy + p->ch)))
    {
        /* If the pointer is inside the panel and we have not already unhidden it, do so and
         * set a timer to recheck it in a half second. */
        if (p->hide_timeout == 0)
        {
            p->hide_timeout = g_timeout_add(500, (GSourceFunc) panel_leave_real, p);
            panel_set_visibility(p, TRUE);
        }
    }
    else
    {
        /* If the pointer is not inside the panel, simulate a timer expiration. */
        panel_leave_real(p);
    }
    return TRUE;
}

static gboolean panel_drag_motion(GtkWidget *widget, GdkDragContext *drag_context, gint x,
      gint y, guint time, Panel *p)
{
    panel_enter(NULL, NULL, p);
    return TRUE;
}

void panel_establish_autohide(Panel *p)
{
    if (p->autohide)
    {
        gtk_widget_add_events(p->topgwin, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
        g_signal_connect(G_OBJECT(p->topgwin), "enter-notify-event", G_CALLBACK(panel_enter), p);
        g_signal_connect(G_OBJECT(p->topgwin), "drag-motion", (GCallback) panel_drag_motion, p);
        gtk_drag_dest_set(p->topgwin, GTK_DEST_DEFAULT_MOTION, NULL, 0, 0);
        gtk_drag_dest_set_track_motion(p->topgwin, TRUE);
        panel_enter(NULL, NULL, p);
    }
    else if ( ! p->visible)
    {
	gtk_widget_show(p->box);
        p->visible = TRUE;
    }
}

/* Set an image from a file with scaling to the panel icon size. */
void panel_image_set_from_file(Panel * p, GtkWidget * image, const char * file)
{
    GdkPixbuf * pixbuf = gdk_pixbuf_new_from_file_at_scale(file, p->icon_size, p->icon_size, TRUE, NULL);
    if (pixbuf != NULL)
    {
        gtk_image_set_from_pixbuf(GTK_IMAGE(image), pixbuf);
        g_object_unref(pixbuf);
    }
}

/* Set an image from a icon theme with scaling to the panel icon size. */
gboolean panel_image_set_icon_theme(Panel * p, GtkWidget * image, const gchar * icon)
{
    if (gtk_icon_theme_has_icon(p->icon_theme, icon))
    {
        GdkPixbuf * pixbuf = gtk_icon_theme_load_icon(p->icon_theme, icon, p->icon_size, 0, NULL);
        gtk_image_set_from_pixbuf(GTK_IMAGE(image), pixbuf);
        g_object_unref(pixbuf);
        return TRUE;
    }
    return FALSE;
}

static void
panel_start_gui(Panel *p)
{
    Atom state[3];
    XWMHints wmhints;
    guint32 val;

    ENTER;

    p->curdesk = get_net_current_desktop();
    p->desknum = get_net_number_of_desktops();
    p->workarea = get_xaproperty (GDK_ROOT_WINDOW(), a_NET_WORKAREA, XA_CARDINAL, &p->wa_len);

    /* main toplevel window */
    /* p->topgwin =  gtk_window_new(GTK_WINDOW_TOPLEVEL); */
    p->topgwin = (GtkWidget*)g_object_new(PANEL_TOPLEVEL_TYPE, NULL);
    gtk_widget_set_name(p->topgwin, "PanelToplevel");
    p->display = gdk_display_get_default();
    gtk_container_set_border_width(GTK_CONTAINER(p->topgwin), 0);
    gtk_window_set_resizable(GTK_WINDOW(p->topgwin), FALSE);
    gtk_window_set_wmclass(GTK_WINDOW(p->topgwin), "panel", "lxpanel");
    gtk_window_set_title(GTK_WINDOW(p->topgwin), "panel");
    gtk_window_set_position(GTK_WINDOW(p->topgwin), GTK_WIN_POS_NONE);
    gtk_window_set_decorated(GTK_WINDOW(p->topgwin), FALSE);

    gtk_window_group_add_window( win_grp, (GtkWindow*)p->topgwin );

    g_signal_connect(G_OBJECT(p->topgwin), "delete-event",
          G_CALLBACK(panel_delete_event), p);
    g_signal_connect(G_OBJECT(p->topgwin), "destroy-event",
          G_CALLBACK(panel_destroy_event), p);
    g_signal_connect (G_OBJECT (p->topgwin), "size-request",
          (GCallback) panel_size_req, p);
    g_signal_connect (G_OBJECT (p->topgwin), "size-allocate",
          (GCallback) panel_size_alloc, p);
    g_signal_connect (G_OBJECT (p->topgwin), "configure-event",
          (GCallback) panel_configure_event, p);

    gtk_widget_add_events( p->topgwin, GDK_BUTTON_PRESS_MASK );
    g_signal_connect(G_OBJECT (p->topgwin), "button-press-event",
          (GCallback) panel_button_press_event_with_panel, p);

    g_signal_connect (G_OBJECT (p->topgwin), "realize",
          (GCallback) panel_realize, p);

    g_signal_connect (G_OBJECT (p->topgwin), "style-set",
          (GCallback)panel_style_set, p);
    gtk_widget_realize(p->topgwin);
    //gdk_window_set_decorations(p->topgwin->window, 0);

    // main layout manager as a single child of panel
    p->box = p->my_box_new(FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(p->box), 0);
//    gtk_container_add(GTK_CONTAINER(p->bbox), p->box);
    gtk_container_add(GTK_CONTAINER(p->topgwin), p->box);
    gtk_widget_show(p->box);
    if (p->round_corners)
        make_round_corners(p);

    p->topxwin = GDK_WINDOW_XWINDOW(GTK_WIDGET(p->topgwin)->window);
    DBG("topxwin = %x\n", p->topxwin);

    /* the settings that should be done before window is mapped */
    wmhints.flags = InputHint;
    wmhints.input = 0;
    XSetWMHints (GDK_DISPLAY(), p->topxwin, &wmhints);
#define WIN_HINTS_SKIP_FOCUS      (1<<0)    /* "alt-tab" skips this win */
    val = WIN_HINTS_SKIP_FOCUS;
    XChangeProperty(GDK_DISPLAY(), p->topxwin,
          XInternAtom(GDK_DISPLAY(), "_WIN_HINTS", False), XA_CARDINAL, 32,
          PropModeReplace, (unsigned char *) &val, 1);

    panel_set_dock_type(p);

    /* window mapping point */
    gtk_widget_show_all(p->topgwin);

    /* the settings that should be done after window is mapped */
    panel_establish_autohide(p);

    /* send it to running wm */
    Xclimsg(p->topxwin, a_NET_WM_DESKTOP, 0xFFFFFFFF, 0, 0, 0, 0);
    /* and assign it ourself just for case when wm is not running */
    val = 0xFFFFFFFF;
    XChangeProperty(GDK_DISPLAY(), p->topxwin, a_NET_WM_DESKTOP, XA_CARDINAL, 32,
          PropModeReplace, (unsigned char *) &val, 1);

    state[0] = a_NET_WM_STATE_SKIP_PAGER;
    state[1] = a_NET_WM_STATE_SKIP_TASKBAR;
    state[2] = a_NET_WM_STATE_STICKY;
    XChangeProperty(GDK_DISPLAY(), p->topxwin, a_NET_WM_STATE, XA_ATOM,
          32, PropModeReplace, (unsigned char *) state, 3);

    calculate_position(p);
    gdk_window_move_resize(p->topgwin->window, p->ax, p->ay, p->aw, p->ah);
    panel_set_wm_strut(p);

    RET();
}

/* Exchange the "width" and "height" terminology for vertical and horizontal panels. */
void panel_adjust_geometry_terminology(Panel * p)
{
    if ((p->height_label != NULL) && (p->width_label != NULL)
    && (p->alignment_left_label != NULL) && (p->alignment_right_label != NULL))
    {
        if ((p->edge == EDGE_TOP) || (p->edge == EDGE_BOTTOM))
        {
            gtk_label_set_text(GTK_LABEL(p->height_label), _("Height:"));
            gtk_label_set_text(GTK_LABEL(p->width_label), _("Width:"));
            gtk_button_set_label(GTK_BUTTON(p->alignment_left_label), _("Left"));
            gtk_button_set_label(GTK_BUTTON(p->alignment_right_label), _("Right"));
        }
        else
        {
            gtk_label_set_text(GTK_LABEL(p->height_label), _("Width:"));
            gtk_label_set_text(GTK_LABEL(p->width_label), _("Height:"));
            gtk_button_set_label(GTK_BUTTON(p->alignment_left_label), _("Top"));
            gtk_button_set_label(GTK_BUTTON(p->alignment_right_label), _("Bottom"));
        }
    }
}

/* Draw text into a label, with the user preference color and optionally bold. */
void panel_draw_label_text(Panel * p, GtkWidget * label, const char * text,
                           gboolean bold, float custom_size_factor,
                           gboolean custom_color)
{
    if (text == NULL)
    {
        /* Null string. */
        gtk_label_set_text(GTK_LABEL(label), NULL);
        return;
    }

    /* Compute an appropriate size so the font will scale with the panel's icon size. */
    int font_desc;
    if (p->usefontsize)
        font_desc = p->fontsize;
    else
    {
        if (p->icon_size < 20)
            font_desc = 9;
        else if (p->icon_size >= 20 && p->icon_size < 36)
            font_desc = 10;
        else
            font_desc = 12;
    }
    font_desc *= custom_size_factor;

    /* Check the string for characters that need to be escaped.
     * If any are found, create the properly escaped string and use it instead. */
    const char * valid_markup = text;
    char * escaped_text = NULL;
    const char * q;
    for (q = text; *q != '\0'; q += 1)
    {
        if ((*q == '<') || (*q == '>') || (*q == '&'))
        {
            escaped_text = g_markup_escape_text(text, -1);
            valid_markup = escaped_text;
            break;
        }
    }

    gchar * formatted_text;
    if ((custom_color) && (p->usefontcolor))
    {
        /* Color, optionally bold. */
        formatted_text = g_strdup_printf("<span font_desc=\"%d\" color=\"#%06x\">%s%s%s</span>",
                font_desc,
                gcolor2rgb24(&p->gfontcolor),
                ((bold) ? "<b>" : ""),
                valid_markup,
                ((bold) ? "</b>" : ""));
    }
    else
    {
        /* No color, optionally bold. */
        formatted_text = g_strdup_printf("<span font_desc=\"%d\">%s%s%s</span>",
                font_desc,
                ((bold) ? "<b>" : ""),
                valid_markup,
                ((bold) ? "</b>" : ""));
    }

    gtk_label_set_markup(GTK_LABEL(label), formatted_text);
    g_free(formatted_text);
    g_free(escaped_text);
}

void panel_set_panel_configuration_changed(Panel *p)
{
    GList *plugins, *l;

    GtkOrientation previous_orientation = p->orientation;
    p->orientation = (p->edge == EDGE_TOP || p->edge == EDGE_BOTTOM)
        ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;

    /* either first run or orientation was changed */
    if (p->my_box_new == NULL || previous_orientation != p->orientation)
    {
        panel_adjust_geometry_terminology(p);
        if (p->my_box_new != NULL)
            p->height = ((p->orientation == GTK_ORIENTATION_HORIZONTAL) ? PANEL_HEIGHT_DEFAULT : PANEL_WIDTH_DEFAULT);
        if (p->height_control != NULL)
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(p->height_control), p->height);
        if ((p->widthtype == WIDTH_PIXEL) && (p->width_control != NULL))
        {
            int value = ((p->orientation == GTK_ORIENTATION_HORIZONTAL) ? gdk_screen_width() : gdk_screen_height());
            gtk_spin_button_set_range(GTK_SPIN_BUTTON(p->width_control), 0, value);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(p->width_control), value);
        }
    }

    if (p->orientation == GTK_ORIENTATION_HORIZONTAL) {
        p->my_box_new = gtk_hbox_new;
        p->my_separator_new = gtk_vseparator_new;
    } else {
        p->my_box_new = gtk_vbox_new;
        p->my_separator_new = gtk_hseparator_new;
    }

    /* recreate the main layout box */
    if (p->box != NULL)
    {
        gtk_orientable_set_orientation(GTK_ORIENTABLE(p->box), p->orientation);
    }

    /* NOTE: This loop won't be executed when panel started since
       plugins are not loaded at that time.
       This is used when the orientation of the panel is changed
       from the config dialog, and plugins should be re-layout.
    */
    plugins = p->box ? gtk_container_get_children(GTK_CONTAINER(p->box)) : NULL;
    for( l = plugins; l; l = l->next ) {
        GtkWidget *w = (GtkWidget*)l->data;
        LXPanelPluginInit *init = PLUGIN_CLASS(w);
        if (init->reconfigure)
            init->reconfigure(p, w);
    }
    g_list_free(plugins);
}

static int
panel_parse_global(Panel *p, config_setting_t *cfg)
{
    const char *str;
    gint i;

    /* check Global config */
    if (!cfg || strcmp(config_setting_get_name(cfg), "Global") != 0)
    {
        ERR( "lxpanel: Global section not found\n");
        RET(0);
    }
    if (config_setting_lookup_string(cfg, "edge", &str))
        p->edge = str2num(edge_pair, str, EDGE_NONE);
    if (config_setting_lookup_string(cfg, "allign", &str))
        p->allign = str2num(allign_pair, str, ALLIGN_NONE);
    config_setting_lookup_int(cfg, "monitor", &p->monitor);
    config_setting_lookup_int(cfg, "margin", &p->margin);
    if (config_setting_lookup_string(cfg, "widthtype", &str))
        p->widthtype = str2num(width_pair, str, WIDTH_NONE);
    config_setting_lookup_int(cfg, "width", &p->width);
    if (config_setting_lookup_string(cfg, "heighttype", &str))
        p->heighttype = str2num(height_pair, str, HEIGHT_NONE);
    config_setting_lookup_int(cfg, "height", &p->height);
    if (config_setting_lookup_int(cfg, "spacing", &i) && i > 0)
        p->spacing = i;
    if (config_setting_lookup_int(cfg, "setdocktype", &i))
        p->setdocktype = i != 0;
    if (config_setting_lookup_int(cfg, "setpartialstrut", &i))
        p->setstrut = i != 0;
    if (config_setting_lookup_int(cfg, "RoundCorners", &i))
        p->round_corners = i != 0;
    if (config_setting_lookup_int(cfg, "transparent", &i))
        p->transparent = i != 0;
    if (config_setting_lookup_int(cfg, "alpha", &p->alpha))
    {
        if (p->alpha > 255)
            p->alpha = 255;
    }
    if (config_setting_lookup_int(cfg, "autohide", &i))
        p->autohide = i != 0;
    config_setting_lookup_int(cfg, "heightwhenhidden", &p->height_when_hidden);
    if (config_setting_lookup_string(cfg, "tintcolor", &str))
    {
        if (!gdk_color_parse (str, &p->gtintcolor))
            gdk_color_parse ("white", &p->gtintcolor);
        p->tintcolor = gcolor2rgb24(&p->gtintcolor);
            DBG("tintcolor=%x\n", p->tintcolor);
    }
    if (config_setting_lookup_int(cfg, "usefontcolor", &i))
        p->usefontcolor = i != 0;
    if (config_setting_lookup_string(cfg, "fontcolor", &str))
    {
        if (!gdk_color_parse (str, &p->gfontcolor))
            gdk_color_parse ("black", &p->gfontcolor);
        p->fontcolor = gcolor2rgb24(&p->gfontcolor);
            DBG("fontcolor=%x\n", p->fontcolor);
    }
    if (config_setting_lookup_int(cfg, "usefontsize", &i))
        p->usefontsize = i != 0;
    if (config_setting_lookup_int(cfg, "fontsize", &i) && i > 0)
        p->fontsize = i;
    if (config_setting_lookup_int(cfg, "background", &i))
        p->background = i != 0;
    if (config_setting_lookup_string(cfg, "backgroundfile", &str))
        p->background_file = g_strdup(str);
    config_setting_lookup_int(cfg, "iconsize", &p->icon_size);
    if (config_setting_lookup_int(cfg, "loglevel", &configured_log_level))
    {
        if (!log_level_set_on_commandline)
            log_level = configured_log_level;
    }

    panel_normalize_configuration(p);

    return 1;
}

static int
panel_parse_plugin(Panel *p, config_setting_t *cfg)
{
    const char *type = NULL;

    ENTER;
    config_setting_lookup_string(cfg, "type", &type);
    DBG("plug %s\n", type);

    if (!type || lxpanel_add_plugin(p, type, cfg, -1) == NULL) {
        ERR( "lxpanel: can't load %s plugin\n", type);
        goto error;
    }
    RET(1);

error:
    RET(0);
}

int panel_start( Panel *p )
{
    config_setting_t *list, *s;
    int i;

    /* parse global section */
    ENTER;

    list = config_setting_get_member(config_root_setting(p->config), "");
    if (!list || !panel_parse_global(p, config_setting_get_elem(list, 0)))
        RET(0);

    panel_start_gui(p);

    for (i = 1; (s = config_setting_get_elem(list, i)) != NULL; )
        if (strcmp(config_setting_get_name(s), "Plugin") == 0 &&
            panel_parse_plugin(p, s)) /* success on plugin start */
            i++;
        else /* remove invalid data from config */
            config_setting_remove_elem(list, i);

    /* update backgrond of panel and all plugins */
    panel_update_background( p );
    return 1;
}

void panel_destroy(Panel *p)
{
    ENTER;

    if (p->pref_dialog != NULL)
        gtk_widget_destroy(p->pref_dialog);
    if (p->plugin_pref_dialog != NULL)
        /* just close the dialog, it will do all required cleanup */
        gtk_dialog_response(GTK_DIALOG(p->plugin_pref_dialog), GTK_RESPONSE_CLOSE);

    if (p->bg != NULL)
    {
        g_signal_handlers_disconnect_by_func(G_OBJECT(p->bg), on_root_bg_changed, p);
        g_object_unref(p->bg);
    }

    if( p->config_changed )
        panel_config_save( p );
    config_destroy(p->config);

    if( p->topgwin )
    {
        gtk_window_group_remove_window( win_grp, GTK_WINDOW(  p->topgwin ) );
        gtk_widget_destroy(p->topgwin);
    }
    g_free(p->workarea);
    g_free( p->background_file );
    g_slist_free( p->system_menus );
    gdk_flush();
    XFlush(GDK_DISPLAY());
    XSync(GDK_DISPLAY(), True);

    g_free( p->name );
    g_free(p);
    RET();
}

Panel* panel_new( const char* config_file, const char* config_name )
{
    Panel* panel = NULL;

    if (G_LIKELY(config_file))
    {
        panel = panel_allocate();
        panel->name = g_strdup(config_name);
        g_debug("starting panel from file %s",config_file);
        if (!config_read_file(panel->config, config_file) ||
            !panel_start(panel))
        {
            ERR( "lxpanel: can't start panel\n");
            panel_destroy( panel );
            panel = NULL;
        }
    }
    return panel;
}

static void
usage()
{
    g_print(_("lxpanel %s - lightweight GTK2+ panel for UNIX desktops\n"), version);
    g_print(_("Command line options:\n"));
    g_print(_(" --help      -- print this help and exit\n"));
    g_print(_(" --version   -- print version and exit\n"));
    g_print(_(" --log <number> -- set log level 0-5. 0 - none 5 - chatty\n"));
//    g_print(_(" --configure -- launch configuration utility\n"));
    g_print(_(" --profile name -- use specified profile\n"));
    g_print("\n");
    g_print(_(" -h  -- same as --help\n"));
    g_print(_(" -p  -- same as --profile\n"));
    g_print(_(" -v  -- same as --version\n"));
 //   g_print(_(" -C  -- same as --configure\n"));
    g_print(_("\nVisit http://lxde.org/ for detail.\n\n"));
}

int panel_handle_x_error(Display * d, XErrorEvent * ev)
{
    char buf[256];

    if (log_level >= LOG_WARN) {
        XGetErrorText(GDK_DISPLAY(), ev->error_code, buf, 256);
        LOG(LOG_WARN, "lxpanel : X error: %s\n", buf);
    }
    return 0;	/* Ignored */
}

int panel_handle_x_error_swallow_BadWindow_BadDrawable(Display * d, XErrorEvent * ev)
{
    if ((ev->error_code != BadWindow) && (ev->error_code != BadDrawable))
        panel_handle_x_error(d, ev);
    return 0;	/* Ignored */
}

/* Lightweight lock related functions - X clipboard hacks */

#define CLIPBOARD_NAME "LXPANEL_SELECTION"

/*
 * clipboard_get_func - dummy get_func for gtk_clipboard_set_with_data ()
 */
static void
clipboard_get_func(
    GtkClipboard *clipboard G_GNUC_UNUSED,
    GtkSelectionData *selection_data G_GNUC_UNUSED,
    guint info G_GNUC_UNUSED,
    gpointer user_data_or_owner G_GNUC_UNUSED)
{
}

/*
 * clipboard_clear_func - dummy clear_func for gtk_clipboard_set_with_data ()
 */
static void clipboard_clear_func(
    GtkClipboard *clipboard G_GNUC_UNUSED,
    gpointer user_data_or_owner G_GNUC_UNUSED)
{
}

/*
 * Lightweight version for checking single instance.
 * Try and get the CLIPBOARD_NAME clipboard instead of using file manipulation.
 *
 * Returns TRUE if successfully retrieved and FALSE otherwise.
 */
static gboolean check_main_lock()
{
    static const GtkTargetEntry targets[] = { { CLIPBOARD_NAME, 0, 0 } };
    gboolean retval = FALSE;
    GtkClipboard *clipboard;
    Atom atom;

    atom = gdk_x11_get_xatom_by_name(CLIPBOARD_NAME);

    XGrabServer(GDK_DISPLAY());

    if (XGetSelectionOwner(GDK_DISPLAY(), atom) != None)
        goto out;

    clipboard = gtk_clipboard_get(gdk_atom_intern(CLIPBOARD_NAME, FALSE));

    if (gtk_clipboard_set_with_data(clipboard, targets,
                                    G_N_ELEMENTS (targets),
                                    clipboard_get_func,
                                    clipboard_clear_func, NULL))
        retval = TRUE;

out:
    XUngrabServer (GDK_DISPLAY ());
    gdk_flush ();

    return retval;
}
#undef CLIPBOARD_NAME

static void _start_panels_from_dir(const char *panel_dir)
{
    GDir* dir = g_dir_open( panel_dir, 0, NULL );
    const gchar* name;

    if( ! dir )
    {
        return;
    }

    while((name = g_dir_read_name(dir)) != NULL)
    {
        char* panel_config = g_build_filename( panel_dir, name, NULL );
        if (strchr(panel_config, '~') == NULL)	/* Skip editor backup files in case user has hand edited in this directory */
        {
            Panel* panel = panel_new( panel_config, name );
            if( panel )
                all_panels = g_slist_prepend( all_panels, panel );
        }
        g_free( panel_config );
    }
    g_dir_close( dir );
}

static gboolean start_all_panels( )
{
    char *panel_dir;

    /* try user panels */
    panel_dir = _user_config_file_name("panels", NULL);
    _start_panels_from_dir(panel_dir);
    g_free(panel_dir);
    if (all_panels != NULL)
        return TRUE;
    /* else try XDG fallback */
    panel_dir = _system_config_file_name("panels");
    _start_panels_from_dir(panel_dir);
    g_free(panel_dir);
    if (all_panels != NULL)
        return TRUE;
    /* last try at old fallback for compatibility reasons */
    panel_dir = _old_system_config_file_name("panels");
    _start_panels_from_dir(panel_dir);
    g_free(panel_dir);
    return all_panels != NULL;
}

void load_global_config();
void free_global_config();

static void _ensure_user_config_dirs(void)
{
    char *dir = g_build_filename(g_get_user_config_dir(), "lxpanel", cprofile,
                                 "panels", NULL);

    /* make sure the private profile and panels dir exists */
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
}

int main(int argc, char *argv[], char *env[])
{
    int i;
    const char* desktop_name;

    setlocale(LC_CTYPE, "");

	g_thread_init(NULL);
/*	gdk_threads_init();
	gdk_threads_enter(); */

    gtk_init(&argc, &argv);

#ifdef ENABLE_NLS
    bindtextdomain ( GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR );
    bind_textdomain_codeset ( GETTEXT_PACKAGE, "UTF-8" );
    textdomain ( GETTEXT_PACKAGE );
#endif

    XSetLocaleModifiers("");
    XSetErrorHandler((XErrorHandler) panel_handle_x_error);

    resolve_atoms();

    desktop_name = g_getenv("XDG_CURRENT_DESKTOP");
    is_in_lxde = desktop_name && (0 == strcmp(desktop_name, "LXDE"));

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage();
            exit(0);
        } else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
            printf("lxpanel %s\n", version);
            exit(0);
        } else if (!strcmp(argv[i], "--log")) {
            i++;
            if (i == argc) {
                ERR( "lxpanel: missing log level\n");
                usage();
                exit(1);
            } else {
                log_level = atoi(argv[i]);
                log_level_set_on_commandline = true;
            }
        } else if (!strcmp(argv[i], "--configure") || !strcmp(argv[i], "-C")) {
            config = 1;
        } else if (!strcmp(argv[i], "--profile") || !strcmp(argv[i], "-p")) {
            i++;
            if (i == argc) {
                ERR( "lxpanel: missing profile name\n");
                usage();
                exit(1);
            } else {
                cprofile = g_strdup(argv[i]);
            }
        } else {
            printf("lxpanel: unknown option - %s\n", argv[i]);
            usage();
            exit(1);
        }
    }

    /* Check for duplicated lxpanel instances */
    if (!check_main_lock() && !config) {
        printf("There is already an instance of LXPanel.  Now to exit\n");
        exit(1);
    }

    _ensure_user_config_dirs();

    /* Add our own icons to the search path of icon theme */
    gtk_icon_theme_append_search_path( gtk_icon_theme_get_default(), PACKAGE_DATA_DIR "/images" );

    fbev = fb_ev_new();
    win_grp = gtk_window_group_new();

restart:
    is_restarting = FALSE;

    /* init LibFM */
    fm_gtk_init(NULL);

    /* prepare modules data */
    _prepare_modules();

    load_global_config();

	/* NOTE: StructureNotifyMask is required by XRandR
	 * See init_randr_support() in gdkscreen-x11.c of gtk+ for detail.
	 */
    gdk_window_set_events(gdk_get_default_root_window(), GDK_STRUCTURE_MASK |
            GDK_SUBSTRUCTURE_MASK | GDK_PROPERTY_CHANGE_MASK);
    gdk_window_add_filter(gdk_get_default_root_window (), (GdkFilterFunc)panel_event_filter, NULL);

    if( G_UNLIKELY( ! start_all_panels() ) )
        g_warning( "Config files are not found.\n" );
/*
 * FIXME: configure??
    if (config)
        configure();
*/
    gtk_main();

    XSelectInput (GDK_DISPLAY(), GDK_ROOT_WINDOW(), NoEventMask);
    gdk_window_remove_filter(gdk_get_default_root_window (), (GdkFilterFunc)panel_event_filter, NULL);

    /* destroy all panels */
    g_slist_foreach( all_panels, (GFunc) panel_destroy, NULL );
    g_slist_free( all_panels );
    all_panels = NULL;
    g_free( cfgfile );

    free_global_config();

    _unload_modules();
    fm_gtk_finalize();

    if( is_restarting )
        goto restart;

    /* gdk_threads_leave(); */

    g_object_unref(win_grp);
    g_object_unref(fbev);

    return 0;
}

GtkOrientation panel_get_orientation(Panel *panel)
{
    return panel->orientation;
}

gint panel_get_icon_size(Panel *panel)
{
    return panel->icon_size;
}

gint panel_get_height(Panel *panel)
{
    return panel->height;
}

GtkWindow *panel_get_toplevel_window(Panel *panel)
{
    return GTK_WINDOW(panel->topgwin);
}

Window panel_get_xwindow(Panel *panel)
{
    return panel->topxwin;
}

gint panel_get_monitor(Panel *panel)
{
    return panel->monitor;
}

GtkStyle *panel_get_defstyle(Panel *panel)
{
    return panel->defstyle;
}

GtkIconTheme *panel_get_icon_theme(Panel *panel)
{
    return panel->icon_theme;
}

gboolean panel_is_at_bottom(Panel *panel)
{
    return panel->edge == EDGE_BOTTOM;
}

GtkWidget *panel_box_new(Panel *panel, gboolean homogeneous, gint spacing)
{
    return panel->my_box_new(homogeneous, spacing);
}

GtkWidget *panel_separator_new(Panel *panel)
{
    return panel->my_separator_new();
}

gboolean _class_is_present(LXPanelPluginInit *init)
{
    GSList *sl;

    for (sl = all_panels; sl; sl = sl->next )
    {
        Panel *panel = (Panel*)sl->data;
        GList *plugins, *p;

        plugins = gtk_container_get_children(GTK_CONTAINER(panel->box));
        for (p = plugins; p; p = p->next)
            if (PLUGIN_CLASS(p->data) == init)
            {
                g_list_free(plugins);
                return TRUE;
            }
        g_list_free(plugins);
    }
    return FALSE;
}
