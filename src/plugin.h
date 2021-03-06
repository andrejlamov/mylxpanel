/*
 * Copyright (c) 2014 LxDE Developers, see the file AUTHORS for details.
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

#ifndef __PLUGIN_H__
#define __PLUGIN_H__ 1

#include <libfm/fm.h>

#include "panel.h"
#include "conf.h"

G_BEGIN_DECLS

/* New plugins style which uses FmModule loader, our module type is "lxpanel_gtk" */

#define FM_MODULE_lxpanel_gtk_VERSION 1 /* version of this API */

/**
 * LXPanelPluginInit:
 * @init: (allow-none): callback on lxpanel start
 * @finalize: (allow-none): callback on lxpanel exit
 * @name: name to represent plugin in lists
 * @description: tooltip on the plugin in lists
 * @new_instance: callback to create new plugin instance in panel
 * @config: (allow-none): callback to show configuration dialog
 * @reconfigure: (allow-none): callback to apply panel configuration change
 * @button_press_event: (allow-none): callback on "button-press-event" signal
 * @show_system_menu: (allow-none): callback to queue show system menu
 *
 * Callback @init is called on module loading, only once per application
 * lifetime.
 *
 * Callback @new_instance is called when panel tries to add some plugin.
 * It should initialize instance data, prepare widget, and return the
 * widget or %NULL if something went wrong. The @panel and @settings data
 * are guaranteed to be valid until gtk_widget_destroy() is called on the
 * instance. Instance own data should be attached to the instance using
 * lxpanel_plugin_set_data().
 *
 * Callback @config is called when user tries to configure the instance
 * (either via context menu or from plugins selection dialog). It should
 * create dialog window with instance configuration options. Returned
 * dialog will be destroyed on responce or on instance destroy and any
 * changed settings will be saved to the panel configuration file. See
 * also lxpanel_generic_config_dlg().
 *
 * Callback @reconfigure is called when panel configuration was changed
 * in the panel configuration dialog.
 */
typedef struct {
    /*< public >*/
    void (*init)(void);         /* optional startup */
    void (*finalize)(void);     /* optional finalize */
    char *name;                 /* name to represent in lists */
    char *description;          /* tooltip text */
    GtkWidget *(*new_instance)(Panel *panel, config_setting_t *settings);
    GtkWidget *(*config)(Panel *panel, GtkWidget *instance, GtkWindow *parent);
    void (*reconfigure)(Panel *panel, GtkWidget *instance);
    gboolean (*button_press_event)(GtkWidget *widget, GdkEventButton *event, Panel *panel);
    void (*show_system_menu)(GtkWidget *widget);
    int one_per_system : 1;     /* True to disable more than one instance */
    int expand_available : 1;   /* True if "stretch" option is available */
    int expand_default : 1;     /* True if "stretch" option is default */
    int superseded : 1;         /* True if plugin was superseded by another */
    /*< private >*/
    gpointer _reserved1;
    gpointer _reserved2;
} LXPanelPluginInit; /* constant data */

extern LXPanelPluginInit fm_module_init_lxpanel_gtk;

extern GQuark lxpanel_plugin_qdata; /* access to plugin private data */
/**
 * lxpanel_plugin_get_data
 * @_i: instance widget
 *
 * Retrieves instance data attached to the widget.
 */
#define lxpanel_plugin_get_data(_i) g_object_get_qdata(G_OBJECT(_i),lxpanel_plugin_qdata)
/**
 * lxpanel_plugin_set_data
 * @_i: instance widget
 * @_data: instance data
 * @_destructor: destructor for @_data
 *
 * Attaches data to the widget instance. The @_destructor callback will
 * be called on @_data when @_i is destroyed and therefore it should free
 * any allocated data. The instance widget at that moment is already not
 * available to use in any way so not rely on it.
 */
#define lxpanel_plugin_set_data(_i,_data,_destructor) g_object_set_qdata_full(G_OBJECT(_i),lxpanel_plugin_qdata,_data,_destructor)

/* register new plugin type - can be called from plugins init() too */
extern gboolean lxpanel_register_plugin_type(const char *name, LXPanelPluginInit *init);

/* few helper functions */
extern GtkMenu* lxpanel_get_plugin_menu(Panel* panel, GtkWidget* plugin, gboolean use_sub_menu);
extern gboolean lxpanel_plugin_button_press_event(GtkWidget *plugin, GdkEventButton *event, Panel *panel);
			/* Handler for "button_press_event" signal with Plugin as parameter */
extern void lxpanel_plugin_adjust_popup_position(GtkWidget * popup, GtkWidget * plugin);
			/* Helper to move popup windows away from the panel */
extern void lxpanel_plugin_popup_set_position_helper(Panel * p, GtkWidget * near, GtkWidget * popup, GtkRequisition * popup_req, gint * px, gint * py);
			/* Helper for position-calculation callback for popup menus */
extern void plugin_widget_set_background(GtkWidget * plugin, Panel * p);
			/* Recursively set the background of all widgets on a panel background configuration change */
extern gboolean lxpanel_launch_path(Panel *panel, FmPath *path);
extern void lxpanel_plugin_show_config_dialog(Panel* panel, GtkWidget* plugin);
			/* Calls config() callback and shows configuration window */

G_END_DECLS

#endif /* __PLUGIN_H__ */
