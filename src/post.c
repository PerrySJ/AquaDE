/* ================================================================
 * The post box: AquaDE as the machine's notification daemon.
 *
 * Taking the org.freedesktop.Notifications name means what lands here is
 * the real thing -- what every application on the machine believes it is
 * telling you -- rather than a demonstration. There is no second thread:
 * sd_bus hands out a file descriptor, and the compositor's own event loop
 * is perfectly happy to watch one.
 * ================================================================ */

#include "post.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include <wayland-server-core.h>

static sd_bus *g_bus;
static sd_bus_slot *g_slot;
static struct wl_event_source *g_src;
static struct desk_note g_note[DESK_POST_MAX];
static int g_n;
static unsigned g_next_id = 1;
static void (*g_changed)(void *);
static void *g_changed_user;

static void changed(void)
{
	if (g_changed) {
		g_changed(g_changed_user);
	}
}

static void copy_into(char *dst, size_t n, const char *src)
{
	if (!src) {
		dst[0] = '\0';
		return;
	}
	size_t l = strlen(src);
	if (l >= n) {
		/* cut between characters, never inside one: cairo refuses half a
		 * character, and everything drawn after it on the same letter */
		l = n - 1;
		while (l > 0 && ((unsigned char)src[l] & 0xC0) == 0x80) {
			l--;
		}
	}
	memcpy(dst, src, l);
	dst[l] = '\0';
}

static int find_id(unsigned id)
{
	for (int i = 0; i < g_n; i++) {
		if (g_note[i].id == id) {
			return i;
		}
	}
	return -1;
}

static void drop_at(int i)
{
	if (i < 0 || i >= g_n) {
		return;
	}
	memmove(&g_note[i], &g_note[i + 1], (size_t)(g_n - i - 1) * sizeof g_note[0]);
	g_n--;
}

/* NotificationClosed reason 2 is "dismissed by the user", which is what
 * taking a letter out of the box and throwing it away amounts to. */
static void emit_closed(unsigned id, unsigned reason)
{
	if (!g_bus) {
		return;
	}
	sd_bus_emit_signal(g_bus, "/org/freedesktop/Notifications",
		"org.freedesktop.Notifications", "NotificationClosed", "uu", id, reason);
}

/* ---- the hints ---------------------------------------------------
 *
 * a{sv}, and the values are of whatever type the sender felt like. Only
 * three of them matter to a letter: how urgent it is, which application
 * it came from -- which is how the letter gets that application's icon
 * printed on it -- and an image path when there is one. */
static int read_hints(sd_bus_message *m, struct desk_note *note)
{
	int r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
	if (r < 0) {
		return r;
	}
	while ((r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv")) > 0) {
		const char *key = NULL;
		r = sd_bus_message_read(m, "s", &key);
		if (r < 0) {
			return r;
		}
		const char *contents = NULL;
		char type = 0;
		r = sd_bus_message_peek_type(m, &type, &contents);
		if (r < 0) {
			return r;
		}
		if (key && strcmp(key, "urgency") == 0 && contents && contents[0] == 'y') {
			uint8_t u = DESK_URGENCY_NORMAL;
			sd_bus_message_read(m, "v", "y", &u);
			note->urgency = u;
		} else if (key && strcmp(key, "desktop-entry") == 0
			&& contents && contents[0] == 's') {
			const char *s = NULL;
			sd_bus_message_read(m, "v", "s", &s);
			copy_into(note->desktop, sizeof note->desktop, s);
		} else if (key && strcmp(key, "resident") == 0
			&& contents && contents[0] == 'b') {
			int b = 0;
			sd_bus_message_read(m, "v", "b", &b);
			note->resident = b != 0;
		} else if (key && strcmp(key, "image-path") == 0
			&& contents && contents[0] == 's' && !note->icon[0]) {
			const char *s = NULL;
			sd_bus_message_read(m, "v", "s", &s);
			copy_into(note->icon, sizeof note->icon, s);
		} else {
			/* everything else, including image-data, goes past unread */
			r = sd_bus_message_skip(m, "v");
			if (r < 0) {
				return r;
			}
		}
		r = sd_bus_message_exit_container(m);
		if (r < 0) {
			return r;
		}
	}
	if (r < 0) {
		return r;
	}
	return sd_bus_message_exit_container(m);
}

/* A letter into the box: on top, or in place of the one it replaces. */
static unsigned post_put(struct desk_note *note, unsigned replaces)
{
	note->at = time(NULL);
	note->read = false;
	/* A sender that gives an id is correcting something it already sent,
	 * so that letter is replaced where it stands rather than a second one
	 * arriving underneath it. */
	int at = replaces ? find_id(replaces) : -1;
	if (at >= 0) {
		note->id = replaces;
		g_note[at] = *note;
	} else {
		note->id = g_next_id++;
		if (g_n >= DESK_POST_MAX) {
			/* the box is full: the bottom letter goes */
			emit_closed(g_note[g_n - 1].id, 4);
			g_n--;
		}
		memmove(&g_note[1], &g_note[0], (size_t)g_n * sizeof g_note[0]);
		g_note[0] = *note;
		g_n++;
	}
	changed();
	return note->id;
}

unsigned desk_post_own(const struct desk_note *note)
{
	struct desk_note n = *note;
	copy_into(n.app, sizeof n.app, DESK_POST_OWN);
	n.own = true;
	if (n.urgency < DESK_URGENCY_LOW || n.urgency > DESK_URGENCY_CRITICAL) {
		n.urgency = DESK_URGENCY_NORMAL;
	}
	return post_put(&n, 0);
}

static int on_notify(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)user;
	(void)err;
	const char *app = NULL, *icon = NULL, *summary = NULL, *body = NULL;
	uint32_t replaces = 0;
	int32_t expires = 0;

	int r = sd_bus_message_read(m, "susss", &app, &replaces, &icon, &summary, &body);
	if (r < 0) {
		return r;
	}
	struct desk_note note = { 0 };
	/* the actions, in pairs: the key, and the words for it. There are no
	 * buttons on a sheet of paper; they are printed on the back of its
	 * corner instead. */
	r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "s");
	if (r < 0) {
		return r;
	}
	for (;;) {
		const char *key = NULL, *label = NULL;
		r = sd_bus_message_read(m, "s", &key);
		if (r <= 0) {
			break;
		}
		r = sd_bus_message_read(m, "s", &label);
		if (r <= 0) {
			break;
		}
		if (note.n_actions < DESK_POST_ACTIONS && key && *key) {
			copy_into(note.action_key[note.n_actions], sizeof note.action_key[0], key);
			copy_into(note.action_label[note.n_actions], sizeof note.action_label[0],
				label && *label ? label : key);
			note.n_actions++;
		}
	}
	if (r < 0) {
		return r;
	}
	r = sd_bus_message_exit_container(m);
	if (r < 0) {
		return r;
	}

	note.urgency = DESK_URGENCY_NORMAL;
	copy_into(note.app, sizeof note.app, app);
	copy_into(note.icon, sizeof note.icon, icon);
	copy_into(note.summary, sizeof note.summary, summary);
	copy_into(note.body, sizeof note.body, body);

	r = read_hints(m, &note);
	if (r < 0) {
		return r;
	}
	sd_bus_message_read(m, "i", &expires);

	unsigned id = post_put(&note, replaces);
	return sd_bus_reply_method_return(m, "u", id);
}

static int on_close(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)user;
	(void)err;
	uint32_t id = 0;
	int r = sd_bus_message_read(m, "u", &id);
	if (r < 0) {
		return r;
	}
	int at = find_id(id);
	if (at >= 0) {
		drop_at(at);
		emit_closed(id, 3);   /* closed by a call to CloseNotification */
		changed();
	}
	return sd_bus_reply_method_return(m, "");
}

static int on_caps(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)user;
	(void)err;
	/* No markup: a letter is a sheet of paper with a thing typed on it.
	 * Actions, printed on the back of its corner. Persistence is the whole
	 * point of a post box. */
	return sd_bus_reply_method_return(m, "as", 3, "body", "actions", "persistence");
}

static int on_server_info(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)user;
	(void)err;
	/* name, vendor, version, and the version of the specification */
	return sd_bus_reply_method_return(m, "ssss", "AquaDE", "desk", DESK_VERSION, "1.2");
}

static const sd_bus_vtable NOTE_VTABLE[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("Notify", "susssasa{sv}i", "u", on_notify,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("CloseNotification", "u", "", on_close,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("GetCapabilities", "", "as", on_caps,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("GetServerInformation", "", "ssss", on_server_info,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_SIGNAL("NotificationClosed", "uu", 0),
	SD_BUS_SIGNAL("ActionInvoked", "us", 0),
	SD_BUS_VTABLE_END,
};

static int bus_ready(int fd, uint32_t mask, void *data)
{
	(void)fd;
	(void)mask;
	(void)data;
	if (!g_bus) {
		return 0;
	}
	for (;;) {
		int r = sd_bus_process(g_bus, NULL);
		if (r <= 0) {
			break;
		}
	}
	sd_bus_flush(g_bus);
	return 0;
}

bool desk_post_init(struct wl_event_loop *loop)
{
	if (g_bus) {
		return true;
	}
	int r = sd_bus_open_user(&g_bus);
	if (r < 0) {
		g_bus = NULL;
		return false;
	}
	r = sd_bus_add_object_vtable(g_bus, &g_slot, "/org/freedesktop/Notifications",
		"org.freedesktop.Notifications", NOTE_VTABLE, NULL);
	if (r < 0) {
		desk_post_finish();
		return false;
	}
	/* Allow replacement in turn: if somebody starts a real notification
	 * daemon later, it should be able to take the name back rather than
	 * silently getting nothing. */
	r = sd_bus_request_name(g_bus, "org.freedesktop.Notifications",
		SD_BUS_NAME_REPLACE_EXISTING | SD_BUS_NAME_ALLOW_REPLACEMENT);
	if (r < 0) {
		desk_post_finish();
		return false;
	}
	int fd = sd_bus_get_fd(g_bus);
	if (fd < 0) {
		desk_post_finish();
		return false;
	}
	g_src = wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE, bus_ready, NULL);
	if (!g_src) {
		desk_post_finish();
		return false;
	}
	return true;
}

void desk_post_finish(void)
{
	if (g_src) {
		wl_event_source_remove(g_src);
		g_src = NULL;
	}
	if (g_slot) {
		sd_bus_slot_unref(g_slot);
		g_slot = NULL;
	}
	if (g_bus) {
		sd_bus_flush_close_unref(g_bus);
		g_bus = NULL;
	}
}

int desk_post_letters(const struct desk_note **out)
{
	if (out) {
		*out = g_note;
	}
	return g_n;
}

int desk_post_unread(void)
{
	int n = 0;
	for (int i = 0; i < g_n; i++) {
		if (!g_note[i].read) {
			n++;
		}
	}
	return n;
}

void desk_post_mark_read(unsigned id)
{
	int at = find_id(id);
	if (at >= 0 && !g_note[at].read) {
		g_note[at].read = true;
		changed();
	}
}

void desk_post_set_read(unsigned id, bool read)
{
	int at = find_id(id);
	if (at >= 0 && g_note[at].read != read) {
		g_note[at].read = read;
		changed();
	}
}

void desk_post_invoke(unsigned id, const char *key)
{
	int at = find_id(id);
	if (at < 0 || !key || !g_bus) {
		return;
	}
	sd_bus_emit_signal(g_bus, "/org/freedesktop/Notifications",
		"org.freedesktop.Notifications", "ActionInvoked", "us", id, key);
	if (g_note[at].resident) {
		g_note[at].read = true;
		changed();
		return;
	}
	drop_at(at);
	emit_closed(id, 2);
	changed();
}

void desk_post_dismiss(unsigned id)
{
	int at = find_id(id);
	if (at < 0) {
		return;
	}
	drop_at(at);
	emit_closed(id, 2);   /* dismissed by the user */
	changed();
}

void desk_post_on_change(void (*cb)(void *user), void *user)
{
	g_changed = cb;
	g_changed_user = user;
}
