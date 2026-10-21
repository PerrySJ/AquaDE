#ifndef DESK_TRAY_H
#define DESK_TRAY_H

/* The system tray: what programs put there, through StatusNotifierItem.
 *
 * AquaDE is the watcher the items register with, if nobody else on the
 * bus is, and a host either way -- the thing that shows them, which here
 * is a row of paper slips tucked under the Control Panel at the back of
 * the desk. X11's XEmbed tray is not here: it is a window inside a window,
 * and a slip of paper is not a place for somebody else's window. */

#include <cairo.h>
#include <stdbool.h>
#include <stdint.h>

struct wl_event_loop;

struct desk_tray_item {
	unsigned id;             /* the same for as long as the item lives */
	char title[128];         /* its Title, else its tooltip, else its Id */
	char name_id[96];        /* its Id */
	cairo_surface_t *icon;   /* NULL until found, and when it has none */
	bool attention;          /* Status NeedsAttention */
	bool passive;            /* Status Passive: there, but nothing to say */
	bool has_menu;           /* it has a menu for the host to show */
	bool is_menu;            /* and it is nothing but that menu: any click shows it */
};

/* changed is called on the compositor's thread whenever the list or
 * anything shown of it changes. False when there is no session bus. */
bool desk_tray_init(struct wl_event_loop *loop, void (*changed)(void *user), void *user);
void desk_tray_finish(void);

/* The items, in the order they arrived. Valid until the next call into
 * the tray. */
int desk_tray_items(const struct desk_tray_item **out);

/* how: 0 the primary action (a left click), 1 the secondary (middle),
 * 2 its context menu (right). x, y: where on the screen it was asked. */
void desk_tray_click(unsigned id, int how, int x, int y);
void desk_tray_scroll(unsigned id, int delta, bool horizontal);
/* An item's menu (com.canonical.dbusmenu). One at a time: open asks for
 * the item's, and changed() is called when it has come and whenever it
 * changes; close says so to the program and lets it go. */
enum {
	DESK_TRAY_MENU_NONE = 0,   /* it has none, or it could not be had */
	DESK_TRAY_MENU_LOADING,
	DESK_TRAY_MENU_READY,
};
struct desk_tray_menu_entry {
	int32_t id;
	char label[128];         /* the underscores that mark keys taken out */
	bool enabled, separator, submenu;
	int toggle;              /* 0 none, 1 a check box, 2 a radio button */
	int state;               /* 1 on, 0 off, -1 neither */
	cairo_surface_t *icon;   /* may be NULL */
};
void desk_tray_menu_open(unsigned id);
void desk_tray_menu_close(void);
/* The entries under parent (0: the top of it) that are to be shown, in
 * order; valid until the next call into the tray. */
int desk_tray_menu(int32_t parent, const struct desk_tray_menu_entry **out, int *state);
/* A submenu is about to be shown, and an entry was chosen. */
void desk_tray_menu_show(int32_t entry);
void desk_tray_menu_click(int32_t entry);

/* Once a second or so: calls that never came back, and the like. */
void desk_tray_tick(void);

#endif
