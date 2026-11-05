#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "ui.h"

void cmd_rainheart(int argc, char **argv);

#define HEART_W 7
#define HEART_H 6
#define MAX_DROPS 160

enum state { ST_PLAY, ST_HELP };

struct drop {
    int x, y, speed;
};

static struct {
    int width, height, running;
    int state;
    struct drop drops[MAX_DROPS];
} g = {0};

/* Persists between runs: not reset by game_init(). */
static int heart_x = 10;
static int heart_y = 3;
static unsigned int run_seed = 12345u;

static const char *heart_art[HEART_H] = {
    " ## ## ",
    "#######",
    "#######",
    " ##### ",
    "  ###  ",
    "   #   "
};

static const char *help_lines[] = {
    "RAINHEART",
    "",
    "W A S D   move the heart",
    "H         toggle this help",
    "E         leave the screen saver",
    "Q         quit",
    "",
    "Press H or Enter to return"
};

static int to_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int clampi(int v, int lo, int hi)
{
    if (hi < lo)
        return lo;
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static int rand_range(int n)
{
    return n > 0 ? rand() % n : 0;
}

static void clamp_heart(void)
{
    heart_x = clampi(heart_x, 0, g.width - HEART_W);
    heart_y = clampi(heart_y, 0, g.height - HEART_H);
}

static void drop_respawn(struct drop *d)
{
    d->x = rand_range(g.width);
    d->y = -rand_range(6);
    d->speed = 1 + rand_range(2);
}

static void advance_rain(void)
{
    int i;

    for (i = 0; i < MAX_DROPS; i++) {
        struct drop *d = &g.drops[i];
        d->y += d->speed;
        if (d->y >= g.height)
            drop_respawn(d);
    }
}

static void enter_play(void)
{
    g.state = ST_PLAY;
    clamp_heart();
}

static void enter_help(void)
{
    g.state = ST_HELP;
}

static void draw_rain(void)
{
    int i;

    for (i = 0; i < MAX_DROPS; i++) {
        const struct drop *d = &g.drops[i];
        if (d->x >= 0 && d->x < g.width && d->y >= 0 && d->y < g.height)
            printf("\033[%d;%dH\033[97m|\033[0m", d->y + 1, d->x + 1);
    }
}

static void draw_heart(void)
{
    int r, c;

    clamp_heart();
    for (r = 0; r < HEART_H; r++) {
        for (c = 0; c < HEART_W; c++) {
            if (heart_art[r][c] == '#')
                printf("\033[%d;%dH\033[1;31m#", heart_y + r + 1, heart_x + c + 1);
        }
    }
    printf("\033[0m");
}

static void draw_help(void)
{
    int n = (int)(sizeof(help_lines) / sizeof(help_lines[0]));
    int i;
    int start = clampi((g.height - n) / 2 + 1, 1, g.height);

    for (i = 0; i < n; i++) {
        int len = (int)strlen(help_lines[i]);
        int x = clampi((g.width - len) / 2 + 1, 1, g.width);
        printf("\033[%d;%dH\033[97m%s\033[0m", start + i, x, help_lines[i]);
    }
}

static void draw_status(void)
{
    char buf[160];
    int len, x, row;

    if (g.state == ST_HELP)
        snprintf(buf, sizeof(buf), "Help: H or Enter returns  E leave  Q quit");
    else
        snprintf(buf, sizeof(buf), "WASD move  H help  E leave  Q quit");

    len = (int)strlen(buf);
    if (len > g.width)
        len = g.width;
    x = clampi((g.width - len) / 2 + 1, 1, g.width);
    row = g.height + 2;

    printf("\033[%d;%dH\033[7m%.*s\033[0m", row, x, len, buf);
}

static void game_init(void)
{
    int i;

    g.width = cols;
    g.height = rows - 2;
    if (g.width < 1)
        g.width = 1;
    if (g.height < 1)
        g.height = 1;
    g.running = 1;
    g.state = ST_PLAY;

    srand(run_seed);
    run_seed = run_seed * 1103515245u + 12345u;

    for (i = 0; i < MAX_DROPS; i++) {
        drop_respawn(&g.drops[i]);
        g.drops[i].y = rand_range(g.height);
    }

    clamp_heart();
    printf("\033[?25l");
    fflush(stdout);
}

static void game_draw(void)
{
    printf("\033[0m\033[2J");
    if (g.state == ST_HELP) {
        draw_help();
    } else {
        draw_rain();
        draw_heart();
    }
    draw_status();
    fflush(stdout);
}

static void game_handle_input(int ch)
{
    int k = to_lower(ch);

    if (k == 'q') {
        g.running = 0;
        return;
    }

    switch (g.state) {
    case ST_PLAY:
        switch (k) {
        case 'w': heart_y -= 1; break;
        case 's': heart_y += 1; break;
        case 'a': heart_x -= 1; break;
        case 'd': heart_x += 1; break;
        case 'h': enter_help(); break;
        case 'e': g.running = 0; break;
        default: break;
        }
        clamp_heart();
        advance_rain();
        break;
    case ST_HELP:
        switch (k) {
        case 'h':
        case '\n':
        case '\r': enter_play(); break;
        case 'e': g.running = 0; break;
        default: break;
        }
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

void cmd_rainheart(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    log_add(LOG_NORMAL, "Starting rainheart...");
    game_init();
    while (g.running) {
        game_draw();
        int ch = getchar();
        if (ch == EOF)
            break;
        game_handle_input(ch);
    }
    game_cleanup();
}

