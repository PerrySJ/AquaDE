/* ================================================================
 * Which input method is on.
 *
 * IBus keeps a bus of its own, whose address is in a file under
 * ~/.config/ibus/bus; the engine in use is its GlobalEngine, and each
 * engine names the symbol it is shown by (Mozc: "あ" with kana on, "A"
 * with it off). fcitx5 is on the session bus and is asked.
 * ================================================================ */

#include "ime.h"

#include <dirent.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <systemd/sd-bus.h>
#include <wayland-server-core.h>

static struct wl_event_loop *g_loop;
static void (*g_changed)(void *);
static void *g_user;
static char g_label[16];

static sd_bus *g_ibus;
static struct wl_event_source *g_ibus_src;
static sd_bus_slot *g_ibus_match;
static time_t g_ibus_tried;

static sd_bus *g_session;
static struct wl_event_source *g_session_src;
static bool g_fcitx_asking;

/* typing into AquaDE itself: the input context it has of IBus */
static struct desk_ime_typing g_typ;
static bool g_typing;
static unsigned g_typing_gen;
static char g_ic_path[160];
static sd_bus_slot *g_ic_match;
static char g_pre[512];
static int g_pre_cursor;
static bool g_pre_visible;

static void ic_forget(void);

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
		l = n - 1;
	}
	memcpy(dst, src, l);
	dst[l] = '\0';
}

static void set_label(const char *s)
{
	if (strcmp(s, g_label) == 0) {
		return;
	}
	str_copy(g_label, sizeof g_label, s);
	if (g_changed) {
		g_changed(g_user);
	}
}

/* An engine known only by its name: "xkb:jp::jpn" is a Japanese keyboard,
 * "mozc-on" Mozc with its kana on. */
static void label_from_name(const char *name, char *out, size_t n)
{
	if (strncmp(name, "xkb:", 4) == 0) {
		char lay[8] = { 0 };
		int k = 0;
		for (const char *p = name + 4; *p && *p != ':' && k < 3; p++) {
			lay[k++] = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
		}
		str_copy(out, n, lay);
		return;
	}
	if (strstr(name, "-off") || strncmp(name, "keyboard-", 9) == 0) {
		str_copy(out, n, "A");
	} else if (strstr(name, "mozc") || strstr(name, "anthy") || strstr(name, "kkc")
		|| strstr(name, "skk")) {
		str_copy(out, n, "\xe3\x81\x82");   /* あ */
	} else if (strstr(name, "pinyin") || strstr(name, "rime") || strstr(name, "chewing")) {
		str_copy(out, n, "\xe4\xb8\xad");   /* 中 */
	} else if (strstr(name, "hangul")) {
		str_copy(out, n, "\xed\x95\x9c");   /* 한 */
	} else {
		char up[4] = { 0 };
		for (int k = 0; k < 2 && name[k]; k++) {
			up[k] = (char)(name[k] >= 'a' && name[k] <= 'z' ? name[k] - 32 : name[k]);
		}
		str_copy(out, n, up);
	}
}

/* ---- IBus ------------------------------------------------------- */

static void ibus_drop(void)
{
	ic_forget();
	if (g_ibus_src) {
		wl_event_source_remove(g_ibus_src);
		g_ibus_src = NULL;
	}
	if (g_ibus_match) {
		sd_bus_slot_unref(g_ibus_match);
		g_ibus_match = NULL;
	}
	if (g_ibus) {
		sd_bus_flush_close_unref(g_ibus);
		g_ibus = NULL;
	}
}

/* The engine is an IBusEngineDesc, (sa{sv}ssssssssussssssss): its name
 * third, its symbol thirteenth. */
static int on_engine(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)user;
	(void)err;
	if (sd_bus_message_is_method_error(m, NULL)) {
		return 0;
	}
	/* a property is a variant, and IBus wraps the engine in another */
	char type = 0;
	const char *contents = NULL;
	for (int depth = 0; depth < 4; depth++) {
		if (sd_bus_message_peek_type(m, &type, &contents) < 0 || !contents) {
			return 0;
		}
		if (type != 'v') {
			break;
		}
		if (sd_bus_message_enter_container(m, 'v', contents) < 0) {
			return 0;
		}
	}
	if (type != 'r' || sd_bus_message_enter_container(m, 'r', contents) < 0) {
		return 0;
	}
	const char *s = NULL, *name = NULL, *symbol = NULL;
	char label[16] = { 0 };
	/* the type name, then what is attached, then name and the rest */
	if (sd_bus_message_read(m, "s", &s) >= 0 && sd_bus_message_skip(m, "a{sv}") >= 0
		&& sd_bus_message_read(m, "s", &name) >= 0) {
		const char *skip[7];
		uint32_t rank = 0;
		const char *hotkeys = NULL;
		if (sd_bus_message_read(m, "sssssss", &skip[0], &skip[1], &skip[2], &skip[3],
				&skip[4], &skip[5], &skip[6]) >= 0
			&& sd_bus_message_read(m, "u", &rank) >= 0
			&& sd_bus_message_read(m, "ss", &hotkeys, &symbol) >= 0 && symbol && *symbol) {
			str_copy(label, sizeof label, symbol);
		}
		if (!label[0] && name) {
			label_from_name(name, label, sizeof label);
		}
	}
	set_label(label);
	return 0;
}

static void ibus_ask(void)
{
	if (g_ibus) {
		sd_bus_call_method_async(g_ibus, NULL, "org.freedesktop.IBus", "/org/freedesktop/IBus",
			"org.freedesktop.DBus.Properties", "Get", on_engine, NULL, "ss",
			"org.freedesktop.IBus", "GlobalEngine");
		sd_bus_flush(g_ibus);
	}
}

static int on_engine_changed(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)m;
	(void)user;
	(void)err;
	ibus_ask();
	return 0;
}

static int ibus_ready(int fd, uint32_t mask, void *data)
{
	(void)fd;
	(void)data;
	if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
		ibus_drop();
		set_label("");
		return 0;
	}
	for (int k = 0; k < 64 && g_ibus; k++) {
		int r = sd_bus_process(g_ibus, NULL);
		if (r < 0) {
			ibus_drop();
			set_label("");
			return 0;
		}
		if (r == 0) {
			break;
		}
	}
	if (g_ibus) {
		sd_bus_flush(g_ibus);
	}
	return 0;
}

static bool ibus_address_in(const char *dir, char *out, size_t n, time_t *best_at);

/* Where IBus is: IBUS_ADDRESS, or the newest file under ~/.config/ibus/bus
 * whose daemon is still running. */
static bool ibus_address(char *out, size_t n)
{
	const char *env = getenv("IBUS_ADDRESS");
	if (env && *env) {
		str_copy(out, n, env);
		return true;
	}
	const char *home = getenv("HOME");
	const char *cfg = getenv("XDG_CONFIG_HOME");
	/* where IBus itself looks, and where it looks by default */
	char dirs[2][400];
	int nd = 0;
	if (cfg && *cfg) {
		snprintf(dirs[nd++], sizeof dirs[0], "%s/ibus/bus", cfg);
	}
	if (home && *home) {
		snprintf(dirs[nd++], sizeof dirs[0], "%s/.config/ibus/bus", home);
	}
	time_t best = 0;
	bool found = false;
	for (int k = 0; k < nd && !found; k++) {
		if (ibus_address_in(dirs[k], out, n, &best)) {
			found = true;
		}
	}
	return found;
}

static bool ibus_address_in(const char *dir, char *out, size_t n, time_t *best_at)
{
	DIR *d = opendir(dir);
	if (!d) {
		return false;
	}
	time_t best = *best_at;
	bool found = false;
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.') {
			continue;
		}
		char path[1100];
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		struct stat st;
		if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_mtime < best) {
			continue;
		}
		FILE *f = fopen(path, "re");
		if (!f) {
			continue;
		}
		char line[512], addr[512] = { 0 };
		long pid = 0;
		while (fgets(line, sizeof line, f)) {
			line[strcspn(line, "\r\n")] = '\0';
			if (strncmp(line, "IBUS_ADDRESS=", 13) == 0) {
				str_copy(addr, sizeof addr, line + 13);
			} else if (strncmp(line, "IBUS_DAEMON_PID=", 16) == 0) {
				pid = atol(line + 16);
			}
		}
		fclose(f);
		if (addr[0] && pid > 0 && kill((pid_t)pid, 0) == 0) {
			str_copy(out, n, addr);
			best = st.st_mtime;
			found = true;
		}
	}
	closedir(d);
	*best_at = best;
	return found;
}

static bool ibus_connect(void)
{
	char addr[512];
	if (!ibus_address(addr, sizeof addr)) {
		return false;
	}
	if (sd_bus_new(&g_ibus) < 0) {
		g_ibus = NULL;
		return false;
	}
	if (sd_bus_set_address(g_ibus, addr) < 0 || sd_bus_set_bus_client(g_ibus, 1) < 0
		|| sd_bus_start(g_ibus) < 0) {
		sd_bus_unref(g_ibus);
		g_ibus = NULL;
		return false;
	}
	sd_bus_match_signal(g_ibus, &g_ibus_match, NULL, "/org/freedesktop/IBus",
		"org.freedesktop.IBus", "GlobalEngineChanged", on_engine_changed, NULL);
	int fd = sd_bus_get_fd(g_ibus);
	if (fd >= 0 && g_loop) {
		g_ibus_src = wl_event_loop_add_fd(g_loop, fd, WL_EVENT_READABLE, ibus_ready, NULL);
	}
	ibus_ask();
	return true;
}

/* ---- typing through IBus ------------------------------------------
 *
 * An input context of AquaDE's own: told it can show what is being
 * written and the candidates itself, so that IBus sends them here rather
 * than to its panel. Keys go over as they come, without waiting (a key
 * not wanted comes back as not taken, in order, and is typed then). */

#define IBUS_CAP_PREEDIT_TEXT 1u
#define IBUS_CAP_AUXILIARY_TEXT 2u
#define IBUS_CAP_LOOKUP_TABLE 4u
#define IBUS_CAP_FOCUS 8u
#define IBUS_RELEASE_MASK (1u << 30)

static void ic_call(const char *iface, const char *member, sd_bus_message_handler_t cb,
	void *data, const char *types, ...)
{
	if (!g_ibus || !g_ic_path[0]) {
		return;
	}
	sd_bus_message *m = NULL;
	if (sd_bus_message_new_method_call(g_ibus, &m, "org.freedesktop.IBus", g_ic_path,
			iface, member) < 0) {
		return;
	}
	va_list ap;
	va_start(ap, types);
	int r = types ? sd_bus_message_appendv(m, types, ap) : 0;
	va_end(ap);
	if (r >= 0) {
		sd_bus_call_async(g_ibus, NULL, m, cb, data, 0);
		sd_bus_flush(g_ibus);
	}
	sd_bus_message_unref(m);
}

static void ic_forget(void)
{
	if (g_ic_match) {
		sd_bus_slot_unref(g_ic_match);
		g_ic_match = NULL;
	}
	g_ic_path[0] = '\0';
}

/* An IBusText, (sa{sv}sv), in a variant: the text of it. */
static int read_text(sd_bus_message *m, char *out, size_t n)
{
	char type = 0;
	const char *contents = NULL;
	out[0] = '\0';
	if (sd_bus_message_peek_type(m, &type, &contents) < 0 || type != 'v'
		|| sd_bus_message_enter_container(m, 'v', contents) < 0) {
		return -1;
	}
	int r = -1;
	if (sd_bus_message_peek_type(m, &type, &contents) >= 0 && type == 'r'
		&& sd_bus_message_enter_container(m, 'r', contents) >= 0) {
		const char *name = NULL, *text = NULL;
		if (sd_bus_message_read(m, "s", &name) >= 0 && sd_bus_message_skip(m, "a{sv}") >= 0
			&& sd_bus_message_read(m, "s", &text) >= 0 && sd_bus_message_skip(m, "v") >= 0) {
			str_copy(out, n, text);
			r = 0;
		}
		sd_bus_message_exit_container(m);
	}
	sd_bus_message_exit_container(m);
	return r;
}

/* An IBusLookupTable, (sa{sv}uubbiavav), in a variant: the candidates,
 * which is picked, how many to a page. */
static void read_lookup(sd_bus_message *m, bool visible_follows)
{
	static char texts[64][96];
	static const char *cands[64];
	int n = 0;
	uint32_t page = 0, cursor = 0;
	char type = 0;
	const char *contents = NULL;
	if (sd_bus_message_peek_type(m, &type, &contents) < 0 || type != 'v'
		|| sd_bus_message_enter_container(m, 'v', contents) < 0) {
		return;
	}
	if (sd_bus_message_peek_type(m, &type, &contents) >= 0 && type == 'r'
		&& sd_bus_message_enter_container(m, 'r', contents) >= 0) {
		const char *name = NULL;
		int cursor_visible = 0, round = 0;
		int32_t orient = 0;
		if (sd_bus_message_read(m, "s", &name) >= 0 && sd_bus_message_skip(m, "a{sv}") >= 0
			&& sd_bus_message_read(m, "uubbi", &page, &cursor, &cursor_visible, &round,
				&orient) >= 0
			&& sd_bus_message_enter_container(m, 'a', "v") >= 0) {
			while (n < 64 && read_text(m, texts[n], sizeof texts[n]) >= 0) {
				cands[n] = texts[n];
				n++;
			}
			sd_bus_message_exit_container(m);
		}
		sd_bus_message_exit_container(m);
	}
	sd_bus_message_exit_container(m);
	int visible = 1;
	if (visible_follows && sd_bus_message_read(m, "b", &visible) < 0) {
		visible = 1;
	}
	if (g_typing && g_typ.lookup) {
		g_typ.lookup(cands, n, (int)cursor, page ? (int)page : 9, visible && n > 0, g_typ.user);
	}
}

static void tell_preedit(void)
{
	if (g_typing && g_typ.preedit) {
		g_typ.preedit(g_pre, g_pre_cursor, g_pre_visible && g_pre[0], g_typ.user);
	}
}

static int on_ic_signal(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)user;
	(void)err;
	const char *what = sd_bus_message_get_member(m);
	if (!what || !g_typing) {
		return 0;
	}
	if (strcmp(what, "CommitText") == 0) {
		char text[512];
		if (read_text(m, text, sizeof text) >= 0 && g_typ.commit) {
			g_typ.commit(text, g_typ.user);
		}
	} else if (strcmp(what, "UpdatePreeditText") == 0
		|| strcmp(what, "UpdatePreeditTextWithMode") == 0) {
		uint32_t cursor = 0;
		int visible = 0;
		if (read_text(m, g_pre, sizeof g_pre) >= 0
			&& sd_bus_message_read(m, "ub", &cursor, &visible) >= 0) {
			g_pre_cursor = (int)cursor;
			g_pre_visible = visible;
			tell_preedit();
		}
	} else if (strcmp(what, "ShowPreeditText") == 0) {
		g_pre_visible = true;
		tell_preedit();
	} else if (strcmp(what, "HidePreeditText") == 0) {
		g_pre_visible = false;
		tell_preedit();
	} else if (strcmp(what, "UpdateLookupTable") == 0) {
		read_lookup(m, true);
	} else if (strcmp(what, "HideLookupTable") == 0) {
		if (g_typ.lookup) {
			g_typ.lookup(NULL, 0, 0, 9, false, g_typ.user);
		}
	} else if (strcmp(what, "ForwardKeyEvent") == 0) {
		/* the engine giving a key back to be typed as it is */
		uint32_t keyval = 0, keycode = 0, state = 0;
		if (sd_bus_message_read(m, "uuu", &keyval, &keycode, &state) >= 0
			&& !(state & IBUS_RELEASE_MASK) && g_typ.key) {
			g_typ.key(keyval, keycode, state, false, false, g_typ.user);
		}
	}
	return 0;
}

static int on_ic_made(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)err;
	unsigned gen = (unsigned)(uintptr_t)user;
	const char *path = NULL;
	if (sd_bus_message_is_method_error(m, NULL) || sd_bus_message_read(m, "o", &path) < 0) {
		return 0;
	}
	if (!g_typing || gen != g_typing_gen || !g_ibus) {
		/* not wanted any more */
		sd_bus_call_method_async(g_ibus, NULL, "org.freedesktop.IBus", path,
			"org.freedesktop.IBus.Service", "Destroy", NULL, NULL, "");
		return 0;
	}
	str_copy(g_ic_path, sizeof g_ic_path, path);
	sd_bus_match_signal(g_ibus, &g_ic_match, NULL, g_ic_path,
		"org.freedesktop.IBus.InputContext", NULL, on_ic_signal, NULL);
	/* With nowhere of its own to show the candidates (typing into a
	 * program's window), the input method shows them itself -- Mozc in
	 * its own window, at the cursor given it. The engine's properties (its
	 * input mode, Mozc's あ/A) go on to IBus's own panel, which shows them
	 * in the tray. */
	ic_call("org.freedesktop.IBus.InputContext", "SetCapabilities", NULL, NULL, "u",
		IBUS_CAP_PREEDIT_TEXT | IBUS_CAP_AUXILIARY_TEXT | IBUS_CAP_FOCUS
		| (g_typ.lookup ? IBUS_CAP_LOOKUP_TABLE : 0u));
	ic_call("org.freedesktop.IBus.InputContext", "FocusIn", NULL, NULL, NULL);
	return 0;
}

bool desk_ime_typing_begin(const struct desk_ime_typing *t)
{
	g_typ = *t;
	g_typing = true;
	g_typing_gen++;
	g_pre[0] = '\0';
	g_pre_visible = false;
	if (!g_ibus) {
		return false;
	}
	if (g_ic_path[0]) {
		ic_call("org.freedesktop.IBus.InputContext", "FocusIn", NULL, NULL, NULL);
		return true;
	}
	if (sd_bus_call_method_async(g_ibus, NULL, "org.freedesktop.IBus", "/org/freedesktop/IBus",
			"org.freedesktop.IBus", "CreateInputContext", on_ic_made,
			(void *)(uintptr_t)g_typing_gen, "s", "AquaDE") < 0) {
		return false;
	}
	sd_bus_flush(g_ibus);
	return true;
}

void *desk_ime_typing_owner(void)
{
	return g_typing ? g_typ.user : NULL;
}

void desk_ime_typing_end(void)
{
	if (!g_typing) {
		return;
	}
	g_typing = false;
	g_typing_gen++;
	if (g_ic_path[0]) {
		ic_call("org.freedesktop.IBus.InputContext", "Reset", NULL, NULL, NULL);
		ic_call("org.freedesktop.IBus.InputContext", "FocusOut", NULL, NULL, NULL);
		ic_call("org.freedesktop.IBus.Service", "Destroy", NULL, NULL, NULL);
	}
	ic_forget();
}

struct key_ask {
	uint32_t keysym, keycode, mods;
	unsigned gen;
};

static int on_key_taken(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)err;
	struct key_ask *k = user;
	int taken = 0;
	if (sd_bus_message_is_method_error(m, NULL) || sd_bus_message_read(m, "b", &taken) < 0) {
		taken = 0;
	}
	if (g_typing && k->gen == g_typing_gen && g_typ.key) {
		g_typ.key(k->keysym, k->keycode, k->mods, taken, true, g_typ.user);
	}
	free(k);
	return 0;
}

bool desk_ime_typing_key(uint32_t keysym, uint32_t keycode, uint32_t mods, bool released)
{
	if (!g_typing || !g_ibus || !g_ic_path[0]) {
		return false;
	}
	uint32_t state = mods | (released ? IBUS_RELEASE_MASK : 0u);
	if (released) {
		ic_call("org.freedesktop.IBus.InputContext", "ProcessKeyEvent", NULL, NULL, "uuu",
			keysym, keycode, state);
		return true;
	}
	struct key_ask *k = malloc(sizeof *k);
	if (!k) {
		return false;
	}
	*k = (struct key_ask){ keysym, keycode, mods, g_typing_gen };
	sd_bus_message *m = NULL;
	if (sd_bus_message_new_method_call(g_ibus, &m, "org.freedesktop.IBus", g_ic_path,
			"org.freedesktop.IBus.InputContext", "ProcessKeyEvent") < 0
		|| sd_bus_message_append(m, "uuu", keysym, keycode, state) < 0
		|| sd_bus_call_async(g_ibus, NULL, m, on_key_taken, k, 0) < 0) {
		sd_bus_message_unref(m);
		free(k);
		return false;
	}
	sd_bus_message_unref(m);
	sd_bus_flush(g_ibus);
	return true;
}

void desk_ime_typing_cursor(int x, int y, int w, int h)
{
	if (g_ic_path[0]) {
		ic_call("org.freedesktop.IBus.InputContext", "SetCursorLocation", NULL, NULL, "iiii",
			(int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h);
	}
}

void desk_ime_typing_reset(void)
{
	if (g_ic_path[0]) {
		ic_call("org.freedesktop.IBus.InputContext", "Reset", NULL, NULL, NULL);
	}
	g_pre[0] = '\0';
	g_pre_visible = false;
	tell_preedit();
	if (g_typing && g_typ.lookup) {
		g_typ.lookup(NULL, 0, 0, 9, false, g_typ.user);
	}
}

/* ---- fcitx5 ----------------------------------------------------- */

static char g_fcitx_im[64];

static int on_fcitx_state(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)user;
	(void)err;
	g_fcitx_asking = false;
	int32_t state = 0;
	if (sd_bus_message_is_method_error(m, NULL) || sd_bus_message_read(m, "i", &state) < 0) {
		return 0;
	}
	/* 2 is active: the input method itself, not the keyboard under it */
	char label[16];
	if (state == 2 && strncmp(g_fcitx_im, "keyboard-", 9) != 0) {
		label_from_name(g_fcitx_im, label, sizeof label);
	} else if (strncmp(g_fcitx_im, "keyboard-", 9) == 0) {
		char lay[8] = { 0 };
		int k = 0;
		for (const char *p = g_fcitx_im + 9; *p && *p != '-' && k < 3; p++) {
			lay[k++] = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
		}
		str_copy(label, sizeof label, lay);
	} else {
		str_copy(label, sizeof label, "A");
	}
	set_label(label);
	return 0;
}

static int on_fcitx_im(sd_bus_message *m, void *user, sd_bus_error *err)
{
	(void)user;
	(void)err;
	const char *im = NULL;
	if (sd_bus_message_is_method_error(m, NULL) || sd_bus_message_read(m, "s", &im) < 0) {
		g_fcitx_asking = false;
		return 0;
	}
	str_copy(g_fcitx_im, sizeof g_fcitx_im, im);
	if (sd_bus_call_method_async(g_session, NULL, "org.fcitx.Fcitx5", "/controller",
			"org.fcitx.Fcitx.Controller1", "State", on_fcitx_state, NULL, "") < 0) {
		g_fcitx_asking = false;
	}
	return 0;
}

static int session_ready(int fd, uint32_t mask, void *data)
{
	(void)fd;
	(void)mask;
	(void)data;
	for (int k = 0; k < 64 && g_session; k++) {
		if (sd_bus_process(g_session, NULL) <= 0) {
			break;
		}
	}
	if (g_session) {
		sd_bus_flush(g_session);
	}
	return 0;
}

static void fcitx_ask(void)
{
	if (!g_session || g_fcitx_asking) {
		return;
	}
	if (sd_bus_call_method_async(g_session, NULL, "org.fcitx.Fcitx5", "/controller",
			"org.fcitx.Fcitx.Controller1", "CurrentInputMethod", on_fcitx_im, NULL, "") >= 0) {
		g_fcitx_asking = true;
		sd_bus_flush(g_session);
	}
}

/* ---- the rest ----------------------------------------------------- */

bool desk_ime_init(struct wl_event_loop *loop, void (*changed)(void *), void *user)
{
	g_loop = loop;
	g_changed = changed;
	g_user = user;
	g_ibus_tried = time(NULL);
	if (ibus_connect()) {
		return true;
	}
	if (sd_bus_open_user(&g_session) < 0) {
		g_session = NULL;
		return false;
	}
	int fd = sd_bus_get_fd(g_session);
	if (fd >= 0) {
		g_session_src = wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE, session_ready, NULL);
	}
	fcitx_ask();
	return true;
}

void desk_ime_finish(void)
{
	ibus_drop();
	if (g_session_src) {
		wl_event_source_remove(g_session_src);
		g_session_src = NULL;
	}
	if (g_session) {
		sd_bus_flush_close_unref(g_session);
		g_session = NULL;
	}
}

void desk_ime_tick(void)
{
	if (g_ibus) {
		ibus_ready(-1, 0, NULL);
		return;
	}
	/* IBus comes and goes with the session; look for it now and again */
	time_t now = time(NULL);
	if (now - g_ibus_tried >= 5) {
		g_ibus_tried = now;
		if (ibus_connect()) {
			return;
		}
	}
	if (g_session) {
		session_ready(-1, 0, NULL);
		fcitx_ask();
	} else if (g_label[0]) {
		set_label("");
	}
}
