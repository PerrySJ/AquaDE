#ifndef DESK_IME_H
#define DESK_IME_H

/* Which input method is on, as the one or two characters a clock face has
 * room for: "あ" with Mozc's kana on, "A" with it off, "JP" for a plain
 * Japanese keyboard. Read from IBus, or else fcitx5; an empty string when
 * there is neither. */

#include <stdbool.h>
#include <stdint.h>

struct wl_event_loop;

bool desk_ime_init(struct wl_event_loop *loop, void (*changed)(void *user), void *user);
void desk_ime_finish(void);
/* Once a second or so: finds IBus again if it went away, asks fcitx5. */
void desk_ime_tick(void);

/* Typing into something AquaDE draws itself (the calendar's notes), through
 * the input method: the keys go to IBus, and back come the text written
 * (commit), the text being written and not yet decided (preedit), and the
 * candidates for it (lookup: the whole list, which one is picked, how many
 * to a page) -- which AquaDE shows itself, the input method's own panel
 * having nowhere to put them. key: what became of a key handed over
 * (answer), in the order they were handed over -- not taken by the input
 * method, the caller does with it what a plain key does -- or a key the
 * input method gives back of its own accord, to be typed as it is. With no
 * lookup, the input method shows its candidates itself (Mozc does, in a
 * window of its own at the cursor), which is how a program's window is
 * typed into (text-input, wm.c). Without
 * IBus none of this happens: typing_begin says so, and the caller types
 * the keys itself. */
struct desk_ime_typing {
	void (*commit)(const char *text, void *user);
	void (*preedit)(const char *text, int cursor, bool visible, void *user);
	void (*lookup)(const char *const *cands, int n, int cursor, int page, bool visible,
		void *user);
	void (*key)(uint32_t keysym, uint32_t keycode, uint32_t mods, bool taken, bool answer,
		void *user);
	void *user;
};
bool desk_ime_typing_begin(const struct desk_ime_typing *t);
void desk_ime_typing_end(void);
/* whose typing it is (the user of the one begun), or NULL */
void *desk_ime_typing_owner(void);
/* A key (evdev code, X keysym, modifiers as X has them), pressed or let
 * go: false if it could not be handed over, and the caller has it. */
bool desk_ime_typing_key(uint32_t keysym, uint32_t keycode, uint32_t mods, bool released);
/* What is being written is thrown away (the note was put down). */
void desk_ime_typing_reset(void);
/* Where it is being written, on the screen the input method's own windows
 * go on (its candidates, if it shows them itself, as Mozc does on X). */
void desk_ime_typing_cursor(int x, int y, int w, int h);

#endif
