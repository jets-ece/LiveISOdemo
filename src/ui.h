#ifndef UI_H
#define UI_H

/* ------------------------------------------------------------------ */
/* Shared definitions between init.c, commands.c and color.c           */
/* ------------------------------------------------------------------ */

#define INPUT_MAX 256
#define LINE_LEN  512

/* Output line styles for log_add() */
enum { LOG_NORMAL = 0, LOG_ECHO = 1, LOG_ERROR = 2 };

/* Screen size (updated on every draw_frame) */
extern int rows, cols;

/* Current screen theme, 256-color palette indexes */
extern int th_bg, th_fg;

/* ---- provided by init.c (core UI) ---- */
void log_add(int style, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void log_clear(void);
void draw_frame(void);         /* full repaint (use after changing the theme) */
void redraw_dynamic(void);     /* repaint output window, status bar, input box */

/* ---- command table (defined in commands.c) ---- */
typedef void (*cmd_fn)(int argc, char **argv);
struct cmd {
    const char *name;
    const char *help;
    cmd_fn      fn;
};
extern const struct cmd cmds[];     /* terminated by { NULL, NULL, NULL } */

/* ---- provided by color.c ---- */
void cmd_color(int argc, char **argv);
void color_set(int bg, int fg);     /* set + repaint; callable from game code */
int  color_parse(const char *s);    /* name or 0-255 -> palette index, -1 on error */
int  color_auto_fg(int bg);         /* readable text color for a background */

void cmd_cave_exp(int argc, char **argv);
#endif
