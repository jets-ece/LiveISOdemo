#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "ui.h"
#include <time.h>

void cmd_grades(int argc, char **argv);

#define NUM_GRADES 5
#define REEL_LEN 40
#define CARD_STEP 6
#define STRIP_WIDTH 41

static const char grade_chars[NUM_GRADES] = {'A', 'B', 'C', 'D', 'F'};
static const char *grade_colors[NUM_GRADES] = {
    "\033[1;32m", "\033[1;36m", "\033[1;33m", "\033[1;35m", "\033[1;31m"
};
static const char *grade_msgs[NUM_GRADES] = {
    "Top of the class!",
    "Solid work.",
    "Average. Could be worse.",
    "Barely passing... ouch.",
    "Failed. Enroll again?"
};

struct subject {
    const char *name;
    int chance[NUM_GRADES];   /* percent for A, B, C, D, F; sums to 100 */
};

static const struct subject subjects[] = {
    {"English",     {80, 10, 5,  3, 2}},
    {"Electronics", { 1,  9, 10, 20, 60}},
};
#define NUM_SUBJECTS ((int)(sizeof subjects / sizeof subjects[0]))

/* Persists between runs: game_init never resets this. */
static int tally[NUM_SUBJECTS][NUM_GRADES];

enum state { ST_MENU, ST_ROLLING, ST_RESULT };

static struct {
    int width, height, running;
    enum state state;
    int selected;             /* menu cursor */
    int subject;              /* subject being gambled on */
    int reel[REEL_LEN];       /* grade indexes shown on the strip */
    int reel_pos;             /* index of the card under the marker */
    int result;               /* grade index rolled on stop */
} g = {0};

static void game_init(void);
static void game_draw(void);
static void game_handle_input(int ch);
static void game_cleanup(void);

static int to_lower(int ch) {
    return (ch >= 'A' && ch <= 'Z') ? ch - 'A' + 'a' : ch;
}

static void put(int row, int col, const char *s) {
    if (row < 1 || row > rows || col > cols) return;
    if (col < 1) col = 1;
    printf("\033[%d;%dH%s", row, col, s);
}

static void put_center(int row, const char *s) {
    int x = (g.width - (int)strlen(s)) / 2 + 1;
    if (x < 1) x = 1;
    put(row, x, s);
}

static void draw_card(int row, int x, int grade, int center) {
    char buf[64];
    snprintf(buf, sizeof buf, "%s%s[ %c ]\033[0m",
             grade_colors[grade], center ? "\033[7m" : "", grade_chars[grade]);
    put(row, x, buf);
}

static int mid_row(void) {
    int m = g.height / 2;
    return m < 7 ? 7 : m;
}

static int strip_x0(void) {
    int x = (g.width - STRIP_WIDTH) / 2 + 1;
    return x < 1 ? 1 : x;
}

static void draw_status(const char *hint) {
    if (rows < 1 || g.width < 1) return;
    printf("\033[%d;1H\033[7m%-*.*s\033[0m", rows, g.width, g.width, hint);
}

static int roll_grade(int subject) {
    int r = rand() % 100;
    int acc = 0;
    for (int i = 0; i < NUM_GRADES; i++) {
        acc += subjects[subject].chance[i];
        if (r < acc) return i;
    }
    return NUM_GRADES - 1;
}

static void enter_menu(void) {
    g.state = ST_MENU;
    g.reel_pos = 0;
    g.result = 0;
}

static void enter_rolling(void) {
    g.state = ST_ROLLING;
    g.subject = g.selected;
    for (int i = 0; i < REEL_LEN; i++)
        g.reel[i] = rand() % NUM_GRADES;
    g.reel_pos = 0;
}

static void enter_result(void) {
    char buf[96];
    g.result = roll_grade(g.subject);
    g.reel[g.reel_pos] = g.result;
    tally[g.subject][g.result]++;
    g.state = ST_RESULT;
    snprintf(buf, sizeof buf, "Grades: %s -> %c",
             subjects[g.subject].name, grade_chars[g.result]);
    log_add(LOG_NORMAL, "%s", buf);
}

static void draw_menu(void) {
    char line[96];
    int row;

    put_center(4, "Pick a subject to enroll in:");
    for (int i = 0; i < NUM_SUBJECTS; i++) {
        snprintf(line, sizeof line, "%s%s %-14s\033[0m",
                 i == g.selected ? "\033[7m" : "",
                 i == g.selected ? ">" : " ",
                 subjects[i].name);
        put(6 + i * 2, 10, line);
    }

    row = 6 + NUM_SUBJECTS * 2;
    snprintf(line, sizeof line, "Odds: A %d%%  B %d%%  C %d%%  D %d%%  F %d%%",
             subjects[g.selected].chance[0], subjects[g.selected].chance[1],
             subjects[g.selected].chance[2], subjects[g.selected].chance[3],
             subjects[g.selected].chance[4]);
    put(row + 1, 10, line);

    snprintf(line, sizeof line, "Your record: A:%d B:%d C:%d D:%d F:%d",
             tally[g.selected][0], tally[g.selected][1], tally[g.selected][2],
             tally[g.selected][3], tally[g.selected][4]);
    put(row + 3, 10, line);
}

static void draw_rolling(void) {
    char line[96];
    int mid = mid_row();
    int x0 = strip_x0();

    snprintf(line, sizeof line, "Enrolling in %s", subjects[g.subject].name);
    put_center(mid - 4, line);

    for (int k = 0; k < 7; k++) {
        int idx = (g.reel_pos + k - 3 + REEL_LEN * 2) % REEL_LEN;
        draw_card(mid, x0 + k * CARD_STEP, g.reel[idx], k == 3);
    }
    put(mid - 1, x0 + 20, "v");
    put_center(mid + 3, "Press g to stop the reel!");
}

static void draw_result(void) {
    char line[96];
    int mid = mid_row();
    int x = (g.width - 5) / 2 + 1;

    if (x < 1) x = 1;
    put_center(mid - 4, "Result:");
    draw_card(mid, x, g.result, 1);
    put_center(mid + 2, grade_msgs[g.result]);
    snprintf(line, sizeof line, "Enrolled in %s", subjects[g.subject].name);
    put_center(mid + 4, line);
}

static void game_draw(void) {
    const char *hint = "";

    printf("\033[0m\033[2J\033[H");
    put_center(2, "== CASE GRADES ==");

    switch (g.state) {
    case ST_MENU:
        draw_menu();
        hint = "w/s: move  Enter: enroll  e/q: leave";
        break;
    case ST_ROLLING:
        draw_rolling();
        hint = "g: stop  (other keys spin)  q: quit";
        break;
    case ST_RESULT:
        draw_result();
        hint = "Enter/g/b: back to menu  e/q: leave";
        break;
    }

    draw_status(hint);
    printf("\033[0m");
    fflush(stdout);
}

static void game_handle_input(int ch) {
    ch = to_lower(ch);

    if (ch == 'q' || ch == 'e') {
        g.running = 0;
        return;
    }

    switch (g.state) {
    case ST_MENU:
        switch (ch) {
        case 'w':
            g.selected = (g.selected + NUM_SUBJECTS - 1) % NUM_SUBJECTS;
            break;
        case 's':
            g.selected = (g.selected + 1) % NUM_SUBJECTS;
            break;
        case '\n':
        case '\r':
            enter_rolling();
            break;
        default:
            break;
        }
        break;

    case ST_ROLLING:
        if (ch == 'g')
            enter_result();
        else
            g.reel_pos = (g.reel_pos + 1) % REEL_LEN;
        break;

    case ST_RESULT:
        switch (ch) {
        case 'g':
        case 'b':
        case '\n':
        case '\r':
            enter_menu();
            break;
        default:
            break;
        }
        break;
    }
}

static void game_init(void) {
    static int seeded = 0;

    if (!seeded) {
        srand((unsigned)time(NULL));
        seeded = 1;
    }
    g.width = cols;
    g.height = rows - 2;
    g.running = 1;
    enter_menu();
    g.selected = 0;
    printf("\033[?25l");
    fflush(stdout);
}

static void game_cleanup(void) {
    printf("\033[0m\033[?25h\033[2J\033[H");
    fflush(stdout);
}

void cmd_grades(int argc, char **argv) {
    (void)argc;
    (void)argv;
    log_add(LOG_NORMAL, "Starting grades...");
    game_init();
    while (g.running) {
        game_draw();
        int ch = getchar();
        if (ch == EOF) break;
        game_handle_input(ch);
    }
    game_cleanup();
}

