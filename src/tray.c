/* ================================================================
 * The system tray, the StatusNotifierItem way.
 *
 * A program with something to show in the tray puts an object on the
 * session bus (org.kde.StatusNotifierItem: an icon, a title, a status)
 * and tells the watcher (org.kde.StatusNotifierWatcher) where it is. A
 * host asks the watcher what there is and shows it. AquaDE is the watcher
 * when nobody else on the bus is -- nested in a desktop that has one of
 * its own, it asks that one -- and a host either way.
 *
 * Everything here runs on the compositor's thread off the bus's fd, except
 * finding an icon by name, which can mean turning an SVG into a PNG with
 * another process and goes to a thread of its own.
 * ================================================================ */

#include "tray.h"
#include "apps.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>

#include <systemd/sd-bus.h>
#include <wayland-server-core.h>

#define TRAY_MAX 32
#define WATCHER_NAME "org.kde.StatusNotifierWatcher"
#define WATCHER_PATH "/StatusNotifierWatcher"
#define ITEM_IFACE "org.kde.StatusNotifierItem"

struct item {
	bool used;
	unsigned id;
	unsigned order;
	char service[128];
	char path[160];
	/* what it says of itself */
	char item_id[96];
	char title[128];
	char status[24];
	char category[32];
	char icon_name[160];
	char att_icon_name[160];
	char theme_path[256];
	char menu[160];
	char tip[128];
	bool item_is_menu;
	cairo_surface_t *pixmap;      /* IconPixmap, the size nearest a tray's */
	cairo_surface_t *att_pixmap;
	sd_bus_slot *signals;
	sd_bus_slot *call;
	bool again;                   /* changed while a fetch was out */
	bool loaded;
};

static sd_bus *g_bus;
static struct wl_event_source *g_src, *g_icon_src;
static sd_bus_slot *g_vtable, *g_owner_match, *g_ext_match[2];
static bool g_watcher;            /* we are it */
static char g_host[96];
static struct item g_items[TRAY_MAX];
static unsigned g_next_id = 1, g_next_order = 1;
static struct desk_tray_item g_pub[TRAY_MAX];
static int g_npub;
static void (*g_changed)(void *);
static void *g_changed_user;
static unsigned g_menu_of;          /* the item whose menu is open; 0 none */

static void str_copy(char *dst, size_t n, const char *src)
{
	if (!n) {
		return;
	}
	if (!src) {
		src = "";
	}
	size_t l = strlen(src);
	if (l >= n) {
		/* cut between characters, never inside one */
		l = n - 1;
		while (l > 0 && ((unsigned char)src[l] & 0xc0) == 0x80) {
			l--;
		}
	}
	memcpy(dst, src, l);
	dst[l] = '\0';
}

static struct item *item_by_id(unsigned id)
{
	for (int i = 0; i < TRAY_MAX; i++) {
		if (g_items[i].used && g_items[i].id == id) {
			return &g_items[i];
		}
	}
	return NULL;
}

/* ---- icons by name, off the thread -------------------------------- */

struct icon_job {
	char name[160];
	char theme[256];
	cairo_surface_t *surf;
	int state;        /* 0 free, 1 being looked for, 2 done (surf may be NULL) */
	unsigned used;
};
static struct icon_job g_jobs[48];
static unsigned g_job_clock;
static pthread_mutex_t g_job_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_evfd = -1;

struct job_arg {
	char name[160];
	char theme[256];
};

static void *icon_find(void *p)
{
	struct job_arg *a = p;
	char path[768];
	cairo_surface_t *s = NULL;
	if (desk_icon_path(a->name, a->theme, path, sizeof path)) {
		s = cairo_image_surface_create_from_png(path);
		if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
			cairo_surface_destroy(s);
			s = NULL;
		}
	}
	pthread_mutex_lock(&g_job_lock);
	bool kept = false;
	for (size_t i = 0; i < sizeof g_jobs / sizeof *g_jobs; i++) {
		struct icon_job *j = &g_jobs[i];
		if (j->state == 1 && strcmp(j->name, a->name) == 0 && strcmp(j->theme, a->theme) == 0) {
			j->surf = s;
			j->state = 2;
			kept = true;
			break;
		}
	}
	pthread_mutex_unlock(&g_job_lock);
	if (!kept && s) {
		cairo_surface_destroy(s);
	}
	if (g_evfd >= 0) {
		uint64_t one = 1;
		if (write(g_evfd, &one, sizeof one) < 0) {
			/* the main thread will see it on its next look anyway */
		}
	}
	free(a);
	return NULL;
}

/* The icon by that name, with a reference for the caller; NULL while it is
 * being looked for, and when there is none. */
static cairo_surface_t *icon_named(const char *name, const char *theme)
{
	if (!name || !*name) {
		return NULL;
	}
	if (!theme) {
		theme = "";
	}
	cairo_surface_t *out = NULL;
	pthread_mutex_lock(&g_job_lock);
	struct icon_job *slot = NULL;
	for (size_t i = 0; i < sizeof g_jobs / sizeof *g_jobs; i++) {
		struct icon_job *j = &g_jobs[i];
		if (j->state && strcmp(j->name, name) == 0 && strcmp(j->theme, theme) == 0) {
			j->used = ++g_job_clock;
			if (j->state == 2 && j->surf) {
				out = cairo_surface_reference(j->surf);
			}
			pthread_mutex_unlock(&g_job_lock);
			return out;
		}
		/* a free place, or else the done one least lately wanted */
		if (!j->state) {
			if (!slot || slot->state) {
				slot = j;
			}
		} else if (j->state == 2 && (!slot || (slot->state && j->used < slot->used))) {
			slot = j;
		}
	}
	if (slot) {
		if (slot->state == 2 && slot->surf) {
			cairo_surface_destroy(slot->surf);
		}
		str_copy(slot->name, sizeof slot->name, name);
		str_copy(slot->theme, sizeof slot->theme, theme);
		slot->surf = NULL;
		slot->state = 1;
		slot->used = ++g_job_clock;
		struct job_arg *a = calloc(1, sizeof *a);
		pthread_t th;
		if (a) {
			str_copy(a->name, sizeof a->name, name);
			str_copy(a->theme, sizeof a->theme, theme);
		}
		if (!a || pthread_create(&th, NULL, icon_find, a) != 0) {
			free(a);
			slot->state = 2;   /* none, then */
		} else {
			pthread_detach(th);
		}
	}
	pthread_mutex_unlock(&g_job_lock);
	return NULL;
}

/* ---- what is shown ------------------------------------------------ */

static void publish(void)
{
	for (int i = 0; i < g_npub; i++) {
		if (g_pub[i].icon) {
			cairo_surface_destroy(g_pub[i].icon);
		}
	}
	g_npub = 0;
	/* in the order they came */
	const struct item *order[TRAY_MAX];
	int n = 0;
	for (int i = 0; i < TRAY_MAX; i++) {
		if (g_items[i].used && g_items[i].loaded) {
			order[n++] = &g_items[i];
		}
	}
	for (int a = 1; a < n; a++) {
		for (int b = a; b > 0 && order[b]->order < order[b - 1]->order; b--) {
			const struct item *t = order[b];
			order[b] = order[b - 1];
			order[b - 1] = t;
		}
	}
	for (int k = 0; k < n; k++) {
		const struct item *it = order[k];
		struct desk_tray_item *p = &g_pub[g_npub++];
		memset(p, 0, sizeof *p);
		p->id = it->id;
		str_copy(p->name_id, sizeof p->name_id, it->item_id[0] ? it->item_id : it->service);
		str_copy(p->title, sizeof p->title,
			it->title[0] ? it->title : it->tip[0] ? it->tip : p->name_id);
		p->attention = strcmp(it->status, "NeedsAttention") == 0;
		p->passive = strcmp(it->status, "Passive") == 0;
		p->has_menu = it->menu[0] && strcmp(it->menu, "/NO_DBUSMENU") != 0;
		p->is_menu = it->item_is_menu;
		cairo_surface_t *icon = NULL;
		if (p->attention) {
			icon = icon_named(it->att_icon_name, it->theme_path);
			if (!icon && it->att_pixmap) {
				icon = cairo_surface_reference(it->att_pixmap);
			}
		}
		if (!icon) {
			icon = icon_named(it->icon_name, it->theme_path);
		}
		if (!icon && it->pixmap) {
			icon = cairo_surface_reference(it->pixmap);
		}
		p->icon = icon;
	}
	if (g_changed) {
		g_changed(g_changed_user);
	}
}

/* ---- reading an item ---------------------------------------------- */

static int read_v_str(sd_bus_message *m, const char *type, char *out, size_t n)
{
	const char *s = NULL;
	int r = sd_bus_message_read(m, "v", type, &s);
	if (r >= 0) {
		str_copy(out, n, s);
	}
	return r;
}

/* The best of an a(iiay): the one nearest 48 pixels across, bigger first. */
static cairo_surface_t *read_pixmaps(sd_bus_message *m, const char *contents)
{
	cairo_surface_t *best = NULL;
	int best_w = 0;
	if (sd_bus_message_enter_container(m, 'v', contents) < 0) {
		return NULL;
	}
	if (sd_bus_message_enter_container(m, 'a', "(iiay)") < 0) {
		sd_bus_message_exit_container(m);
		return NULL;
	}
	while (sd_bus_message_enter_container(m, 'r', "iiay") > 0) {
		int32_t w = 0, h = 0;
		const void *data = NULL;
		size_t size = 0;
		if (sd_bus_message_read(m, "ii", &w, &h) < 0
			|| sd_bus_message_read_array(m, 'y', &data, &size) < 0) {
			sd_bus_message_exit_container(m);
			break;
		}
		sd_bus_message_exit_container(m);
		if (w <= 0 || h <= 0 || w > 1024 || h > 1024 || size < (size_t)w * h * 4) {
			continue;
		}
		/* prefer 48 or the next bigger, and else the biggest there is */
		bool better = !best
			|| (best_w < 48 && w > best_w)
			|| (w >= 48 && w < best_w);
		if (!better) {
			continue;
		}
		cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
		if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
			cairo_surface_destroy(s);
			continue;
		}
		cairo_surface_flush(s);
		unsigned char *dst = cairo_image_surface_get_data(s);
		int stride = cairo_image_surface_get_stride(s);
		const unsigned char *src = data;
		for (int y = 0; y < h; y++) {
			uint32_t *row = (uint32_t *)(dst + (size_t)y * stride);
			for (int x = 0; x < w; x++) {
				/* ARGB, most significant byte first, not premultiplied */
				const unsigned char *px = src + ((size_t)y * w + x) * 4;
				unsigned a = px[0];
				unsigned r = px[1] * a / 255, g = px[2] * a / 255, b = px[3] * a / 255;
				row[x] = (a << 24) | (r << 16) | (g << 8) | b;
			}
		}
		cairo_surface_mark_dirty(s);
		if (best) {
			cairo_surface_destroy(best);
		}
		best = s;
		best_w = w;
	}
	sd_bus_message_exit_container(m);
	sd_bus_message_exit_container(m);
	return best;
}

static void read_tooltip(sd_bus_message *m, const char *contents, char *out, size_t n)
{
	if (sd_bus_message_enter_container(m, 'v', contents) < 0) {
		return;
	}
	if (sd_bus_message_enter_container(m, 'r', "sa(iiay)ss") >= 0) {
		const char *icon = NULL, *title = NULL, *text = NULL;
		if (sd_bus_message_read(m, "s", &icon) >= 0
			&& sd_bus_message_skip(m, "a(iiay)") >= 0
			&& sd_bus_message_read(m, "ss", &title, &text) >= 0) {
			str_copy(out, n, title && *title ? title : text);
		}
		sd_bus_message_exit_container(m);
	}
	sd_bus_message_exit_container(m);
}

static void read_props(struct item *it, sd_bus_message *m)
{
	if (sd_bus_message_enter_container(m, 'a', "{sv}") < 0) {
		return;
	}
	while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
		const char *key = NULL;
		char type = 0;
		const char *contents = NULL;
		if (sd_bus_message_read(m, "s", &key) < 0
			|| sd_bus_message_peek_type(m, &type, &contents) < 0 || !contents) {
			sd_bus_message_exit_container(m);
			break;
		}
		int r = -1;
		bool s = strcmp(contents, "s") == 0, o = strcmp(contents, "o") == 0;
		if (strcmp(key, "Id") == 0 && s) {
			r = read_v_str(m, "s", it->item_id, sizeof it->item_id);
		} else if (strcmp(key, "Title") == 0 && s) {
			r = read_v_str(m, "s", it->title, sizeof it->title);
		} else if (strcmp(key, "Status") == 0 && s) {
			r = read_v_str(m, "s", it->status, sizeof it->status);
		} else if (strcmp(key, "Category") == 0 && s) {
			r = read_v_str(m, "s", it->category, sizeof it->category);
		} else if (strcmp(key, "IconName") == 0 && s) {
			r = read_v_str(m, "s", it->icon_name, sizeof it->icon_name);
		} else if (strcmp(key, "AttentionIconName") == 0 && s) {
			r = read_v_str(m, "s", it->att_icon_name, sizeof it->att_icon_name);
		} else if (strcmp(key, "IconThemePath") == 0 && s) {
			r = read_v_str(m, "s", it->theme_path, sizeof it->theme_path);
		} else if (strcmp(key, "Menu") == 0 && (o || s)) {
			r = read_v_str(m, contents, it->menu, sizeof it->menu);
		} else if (strcmp(key, "ItemIsMenu") == 0 && strcmp(contents, "b") == 0) {
			int b = 0;
			r = sd_bus_message_read(m, "v", "b", &b);
			it->item_is_menu = b != 0;
		} else if (strcmp(key, "IconPixmap") == 0 && strcmp(contents, "a(iiay)") == 0) {
			cairo_surface_t *px = read_pixmaps(m, contents);
			if (it->pixmap) {
				cairo_surface_destroy(it->pixmap);
			}
			it->pixmap = px;
			r = 0;
		} else if (strcmp(key, "AttentionIconPixmap") == 0 && strcmp(contents, "a(iiay)") == 0) {
			cairo_surface_t *px = read_pixmaps(m, contents);
			if (it->att_pixmap) {
				cairo_surface_destroy(it->att_pixmap);
			}
			it->att_pixmap = px;
			r = 0;
		} else if (strcmp(key, "ToolTip") == 0 && strcmp(contents, "(sa(iiay)ss)") == 0) {
			read_tooltip(m, contents, it->tip, sizeof it->tip);
			r = 0;
		}
		if (r < 0) {
			sd_bus_message_skip(m, "v");
		}
		sd_bus_message_exit_container(m);
	}
	sd_bus_message_exit_container(m);
}

static void fetch(struct item *it);
static void menu_forget(void);
static void item_drop(struct item *it);

static int on_props(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)err;
	struct item *it = item_by_id((unsigned)(uintptr_t)user);
	if (!it) {
		return 0;
	}
	sd_bus_slot_unref(it->call);
	it->call = NULL;
	if (sd_bus_message_is_method_error(m, NULL)) {
		const sd_bus_error *e = sd_bus_message_get_error(m);
		/* gone, or never there: it is not an item */
		if (e && e->name && (strstr(e->name, "ServiceUnknown") || strstr(e->name, "UnknownObject")
				|| strstr(e->name, "NameHasNoOwner"))) {
			item_drop(it);
		}
		publish();
		return 0;
	}
	read_props(it, m);
	it->loaded = true;
	if (it->again) {
		it->again = false;
		fetch(it);
	}
	publish();
	return 0;
}

static void fetch(struct item *it)
{
	if (it->call) {
		it->again = true;
		return;
	}
	int r = sd_bus_call_method_async(g_bus, &it->call, it->service, it->path,
		"org.freedesktop.DBus.Properties", "GetAll", on_props,
		(void *)(uintptr_t)it->id, "s", ITEM_IFACE);
	if (r < 0) {
		it->call = NULL;
	}
}

static int on_item_signal(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)m;
	(void)err;
	struct item *it = item_by_id((unsigned)(uintptr_t)user);
	if (it) {
		fetch(it);
	}
	return 0;
}

static void item_key(const struct item *it, char *out, size_t n)
{
	snprintf(out, n, "%s%s", it->service, it->path);
}

static struct item *item_add(const char *service, const char *path)
{
	for (int i = 0; i < TRAY_MAX; i++) {
		if (g_items[i].used && strcmp(g_items[i].service, service) == 0
			&& strcmp(g_items[i].path, path) == 0) {
			return NULL;   /* already here */
		}
	}
	struct item *it = NULL;
	for (int i = 0; i < TRAY_MAX; i++) {
		if (!g_items[i].used) {
			it = &g_items[i];
			break;
		}
	}
	if (!it) {
		return NULL;
	}
	memset(it, 0, sizeof *it);
	it->used = true;
	it->id = g_next_id++;
	it->order = g_next_order++;
	str_copy(it->service, sizeof it->service, service);
	str_copy(it->path, sizeof it->path, path);
	char rule[512];
	snprintf(rule, sizeof rule,
		"type='signal',sender='%s',path='%s',interface='" ITEM_IFACE "'", service, path);
	sd_bus_add_match_async(g_bus, &it->signals, rule, on_item_signal, NULL,
		(void *)(uintptr_t)it->id);
	fetch(it);
	return it;
}

static void item_drop(struct item *it)
{
	char key[300];
	item_key(it, key, sizeof key);
	if (g_menu_of == it->id) {
		menu_forget();
	}
	if (it->signals) {
		sd_bus_slot_unref(it->signals);
	}
	if (it->call) {
		sd_bus_slot_unref(it->call);
	}
	if (it->pixmap) {
		cairo_surface_destroy(it->pixmap);
	}
	if (it->att_pixmap) {
		cairo_surface_destroy(it->att_pixmap);
	}
	memset(it, 0, sizeof *it);
	if (g_watcher) {
		sd_bus_emit_signal(g_bus, WATCHER_PATH, WATCHER_NAME,
			"StatusNotifierItemUnregistered", "s", key);
		sd_bus_emit_properties_changed(g_bus, WATCHER_PATH, WATCHER_NAME,
			"RegisteredStatusNotifierItems", NULL);
	}
}

/* An item named as the watchers pass them about: a bus name and a path run
 * together, a bus name alone, or -- registering -- only a path, when the
 * bus name is whoever sent it. */
static void split_item(const char *s, const char *sender, char *service, size_t sn,
	char *path, size_t pn)
{
	if (s[0] == '/') {
		str_copy(service, sn, sender);
		str_copy(path, pn, s);
		return;
	}
	const char *slash = strchr(s, '/');
	if (slash) {
		size_t l = (size_t)(slash - s);
		if (l >= sn) {
			l = sn - 1;
		}
		memcpy(service, s, l);
		service[l] = '\0';
		str_copy(path, pn, slash);
		return;
	}
	str_copy(service, sn, s);
	str_copy(path, pn, "/StatusNotifierItem");
}

/* ---- being the watcher -------------------------------------------- */

static int on_register_item(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)user;
	(void)err;
	const char *arg = NULL;
	int r = sd_bus_message_read(m, "s", &arg);
	if (r < 0) {
		return r;
	}
	char service[128], path[160];
	split_item(arg, sd_bus_message_get_sender(m), service, sizeof service, path, sizeof path);
	struct item *it = item_add(service, path);
	if (it) {
		char key[300];
		item_key(it, key, sizeof key);
		sd_bus_emit_signal(g_bus, WATCHER_PATH, WATCHER_NAME,
			"StatusNotifierItemRegistered", "s", key);
		sd_bus_emit_properties_changed(g_bus, WATCHER_PATH, WATCHER_NAME,
			"RegisteredStatusNotifierItems", NULL);
	}
	return sd_bus_reply_method_return(m, "");
}

static int on_register_host(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)user;
	(void)err;
	/* there is always one: this */
	return sd_bus_reply_method_return(m, "");
}

static int prop_items(sd_bus *bus, const char *path, const char *iface, const char *prop,
	sd_bus_message *reply, void *user, sd_bus_error *err)
{
	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)user;
	(void)err;
	int r = sd_bus_message_open_container(reply, 'a', "s");
	if (r < 0) {
		return r;
	}
	for (int i = 0; i < TRAY_MAX; i++) {
		if (g_items[i].used) {
			char key[300];
			item_key(&g_items[i], key, sizeof key);
			sd_bus_message_append(reply, "s", key);
		}
	}
	return sd_bus_message_close_container(reply);
}

static int prop_true(sd_bus *bus, const char *path, const char *iface, const char *prop,
	sd_bus_message *reply, void *user, sd_bus_error *err)
{
	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)user;
	(void)err;
	return sd_bus_message_append(reply, "b", 1);
}

static int prop_version(sd_bus *bus, const char *path, const char *iface, const char *prop,
	sd_bus_message *reply, void *user, sd_bus_error *err)
{
	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)user;
	(void)err;
	return sd_bus_message_append(reply, "i", 0);
}

static const sd_bus_vtable WATCHER_VTABLE[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("RegisterStatusNotifierItem", "s", "", on_register_item,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("RegisterStatusNotifierHost", "s", "", on_register_host,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_PROPERTY("RegisteredStatusNotifierItems", "as", prop_items, 0,
		SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("IsStatusNotifierHostRegistered", "b", prop_true, 0,
		SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("ProtocolVersion", "i", prop_version, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_SIGNAL("StatusNotifierItemRegistered", "s", 0),
	SD_BUS_SIGNAL("StatusNotifierItemUnregistered", "s", 0),
	SD_BUS_SIGNAL("StatusNotifierHostRegistered", "", 0),
	SD_BUS_SIGNAL("StatusNotifierHostUnregistered", "", 0),
	SD_BUS_VTABLE_END
};

/* ---- asking somebody else's watcher ------------------------------- */

static int on_ext_registered(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)user;
	(void)err;
	const char *arg = NULL;
	if (g_watcher || sd_bus_message_read(m, "s", &arg) < 0 || !arg) {
		return 0;
	}
	char service[128], path[160];
	split_item(arg, "", service, sizeof service, path, sizeof path);
	item_add(service, path);
	return 0;
}

static int on_ext_unregistered(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)user;
	(void)err;
	const char *arg = NULL;
	if (g_watcher || sd_bus_message_read(m, "s", &arg) < 0 || !arg) {
		return 0;
	}
	char service[128], path[160];
	split_item(arg, "", service, sizeof service, path, sizeof path);
	for (int i = 0; i < TRAY_MAX; i++) {
		if (g_items[i].used && strcmp(g_items[i].service, service) == 0
			&& strcmp(g_items[i].path, path) == 0) {
			item_drop(&g_items[i]);
		}
	}
	publish();
	return 0;
}

static int on_ext_items(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)user;
	(void)err;
	if (sd_bus_message_is_method_error(m, NULL)) {
		return 0;
	}
	if (sd_bus_message_enter_container(m, 'v', "as") < 0) {
		return 0;
	}
	if (sd_bus_message_enter_container(m, 'a', "s") >= 0) {
		const char *s = NULL;
		while (sd_bus_message_read(m, "s", &s) > 0) {
			char service[128], path[160];
			split_item(s, "", service, sizeof service, path, sizeof path);
			item_add(service, path);
		}
		sd_bus_message_exit_container(m);
	}
	sd_bus_message_exit_container(m);
	return 0;
}

static int on_ignore(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)m;
	(void)user;
	(void)err;
	return 0;
}

static void follow_external(void)
{
	sd_bus_match_signal(g_bus, &g_ext_match[0], WATCHER_NAME, WATCHER_PATH, WATCHER_NAME,
		"StatusNotifierItemRegistered", on_ext_registered, NULL);
	sd_bus_match_signal(g_bus, &g_ext_match[1], WATCHER_NAME, WATCHER_PATH, WATCHER_NAME,
		"StatusNotifierItemUnregistered", on_ext_unregistered, NULL);
	sd_bus_call_method_async(g_bus, NULL, WATCHER_NAME, WATCHER_PATH, WATCHER_NAME,
		"RegisterStatusNotifierHost", on_ignore, NULL, "s", g_host);
	sd_bus_call_method_async(g_bus, NULL, WATCHER_NAME, WATCHER_PATH,
		"org.freedesktop.DBus.Properties", "Get", on_ext_items, NULL, "ss",
		WATCHER_NAME, "RegisteredStatusNotifierItems");
}

static bool become_watcher(void)
{
	if (sd_bus_request_name(g_bus, WATCHER_NAME, 0) < 0) {
		return false;
	}
	g_watcher = true;
	for (int i = 0; i < 2; i++) {
		if (g_ext_match[i]) {
			sd_bus_slot_unref(g_ext_match[i]);
			g_ext_match[i] = NULL;
		}
	}
	sd_bus_emit_signal(g_bus, WATCHER_PATH, WATCHER_NAME, "StatusNotifierHostRegistered", "");
	return true;
}

/* Somebody left: an item's program, or the watcher we were asking. */
static int on_owner_changed(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)user;
	(void)err;
	const char *name = NULL, *old_owner = NULL, *new_owner = NULL;
	if (sd_bus_message_read(m, "sss", &name, &old_owner, &new_owner) < 0 || !name) {
		return 0;
	}
	if (new_owner && *new_owner) {
		return 0;
	}
	bool dropped = false;
	for (int i = 0; i < TRAY_MAX; i++) {
		if (g_items[i].used && strcmp(g_items[i].service, name) == 0) {
			item_drop(&g_items[i]);
			dropped = true;
		}
	}
	if (!g_watcher && strcmp(name, WATCHER_NAME) == 0) {
		/* nobody is keeping the list now; keep it ourselves, and the
		 * items will come and register again */
		become_watcher();
	}
	if (dropped) {
		publish();
	}
	return 0;
}

/* ---- an item's menu, the dbusmenu way ---------------------------
 *
 * An item with a menu puts it on the bus beside itself, at the path its
 * Menu property names, as com.canonical.dbusmenu: a tree of entries, each
 * an id and a dictionary of what it shows. A host asks for the tree
 * (GetLayout), says it is about to show a part of it (AboutToShow, after
 * which the program may have changed it), and says which entry was chosen
 * (Event "clicked"). Only one is ever open here: the card that has been
 * pulled out from under the machine. */

#define MENU_MAX 256
#define MENU_IFACE "com.canonical.dbusmenu"

struct menu_node {
	int32_t id, parent;
	char label[128];
	char icon_name[160];
	cairo_surface_t *icon;       /* its icon-data, a PNG */
	bool enabled, visible, separator, submenu;
	int toggle, state;
};

static struct menu_node g_menu[MENU_MAX];
static int g_menu_n;
static char g_menu_service[128], g_menu_path[160], g_menu_theme[256];
static int g_menu_state;
static sd_bus_slot *g_menu_call, *g_menu_signals;
static bool g_menu_again;
static struct desk_tray_menu_entry g_menu_pub[MENU_MAX];
static int g_menu_npub;

static uint32_t event_time(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void menu_nodes_free(void)
{
	for (int i = 0; i < g_menu_n; i++) {
		if (g_menu[i].icon) {
			cairo_surface_destroy(g_menu[i].icon);
		}
	}
	g_menu_n = 0;
}

static void menu_pub_free(void)
{
	for (int i = 0; i < g_menu_npub; i++) {
		if (g_menu_pub[i].icon) {
			cairo_surface_destroy(g_menu_pub[i].icon);
		}
	}
	g_menu_npub = 0;
}

/* "_Open", "__Quit" and "Save _As…" as they are shown: the underscore marks
 * the key that chooses it, two of them are one. */
static void menu_label(char *out, size_t n, const char *s)
{
	size_t k = 0;
	for (const char *p = s; *p && k + 1 < n; p++) {
		if (*p == '_') {
			if (p[1] != '_') {
				continue;
			}
			p++;
		}
		out[k++] = *p;
	}
	out[k] = '\0';
}

struct png_bytes {
	const unsigned char *p;
	size_t n, at;
};

static cairo_status_t png_read(void *closure, unsigned char *data, unsigned int len)
{
	struct png_bytes *b = closure;
	if (b->at + len > b->n) {
		return CAIRO_STATUS_READ_ERROR;
	}
	memcpy(data, b->p + b->at, len);
	b->at += len;
	return CAIRO_STATUS_SUCCESS;
}

static void menu_read_props(sd_bus_message *m, struct menu_node *node)
{
	if (sd_bus_message_enter_container(m, 'a', "{sv}") < 0) {
		return;
	}
	while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
		const char *key = NULL;
		char type = 0;
		const char *contents = NULL;
		if (sd_bus_message_read(m, "s", &key) < 0
			|| sd_bus_message_peek_type(m, &type, &contents) < 0 || !contents) {
			sd_bus_message_exit_container(m);
			break;
		}
		int r = -1;
		const char *str = NULL;
		int b = 0;
		int32_t i = 0;
		if (strcmp(contents, "s") == 0) {
			r = sd_bus_message_read(m, "v", "s", &str);
			if (r >= 0 && str) {
				if (strcmp(key, "type") == 0) {
					node->separator = strcmp(str, "separator") == 0;
				} else if (strcmp(key, "label") == 0) {
					menu_label(node->label, sizeof node->label, str);
				} else if (strcmp(key, "icon-name") == 0) {
					str_copy(node->icon_name, sizeof node->icon_name, str);
				} else if (strcmp(key, "toggle-type") == 0) {
					node->toggle = strcmp(str, "checkmark") == 0 ? 1
						: strcmp(str, "radio") == 0 ? 2 : 0;
				} else if (strcmp(key, "children-display") == 0) {
					node->submenu = strcmp(str, "submenu") == 0;
				}
			}
		} else if (strcmp(contents, "b") == 0) {
			r = sd_bus_message_read(m, "v", "b", &b);
			if (r >= 0 && strcmp(key, "enabled") == 0) {
				node->enabled = b != 0;
			} else if (r >= 0 && strcmp(key, "visible") == 0) {
				node->visible = b != 0;
			}
		} else if (strcmp(contents, "i") == 0) {
			r = sd_bus_message_read(m, "v", "i", &i);
			if (r >= 0 && strcmp(key, "toggle-state") == 0) {
				node->state = i;
			}
		} else if (strcmp(contents, "ay") == 0 && strcmp(key, "icon-data") == 0) {
			if (sd_bus_message_enter_container(m, 'v', "ay") >= 0) {
				const void *data = NULL;
				size_t size = 0;
				if (sd_bus_message_read_array(m, 'y', &data, &size) >= 0 && size > 8) {
					struct png_bytes pb = { data, size, 0 };
					cairo_surface_t *png = cairo_image_surface_create_from_png_stream(
						png_read, &pb);
					if (cairo_surface_status(png) == CAIRO_STATUS_SUCCESS) {
						if (node->icon) {
							cairo_surface_destroy(node->icon);
						}
						node->icon = png;
					} else {
						cairo_surface_destroy(png);
					}
				}
				sd_bus_message_exit_container(m);
				r = 0;
			}
		}
		if (r < 0) {
			sd_bus_message_skip(m, "v");
		}
		sd_bus_message_exit_container(m);
	}
	sd_bus_message_exit_container(m);
}

/* One entry and everything under it, (ia{sv}av). The top of the tree is
 * the menu itself and is not kept; its children are the ones under 0. */
static void menu_read(sd_bus_message *m, int32_t parent, int depth)
{
	if (sd_bus_message_enter_container(m, 'r', "ia{sv}av") <= 0) {
		return;
	}
	struct menu_node node = { 0 };
	node.parent = parent;
	node.enabled = node.visible = true;
	node.state = -1;
	sd_bus_message_read(m, "i", &node.id);
	menu_read_props(m, &node);
	if (depth > 0 && g_menu_n < MENU_MAX) {
		g_menu[g_menu_n++] = node;
	} else if (node.icon) {
		cairo_surface_destroy(node.icon);
	}
	if (sd_bus_message_enter_container(m, 'a', "v") >= 0) {
		for (;;) {
			char type = 0;
			const char *contents = NULL;
			if (sd_bus_message_peek_type(m, &type, &contents) <= 0 || type != 'v') {
				break;
			}
			/* each child is a variant of one; some put it in two */
			int wraps = 0;
			while (type == 'v' && contents && wraps < 3
				&& sd_bus_message_enter_container(m, 'v', contents) > 0) {
				wraps++;
				if (sd_bus_message_peek_type(m, &type, &contents) <= 0) {
					break;
				}
			}
			if (depth < 12 && type == 'r' && contents && strcmp(contents, "ia{sv}av") == 0) {
				menu_read(m, node.id, depth + 1);
			} else if (wraps > 0) {
				sd_bus_message_skip(m, NULL);   /* the rest of whatever it is */
			} else if (sd_bus_message_skip(m, "v") < 0) {
				break;
			}
			for (; wraps > 0; wraps--) {
				sd_bus_message_exit_container(m);
			}
		}
		sd_bus_message_exit_container(m);
	}
	sd_bus_message_exit_container(m);
}

static void menu_fetch(void);

static int on_layout(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)user;
	(void)err;
	sd_bus_slot_unref(g_menu_call);
	g_menu_call = NULL;
	if (!g_menu_of) {
		return 0;
	}
	if (sd_bus_message_is_method_error(m, NULL)) {
		/* a menu it says it has and cannot show: none, then */
		menu_nodes_free();
		g_menu_state = DESK_TRAY_MENU_NONE;
	} else {
		uint32_t revision = 0;
		menu_nodes_free();
		if (sd_bus_message_read(m, "u", &revision) >= 0) {
			menu_read(m, 0, 0);
		}
		g_menu_state = DESK_TRAY_MENU_READY;
	}
	if (g_menu_again) {
		g_menu_again = false;
		menu_fetch();
	}
	if (g_changed) {
		g_changed(g_changed_user);
	}
	return 0;
}

static void menu_fetch(void)
{
	if (!g_menu_of || !g_bus) {
		return;
	}
	if (g_menu_call) {
		g_menu_again = true;
		return;
	}
	if (sd_bus_call_method_async(g_bus, &g_menu_call, g_menu_service, g_menu_path, MENU_IFACE,
			"GetLayout", on_layout, NULL, "iias", (int32_t)0, (int32_t)-1, 0) < 0) {
		g_menu_call = NULL;
		g_menu_state = DESK_TRAY_MENU_NONE;
	}
	sd_bus_flush(g_bus);
}

/* Shown what was asked for; whether it changed anything or not, the
 * layout is asked for after it, so the order the program sees is right. */
static int on_about_to_show(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)m;
	(void)user;
	(void)err;
	menu_fetch();
	return 0;
}

static int on_menu_signal(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)m;
	(void)user;
	(void)err;
	menu_fetch();
	return 0;
}

static void menu_event(int32_t id, const char *what)
{
	if (!g_menu_of || !g_bus) {
		return;
	}
	sd_bus_call_method_async(g_bus, NULL, g_menu_service, g_menu_path, MENU_IFACE, "Event",
		on_ignore, NULL, "isvu", id, what, "i", (int32_t)0, event_time());
	sd_bus_flush(g_bus);
}

/* Let go of it without a word to the program: it has gone. */
static void menu_forget(void)
{
	if (g_menu_call) {
		sd_bus_slot_unref(g_menu_call);
		g_menu_call = NULL;
	}
	if (g_menu_signals) {
		sd_bus_slot_unref(g_menu_signals);
		g_menu_signals = NULL;
	}
	menu_nodes_free();
	menu_pub_free();
	g_menu_of = 0;
	g_menu_again = false;
	g_menu_state = DESK_TRAY_MENU_NONE;
}

void desk_tray_menu_open(unsigned id)
{
	desk_tray_menu_close();
	struct item *it = item_by_id(id);
	if (!it || !g_bus) {
		return;
	}
	g_menu_of = id;
	if (!it->menu[0] || strcmp(it->menu, "/NO_DBUSMENU") == 0) {
		g_menu_state = DESK_TRAY_MENU_NONE;
		return;
	}
	str_copy(g_menu_service, sizeof g_menu_service, it->service);
	str_copy(g_menu_path, sizeof g_menu_path, it->menu);
	str_copy(g_menu_theme, sizeof g_menu_theme, it->theme_path);
	g_menu_state = DESK_TRAY_MENU_LOADING;
	char rule[512];
	snprintf(rule, sizeof rule, "type='signal',sender='%s',path='%s',interface='" MENU_IFACE "'",
		g_menu_service, g_menu_path);
	sd_bus_add_match_async(g_bus, &g_menu_signals, rule, on_menu_signal, NULL, NULL);
	sd_bus_call_method_async(g_bus, NULL, g_menu_service, g_menu_path, MENU_IFACE,
		"AboutToShow", on_about_to_show, NULL, "i", (int32_t)0);
	menu_event(0, "opened");
}

void desk_tray_menu_close(void)
{
	if (g_menu_of && g_menu_state != DESK_TRAY_MENU_NONE) {
		menu_event(0, "closed");
	}
	menu_forget();
}

void desk_tray_menu_show(int32_t entry)
{
	if (!g_menu_of || g_menu_state == DESK_TRAY_MENU_NONE || !g_bus) {
		return;
	}
	sd_bus_call_method_async(g_bus, NULL, g_menu_service, g_menu_path, MENU_IFACE,
		"AboutToShow", on_about_to_show, NULL, "i", entry);
	menu_event(entry, "opened");
}

void desk_tray_menu_click(int32_t entry)
{
	menu_event(entry, "clicked");
}

int desk_tray_menu(int32_t parent, const struct desk_tray_menu_entry **out, int *state)
{
	menu_pub_free();
	for (int i = 0; i < g_menu_n && g_menu_npub < MENU_MAX; i++) {
		const struct menu_node *node = &g_menu[i];
		if (node->parent != parent || !node->visible) {
			continue;
		}
		struct desk_tray_menu_entry *e = &g_menu_pub[g_menu_npub++];
		memset(e, 0, sizeof *e);
		e->id = node->id;
		str_copy(e->label, sizeof e->label, node->label);
		e->enabled = node->enabled;
		e->separator = node->separator;
		e->toggle = node->toggle;
		e->state = node->state;
		/* a submenu is one that says so, or one that simply has entries */
		e->submenu = node->submenu;
		for (int k = i + 1; k < g_menu_n && !e->submenu; k++) {
			e->submenu = g_menu[k].parent == node->id;
		}
		e->icon = node->icon ? cairo_surface_reference(node->icon)
			: icon_named(node->icon_name, g_menu_theme);
	}
	if (out) {
		*out = g_menu_pub;
	}
	if (state) {
		*state = g_menu_of ? g_menu_state : DESK_TRAY_MENU_NONE;
	}
	return g_menu_npub;
}

/* ---- the bus in the compositor's loop ------------------------------ */

static void pump(void)
{
	if (!g_bus) {
		return;
	}
	for (int k = 0; k < 256; k++) {
		if (sd_bus_process(g_bus, NULL) <= 0) {
			break;
		}
	}
	sd_bus_flush(g_bus);
}

static int bus_ready(int fd, uint32_t mask, void *data)
{
	(void)fd;
	(void)mask;
	(void)data;
	pump();
	return 0;
}

static int icons_ready(int fd, uint32_t mask, void *data)
{
	(void)mask;
	(void)data;
	uint64_t n;
	if (read(fd, &n, sizeof n) < 0) {
		/* nothing waiting after all */
	}
	publish();
	return 0;
}

bool desk_tray_init(struct wl_event_loop *loop, void (*changed)(void *), void *user)
{
	if (g_bus) {
		return true;
	}
	g_changed = changed;
	g_changed_user = user;
	if (sd_bus_open_user(&g_bus) < 0) {
		g_bus = NULL;
		return false;
	}
	snprintf(g_host, sizeof g_host, "org.kde.StatusNotifierHost-%d", (int)getpid());
	sd_bus_request_name(g_bus, g_host, 0);
	sd_bus_match_signal(g_bus, &g_owner_match, "org.freedesktop.DBus", "/org/freedesktop/DBus",
		"org.freedesktop.DBus", "NameOwnerChanged", on_owner_changed, NULL);
	/* The watcher's object is always there; the name says whether anybody
	 * is being sent to it. A desktop around us that has a watcher of its
	 * own keeps it -- its panel is showing those items -- and we ask it. */
	sd_bus_add_object_vtable(g_bus, &g_vtable, WATCHER_PATH, WATCHER_NAME, WATCHER_VTABLE, NULL);
	if (!become_watcher()) {
		follow_external();
	}
	int fd = sd_bus_get_fd(g_bus);
	if (fd >= 0) {
		g_src = wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE, bus_ready, NULL);
	}
	g_evfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (g_evfd >= 0) {
		g_icon_src = wl_event_loop_add_fd(loop, g_evfd, WL_EVENT_READABLE, icons_ready, NULL);
	}
	pump();
	return true;
}

void desk_tray_finish(void)
{
	menu_forget();
	for (int i = 0; i < TRAY_MAX; i++) {
		if (g_items[i].used) {
			item_drop(&g_items[i]);
		}
	}
	for (int i = 0; i < g_npub; i++) {
		if (g_pub[i].icon) {
			cairo_surface_destroy(g_pub[i].icon);
		}
	}
	g_npub = 0;
	if (g_src) {
		wl_event_source_remove(g_src);
		g_src = NULL;
	}
	if (g_icon_src) {
		wl_event_source_remove(g_icon_src);
		g_icon_src = NULL;
	}
	if (g_evfd >= 0) {
		close(g_evfd);
		g_evfd = -1;
	}
	sd_bus_slot_unref(g_vtable);
	sd_bus_slot_unref(g_owner_match);
	for (int i = 0; i < 2; i++) {
		sd_bus_slot_unref(g_ext_match[i]);
		g_ext_match[i] = NULL;
	}
	g_vtable = g_owner_match = NULL;
	if (g_bus) {
		sd_bus_flush_close_unref(g_bus);
		g_bus = NULL;
	}
	g_watcher = false;
}

int desk_tray_items(const struct desk_tray_item **out)
{
	if (out) {
		*out = g_pub;
	}
	return g_npub;
}

void desk_tray_click(unsigned id, int how, int x, int y)
{
	struct item *it = item_by_id(id);
	if (!it || !g_bus) {
		return;
	}
	/* an item that is nothing but a menu wants its menu, whichever click */
	const char *what = how == 1 ? "SecondaryActivate"
		: (how == 2 || it->item_is_menu) ? "ContextMenu" : "Activate";
	sd_bus_call_method_async(g_bus, NULL, it->service, it->path, ITEM_IFACE, what,
		on_ignore, NULL, "ii", (int32_t)x, (int32_t)y);
	sd_bus_flush(g_bus);
}

void desk_tray_scroll(unsigned id, int delta, bool horizontal)
{
	struct item *it = item_by_id(id);
	if (!it || !g_bus) {
		return;
	}
	sd_bus_call_method_async(g_bus, NULL, it->service, it->path, ITEM_IFACE, "Scroll",
		on_ignore, NULL, "is", (int32_t)delta, horizontal ? "horizontal" : "vertical");
	sd_bus_flush(g_bus);
}

void desk_tray_tick(void)
{
	pump();
}
