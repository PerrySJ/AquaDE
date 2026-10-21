#ifndef DESK_POST_H
#define DESK_POST_H

/* What is in the post box.
 *
 * AquaDE is the machine's notification daemon: it takes the
 * org.freedesktop.Notifications name on the session bus, so what arrives
 * here is what every other application on the machine thinks it is
 * telling you. Each one becomes a letter.
 *
 * The bus is polled on the compositor's own event loop -- sd_bus hands
 * out a file descriptor and the wayland loop is perfectly happy to watch
 * one -- so there is no second thread and no lock. */

#include <stdbool.h>
#include <time.h>

struct wl_event_loop;

enum {
	DESK_POST_MAX = 32,    /* letters the box holds before the oldest goes */
	DESK_POST_ACTIONS = 4, /* actions kept for each */
	DESK_URGENCY_LOW = 0,
	DESK_URGENCY_NORMAL = 1,
	DESK_URGENCY_CRITICAL = 2,
};

struct desk_note {
	unsigned id;             /* the id the sender knows it by */
	char app[64];            /* app_name, as given */
	char summary[160];
	char body[512];
	char icon[160];          /* app_icon: a name or a path, often empty */
	char desktop[96];        /* desktop-entry hint, without the suffix */
	time_t at;
	int urgency;
	bool read;               /* taken out of the box and read */
	bool resident;           /* the sender asked that it stay after an action */
	/* What the sender offers to do about it, as it named them: the key it
	 * wants back, and the words to show. "default" is what clicking the
	 * notification itself is meant to do. */
	int n_actions;
	char action_key[DESK_POST_ACTIONS][32];
	char action_label[DESK_POST_ACTIONS][48];
	bool own;                /* the desk's own (desk_post_own): its actions are ours */
};

/* Claim the name and start listening. False when there is no session bus
 * or somebody else will not give the name up; the box then simply stays
 * empty, which is a great deal better than refusing to start. */
bool desk_post_init(struct wl_event_loop *loop);
void desk_post_finish(void);

/* The letters, newest first. The array belongs to the post box and is
 * only valid until the next time the loop runs. */
int desk_post_letters(const struct desk_note **out);
int desk_post_unread(void);
void desk_post_mark_read(unsigned id);
void desk_post_set_read(unsigned id, bool read);
/* Do what the sender offered, by its key: tell it so, and -- unless it
 * asked to stay -- take the letter out of the box. */
void desk_post_invoke(unsigned id, const char *key);
/* Take one out of the box for good, telling the sender it has been
 * dismissed the way the spec says to. */
void desk_post_dismiss(unsigned id);

/* Called whenever the post changes, so the compositor can repaint. */
void desk_post_on_change(void (*cb)(void *user), void *user);

/* A letter from the desk itself -- that the screen is drawn on the CPU,
 * say: summary, body, desktop (whose icon heads it) and actions from note,
 * the rest made up here, signed DESK_POST_OWN and marked own: its actions
 * are the compositor's to do, not a program's to be told of. Its id. */
#define DESK_POST_OWN "AquaDE"
unsigned desk_post_own(const struct desk_note *note);

#endif
