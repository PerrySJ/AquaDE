#pragma once

#include <stdbool.h>

/* The memo pad on the shelf: sheets of what has been written on it, kept
 * in $XDG_DATA_HOME/aquade/memo -- the sheets one after another, a line
 * with nothing but a form feed between two. Read the first time it is
 * asked for; written again whenever a sheet changes. */
#define DESK_MEMO_SHEETS 64
#define DESK_MEMO_TEXT 2048

int desk_memo_sheets(void);                 /* at least 1 */
const char *desk_memo_sheet(int i);         /* "" past the end */
void desk_memo_set(int i, const char *text);
/* a new sheet after the last, unless the last is still blank: its index */
int desk_memo_add(void);
/* the sheet the pad is open at (the last written on, at first) */
int desk_memo_top(void);
void desk_memo_set_top(int i);
/* changes whenever anything written on it does */
unsigned desk_memo_generation(void);
