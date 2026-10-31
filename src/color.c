/*
 * color.c - the "color" command (screen color changing for the game).
 *
 * Everything color related lives here so it can be extended:
 *   - add names to color_names[]
 *   - add sub-commands in cmd_color()
 *   - call color_set() from other game code to recolor the screen
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "ui.h"

static const struct { const char *name; int code; } color_names[] = {
    {"black", 0}, {"red", 1}, {"green", 2}, {"yellow", 3}, {"blue", 4},
    {"magenta", 5}, {"cyan", 6}, {"white", 7}, {"gray", 8}, {"grey", 8},
    {"orange", 208}, {"pink", 213}, {"purple", 93}, {"brown", 94},
    {"lime", 118}, {"navy", 17}, {"teal", 30},
};
#define NCOLORS ((int)(sizeof(color_names) / sizeof(color_names[0])))

int color_parse(const char *s) {
    for (int i = 0; i < NCOLORS; i++)
        if (strcasecmp(s, color_names[i].name) == 0) return color_names[i].code;
    char *end;
    long v = strtol(s, &end, 10);
    if (*s && *end == '\0' && v >= 0 && v <= 255) return (int)v;
    return -1;
}

/* Pick black or white text depending on how light the background is. */
int color_auto_fg(int bg) {
    if (bg < 16) {
        static const int light[16] = {0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1};
        return light[bg] ? 0 : 7;
    }
    if (bg >= 232) return (bg >= 244) ? 0 : 7;
    int v = bg - 16;
    int b = v % 6, g = (v / 6) % 6, r = v / 36;
    int lum = r * 30 + g * 59 + b * 11;          /* 0..500 */
    return (lum > 200) ? 0 : 7;
}

void color_set(int bg, int fg) {
    th_bg = bg;
    th_fg = fg;
    draw_frame();
}

static void color_usage(void) {
    log_add(LOG_NORMAL, "usage: color <bg> [fg]   |   color reset");
    log_add(LOG_NORMAL, "  colors are names or 0-255 (256-color palette)");
    char line[256] = "  names: ";
    for (int i = 0; i < NCOLORS; i++) {
        if (strlen(line) + strlen(color_names[i].name) + 2 > (size_t)(cols - 6)) {
            log_add(LOG_NORMAL, "%s", line);
            strcpy(line, "         ");
        }
        strcat(line, color_names[i].name);
        strcat(line, " ");
    }
    log_add(LOG_NORMAL, "%s", line);
}

void cmd_color(int argc, char **argv) {
    if (argc < 2) { color_usage(); return; }

    if (strcmp(argv[1], "reset") == 0) {
        color_set(0, 7);
        log_add(LOG_NORMAL, "Screen color reset.");
        return;
    }

    int bg = color_parse(argv[1]);
    if (bg < 0) { log_add(LOG_ERROR, "color: unknown color '%s'", argv[1]); return; }

    int fg = (argc > 2) ? color_parse(argv[2]) : color_auto_fg(bg);
    if (fg < 0) { log_add(LOG_ERROR, "color: unknown color '%s'", argv[2]); return; }

    color_set(bg, fg);
    log_add(LOG_NORMAL, "Screen color set (bg=%d fg=%d).", bg, fg);
}
