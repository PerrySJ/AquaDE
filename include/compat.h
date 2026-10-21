#pragma once

/* DEsk went by DeskWM before (the commands deskwm, deskwm-session, ...).
 * What was set up for it keeps working:
 *
 *   - every DESKWM_* in the environment is read as the DESK_* it now is,
 *     unless that is set too;
 *   - what it kept in ~/.config/deskwm, ~/.local/share/deskwm,
 *     ~/.local/state/deskwm and ~/.cache/deskwm (or under the XDG_*_HOME
 *     ones) is moved to .../desk the first time, if there is nothing there
 *     yet, and ~/.config/deskwm-mimeapps.list to desk-mimeapps.list.
 *
 * Each program calls desk_compat() first thing in main(). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern char **environ;

static inline void desk_compat_env(void)
{
	/* copied out first: setenv may move environ about */
	char *old[64];
	int n = 0;
	for (char **e = environ; e && *e && n < 64; e++) {
		if (!strncmp(*e, "DESKWM_", 7)) {
			old[n++] = strdup(*e);
		}
	}
	for (int i = 0; i < n; i++) {
		if (!old[i]) {
			continue;
		}
		char *eq = strchr(old[i], '=');
		if (eq) {
			*eq = '\0';
			char name[128];
			snprintf(name, sizeof name, "DESK_%s", old[i] + 7);
			setenv(name, eq + 1, 0);
		}
		free(old[i]);
	}
}

static inline void desk_compat_move(const char *var, const char *fallback, const char *was,
	const char *now)
{
	char base[512], from[640], to[640];
	const char *v = getenv(var);
	const char *home = getenv("HOME");
	if (v && *v == '/') {
		snprintf(base, sizeof base, "%s", v);
	} else if (home && *home) {
		snprintf(base, sizeof base, "%s/%s", home, fallback);
	} else {
		return;
	}
	snprintf(from, sizeof from, "%s/%s", base, was);
	snprintf(to, sizeof to, "%s/%s", base, now);
	struct stat st;
	if (lstat(to, &st) == 0 || lstat(from, &st) != 0) {
		return;
	}
	if (rename(from, to) == 0) {
		fprintf(stderr, "aquade: moved %s to %s (the name DEsk had before)\n", from, to);
	}
}

static inline void desk_compat(void)
{
	desk_compat_env();
	desk_compat_move("XDG_CONFIG_HOME", ".config", "deskwm", "desk");
	desk_compat_move("XDG_CONFIG_HOME", ".config", "deskwm-mimeapps.list", "desk-mimeapps.list");
	desk_compat_move("XDG_DATA_HOME", ".local/share", "deskwm", "desk");
	desk_compat_move("XDG_STATE_HOME", ".local/state", "deskwm", "desk");
	desk_compat_move("XDG_CACHE_HOME", ".cache", "deskwm", "desk");
}
