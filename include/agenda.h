#ifndef DESK_AGENDA_H
#define DESK_AGENDA_H

/* What is written on the desk calendar: notes stuck on days -- plans, at a
 * time or for the day, and things to do, ticked off or not -- each the
 * colour of the pad it was peeled from. Kept in
 * $XDG_DATA_HOME/desk/calendar, a line to a note, written again (whole,
 * then put in place) on every change. */

#include <stdbool.h>

enum { DESK_STICK_PLAN, DESK_STICK_TODO };

/* the pads' colours */
#define DESK_STICK_COLOURS 6

struct desk_stick {
	int id;               /* for as long as AquaDE runs; 0 for none */
	int y, m, d;          /* the day, month 0 to 11 */
	int kind;             /* DESK_STICK_PLAN or DESK_STICK_TODO */
	int colour;           /* 0 to DESK_STICK_COLOURS - 1 */
	int minute;           /* past midnight, for a plan at a time; -1 for none */
	int span;             /* and until how long after, in minutes; 0 for no end */
	bool done;            /* a thing to do, done */
	char text[192];       /* UTF-8, one line */
};

/* Read the file, once, before anything else is asked. */
void desk_agenda_load(void);

/* The notes on a day, of a kind: plans for the day first, then by time,
 * then as they were stuck; things to do as they were stuck. Up to max of
 * them into out (copies); how many there are in all. */
int desk_agenda_day(int y, int m, int d, int kind, struct desk_stick *out, int max);

/* The days of a month with notes on them: for each day (1 to 31), the
 * colour of its first plan -- or thing to do, if it has no plans -- plus
 * one, 0 for none; how many such days. */
int desk_agenda_marks(int y, int m, unsigned char colour_of[32]);

/* Stuck on: its id. Changed, by id. Peeled off. */
int desk_agenda_add(const struct desk_stick *s);
bool desk_agenda_set(const struct desk_stick *s);
bool desk_agenda_remove(int id);
bool desk_agenda_get(int id, struct desk_stick *out);

/* Different every time anything is stuck on, changed or peeled off: for
 * whoever keeps a picture of a page. */
unsigned desk_agenda_generation(void);

/* "10:00 Dentist", "9時半 会議", "１４：３０ 打ち合わせ": the time at the
 * front of what was written, as minutes past midnight, and where the rest
 * begins; -1 if it does not start with one. With when, an end to it too
 * -- "10:00-11:30", "10時〜12時", "9時半から11時まで" -- as how many
 * minutes after the start it is, in *span (0 if none is written). */
int desk_agenda_when(const char *text, int *span, const char **rest);

/* "10:00" or "10:00\u201311:30" (an en dash between), for a plan at a
 * time; "" for one that is not. */
void desk_agenda_time_text(int minute, int span, char *out, int n);

#endif
