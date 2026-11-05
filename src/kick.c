#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "ui.h"

void cmd_kick(int argc, char **argv);

/* ---- Sprites (edit freely; rows may differ in length, spaces are transparent) ---- */
#define SPR_W 5
#define SPR_H 5
#define GOAL_W 9
#define GOAL_H 5

static const char *const run_leg1[SPR_H] = {
    "#####",
    "#   #",
    "# 1 #",
    "#   #",
    "#####"
};

static const char *const run_leg2[SPR_H] = {
    "#####",
    "#   #",
    "# 2 #",
    "#   #",
    "#####"
};

static const char *const goal_bm[GOAL_H] = {
    "+-------+",
    "|#|#|#|#|",
    "|#|#|#|#|",
    "|#|#|#|#|",
    "+-------+"
};

/* ---- Tuning ---- */
#define BALL_DIST   7      /* constant distance from player's left edge to ball */
#define KICK_ZONE   14     /* popup appears when this close to the goal */
#define KICK_FRAMES 12     /* animation frames for the ball flight */
#define FRAME_NS    60000000L

enum { S_TITLE, S_RUN, S_KICK, S_GOAL, S_FOUL };
enum { F_NONE, F_RAN_INTO_GOAL, F_FALSE_START };

/* Persistent across runs: not touched by game_init */
static long best_ms = -1;
static int goals_scored = 0;
static int fouls_total = 0;

static struct {
    int width, height, running;
    int state;
    int player_x, goal_x, ground_y;
    int leg;
    int popup_on;
    long popup_t0;
    long reaction_ms;
    int foul_reason;
    int ball_x, ball_y;
    const char *msg;
} g = {0};

/* ---- Helpers ---- */
static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void sleep_frame(void)
{
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = FRAME_NS;
    nanosleep(&ts, NULL);
}

static void put_char(int row, int col, char ch)
{
    if (row < 1 || row > g.height + 2 || col < 1 || col > g.width)
        return;
    printf("\033[%d;%dH%c", row, col, ch);
}

static void draw_bitmap(const char *const bm[], int h, int x, int y, const char *color)
{
    printf("%s", color);
    for (int r = 0; r < h; r++) {
        int len = (int)strlen(bm[r]);
        for (int c = 0; c < len; c++) {
            if (bm[r][c] != ' ')
                put_char(y + r, x + c, bm[r][c]);
        }
    }
    printf("\033[0m");
}

static void draw_center(int row, const char *s)
{
    int x = (g.width - (int)strlen(s)) / 2 + 1;
    if (x < 1) x = 1;
    if (row < 1) row = 1;
    if (row > g.height + 2) row = g.height + 2;
    printf("\033[%d;%dH%s", row, x, s);
}

static const char *rate_reaction(long ms)
{
    if (ms < 250) return "Lightning";
    if (ms < 400) return "Quick";
    if (ms < 600) return "Average";
    return "Slow";
}

/* ---- State transitions ---- */
static void reset_field(void)
{
    g.player_x = 3;
    g.leg = 0;
    g.popup_on = 0;
    g.ball_x = g.player_x + BALL_DIST;
    g.ball_y = g.ground_y;
}

static void enter_title(void)
{
    g.state = S_TITLE;
    reset_field();
    g.msg = "Press S or ENTER to start.";
}

static void enter_run(void)
{
    g.state = S_RUN;
    reset_field();
    g.reaction_ms = -1;
    g.foul_reason = F_NONE;
    g.msg = "Mash G to run!";
}

static void enter_goal(void)
{
    g.state = S_GOAL;
    goals_scored++;
    if (best_ms < 0 || g.reaction_ms < best_ms)
        best_ms = g.reaction_ms;
    g.msg = "Goal scored! R retry, M menu.";
}

static void enter_foul(int reason)
{
    g.state = S_FOUL;
    g.foul_reason = reason;
    g.popup_on = 0;
    fouls_total++;
    g.msg = "Foul! R retry, M menu.";
}

/* ---- Drawing ---- */
static void game_draw(void)
{
    char line[256];
    int top_y = g.ground_y - SPR_H + 1;
    int dist = g.goal_x - g.player_x;

    printf("\033[H\033[2J");

    printf("\033[32m");
    for (int c = 1; c <= g.width; c++)
        put_char(g.ground_y + 1, c, '-');
    printf("\033[0m");

    draw_bitmap(goal_bm, GOAL_H, g.goal_x, g.ground_y - GOAL_H + 1, "\033[37m");

    printf("\033[1;33m");
    put_char(g.ball_y, g.ball_x, 'O');
    printf("\033[0m");

    draw_bitmap(g.leg ? run_leg2 : run_leg1, SPR_H, g.player_x, top_y, "\033[1;36m");

    if (g.popup_on) {
        printf("\033[1;7;33m");
        draw_center(top_y - 2, "K to KICK");
        printf("\033[0m");
    }

    switch (g.state) {
    case S_TITLE:
        printf("\033[1;32m");
        draw_center(2, "RUN-UP KICK");
        printf("\033[0m");
        draw_center(4, "Mash G to run toward the goal.");
        draw_center(5, "When K to KICK appears, press K fast!");
        draw_center(6, "Running into the goal is a foul.");
        break;
    case S_GOAL:
        printf("\033[1;32m");
        draw_center(3, "GOAL!");
        printf("\033[0m");
        snprintf(line, sizeof line, "Reaction: %ld ms (%s)",
                 g.reaction_ms, rate_reaction(g.reaction_ms));
        draw_center(4, line);
        break;
    case S_FOUL:
        printf("\033[1;31m");
        draw_center(3, "FOUL!");
        printf("\033[0m");
        draw_center(4, g.foul_reason == F_RAN_INTO_GOAL
                           ? "You ran into the goal."
                           : "False start: K was pressed before the popup.");
        break;
    default:
        break;
    }

    if (best_ms >= 0)
        snprintf(line, sizeof line,
                 " Distance: %3d  Best: %ld ms  Goals: %d  Fouls: %d  %s",
                 dist, best_ms, goals_scored, fouls_total, g.msg);
    else
        snprintf(line, sizeof line,
                 " Distance: %3d  Best: --  Goals: %d  Fouls: %d  %s",
                 dist, goals_scored, fouls_total, g.msg);
    printf("\033[0m\033[%d;1H\033[2K\033[7m%.*s\033[0m", g.height + 1, g.width, line);

    printf("\033[%d;1H\033[2K", g.height + 2);
    switch (g.state) {
    case S_TITLE:
        printf("S/ENTER start   Q quit");
        break;
    case S_RUN:
        printf("G run   K kick (on popup)   Q quit");
        break;
    case S_KICK:
        printf("Kick!");
        break;
    default:
        printf("R retry   M menu   Q quit");
        break;
    }

    printf("\033[0m");
    fflush(stdout);
}

/* ---- Kick animation (blocking, plays out inside the key handler) ---- */
static void animate_kick(void)
{
    int sx = g.ball_x;
    int sy = g.ball_y;
    int tx = g.goal_x + GOAL_W / 2;
    int ty = g.ground_y - 1;

    g.state = S_KICK;
    g.msg = "KICK!";

    for (int i = 1; i <= KICK_FRAMES; i++) {
        int arc = (i * (KICK_FRAMES - i)) / 9;
        g.ball_x = sx + (tx - sx) * i / KICK_FRAMES;
        g.ball_y = sy + (ty - sy) * i / KICK_FRAMES - arc;
        if (g.ball_y < 1)
            g.ball_y = 1;
        game_draw();
        sleep_frame();
    }
    enter_goal();
}

/* ---- Input actions ---- */
static void run_step(void)
{
    int step = 1 + rand() % 4;   /* random speed per press */

    g.player_x += step;
    g.leg = !g.leg;
    g.ball_x = g.player_x + BALL_DIST;
    g.ball_y = g.ground_y;

    if (g.player_x + SPR_W >= g.goal_x) {
        enter_foul(F_RAN_INTO_GOAL);
        return;
    }

    if (!g.popup_on && g.goal_x - g.player_x <= KICK_ZONE) {
        g.popup_on = 1;
        g.popup_t0 = now_ms();
        g.msg = "Popup! Press K now!";
    } else if (!g.popup_on) {
        g.msg = "Keep mashing G!";
    }
}

static void kick_pressed(void)
{
    if (!g.popup_on) {
        enter_foul(F_FALSE_START);
        return;
    }
    g.reaction_ms = now_ms() - g.popup_t0;
    g.popup_on = 0;
    animate_kick();
}

static void game_handle_input(int ch)
{
    if (ch >= 'A' && ch <= 'Z')
        ch += 'a' - 'A';

    if (ch == 'q' || ch == 'e') {
        g.running = 0;
        return;
    }

    switch (g.state) {
    case S_TITLE:
        if (ch == 's' || ch == '\n' || ch == '\r')
            enter_run();
        break;
    case S_RUN:
        if (ch == 'g')
            run_step();
        else if (ch == 'k')
            kick_pressed();
        break;
    case S_GOAL:
    case S_FOUL:
        if (ch == 'r')
            enter_run();
        else if (ch == 'm')
            enter_title();
        break;
    default:
        break;
    }
}

static void game_cleanup(void)
{
    printf("\033[0m\033[?25h\033[2J\033[H");
    fflush(stdout);
}

static void game_init(void)
{
    g.width = cols;
    g.height = rows - 2;
    g.running = 1;
    g.ground_y = g.height - 3;
    g.goal_x = g.width - GOAL_W - 4;
    srand((unsigned)time(NULL));
    printf("\033[?25l");
    enter_title();
}

void cmd_kick(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    log_add(LOG_NORMAL, "Starting kick...");
    game_init();
    while (g.running) {
        game_draw();
        int ch = getchar();
        if (ch == EOF) break;
        game_handle_input(ch);
    }
    game_cleanup();
}

