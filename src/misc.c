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

#include <X11/Xatom.h>
#include <X11/cursorfont.h>

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <libfm/fm-gtk.h>

#include "misc.h"
#include "private.h"

#include "dbg.h"

/* data used by themed images buttons */
typedef struct {
    FmIcon *icon;
    guint theme_changed_handler;
    GdkPixbuf* pixbuf;
    GdkPixbuf* hilight;
    gulong hicolor;
    gint size; /* desired size */
} ImgData;

static GQuark img_data_id = 0;

static void on_theme_changed(GtkIconTheme* theme, GtkWidget* img);
static void _gtk_image_set_from_file_scaled(GtkWidget *img, ImgData *data);

/* X11 data types */
Atom a_UTF8_STRING;
Atom a_XROOTPMAP_ID;

/* old WM spec */
Atom a_WM_STATE;
Atom a_WM_CLASS;
Atom a_WM_DELETE_WINDOW;
Atom a_WM_PROTOCOLS;

/* new NET spec */
Atom a_NET_WORKAREA;
Atom a_NET_CLIENT_LIST;
Atom a_NET_CLIENT_LIST_STACKING;
Atom a_NET_NUMBER_OF_DESKTOPS;
Atom a_NET_CURRENT_DESKTOP;
Atom a_NET_DESKTOP_VIEWPORT;
Atom a_NET_DESKTOP_NAMES;
Atom a_NET_ACTIVE_WINDOW;
Atom a_NET_CLOSE_WINDOW;
Atom a_NET_SHOWING_DESKTOP;
Atom a_NET_SUPPORTED;
Atom a_NET_WM_STATE;
Atom a_NET_WM_STATE_SKIP_TASKBAR;
Atom a_NET_WM_STATE_SKIP_PAGER;
Atom a_NET_WM_STATE_STICKY;
Atom a_NET_WM_STATE_HIDDEN;
Atom a_NET_WM_STATE_SHADED;
Atom a_NET_WM_WINDOW_TYPE;
Atom a_NET_WM_WINDOW_TYPE_DESKTOP;
Atom a_NET_WM_WINDOW_TYPE_DOCK;
Atom a_NET_WM_WINDOW_TYPE_TOOLBAR;
Atom a_NET_WM_WINDOW_TYPE_MENU;
Atom a_NET_WM_WINDOW_TYPE_UTILITY;
Atom a_NET_WM_WINDOW_TYPE_SPLASH;
Atom a_NET_WM_WINDOW_TYPE_DIALOG;
Atom a_NET_WM_WINDOW_TYPE_NORMAL;
Atom a_NET_WM_DESKTOP;
Atom a_NET_WM_PID;
Atom a_NET_WM_NAME;
Atom a_NET_WM_VISIBLE_NAME;
Atom a_NET_WM_STRUT;
Atom a_NET_WM_STRUT_PARTIAL;
Atom a_NET_WM_ICON;
Atom a_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR;

/* SYSTEM TRAY spec */
Atom a_NET_SYSTEM_TRAY_OPCODE;
Atom a_NET_SYSTEM_TRAY_MESSAGE_DATA;
Atom a_NET_SYSTEM_TRAY_ORIENTATION;
Atom a_MANAGER;

Atom a_LXPANEL_CMD; /* for private client message */

/* if current window manager is EWMH conforming. */
gboolean is_ewmh_supported;

enum{
    I_UTF8_STRING,
    I_XROOTPMAP_ID,
    I_WM_STATE,
    I_WM_CLASS,
    I_WM_DELETE_WINDOW,
    I_WM_PROTOCOLS,

    I_NET_WORKAREA,
    I_NET_CLIENT_LIST,
    I_NET_CLIENT_LIST_STACKING,
    I_NET_NUMBER_OF_DESKTOPS,
    I_NET_CURRENT_DESKTOP,
    I_NET_DESKTOP_VIEWPORT,
    I_NET_DESKTOP_NAMES,
    I_NET_ACTIVE_WINDOW,
    I_NET_SHOWING_DESKTOP,
    I_NET_SUPPORTED,

    I_NET_WM_STATE,
    I_NET_WM_STATE_SKIP_TASKBAR,
    I_NET_WM_STATE_SKIP_PAGER,
    I_NET_WM_STATE_STICKY,
    I_NET_WM_STATE_HIDDEN,
    I_NET_WM_STATE_SHADED,
    I_NET_WM_WINDOW_TYPE,

    I_NET_WM_WINDOW_TYPE_DESKTOP,
    I_NET_WM_WINDOW_TYPE_DOCK,
    I_NET_WM_WINDOW_TYPE_TOOLBAR,
    I_NET_WM_WINDOW_TYPE_MENU,
    I_NET_WM_WINDOW_TYPE_UTILITY,
    I_NET_WM_WINDOW_TYPE_SPLASH,
    I_NET_WM_WINDOW_TYPE_DIALOG,
    I_NET_WM_WINDOW_TYPE_NORMAL,
    I_NET_WM_DESKTOP,
    I_NET_WM_PID,
    I_NET_WM_NAME,
    I_NET_WM_VISIBLE_NAME,
    I_NET_WM_STRUT,
    I_NET_WM_STRUT_PARTIAL,
    I_NET_WM_ICON,
    I_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR,

    I_NET_SYSTEM_TRAY_OPCODE,
    I_NET_SYSTEM_TRAY_MESSAGE_DATA,
    I_NET_SYSTEM_TRAY_ORIENTATION,
    I_MANAGER,

    I_LXPANEL_CMD,
    N_ATOMS
};

pair allign_pair[] = {
    { ALLIGN_NONE, "none" },
    { ALLIGN_LEFT, "left" },
    { ALLIGN_RIGHT, "right" },
    { ALLIGN_CENTER, "center"},
    { 0, NULL },
};

pair edge_pair[] = {
    { EDGE_NONE, "none" },
    { EDGE_LEFT, "left" },
    { EDGE_RIGHT, "right" },
    { EDGE_TOP, "top" },
    { EDGE_BOTTOM, "bottom" },
    { 0, NULL },
};

pair width_pair[] = {
    { WIDTH_NONE, "none" },
    { WIDTH_REQUEST, "request" },
    { WIDTH_PIXEL, "pixel" },
    { WIDTH_PERCENT, "percent" },
    { 0, NULL },
};

pair height_pair[] = {
    { HEIGHT_NONE, "none" },
    { HEIGHT_PIXEL, "pixel" },
    { 0, NULL },
};

pair bool_pair[] = {
    { 0, "0" },
    { 1, "1" },
    { 0, NULL },
};

int
str2num(pair *p, const gchar *str, int defval)
{
    ENTER;
    for (;p && p->str; p++) {
        if (!g_ascii_strcasecmp(str, p->str))
            RET(p->num);
    }
    RET(defval);
}

const gchar *
num2str(pair *p, int num, const gchar *defval)
{
    ENTER;
    for (;p && p->str; p++) {
        if (num == p->num)
            RET(p->str);
    }
    RET(defval);
}

int buf_gets( char* buf, int len, char **fp )
{
    char* p;
    int i = 0;
    if( !fp || !(p = *fp) || !**fp )
    {
        buf[0] = '\0';
        return 0;
    }

    do
    {
        if( G_LIKELY( i < len ) )
        {
            buf[i] = *p;
            ++i;
        }
        if( G_UNLIKELY(*p == '\n') )
        {
            ++p;
            break;
        }
    }while( *(++p) );
    buf[i] = '\0';
    *fp = p;
    return i;
}

extern int
lxpanel_put_line(FILE* fp, const char* format, ...)
{
    static int indent = 0;
    int i, ret;
    va_list args;

    if( strchr(format, '}') )
        --indent;

    for( i = 0; i < indent; ++i )
        fputs( "    ", fp );

    va_start (args, format);
    ret = vfprintf (fp, format, args);
    va_end (args);

    if( strchr(format, '{') )
        ++indent;
    fputc( '\n', fp );  /* add line break */
    return (ret + 1);
}

extern  int
lxpanel_get_line(char**fp, line *s)
{
    gchar *tmp, *tmp2;

    s->type = LINE_NONE;
    if (!fp)
        RET(s->type);
    while (buf_gets(s->str, s->len, fp)) {

        g_strstrip(s->str);

        if (s->str[0] == '#' || s->str[0] == 0) {
            continue;
        }
        if (!g_ascii_strcasecmp(s->str, "}")) {
            s->type = LINE_BLOCK_END;
            break;
        }

        s->t[0] = s->str;
        for (tmp = s->str; isalnum(*tmp); tmp++);
        for (tmp2 = tmp; isspace(*tmp2); tmp2++);
        if (*tmp2 == '=') {
            for (++tmp2; isspace(*tmp2); tmp2++);
            s->t[1] = tmp2;
            *tmp = 0;
            s->type = LINE_VAR;
        } else if  (*tmp2 == '{') {
            *tmp = 0;
            s->type = LINE_BLOCK_START;
        } else {
            ERR( "parser: unknown token: '%c'\n", *tmp2);
        }
        break;
    }
    return s->type;
}

void resolve_atoms()
{
    static const char* atom_names[ N_ATOMS ];

    atom_names[ I_UTF8_STRING ] = "UTF8_STRING";
    atom_names[ I_XROOTPMAP_ID ] = "_XROOTPMAP_ID";
    atom_names[ I_WM_STATE ] = "WM_STATE";
    atom_names[ I_WM_CLASS ] = "WM_CLASS";
    atom_names[ I_WM_DELETE_WINDOW ] = "WM_DELETE_WINDOW";
    atom_names[ I_WM_PROTOCOLS ] = "WM_PROTOCOLS";
    atom_names[ I_NET_WORKAREA ] = "_NET_WORKAREA";
    atom_names[ I_NET_CLIENT_LIST ] = "_NET_CLIENT_LIST";
    atom_names[ I_NET_CLIENT_LIST_STACKING ] = "_NET_CLIENT_LIST_STACKING";
    atom_names[ I_NET_NUMBER_OF_DESKTOPS ] = "_NET_NUMBER_OF_DESKTOPS";
    atom_names[ I_NET_CURRENT_DESKTOP ] = "_NET_CURRENT_DESKTOP";
    atom_names[ I_NET_DESKTOP_VIEWPORT ] = "_NET_DESKTOP_VIEWPORT";
    atom_names[ I_NET_DESKTOP_NAMES ] = "_NET_DESKTOP_NAMES";
    atom_names[ I_NET_ACTIVE_WINDOW ] = "_NET_ACTIVE_WINDOW";
    atom_names[ I_NET_SHOWING_DESKTOP ] = "_NET_SHOWING_DESKTOP";
    atom_names[ I_NET_SUPPORTED ] = "_NET_SUPPORTED";
    atom_names[ I_NET_WM_DESKTOP ] = "_NET_WM_DESKTOP";
    atom_names[ I_NET_WM_STATE ] = "_NET_WM_STATE";
    atom_names[ I_NET_WM_STATE_SKIP_TASKBAR ] = "_NET_WM_STATE_SKIP_TASKBAR";
    atom_names[ I_NET_WM_STATE_SKIP_PAGER ] = "_NET_WM_STATE_SKIP_PAGER";
    atom_names[ I_NET_WM_STATE_STICKY ] = "_NET_WM_STATE_STICKY";
    atom_names[ I_NET_WM_STATE_HIDDEN ] = "_NET_WM_STATE_HIDDEN";
    atom_names[ I_NET_WM_STATE_SHADED ] = "_NET_WM_STATE_SHADED";
    atom_names[ I_NET_WM_WINDOW_TYPE ] = "_NET_WM_WINDOW_TYPE";

    atom_names[ I_NET_WM_WINDOW_TYPE_DESKTOP ] = "_NET_WM_WINDOW_TYPE_DESKTOP";
    atom_names[ I_NET_WM_WINDOW_TYPE_DOCK ] = "_NET_WM_WINDOW_TYPE_DOCK";
    atom_names[ I_NET_WM_WINDOW_TYPE_TOOLBAR ] = "_NET_WM_WINDOW_TYPE_TOOLBAR";
    atom_names[ I_NET_WM_WINDOW_TYPE_MENU ] = "_NET_WM_WINDOW_TYPE_MENU";
    atom_names[ I_NET_WM_WINDOW_TYPE_UTILITY ] = "_NET_WM_WINDOW_TYPE_UTILITY";
    atom_names[ I_NET_WM_WINDOW_TYPE_SPLASH ] = "_NET_WM_WINDOW_TYPE_SPLASH";
    atom_names[ I_NET_WM_WINDOW_TYPE_DIALOG ] = "_NET_WM_WINDOW_TYPE_DIALOG";
    atom_names[ I_NET_WM_WINDOW_TYPE_NORMAL ] = "_NET_WM_WINDOW_TYPE_NORMAL";
    atom_names[ I_NET_WM_DESKTOP ] = "_NET_WM_DESKTOP";
    atom_names[ I_NET_WM_PID ] = "_NET_WM_PID";
    atom_names[ I_NET_WM_NAME ] = "_NET_WM_NAME";
    atom_names[ I_NET_WM_VISIBLE_NAME ] = "_NET_WM_VISIBLE_NAME";
    atom_names[ I_NET_WM_STRUT ] = "_NET_WM_STRUT";
    atom_names[ I_NET_WM_STRUT_PARTIAL ] = "_NET_WM_STRUT_PARTIAL";
    atom_names[ I_NET_WM_ICON ] = "_NET_WM_ICON";
    atom_names[ I_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR ] = "_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR";

    atom_names[ I_NET_SYSTEM_TRAY_OPCODE ] = "_NET_SYSTEM_TRAY_OPCODE";
    atom_names[ I_NET_SYSTEM_TRAY_MESSAGE_DATA ] = "_NET_SYSTEM_TRAY_MESSAGE_DATA";
    atom_names[ I_NET_SYSTEM_TRAY_ORIENTATION ] = "_NET_SYSTEM_TRAY_ORIENTATION";
    atom_names[ I_MANAGER ] = "MANAGER";

    atom_names[ I_LXPANEL_CMD ] = "_LXPANEL_CMD";

    Atom atoms[ N_ATOMS ];

    ENTER;
   
    if( !  XInternAtoms( GDK_DISPLAY(), (char**)atom_names,
            N_ATOMS, False, atoms ) )
    {
        g_warning( "Error: unable to return Atoms" );
        return;
    }

    a_UTF8_STRING = atoms[ I_UTF8_STRING ];
    a_XROOTPMAP_ID = atoms[ I_XROOTPMAP_ID ];
    a_WM_STATE = atoms[ I_WM_STATE ];
    a_WM_CLASS = atoms[ I_WM_CLASS ];
    a_WM_DELETE_WINDOW = atoms[ I_WM_DELETE_WINDOW ];
    a_WM_PROTOCOLS = atoms[ I_WM_PROTOCOLS ];

    a_NET_WORKAREA = atoms[ I_NET_WORKAREA ];
    a_NET_CLIENT_LIST = atoms[ I_NET_CLIENT_LIST ];
    a_NET_CLIENT_LIST_STACKING = atoms[ I_NET_CLIENT_LIST_STACKING ];
    a_NET_NUMBER_OF_DESKTOPS = atoms[ I_NET_NUMBER_OF_DESKTOPS ];
    a_NET_CURRENT_DESKTOP = atoms[ I_NET_CURRENT_DESKTOP ];
    a_NET_DESKTOP_VIEWPORT = atoms[ I_NET_DESKTOP_VIEWPORT ];
    a_NET_DESKTOP_NAMES = atoms[ I_NET_DESKTOP_NAMES ];
    a_NET_ACTIVE_WINDOW = atoms[ I_NET_ACTIVE_WINDOW ];
    a_NET_SHOWING_DESKTOP = atoms[ I_NET_SHOWING_DESKTOP ];
    a_NET_SUPPORTED = atoms[ I_NET_SUPPORTED ];
    a_NET_WM_STATE = atoms[ I_NET_WM_STATE ];
    a_NET_WM_STATE_SKIP_TASKBAR = atoms[ I_NET_WM_STATE_SKIP_TASKBAR ];
    a_NET_WM_STATE_SKIP_PAGER = atoms[ I_NET_WM_STATE_SKIP_PAGER ];
    a_NET_WM_STATE_STICKY = atoms[ I_NET_WM_STATE_STICKY ];
    a_NET_WM_STATE_HIDDEN = atoms[ I_NET_WM_STATE_HIDDEN ];
    a_NET_WM_STATE_SHADED = atoms[ I_NET_WM_STATE_SHADED ];
    a_NET_WM_WINDOW_TYPE = atoms[ I_NET_WM_WINDOW_TYPE ];

    a_NET_WM_WINDOW_TYPE_DESKTOP = atoms[ I_NET_WM_WINDOW_TYPE_DESKTOP ];
    a_NET_WM_WINDOW_TYPE_DOCK = atoms[ I_NET_WM_WINDOW_TYPE_DOCK ];
    a_NET_WM_WINDOW_TYPE_TOOLBAR = atoms[ I_NET_WM_WINDOW_TYPE_TOOLBAR ];
    a_NET_WM_WINDOW_TYPE_MENU = atoms[ I_NET_WM_WINDOW_TYPE_MENU ];
    a_NET_WM_WINDOW_TYPE_UTILITY = atoms[ I_NET_WM_WINDOW_TYPE_UTILITY ];
    a_NET_WM_WINDOW_TYPE_SPLASH = atoms[ I_NET_WM_WINDOW_TYPE_SPLASH ];
    a_NET_WM_WINDOW_TYPE_DIALOG = atoms[ I_NET_WM_WINDOW_TYPE_DIALOG ];
    a_NET_WM_WINDOW_TYPE_NORMAL = atoms[ I_NET_WM_WINDOW_TYPE_NORMAL ];
    a_NET_WM_DESKTOP = atoms[ I_NET_WM_DESKTOP ];
    a_NET_WM_PID = atoms[ I_NET_WM_PID ];
    a_NET_WM_NAME = atoms[ I_NET_WM_NAME ];
    a_NET_WM_VISIBLE_NAME = atoms[ I_NET_WM_VISIBLE_NAME ];
    a_NET_WM_STRUT = atoms[ I_NET_WM_STRUT ];
    a_NET_WM_STRUT_PARTIAL = atoms[ I_NET_WM_STRUT_PARTIAL ];
    a_NET_WM_ICON = atoms[ I_NET_WM_ICON ];
    a_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR = atoms[ I_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR ];

    a_NET_SYSTEM_TRAY_OPCODE = atoms[ I_NET_SYSTEM_TRAY_OPCODE ];
    a_NET_SYSTEM_TRAY_MESSAGE_DATA = atoms [ I_NET_SYSTEM_TRAY_MESSAGE_DATA ];
    a_NET_SYSTEM_TRAY_ORIENTATION = atoms[ I_NET_SYSTEM_TRAY_ORIENTATION ];
    a_MANAGER = atoms[ I_MANAGER ];

    a_LXPANEL_CMD = atoms[ I_LXPANEL_CMD ];

    RET();
}


void
Xclimsg(Window win, Atom type, long l0, long l1, long l2, long l3, long l4)
{
    XClientMessageEvent xev;
    xev.type = ClientMessage;
    xev.window = win;
    xev.message_type = type;
    xev.format = 32;
    xev.data.l[0] = l0;
    xev.data.l[1] = l1;
    xev.data.l[2] = l2;
    xev.data.l[3] = l3;
    xev.data.l[4] = l4;
    XSendEvent(GDK_DISPLAY(), GDK_ROOT_WINDOW(), False,
          (SubstructureNotifyMask | SubstructureRedirectMask),
          (XEvent *) &xev);
}

void
Xclimsgwm(Window win, Atom type, Atom arg)
{
    XClientMessageEvent xev;

    xev.type = ClientMessage;
    xev.window = win;
    xev.message_type = type;
    xev.format = 32;
    xev.data.l[0] = arg;
    xev.data.l[1] = GDK_CURRENT_TIME;
    XSendEvent(GDK_DISPLAY(), win, False, 0L, (XEvent *) &xev);
}


void *
get_utf8_property(Window win, Atom atom)
{
    Atom type;
    int format;
    gulong nitems;
    gulong bytes_after;
    gchar *val, *retval;
    int result;
    guchar *tmp = NULL;

    type = None;
    retval = NULL;
    result = XGetWindowProperty (GDK_DISPLAY(), win, atom, 0, G_MAXLONG, False,
          a_UTF8_STRING, &type, &format, &nitems,
          &bytes_after, &tmp);
    if (result != Success || type == None)
        return NULL;
    val = (gchar *) tmp;
    if (val) {
        if (type == a_UTF8_STRING && format == 8 && nitems != 0)
            retval = g_strndup (val, nitems);
        XFree (val);
    }
    return retval;

}

char **
get_utf8_property_list(Window win, Atom atom, int *count)
{
    Atom type;
    int format;
    gulong nitems, i;
    gulong bytes_after;
    gchar *s, **retval = NULL;
    int result;
    guchar *tmp = NULL;

    *count = 0;
    result = XGetWindowProperty(GDK_DISPLAY(), win, atom, 0, G_MAXLONG, False,
          a_UTF8_STRING, &type, &format, &nitems,
          &bytes_after, &tmp);
    if (result != Success || type != a_UTF8_STRING || tmp == NULL)
        return NULL;

    if (nitems) {
        gchar *val = (gchar *) tmp;
        DBG("res=%d(%d) nitems=%d val=%s\n", result, Success, nitems, val);
        for (i = 0; i < nitems; i++) {
            if (!val[i])
                (*count)++;
        }
        retval = g_new0 (char*, *count + 2);
        for (i = 0, s = val; (int)i < *count; i++, s = s +  strlen (s) + 1) {
            retval[i] = g_strdup(s);
        }
        if (val[nitems-1]) {
            result = nitems - (s - val);
            DBG("val does not ends by 0, moving last %d bytes\n", result);
            g_memmove(s - 1, s, result);
            val[nitems-1] = 0;
            DBG("s=%s\n", s -1);
            retval[i] = g_strdup(s - 1);
            (*count)++;
        }
    }
    XFree (tmp);

    return retval;

}

void *
get_xaproperty (Window win, Atom prop, Atom type, int *nitems)
{
    Atom type_ret;
    int format_ret;
    unsigned long items_ret;
    unsigned long after_ret;
    unsigned char *prop_data;

    ENTER;
    prop_data = NULL;
    if (XGetWindowProperty (GDK_DISPLAY(), win, prop, 0, 0x7fffffff, False,
              type, &type_ret, &format_ret, &items_ret,
              &after_ret, &prop_data) != Success)
    {
        if( G_UNLIKELY(prop_data) )
            XFree( prop_data );
        if( nitems )
            *nitems = 0;
        RET(NULL);
    }
    if (nitems)
        *nitems = items_ret;
    RET(prop_data);
}

static char*
text_property_to_utf8 (const XTextProperty *prop)
{
  char **list;
  int count;
  char *retval;

  ENTER;
  list = NULL;
  count = gdk_text_property_to_utf8_list (gdk_x11_xatom_to_atom (prop->encoding),
                                          prop->format,
                                          prop->value,
                                          prop->nitems,
                                          &list);

  DBG("count=%d\n", count);
  if (count == 0)
    return NULL;

  retval = list[0];
  list[0] = g_strdup (""); /* something to free */

  g_strfreev (list);

  RET(retval);
}

char *
get_textproperty(Window win, Atom atom)
{
    XTextProperty text_prop;
    char *retval;

    ENTER;
    if (XGetTextProperty(GDK_DISPLAY(), win, &text_prop, atom)) {
        DBG("format=%d enc=%d nitems=%d value=%s   \n",
              text_prop.format,
              text_prop.encoding,
              text_prop.nitems,
              text_prop.value);
        retval = text_property_to_utf8 (&text_prop);
        if (text_prop.nitems > 0)
            XFree (text_prop.value);
        RET(retval);

    }
    RET(NULL);
}


int
get_net_number_of_desktops()
{
    int desknum;
    guint32 *data;

    ENTER;
    data = get_xaproperty (GDK_ROOT_WINDOW(), a_NET_NUMBER_OF_DESKTOPS,
          XA_CARDINAL, 0);
    if (!data)
        RET(0);

    desknum = *data;
    XFree (data);
    RET(desknum);
}


int
get_net_current_desktop ()
{
    int desk;
    guint32 *data;

    ENTER;
    data = get_xaproperty (GDK_ROOT_WINDOW(), a_NET_CURRENT_DESKTOP, XA_CARDINAL, 0);
    if (!data)
        RET(0);

    desk = *data;
    XFree (data);
    RET(desk);
}

int
get_net_wm_desktop(Window win)
{
    int desk = 0;
    guint32 *data;

    ENTER;
    data = get_xaproperty (win, a_NET_WM_DESKTOP, XA_CARDINAL, 0);
    if (data) {
        desk = *data;
        XFree (data);
    }
    RET(desk);
}

GPid
get_net_wm_pid(Window win)
{
    GPid pid = 0;
    guint32 *data;

    ENTER;
    data = get_xaproperty (win, a_NET_WM_PID, XA_CARDINAL, 0);
    if (data) {
        pid = *data;
        XFree (data);
    }
    RET(pid);
}

void
get_net_wm_state(Window win, NetWMState *nws)
{
    Atom *state;
    int num3;


    ENTER;
    memset(nws, 0, sizeof(*nws));
    if (!(state = get_xaproperty(win, a_NET_WM_STATE, XA_ATOM, &num3)))
        RET();

    DBG( "%x: netwm state = { ", (unsigned int)win);
    while (--num3 >= 0) {
        if (state[num3] == a_NET_WM_STATE_SKIP_PAGER) {
            DBG("NET_WM_STATE_SKIP_PAGER ");
            nws->skip_pager = 1;
        } else if (state[num3] == a_NET_WM_STATE_SKIP_TASKBAR) {
            DBG( "NET_WM_STATE_SKIP_TASKBAR ");
        nws->skip_taskbar = 1;
    } else if (state[num3] == a_NET_WM_STATE_STICKY) {
            DBG( "NET_WM_STATE_STICKY ");
        nws->sticky = 1;
        } else if (state[num3] == a_NET_WM_STATE_HIDDEN) {
            DBG( "NET_WM_STATE_HIDDEN ");
            nws->hidden = 1;
    } else if (state[num3] == a_NET_WM_STATE_SHADED) {
            DBG( "NET_WM_STATE_SHADED ");
            nws->shaded = 1;
    } else {
        DBG( "... ");
    }
    }
    XFree(state);
    DBG( "}\n");
    RET();
}

void
get_net_wm_window_type(Window win, NetWMWindowType *nwwt)
{
    Atom *state;
    int num3;


    ENTER;
    memset(nwwt, 0, sizeof(*nwwt));
    if (!(state = get_xaproperty(win, a_NET_WM_WINDOW_TYPE, XA_ATOM, &num3)))
        RET();

    DBG( "%x: netwm state = { ", (unsigned int)win);
    while (--num3 >= 0) {
        if (state[num3] == a_NET_WM_WINDOW_TYPE_DESKTOP) {
            DBG("NET_WM_WINDOW_TYPE_DESKTOP ");
            nwwt->desktop = 1;
        } else if (state[num3] == a_NET_WM_WINDOW_TYPE_DOCK) {
            DBG( "NET_WM_WINDOW_TYPE_DOCK ");
        nwwt->dock = 1;
    } else if (state[num3] == a_NET_WM_WINDOW_TYPE_TOOLBAR) {
            DBG( "NET_WM_WINDOW_TYPE_TOOLBAR ");
        nwwt->toolbar = 1;
        } else if (state[num3] == a_NET_WM_WINDOW_TYPE_MENU) {
            DBG( "NET_WM_WINDOW_TYPE_MENU ");
            nwwt->menu = 1;
    } else if (state[num3] == a_NET_WM_WINDOW_TYPE_UTILITY) {
            DBG( "NET_WM_WINDOW_TYPE_UTILITY ");
            nwwt->utility = 1;
    } else if (state[num3] == a_NET_WM_WINDOW_TYPE_SPLASH) {
            DBG( "NET_WM_WINDOW_TYPE_SPLASH ");
            nwwt->splash = 1;
    } else if (state[num3] == a_NET_WM_WINDOW_TYPE_DIALOG) {
            DBG( "NET_WM_WINDOW_TYPE_DIALOG ");
            nwwt->dialog = 1;
    } else if (state[num3] == a_NET_WM_WINDOW_TYPE_NORMAL) {
            DBG( "NET_WM_WINDOW_TYPE_NORMAL ");
            nwwt->normal = 1;
    } else {
        DBG( "... ");
    }
    }
    XFree(state);
    DBG( "}\n");
    RET();
}

int
get_wm_state (Window win)
{
    unsigned long *data;
    int ret = 0;

    ENTER;
    data = get_xaproperty (win, a_WM_STATE, a_WM_STATE, 0);
    if (data) {
        ret = data[0];
        XFree (data);
    }
    RET(ret);
}

static void
calculate_width(int scrw, int wtype, int allign, int margin,
      int *panw, int *x)
{
    ENTER;
    DBG("scrw=%d\n", scrw);
    DBG("IN panw=%d, margin=%d\n", *panw, margin);
    //scrw -= 2;
    if (wtype == WIDTH_PERCENT) {
        /* sanity check */
        if (*panw > 100)
            *panw = 100;
        else if (*panw < 0)
            *panw = 1;
        *panw = ((gfloat) scrw * (gfloat) *panw) / 100.0;
    }
    if (allign != ALLIGN_CENTER) {
        if (margin > scrw) {
            ERR( "margin is bigger then edge size %d > %d. Ignoring margin\n",
                  margin, scrw);
            margin = 0;
        }
	*panw = MIN(scrw - margin, *panw);
    }
    DBG("OUT panw=%d\n", *panw);
    if (allign == ALLIGN_LEFT)
        *x += margin;
    else if (allign == ALLIGN_RIGHT) {
        *x += scrw - *panw - margin;
        if (*x < 0)
            *x = 0;
    } else if (allign == ALLIGN_CENTER)
        *x += (scrw - *panw) / 2;
    RET();
}


void
calculate_position(Panel *np)
{
    GdkScreen *screen;
    GdkRectangle marea;

    ENTER;
    /* FIXME: Why this doesn't work? */
    /* if you are still going to use this, be sure to update it to take into
       account multiple monitors */
    if (0)  {
//        if (np->curdesk < np->wa_len/4) {
        marea.x = np->workarea[np->curdesk*4 + 0];
        marea.y = np->workarea[np->curdesk*4 + 1];
        marea.width  = np->workarea[np->curdesk*4 + 2];
        marea.height = np->workarea[np->curdesk*4 + 3];
    } else {
        screen = gtk_widget_get_screen(np->topgwin);
        g_assert(np->monitor >= 0 && np->monitor < gdk_screen_get_n_monitors(screen));
        gdk_screen_get_monitor_geometry(screen,np->monitor,&marea);
    }

    if (np->edge == EDGE_TOP || np->edge == EDGE_BOTTOM) {
        np->aw = np->width;
        np->ax = marea.x;
        calculate_width(marea.width, np->widthtype, np->allign, np->margin,
              &np->aw, &np->ax);
        np->ah = ((( ! np->autohide) || (np->visible)) ? np->height : np->height_when_hidden);
        np->ay = marea.y + ((np->edge == EDGE_TOP) ? 0 : (marea.height - np->ah));

    } else {
        np->ah = np->width;
        np->ay = marea.y;
        calculate_width(marea.height, np->widthtype, np->allign, np->margin,
              &np->ah, &np->ay);
        np->aw = ((( ! np->autohide) || (np->visible)) ? np->height : np->height_when_hidden);
        np->ax = marea.x + ((np->edge == EDGE_LEFT) ? 0 : (marea.width - np->aw));
    }
    //g_debug("%s - x=%d y=%d w=%d h=%d\n", __FUNCTION__, np->ax, np->ay, np->aw, np->ah);
    RET();
}

gchar *
expand_tilda(const gchar *file)
{
    ENTER;
    RET((file[0] == '~') ?
        g_strdup_printf("%s%s", getenv("HOME"), file+1)
        : g_strdup(file));

}

/*
 * SuxPanel version 0.1
 * Copyright (c) 2003 Leandro Pereira <leandro@linuxmag.com.br>
 *
 * This program may be distributed under the terms of GNU General
 * Public License version 2. You should have received a copy of the
 * license with this program; if not, please consult http://www.fsf.org/.
 *
 * This program comes with no warranty. Use at your own risk.
 *
 */

/* DestroyNotify handler for image data in _gtk_image_new_from_file_scaled. */
static void img_data_free(ImgData * data)
{
    g_object_unref(data->icon);
    if (data->theme_changed_handler != 0)
        g_signal_handler_disconnect(gtk_icon_theme_get_default(), data->theme_changed_handler);
    if (data->pixbuf != NULL)
        g_object_unref(data->pixbuf);
    if (data->hilight != NULL)
        g_object_unref(data->hilight);
    g_free(data);
}

/* Handler for "changed" signal in _gtk_image_new_from_file_scaled. */
static void on_theme_changed(GtkIconTheme * theme, GtkWidget * img)
{
    ImgData * data = (ImgData *) g_object_get_qdata(G_OBJECT(img), img_data_id);
    _gtk_image_set_from_file_scaled(img, data);
}

/* consumes reference on icon */
static void _lxpanel_button_set_icon(GtkWidget* btn, FmIcon* icon, gint size)
{
    /* Locate the image within the button. */
    GtkWidget * child = gtk_bin_get_child(GTK_BIN(btn));
    GtkWidget * img = NULL;
    if (GTK_IS_IMAGE(child))
        img = child;
    else if (GTK_IS_BOX(child))
    {
        GList * children = gtk_container_get_children(GTK_CONTAINER(child));
        img = GTK_WIDGET(GTK_IMAGE(children->data));
        g_list_free(children);
    }

    if (img != NULL)
    {
        ImgData * data = (ImgData *) g_object_get_qdata(G_OBJECT(img), img_data_id);

        if (icon != data->icon || size != data->size) /* something was changed */
        {
            g_object_unref(data->icon);
            data->icon = icon;
            data->size = size;
            _gtk_image_set_from_file_scaled(img, data);
        }
        else
            g_object_unref(icon);
    }
    else
        g_object_unref(icon);
}

void lxpanel_button_set_icon(GtkWidget* btn, const gchar *name, gint size)
{
    _lxpanel_button_set_icon(btn, fm_icon_from_name(name), size);
}

void lxpanel_button_update_icon(GtkWidget* btn, FmIcon *icon, gint size)
{
    _lxpanel_button_set_icon(btn, g_object_ref(icon), size);
}

/* parameters width and keep_ratio are unused, kept for backward compatibility */
void fb_button_set_from_file(GtkWidget * btn, const char * img_file, gint width, gint height, gboolean keep_ratio)
{
    lxpanel_button_set_icon(btn, img_file, height);
}

static void _gtk_image_set_from_file_scaled(GtkWidget * img, ImgData * data)
{
    if (data->pixbuf != NULL)
    {
        g_object_unref(data->pixbuf);
        data->pixbuf = NULL;
    }

    /* if there is a cached hilighted version of this pixbuf, free it */
    if (data->hilight != NULL)
    {
        g_object_unref(data->hilight);
        data->hilight = NULL;
    }

    if (G_LIKELY(G_IS_THEMED_ICON(data->icon)))
        data->pixbuf = fm_pixbuf_from_icon_with_fallback(data->icon, data->size,
                                                         "application-x-executable");
    else
    {
        char *file = g_icon_to_string(fm_icon_get_gicon(data->icon));
        data->pixbuf = gdk_pixbuf_new_from_file_at_scale(file, -1, data->size, TRUE, NULL);
        g_free(file);
    }

    if (data->pixbuf != NULL)
    {
        /* Set the pixbuf into the image widget. */
        gtk_image_set_from_pixbuf((GtkImage *)img, data->pixbuf);
    }
    else
    {
        /* No pixbuf available.  Set the "missing image" icon. */
        gtk_image_set_from_stock(GTK_IMAGE(img), GTK_STOCK_MISSING_IMAGE, GTK_ICON_SIZE_BUTTON);
    }
}

/* consumes reference on icon */
static GtkWidget *_gtk_image_new_for_icon(FmIcon *icon, gint size)
{
    GtkWidget * img = gtk_image_new();
    ImgData * data = g_new0(ImgData, 1);

    data->icon = icon;
    data->size = size;
    if (img_data_id == 0)
        img_data_id = g_quark_from_static_string("ImgData");
    g_object_set_qdata_full(G_OBJECT(img), img_data_id, data, (GDestroyNotify) img_data_free);
    _gtk_image_set_from_file_scaled(img, data);
    if (G_IS_THEMED_ICON(data->icon))
    {
        /* This image is loaded from icon theme.  Update the image if the icon theme is changed. */
        data->theme_changed_handler = g_signal_connect(gtk_icon_theme_get_default(), "changed", G_CALLBACK(on_theme_changed), img);
    }
    return img;
}

/* parameters width and keep_ratio are unused, kept for backward compatibility */
GtkWidget * _gtk_image_new_from_file_scaled(const gchar * file, gint width, gint height, gboolean keep_ratio)
{
    return _gtk_image_new_for_icon(fm_icon_from_name(file), height);
}

void
get_button_spacing(GtkRequisition *req, GtkContainer *parent, gchar *name)
{
    GtkWidget *b;
    //gint focus_width;
    //gint focus_pad;

    ENTER;
    b = gtk_button_new();
    gtk_widget_set_name(GTK_WIDGET(b), name);
    GTK_WIDGET_UNSET_FLAGS (b, GTK_CAN_FOCUS);
    GTK_WIDGET_UNSET_FLAGS (b, GTK_CAN_DEFAULT);
    gtk_container_set_border_width (GTK_CONTAINER (b), 0);

    if (parent)
        gtk_container_add(parent, b);

    gtk_widget_show(b);
    gtk_widget_size_request(b, req);

    gtk_widget_destroy(b);
    RET();
}


guint32 gcolor2rgb24(GdkColor *color)
{
    guint32 i;

    ENTER;

    i = (color->red * 0xFF / 0xFFFF) & 0xFF;
    i <<= 8;
    i |= (color->green * 0xFF / 0xFFFF) & 0xFF;
    i <<= 8;
    i |= (color->blue * 0xFF / 0xFFFF) & 0xFF;
    DBG("i=%x\n", i);
    RET(i);
}

/* Handler for "enter-notify-event" signal on image that has highlighting requested. */
static gboolean fb_button_enter(GtkImage * widget, GdkEventCrossing * event)
{
    if (gtk_image_get_storage_type(widget) == GTK_IMAGE_PIXBUF)
    {
        ImgData * data = (ImgData *) g_object_get_qdata(G_OBJECT(widget), img_data_id);
        if (data != NULL)
        {
            if (data->hilight == NULL)
            {
                GdkPixbuf * dark = data->pixbuf;
                int height = gdk_pixbuf_get_height(dark);
                int rowstride = gdk_pixbuf_get_rowstride(dark);
                gulong hicolor = data->hicolor;

                GdkPixbuf * light = gdk_pixbuf_add_alpha(dark, FALSE, 0, 0, 0);
                if (light != NULL)
                {
                    guchar extra[3];
                    int i;
                    for (i = 2; i >= 0; i--, hicolor >>= 8)
                        extra[i] = hicolor & 0xFF;

                    guchar * src = gdk_pixbuf_get_pixels(light);
                    guchar * up;
                    for (up = src + height * rowstride; src < up; src += 4)
                    {
                        if (src[3] != 0)
                        {
                            for (i = 0; i < 3; i++)
                            {
                            int value = src[i] + extra[i];
                            if (value > 255) value = 255;
                            src[i] = value;
                            }
                        }
                    }
                data->hilight = light;
                }
            }

        if (data->hilight != NULL)
            gtk_image_set_from_pixbuf(widget, data->hilight);
        }
    }
    return TRUE;
}

/* Handler for "leave-notify-event" signal on image that has highlighting requested. */
static gboolean fb_button_leave(GtkImage * widget, GdkEventCrossing * event, gpointer user_data)
{
    if (gtk_image_get_storage_type(widget) == GTK_IMAGE_PIXBUF)
    {
        ImgData * data = (ImgData *) g_object_get_qdata(G_OBJECT(widget), img_data_id);
        if ((data != NULL) && (data->pixbuf != NULL))
            gtk_image_set_from_pixbuf(widget, data->pixbuf);
    }
    return TRUE;
}


/* consumes reference on icon */
static GtkWidget *_lxpanel_button_new_for_icon(Panel *panel, FmIcon *icon,
                                               gint size, gulong highlight_color,
                                               const gchar *label)
{
    GtkWidget * event_box = gtk_event_box_new();
    gtk_container_set_border_width(GTK_CONTAINER(event_box), 0);
    GTK_WIDGET_UNSET_FLAGS(event_box, GTK_CAN_FOCUS);

    GtkWidget * image = _gtk_image_new_for_icon(icon, size);
    gtk_misc_set_padding(GTK_MISC(image), 0, 0);
    gtk_misc_set_alignment(GTK_MISC(image), 0, 0);
    if (highlight_color != 0)
    {
        ImgData * data = (ImgData *) g_object_get_qdata(G_OBJECT(image), img_data_id);
        data->hicolor = highlight_color;

        gtk_widget_add_events(event_box, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
        g_signal_connect_swapped(G_OBJECT(event_box), "enter-notify-event", G_CALLBACK(fb_button_enter), image);
        g_signal_connect_swapped(G_OBJECT(event_box), "leave-notify-event", G_CALLBACK(fb_button_leave), image);
    }

    if (label == NULL)
        gtk_container_add(GTK_CONTAINER(event_box), image);
    else
    {
        GtkWidget * inner = gtk_hbox_new(FALSE, 0);
        gtk_container_set_border_width(GTK_CONTAINER(inner), 0);
        GTK_WIDGET_UNSET_FLAGS (inner, GTK_CAN_FOCUS);
        gtk_container_add(GTK_CONTAINER(event_box), inner);

        gtk_box_pack_start(GTK_BOX(inner), image, FALSE, FALSE, 0);

        GtkWidget * lbl = gtk_label_new("");
        panel_draw_label_text(panel, lbl, label, FALSE, 1, TRUE);
        gtk_misc_set_padding(GTK_MISC(lbl), 2, 0);
        gtk_box_pack_end(GTK_BOX(inner), lbl, FALSE, FALSE, 0);
    }

    gtk_widget_show_all(event_box);
    return event_box;
}

GtkWidget *lxpanel_button_new_for_icon(Panel *panel, const gchar *name, GdkColor *color, const gchar *label)
{
    gulong highlight_color = color ? gcolor2rgb24(color) : PANEL_ICON_HIGHLIGHT;
    return _lxpanel_button_new_for_icon(panel, fm_icon_from_name(name),
                                        panel->icon_size, highlight_color, label);
}

GtkWidget *lxpanel_button_new_for_fm_icon(Panel *panel, FmIcon *icon, GdkColor *color, const gchar *label)
{
    gulong highlight_color = color ? gcolor2rgb24(color) : PANEL_ICON_HIGHLIGHT;
    return _lxpanel_button_new_for_icon(panel, g_object_ref(icon),
                                        panel->icon_size, highlight_color, label);
}

/* parameters width and keep_ratio are unused, kept for backward compatibility */
GtkWidget * fb_button_new_from_file(
    const gchar * image_file, int width, int height, gulong highlight_color, gboolean keep_ratio)
{
    return fb_button_new_from_file_with_label(image_file, width, height, highlight_color, keep_ratio, NULL, NULL);
}

/* parameters width and keep_ratio are unused, kept for backward compatibility */
GtkWidget * fb_button_new_from_file_with_label(
    const gchar * image_file, int width, int height, gulong highlight_color, gboolean keep_ratio, Panel * panel, const gchar * label)
{
    return _lxpanel_button_new_for_icon(panel, fm_icon_from_name(image_file), height, highlight_color, label);
}

char* translate_exec_to_cmd( const char* exec, const char* icon,
                             const char* title, const char* fpath )
{
    GString* cmd = g_string_sized_new( 256 );
    if (!exec)
	    return NULL;
    for( ; *exec; ++exec )
    {
        if( G_UNLIKELY(*exec == '%') )
        {
            ++exec;
            if( !*exec )
                break;
            switch( *exec )
            {
                case 'c':
                    if( title )
                    {
                        g_string_append( cmd, title );
                    }
                    break;
                case 'i':
                    if( icon )
                    {
                        g_string_append( cmd, "--icon " );
                        g_string_append( cmd, icon );
                    }
                    break;
                case 'k':
                    if( fpath )
                    {
                        char* uri = g_filename_to_uri( fpath, NULL, NULL );
                        g_string_append( cmd, uri );
                        g_free( uri );
                    }
                    break;
                case '%':
                    g_string_append_c( cmd, '%' );
                    break;
            }
        }
        else
            g_string_append_c( cmd, *exec );
    }
    return g_string_free( cmd, FALSE );
}

/*
 This function is used to re-create a new box with different
 orientation from the old one, add all children of the old one to
 the new one, and then destroy the old box.
 It's mainly used when we need to change the orientation of the panel or
 any plugin with a layout box. Since GtkHBox cannot be changed to GtkVBox,
 recreating a new box to replace the old one is required.
*/
/* for compatibility with old plugins */
GtkWidget* recreate_box( GtkBox* oldbox, GtkOrientation orientation )
{
    gtk_orientable_set_orientation(GTK_ORIENTABLE(oldbox), orientation);
    return GTK_WIDGET(oldbox);
}

/* for compatibility with old plugins */
void show_error( GtkWindow* parent_win, const char* msg )
{
    fm_show_error(parent_win, NULL, msg);
}

/* old plugins compatibility mode, use fm_pixbuf_from_icon_with_fallback() instead */
GdkPixbuf * lxpanel_load_icon(const char * name, int width, int height, gboolean use_fallback)
{
    FmIcon * fm_icon;
    GdkPixbuf * icon = NULL;

    fm_icon = fm_icon_from_name(name ? name : "application-x-executable");
    /* well, we don't use parameter width and not really use cache here */
    icon = fm_pixbuf_from_icon_with_fallback(fm_icon, height,
                            use_fallback ? "application-x-executable" : NULL);
    g_object_unref(fm_icon);
    return icon;
}

/*
 * Taken from pcmanfm:
 * Parse Exec command line of app desktop file, and translate
 * it into a real command which can be passed to g_spawn_command_line_async().
 * file_list is a null-terminated file list containing full
 * paths of the files passed to app.
 * returned char* should be freed when no longer needed.
 */
static char* translate_app_exec_to_command_line( const char* pexec,
                                                 GList* file_list )
{
    char* file;
    GList* l;
    gchar *tmp;
    GString* cmd = g_string_new("");
    gboolean add_files = FALSE;

    for( ; *pexec; ++pexec )
    {
        if( *pexec == '%' )
        {
            ++pexec;
            switch( *pexec )
            {
            case 'U':
                for( l = file_list; l; l = l->next )
                {
                    tmp = g_filename_to_uri( (char*)l->data, NULL, NULL );
                    file = g_shell_quote( tmp );
                    g_free( tmp );
                    g_string_append( cmd, file );
                    if (l->next)
                        g_string_append_c( cmd, ' ' );
                    g_free( file );
                }
                add_files = TRUE;
                break;
            case 'u':
                if( file_list && file_list->data )
                {
                    file = (char*)file_list->data;
                    tmp = g_filename_to_uri( file, NULL, NULL );
                    file = g_shell_quote( tmp );
                    g_free( tmp );
                    g_string_append( cmd, file );
                    g_free( file );
                    add_files = TRUE;
                }
                break;
            case 'F':
            case 'N':
                for( l = file_list; l; l = l->next )
                {
                    file = (char*)l->data;
                    tmp = g_shell_quote( file );
                    g_string_append( cmd, tmp );
                    if (l->next)
                        g_string_append_c( cmd, ' ' );
                    g_free( tmp );
                }
                add_files = TRUE;
                break;
            case 'f':
            case 'n':
                if( file_list && file_list->data )
                {
                    file = (char*)file_list->data;
                    tmp = g_shell_quote( file );
                    g_string_append( cmd, tmp );
                    g_free( tmp );
                    add_files = TRUE;
                }
                break;
            case 'D':
                for( l = file_list; l; l = l->next )
                {
                    tmp = g_path_get_dirname( (char*)l->data );
                    file = g_shell_quote( tmp );
                    g_free( tmp );
                    g_string_append( cmd, file );
                    if (l->next)
                        g_string_append_c( cmd, ' ' );
                    g_free( file );
                }
                add_files = TRUE;
                break;
            case 'd':
                if( file_list && file_list->data )
                {
                    tmp = g_path_get_dirname( (char*)file_list->data );
                    file = g_shell_quote( tmp );
                    g_free( tmp );
                    g_string_append( cmd, file );
                    g_free( tmp );
                    add_files = TRUE;
                }
                break;
            case 'c':
                #if 0
                g_string_append( cmd, vfs_app_desktop_get_disp_name( app ) );
                #endif
                break;
            case 'i':
                /* Add icon name */
                #if 0
                if( vfs_app_desktop_get_icon_name( app ) )
                {
                    g_string_append( cmd, "--icon " );
                    g_string_append( cmd, vfs_app_desktop_get_icon_name( app ) );
                }
                #endif
                break;
            case 'k':
                /* Location of the desktop file */
                break;
            case 'v':
                /* Device name */
                break;
            case '%':
                g_string_append_c ( cmd, '%' );
                break;
            case '\0':
                goto _finish;
                break;
            }
        }
        else  /* not % escaped part */
        {
            g_string_append_c ( cmd, *pexec );
        }
    }
_finish:
    if( ! add_files )
    {
        for( l = file_list; l; l = l->next )
        {
            g_string_append_c( cmd, ' ' );
            file = (char*)l->data;
            tmp = g_shell_quote( file );
            g_string_append( cmd, tmp );
            g_free( tmp );
        }
    }

    return g_string_free( cmd, FALSE );
}

gboolean spawn_command_async(GtkWindow *parent_window, gchar const* workdir,
        gchar const* cmd)
{
    GError* err = NULL;
    gchar** argv = NULL;

    LOG(LOG_INFO, "lxpanel: spawning \"%s\"...\n", cmd);

    g_shell_parse_argv(cmd, NULL, &argv, &err);
    if (!err)
        g_spawn_async(workdir, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err);

    if (err)
    {
        ERR("%s\n", err->message);
        fm_show_error(parent_window, NULL, err->message);
        g_error_free(err);
    }

    g_strfreev(argv);

    return !err;
}

/* FIXME: this should be replaced with fm_launch_file_simple() */
gboolean lxpanel_launch_app(const char* exec, GList* files, gboolean in_terminal, char const* in_workdir)
{
    GError *error = NULL;
    char* cmd;
    if( ! exec )
        return FALSE;
    cmd = translate_app_exec_to_command_line(exec, files);
    if( in_terminal )
    {
	char * escaped_cmd = g_shell_quote(cmd);
        char* term_cmd;
        const char* term = fm_config->terminal ? fm_config->terminal : "lxterminal";
        if( strstr(term, "%s") )
            term_cmd = g_strdup_printf(term, escaped_cmd);
        else
            term_cmd = g_strconcat( term, " -e ", escaped_cmd, NULL );
	g_free(escaped_cmd);
        if( cmd != exec )
            g_free(cmd);
        cmd = term_cmd;
    }

    spawn_command_async(NULL, in_workdir, cmd);

    g_free(cmd);

    return (error == NULL);
}

/* vim: set sw=4 et sts=4 : */
