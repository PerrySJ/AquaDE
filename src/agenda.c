/* What is written on the desk calendar (agenda.h). */

#include "agenda.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static struct desk_stick *g_st;
static int g_n, g_cap;
static int g_next_id = 1;
static unsigned g_gen = 1;
static bool g_loaded;

static void agenda_path(char *out, size_t n)
{
	const char *data = getenv("XDG_DATA_HOME");
	const char *home = getenv("HOME");
	if (data && *data) {
		snprintf(out, n, "%s/aquade/calendar", data);
	} else if (home && *home) {
		snprintf(out, n, "%s/.local/share/aquade/calendar", home);
	} else {
		out[0] = '\0';
	}
}

static bool agenda_push(const struct desk_stick *s)
{
	if (g_n == g_cap) {
		int cap = g_cap ? g_cap * 2 : 32;
		struct desk_stick *st = realloc(g_st, sizeof *st * (size_t)cap);
		if (!st) {
			return false;
		}
		g_st = st;
		g_cap = cap;
	}
	g_st[g_n++] = *s;
	return true;
}

/* one line, as it was written; a tab or a line end in the text would end
 * the field or the line, so none is ever written */
static void clean(char *t)
{
	for (char *p = t; *p; p++) {
		if (*p == '\t' || *p == '\n' || *p == '\r') {
			*p = ' ';
		}
	}
}

static void agenda_save(void)
{
	char path[600];
	agenda_path(path, sizeof path);
	if (!path[0]) {
		return;
	}
	char dir[600];
	snprintf(dir, sizeof dir, "%s", path);
	for (char *p = dir + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(dir, 0755);
			*p = '/';
		}
	}
	char tmp[640];
	snprintf(tmp, sizeof tmp, "%s.new", path);
	FILE *f = fopen(tmp, "we");
	if (!f) {
		fprintf(stderr, "aquade: calendar: cannot write %s: %s\n", tmp, strerror(errno));
		return;
	}
	fprintf(f, "# What is stuck on the desk calendar, a note to a line: the day, plan or\n"
		"# todo, the colour of its pad (0-%d), the time (or from-until) or -, done\n"
		"# (1) or not (0), and what is written on it. Fields are separated by tabs.\n",
		DESK_STICK_COLOURS - 1);
	for (int i = 0; i < g_n; i++) {
		const struct desk_stick *s = &g_st[i];
		char when[16] = "-";
		if (s->minute >= 0) {
			snprintf(when, sizeof when, "%02d:%02d", s->minute / 60 % 24, s->minute % 60);
		}
		if (s->minute >= 0 && s->span > 0) {
			int e = (s->minute + s->span) % (24 * 60);
			snprintf(when, sizeof when, "%02d:%02d-%02d:%02d", s->minute / 60 % 24,
				s->minute % 60, e / 60, e % 60);
		}
		fprintf(f, "%04d-%02d-%02d\t%s\t%d\t%s\t%d\t%s\n", s->y, s->m + 1, s->d,
			s->kind == DESK_STICK_TODO ? "todo" : "plan", s->colour, when,
			s->done ? 1 : 0, s->text);
	}
	if (fclose(f) != 0 || rename(tmp, path) != 0) {
		fprintf(stderr, "aquade: calendar: cannot keep %s: %s\n", path, strerror(errno));
	}
}

void desk_agenda_load(void)
{
	if (g_loaded) {
		return;
	}
	g_loaded = true;
	char path[600];
	agenda_path(path, sizeof path);
	FILE *f = path[0] ? fopen(path, "re") : NULL;
	if (!f) {
		return;
	}
	char line[512];
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (line[0] == '#' || !line[0]) {
			continue;
		}
		char *field[6];
		int nf = 0;
		char *p = line;
		while (nf < 6) {
			field[nf++] = p;
			char *tab = nf < 6 ? strchr(p, '\t') : NULL;
			if (!tab) {
				break;
			}
			*tab = '\0';
			p = tab + 1;
		}
		if (nf < 6) {
			continue;
		}
		struct desk_stick s = { 0 };
		int mo = 0, hh = 0, mm = 0, eh = 0, em = 0;
		if (sscanf(field[0], "%d-%d-%d", &s.y, &mo, &s.d) != 3 || mo < 1 || mo > 12
			|| s.d < 1 || s.d > 31) {
			continue;
		}
		s.m = mo - 1;
		s.kind = strcmp(field[1], "todo") == 0 ? DESK_STICK_TODO : DESK_STICK_PLAN;
		s.colour = atoi(field[2]);
		if (s.colour < 0 || s.colour >= DESK_STICK_COLOURS) {
			s.colour = 0;
		}
		int nt = sscanf(field[3], "%d:%d-%d:%d", &hh, &mm, &eh, &em);
		s.minute = nt >= 2 && hh >= 0 && hh < 24 && mm >= 0 && mm < 60 ? hh * 60 + mm : -1;
		if (s.minute >= 0 && nt == 4 && eh >= 0 && eh <= 24 && em >= 0 && em < 60) {
			/* until past midnight, if it ends before it starts */
			s.span = ((eh * 60 + em - s.minute) % (24 * 60) + 24 * 60) % (24 * 60);
		}
		s.done = atoi(field[4]) != 0;
		snprintf(s.text, sizeof s.text, "%s", field[5]);
		s.id = g_next_id++;
		agenda_push(&s);
	}
	fclose(f);
	g_gen++;
}

static int day_key(int y, int m, int d)
{
	return (y * 12 + m) * 32 + d;
}

int desk_agenda_day(int y, int m, int d, int kind, struct desk_stick *out, int max)
{
	desk_agenda_load();
	int key = day_key(y, m, d), n = 0;
	for (int i = 0; i < g_n; i++) {
		const struct desk_stick *s = &g_st[i];
		if (s->kind != kind || day_key(s->y, s->m, s->d) != key) {
			continue;
		}
		if (n < max && out) {
			/* in order as they go in: plans for the day, then by time */
			int j = n;
			while (j > 0 && kind == DESK_STICK_PLAN && out[j - 1].minute > s->minute) {
				out[j] = out[j - 1];
				j--;
			}
			out[j] = *s;
		}
		n++;
	}
	return n;
}

int desk_agenda_marks(int y, int m, unsigned char colour_of[32])
{
	desk_agenda_load();
	memset(colour_of, 0, 32);
	int n = 0;
	/* the plans first, then the days with only things to do */
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < g_n; i++) {
			const struct desk_stick *s = &g_st[i];
			if (s->kind != (pass ? DESK_STICK_TODO : DESK_STICK_PLAN) || s->y != y
				|| s->m != m || s->d < 1 || s->d > 31) {
				continue;
			}
			if (!colour_of[s->d]) {
				colour_of[s->d] = (unsigned char)(s->colour + 1);
				n++;
			}
		}
	}
	return n;
}

int desk_agenda_add(const struct desk_stick *s)
{
	desk_agenda_load();
	struct desk_stick c = *s;
	c.id = g_next_id++;
	clean(c.text);
	if (!agenda_push(&c)) {
		return 0;
	}
	g_gen++;
	agenda_save();
	return c.id;
}

bool desk_agenda_set(const struct desk_stick *s)
{
	for (int i = 0; i < g_n; i++) {
		if (g_st[i].id == s->id) {
			g_st[i] = *s;
			clean(g_st[i].text);
			g_gen++;
			agenda_save();
			return true;
		}
	}
	return false;
}

bool desk_agenda_remove(int id)
{
	for (int i = 0; i < g_n; i++) {
		if (g_st[i].id == id) {
			memmove(&g_st[i], &g_st[i + 1], sizeof *g_st * (size_t)(g_n - i - 1));
			g_n--;
			g_gen++;
			agenda_save();
			return true;
		}
	}
	return false;
}

bool desk_agenda_get(int id, struct desk_stick *out)
{
	for (int i = 0; i < g_n; i++) {
		if (g_st[i].id == id) {
			*out = g_st[i];
			return true;
		}
	}
	return false;
}

unsigned desk_agenda_generation(void)
{
	desk_agenda_load();
	return g_gen;
}

/* A digit, half or full width: its value, and how many bytes it took. */
static int digit(const unsigned char *p, int *len)
{
	if (p[0] >= '0' && p[0] <= '9') {
		*len = 1;
		return p[0] - '0';
	}
	/* U+FF10..FF19 */
	if (p[0] == 0xef && p[1] == 0xbc && p[2] >= 0x90 && p[2] <= 0x99) {
		*len = 3;
		return p[2] - 0x90;
	}
	return -1;
}

static bool is_utf8(const unsigned char *p, const char *s)
{
	return strncmp((const char *)p, s, strlen(s)) == 0;
}

static const unsigned char *spaces(const unsigned char *p)
{
	while (*p == ' ' || is_utf8(p, "\xe3\x80\x80")) {
		p += *p == ' ' ? 1 : 3;
	}
	return p;
}

/* One time of day at p: 10:00, 10：00, 9時, 9時半, 9時15分 -- as minutes
 * past midnight (24:00 is the end of the day), and where it ends; -1 if
 * there is none. */
static int one_time(const unsigned char *p, const unsigned char **after)
{
	int len, v, h = 0, nd = 0;
	while (nd < 2 && (v = digit(p, &len)) >= 0) {
		h = h * 10 + v;
		p += len;
		nd++;
	}
	if (nd == 0 || h > 24) {
		return -1;
	}
	int mins = 0;
	if (*p == ':' || is_utf8(p, "\xef\xbc\x9a")) {
		/* 10:00, 10：00 */
		p += *p == ':' ? 1 : 3;
		int m = 0, md = 0;
		while (md < 2 && (v = digit(p, &len)) >= 0) {
			m = m * 10 + v;
			p += len;
			md++;
		}
		if (md != 2 || m > 59) {
			return -1;
		}
		mins = m;
	} else if (is_utf8(p, "\xe6\x99\x82")) {
		/* 9時, 9時半, 9時15分 */
		p += 3;
		if (is_utf8(p, "\xe5\x8d\x8a")) {
			mins = 30;
			p += 3;
		} else {
			int m = 0, md = 0;
			while (md < 2 && (v = digit(p, &len)) >= 0) {
				m = m * 10 + v;
				p += len;
				md++;
			}
			if (md > 0) {
				if (m > 59) {
					return -1;
				}
				mins = m;
				if (is_utf8(p, "\xe5\x88\x86")) {
					p += 3;
				}
			}
		}
	} else {
		return -1;
	}
	if (h * 60 + mins > 24 * 60) {
		return -1;
	}
	*after = p;
	return h * 60 + mins;
}

/* what goes between a start and an end: - ~ – — 〜 ～ から */
static const unsigned char *until(const unsigned char *p)
{
	static const char *const seps[] = { "-", "~", "\xe2\x80\x93", "\xe2\x80\x94",
		"\xe3\x80\x9c", "\xef\xbd\x9e", "\xe3\x81\x8b\xe3\x82\x89" };
	for (size_t i = 0; i < sizeof seps / sizeof seps[0]; i++) {
		if (is_utf8(p, seps[i])) {
			return p + strlen(seps[i]);
		}
	}
	return NULL;
}

int desk_agenda_when(const char *text, int *span, const char **rest)
{
	const unsigned char *p = spaces((const unsigned char *)text), *q;
	if (span) {
		*span = 0;
	}
	int start = one_time(p, &q);
	if (start < 0) {
		return -1;
	}
	p = q;
	const unsigned char *sep = span ? until(spaces(p)) : NULL;
	int end = sep ? one_time(spaces(sep), &q) : -1;
	if (end >= 0) {
		/* until then, past midnight if it is before the start */
		int len = ((end - start) % (24 * 60) + 24 * 60) % (24 * 60);
		if (len > 0) {
			*span = len;
		}
		p = q;
		if (is_utf8(p, "\xe3\x81\xbe\xe3\x81\xa7")) {
			/* まで */
			p += 6;
		}
	}
	if (rest) {
		*rest = (const char *)spaces(p);
	}
	return start % (24 * 60);
}

void desk_agenda_time_text(int minute, int span, char *out, int n)
{
	if (n <= 0) {
		return;
	}
	out[0] = '\0';
	if (minute < 0) {
		return;
	}
	if (span > 0) {
		int e = (minute + span) % (24 * 60);
		snprintf(out, (size_t)n, "%d:%02d\xe2\x80\x93%d:%02d", minute / 60 % 24, minute % 60,
			e / 60, e % 60);
	} else {
		snprintf(out, (size_t)n, "%d:%02d", minute / 60 % 24, minute % 60);
	}
}
