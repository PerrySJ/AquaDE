/* The screen lock, apart from its drawing (lock.h).
 *
 * The password is checked with PAM, as at login, under the service
 * "aquade" (session/pam/aquade, which includes "login"), or "login" itself
 * where that is not installed. On a thread: pam_unix waits a few seconds
 * after a wrong one on purpose, and the desk must not stop for it. Only a
 * AquaDE that is not the session (nested, in a window, under test) may be
 * pointed at another PAM directory, DESK_LOCK_PAM_DIR: nobody at a locked
 * screen can set an environment variable, and a test cannot use anyone's
 * real password. */

/* explicit_bzero and pipe2 are glibc's and the BSDs', past POSIX */
#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#include "lock.h"
#include "session.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <pwd.h>
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wlr/util/log.h>

#ifdef DESK_HAVE_POST
#include <systemd/sd-bus.h>
#endif

/* ---- the password ---- */

struct check {
	char user[64];
	char pass[512];
	char confdir[512];
	bool ok;
	int fd[2];
	struct wl_event_source *src;
	void (*done)(bool ok, void *data);
	void *data;
};

static int conv(int n, const struct pam_message **msg, struct pam_response **resp, void *data)
{
	const struct check *c = data;
	struct pam_response *r = calloc((size_t)n, sizeof *r);
	if (!r) {
		return PAM_BUF_ERR;
	}
	for (int i = 0; i < n; i++) {
		int style = msg[i]->msg_style;
		if (style == PAM_PROMPT_ECHO_OFF || style == PAM_PROMPT_ECHO_ON) {
			r[i].resp = strdup(c->pass);
			if (!r[i].resp) {
				for (int k = 0; k < i; k++) {
					free(r[k].resp);
				}
				free(r);
				return PAM_BUF_ERR;
			}
		}
	}
	*resp = r;
	return PAM_SUCCESS;
}

static void *check_thread(void *arg)
{
	struct check *c = arg;
	struct pam_conv pc = { conv, c };
	pam_handle_t *h = NULL;
	const char *service = access("/etc/pam.d/aquade", F_OK) == 0 ? "aquade" : "login";
	int r = c->confdir[0] ? pam_start_confdir("aquade", c->user, &pc, c->confdir, &h)
		: pam_start(service, c->user, &pc, &h);
	if (r == PAM_SUCCESS) {
		r = pam_authenticate(h, 0);
		if (r == PAM_SUCCESS) {
			/* tickets and the like, as a login refreshes them */
			pam_setcred(h, PAM_REFRESH_CRED);
		}
		pam_end(h, r);
	}
	c->ok = r == PAM_SUCCESS;
	explicit_bzero(c->pass, sizeof c->pass);
	char one = 1;
	while (write(c->fd[1], &one, 1) < 0 && errno == EINTR) {
		;
	}
	return NULL;
}

static int check_landed(int fd, uint32_t mask, void *data)
{
	(void)mask;
	struct check *c = data;
	char one;
	while (read(fd, &one, 1) < 0 && errno == EINTR) {
		;
	}
	wl_event_source_remove(c->src);
	close(c->fd[0]);
	close(c->fd[1]);
	void (*done)(bool, void *) = c->done;
	void *d = c->data;
	bool ok = c->ok;
	free(c);
	done(ok, d);
	return 0;
}

bool desk_lock_check(struct wl_event_loop *loop, const char *password,
	void (*done)(bool ok, void *data), void *data)
{
	struct check *c = calloc(1, sizeof *c);
	if (!c) {
		return false;
	}
	char name[128];
	desk_lock_user(c->user, sizeof c->user, name, sizeof name);
	snprintf(c->pass, sizeof c->pass, "%s", password);
	const char *dir = getenv("DESK_LOCK_PAM_DIR");
	if (dir && *dir && !desk_session_is_desktop()) {
		snprintf(c->confdir, sizeof c->confdir, "%s", dir);
	}
	c->done = done;
	c->data = data;
	if (pipe2(c->fd, O_CLOEXEC) != 0) {
		explicit_bzero(c->pass, sizeof c->pass);
		free(c);
		return false;
	}
	c->src = wl_event_loop_add_fd(loop, c->fd[0], WL_EVENT_READABLE, check_landed, c);
	pthread_t th;
	if (!c->src || pthread_create(&th, NULL, check_thread, c) != 0) {
		if (c->src) {
			wl_event_source_remove(c->src);
		}
		close(c->fd[0]);
		close(c->fd[1]);
		explicit_bzero(c->pass, sizeof c->pass);
		free(c);
		return false;
	}
	pthread_detach(th);
	return true;
}

/* ---- logind ---- */

#ifdef DESK_HAVE_POST
static sd_bus *g_sys;
static char g_session_path[256];
static void (*g_lock)(void *), (*g_unlock)(void *);
static void *g_data;

static int on_lock(sd_bus_message *m, void *data, sd_bus_error *err)
{
	(void)m;
	(void)data;
	(void)err;
	wlr_log(WLR_INFO, "lock: logind says lock");
	g_lock(g_data);
	return 0;
}

static int on_unlock(sd_bus_message *m, void *data, sd_bus_error *err)
{
	(void)m;
	(void)data;
	(void)err;
	wlr_log(WLR_INFO, "lock: logind says unlock");
	g_unlock(g_data);
	return 0;
}

static int on_sleep(sd_bus_message *m, void *data, sd_bus_error *err)
{
	(void)data;
	(void)err;
	int going = 0;
	if (sd_bus_message_read(m, "b", &going) >= 0 && going) {
		wlr_log(WLR_INFO, "lock: the machine is going to sleep");
		g_lock(g_data);
	}
	return 0;
}

static int bus_ready(int fd, uint32_t mask, void *data)
{
	(void)fd;
	(void)mask;
	(void)data;
	while (sd_bus_process(g_sys, NULL) > 0) {
		;
	}
	return 0;
}

bool desk_lock_watch(struct wl_event_loop *loop, void (*lock)(void *data),
	void (*unlock)(void *data), void *data)
{
	if (!desk_session_is_desktop() || sd_bus_open_system(&g_sys) < 0) {
		return false;
	}
	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	const char *id = getenv("XDG_SESSION_ID");
	int r = id && *id
		? sd_bus_call_method(g_sys, "org.freedesktop.login1", "/org/freedesktop/login1",
			"org.freedesktop.login1.Manager", "GetSession", &err, &reply, "s", id)
		: sd_bus_call_method(g_sys, "org.freedesktop.login1", "/org/freedesktop/login1",
			"org.freedesktop.login1.Manager", "GetSessionByPID", &err, &reply, "u",
			(uint32_t)getpid());
	const char *path = NULL;
	if (r < 0 || sd_bus_message_read(reply, "o", &path) < 0 || !path) {
		wlr_log(WLR_INFO, "lock: no logind session to watch (%s)",
			err.message ? err.message : "no answer");
		sd_bus_error_free(&err);
		sd_bus_message_unref(reply);
		sd_bus_unref(g_sys);
		g_sys = NULL;
		return false;
	}
	snprintf(g_session_path, sizeof g_session_path, "%s", path);
	sd_bus_message_unref(reply);
	g_lock = lock;
	g_unlock = unlock;
	g_data = data;
	sd_bus_match_signal(g_sys, NULL, "org.freedesktop.login1", g_session_path,
		"org.freedesktop.login1.Session", "Lock", on_lock, NULL);
	sd_bus_match_signal(g_sys, NULL, "org.freedesktop.login1", g_session_path,
		"org.freedesktop.login1.Session", "Unlock", on_unlock, NULL);
	sd_bus_match_signal(g_sys, NULL, "org.freedesktop.login1", "/org/freedesktop/login1",
		"org.freedesktop.login1.Manager", "PrepareForSleep", on_sleep, NULL);
	wl_event_loop_add_fd(loop, sd_bus_get_fd(g_sys), WL_EVENT_READABLE, bus_ready, NULL);
	bus_ready(0, 0, NULL);
	wlr_log(WLR_INFO, "lock: watching %s", g_session_path);
	return true;
}

void desk_lock_hint(bool locked)
{
	if (!g_sys || !g_session_path[0]) {
		return;
	}
	sd_bus_call_method(g_sys, "org.freedesktop.login1", g_session_path,
		"org.freedesktop.login1.Session", "SetLockedHint", NULL, NULL, "b", (int)locked);
}
#else
bool desk_lock_watch(struct wl_event_loop *loop, void (*lock)(void *data),
	void (*unlock)(void *data), void *data)
{
	(void)loop;
	(void)lock;
	(void)unlock;
	(void)data;
	return false;
}

void desk_lock_hint(bool locked)
{
	(void)locked;
}
#endif
