#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "ui.h"

/* Forward declaration */
void cmd_keylock(int argc, char **argv);

#define DIGITS 4

/* Finite state machine states */
typedef enum { STATE_LOCKED, STATE_UNLOCKED } lock_state_t;

/* Passcode lives in RAM for the life of the process (initially 0000).
 * Deliberately NOT reset in kl_init, so it survives leaving and re-entering. */
static int passcode[DIGITS] = {0, 0, 0, 0};

static struct {
    lock_state_t state;
    int width, height;
    int dial[DIGITS];    /* LOCKED: digits shown on the dial            */
    int pos;             /* LOCKED: digit being guessed; left = confirmed */
    int draft[DIGITS];   /* UNLOCKED: passcode being edited             */
    int cur;             /* UNLOCKED: selected digit                    */
    int running;         /* cleared by 'e' (exit) or 'q' (quit)         */
    const char *msg;     /* status line */
} kl = {0};

/* ---- helpers ---- */

static void at(int row, int col) { printf("\033[%d;%dH", row, col); }
static int inc(int d) { return (d + 1) % 10; }   /* 9 -> 0 */
static int dec(int d) { return (d + 9) % 10; }   /* 0 -> 9 */

static void zero_digits(int *a) {
    for (int i = 0; i < DIGITS; i++) a[i] = 0;
}

static void center(int y, const char *s) {
    int x = (kl.width - (int)strlen(s)) / 2 + 1;
    if (x < 1) x = 1;
    at(y, x);
    printf("%s", s);
}

/* Draw "[d][d][d][d]" with a ^ marker under the selected digit.
 * Digits before ok_upto are highlighted green (confirmed);
 * the selected digit is shown in reverse video. */
static void draw_digits(int y, const int *d, int ok_upto, int sel) {
    int x0 = (kl.width - DIGITS * 3) / 2 + 1;
    if (x0 < 1) x0 = 1;
    for (int i = 0; i < DIGITS; i++) {
        at(y, x0 + i * 3);
        if (i < ok_upto)    printf("\033[1;32m");
        else if (i == sel)  printf("\033[7m");
        printf("[%d]\033[0m", d[i]);
    }
    at(y + 1, x0 + sel * 3 + 1);
    printf("^");
}

/* ---- state transitions ---- */

static void enter_locked(void) {
    kl.state = STATE_LOCKED;
    zero_digits(kl.dial);
    kl.pos = 0;
}

static void enter_unlocked(void) {
    kl.state = STATE_UNLOCKED;
    memcpy(kl.draft, passcode, sizeof(passcode));
    kl.cur = 0;
}

static void commit_draft(void) {
    memcpy(passcode, kl.draft, sizeof(passcode));
}

/* ---- game callbacks ---- */

static void game_init(void) {
    kl.width = cols;
    kl.height = rows - 2;   /* Leave room for UI at bottom */
    kl.running = 1;
    kl.msg = "";
    enter_locked();
    printf("\033[?25l");    /* hide cursor */
}

static void game_cleanup(void) {
    printf("\033[0m\033[?25h\033[2J\033[H");
    fflush(stdout);
}

static void game_draw(void) {
    int y = kl.height / 2 - 6;
    if (y < 1) y = 1;

    printf("\033[2J");

    if (kl.state == STATE_LOCKED) {
        center(y,     "=== LOCKED ===");
        draw_digits(y + 2, kl.dial, kl.pos, kl.pos);
        center(y + 5, "W/S: change digit   Enter: confirm digit");
        center(y + 6, "Q: quit");
    } else {
        center(y,      "=== UNLOCKED ===");
        center(y + 2,  "   .-\"\"-.   ");
        center(y + 3,  "  /      \\  ");
        center(y + 4,  " .--------. ");
        center(y + 5,  " |  .--.  | ");
        center(y + 6,  " |  '--'  | ");
        center(y + 7,  " '--------' ");
        center(y + 9,  "New passcode (applied on lock):");
        draw_digits(y + 10, kl.draft, 0, kl.cur);
        center(y + 13, "W/S: change   A/D: move   R: reset to 0000");
        center(y + 14, "L: lock   E: exit   Q: quit");
    }

    /* Status line at bottom, like cave_exp's UI bar */
    printf("\033[%d;1H", kl.height + 1);
    printf("\033[48;5;0m\033[38;5;7m%s\033[0m", kl.msg);
    fflush(stdout);
}

static void game_handle_input(int ch) {
    if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';   /* case-insensitive */
    kl.msg = "";

    if (ch == 'q') { kl.running = 0; return; }

    switch (kl.state) {

    case STATE_LOCKED:
        switch (ch) {
        case 'w': kl.dial[kl.pos] = inc(kl.dial[kl.pos]); break;
        case 's': kl.dial[kl.pos] = dec(kl.dial[kl.pos]); break;
        case '\n':
        case '\r':
            if (kl.dial[kl.pos] == passcode[kl.pos]) {
                kl.pos++;
                if (kl.pos == DIGITS) {
                    enter_unlocked();
                    kl.msg = "Unlocked!";
                } else {
                    kl.msg = "Digit accepted.";
                }
            } else {
                kl.pos = 0;     /* wrong: back to the first digit */
                kl.msg = "Wrong digit - start over.";
            }
            break;
        }
        break;

    case STATE_UNLOCKED:
        switch (ch) {
        case 'w': kl.draft[kl.cur] = inc(kl.draft[kl.cur]); break;
        case 's': kl.draft[kl.cur] = dec(kl.draft[kl.cur]); break;
        case 'a': if (kl.cur > 0)          kl.cur--; break;
        case 'd': if (kl.cur < DIGITS - 1) kl.cur++; break;
        case 'r':
            zero_digits(kl.draft);
            kl.msg = "Passcode reset to 0000.";
            break;
        case 'l':
            commit_draft();
            enter_locked();
            kl.msg = "Locked.";
            break;
        case 'e':
            commit_draft();
            kl.running = 0;     /* leave without quitting the program */
            break;
        }
        break;
    }
}

void cmd_keylock(int argc, char **argv) {
    (void)argc;
    (void)argv;

    log_add(LOG_NORMAL, "Starting keylock...");
    log_add(LOG_NORMAL, "W/S: digit | Enter: confirm | Q: quit");

    game_init();

    /* Main game loop */
    while (kl.running) {
        game_draw();

        int ch = getchar();
        if (ch != EOF) {
            game_handle_input(ch);
        }
    }

    game_cleanup();
}
