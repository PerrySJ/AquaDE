/* The memo pad's sheets (memo.h). */

#include "memo.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_sheet[DESK_MEMO_SHEETS][DESK_MEMO_TEXT];
static int g_n = 1, g_top;
static bool g_loaded;
static unsigned g_gen = 1;

static void memo_path(char *out, size_t n)
{
	const char *dh = getenv("XDG_DATA_HOME");
	const char *home = getenv("HOME");
	if (dh && *dh) {
		snprintf(out, n, "%s/aquade/memo", dh);
	} else if (home && *home) {
		snprintf(out, n, "%s/.local/share/aquade/memo", home);
	} else {
		out[0] = '\0';
	}
}

static void load(void)
{
	if (g_loaded) {
		return;
	}
	g_loaded = true;
	g_n = 1;
	g_sheet[0][0] = '\0';
	char path[512];
	memo_path(path, sizeof path);
	FILE *f = path[0] ? fopen(path, "re") : NULL;
	if (!f) {
		return;
	}
	char line[DESK_MEMO_TEXT];
	int s = 0;
	size_t used = 0;
	while (fgets(line, sizeof line, f)) {
		if (!strcmp(line, "\f\n") || !strcmp(line, "\f")) {
			if (s + 1 < DESK_MEMO_SHEETS) {
				/* the newline before the form feed is the separator's */
				if (used > 0 && g_sheet[s][used - 1] == '\n') {
					g_sheet[s][--used] = '\0';
				}
				s++;
				used = 0;
				g_sheet[s][0] = '\0';
			}
			continue;
		}
		size_t k = strlen(line);
		if (used + k < DESK_MEMO_TEXT) {
			memcpy(g_sheet[s] + used, line, k + 1);
			used += k;
		}
	}
	fclose(f);
	if (used > 0 && g_sheet[s][used - 1] == '\n') {
		g_sheet[s][--used] = '\0';
	}
	g_n = s + 1;
	g_top = g_n - 1;
}

static void save(void)
{
	char path[512];
	memo_path(path, sizeof path);
	if (!path[0]) {
		return;
	}
	char dir[512];
	snprintf(dir, sizeof dir, "%s", path);
	for (char *p = dir + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(dir, 0755);
			*p = '/';
		}
	}
	char tmp[600];
	snprintf(tmp, sizeof tmp, "%s.new", path);
	FILE *f = fopen(tmp, "we");
	if (!f) {
		fprintf(stderr, "desk: cannot keep the memo in %s: %s\n", tmp, strerror(errno));
		return;
	}
	/* blank sheets at the end are not kept */
	int n = g_n;
	while (n > 1 && !g_sheet[n - 1][0]) {
		n--;
	}
	for (int i = 0; i < n; i++) {
		if (i) {
			fputs("\n\f\n", f);
		}
		fputs(g_sheet[i], f);
	}
	fputc('\n', f);
	bool ok = fflush(f) == 0 && !ferror(f);
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(tmp, path) != 0) {
		fprintf(stderr, "desk: cannot keep the memo in %s\n", path);
		unlink(tmp);
	}
}

int desk_memo_sheets(void)
{
	load();
	return g_n;
}

const char *desk_memo_sheet(int i)
{
	load();
	return i >= 0 && i < g_n ? g_sheet[i] : "";
}

void desk_memo_set(int i, const char *text)
{
	load();
	if (i < 0 || i >= g_n) {
		return;
	}
	if (!strcmp(g_sheet[i], text ? text : "")) {
		return;
	}
	snprintf(g_sheet[i], DESK_MEMO_TEXT, "%s", text ? text : "");
	g_gen++;
	save();
}

int desk_memo_add(void)
{
	load();
	if (g_sheet[g_n - 1][0] && g_n < DESK_MEMO_SHEETS) {
		g_sheet[g_n][0] = '\0';
		g_n++;
		g_gen++;
	}
	return g_n - 1;
}

int desk_memo_top(void)
{
	load();
	return g_top < g_n ? g_top : g_n - 1;
}

void desk_memo_set_top(int i)
{
	load();
	if (i >= 0 && i < g_n && i != g_top) {
		g_top = i;
		g_gen++;
	}
}

unsigned desk_memo_generation(void)
{
	return g_gen;
}
