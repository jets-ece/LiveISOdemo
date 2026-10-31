#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <termios.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/sysinfo.h>

#include "src/ui.h"

/* Build with -DASCII_UI if your console font lacks box-drawing glyphs. */
#ifdef ASCII_UI
#define BH "-"
#define BV "|"
#define TL "+"
#define TR "+"
#define BL "+"
#define BR "+"
#else
#define BH "\xe2\x94\x80"
#define BV "\xe2\x94\x82"
#define TL "\xe2\x94\x8c"
#define TR "\xe2\x94\x90"
#define BL "\xe2\x94\x94"
#define BR "\xe2\x94\x98"
#endif

#define MAX_LINES 256
#define HIST_MAX  32
#define PROMPT    "PhoenixBoot # "

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */
int rows = 25, cols = 80;
int th_bg = 0, th_fg = 7;           /* current screen theme (256-color) */

static char logbuf[MAX_LINES][LINE_LEN];
static char logsty[MAX_LINES];              /* 0 normal, 1 bold (echo), 2 reverse (error) */
static int  log_n = 0;                      /* total lines ever pushed */
static int  scroll_off = 0;

static char inbuf[INPUT_MAX];
static int  inlen = 0, cur = 0;

static char hist[HIST_MAX][INPUT_MAX];
static int  hist_n = 0, hist_pos = -1;
static char draft[INPUT_MAX];

static struct termios saved_tio;

/* ------------------------------------------------------------------ */
/* Console setup                                                       */
/* ------------------------------------------------------------------ */
static void setup_console(void) {
    int fd = open("/dev/tty0", O_RDWR);
    if (fd < 0) fd = open("/dev/console", O_RDWR);
    if (fd < 0) return;

    setsid();
    ioctl(fd, TIOCSCTTY, 1);
    dup2(fd, 0);
    dup2(fd, 1);
    dup2(fd, 2);
    if (fd > 2) close(fd);
}

static void raw_mode(void) {
    struct termios t;
    if (tcgetattr(0, &saved_tio) < 0) return;
    t = saved_tio;
    t.c_iflag &= ~(IXON | ICRNL | INLCR | BRKINT | ISTRIP);
    t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &t);
}

static void query_size(void) {
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        rows = ws.ws_row;
        cols = ws.ws_col;
    }
    if (rows < 12) rows = 12;
    if (cols < 40) cols = 40;
}

/* ------------------------------------------------------------------ */
/* Log (scrollback) handling                                           */
/* ------------------------------------------------------------------ */
static void log_push(int style, const char *s, int len) {
    int i = log_n % MAX_LINES;
    if (len > LINE_LEN - 1) len = LINE_LEN - 1;
    memcpy(logbuf[i], s, len);
    logbuf[i][len] = '\0';
    logsty[i] = style;
    log_n++;
    scroll_off = 0;
}

static void log_piece(int style, const char *s, int len) {
    char tmp[LINE_LEN];
    int w = cols - 4;
    if (w < 20) w = 20;
    if (len > LINE_LEN - 1) len = LINE_LEN - 1;
    for (int i = 0; i < len; i++) {
        unsigned char c = s[i];
        tmp[i] = (c == '\t') ? ' ' : (c >= 32 && c < 127) ? c : '?';
    }
    if (len == 0) { log_push(style, "", 0); return; }
    for (int off = 0; off < len; off += w)
        log_push(style, tmp + off, (len - off > w) ? w : len - off);
}

void log_add(int style, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    const char *p = buf;
    for (;;) {
        const char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        log_piece(style, p, len);
        if (!nl) break;
        p = nl + 1;
        if (*p == '\0') break;
    }
}

void log_clear(void) {
    log_n = 0;
    scroll_off = 0;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */
static void go(int r, int c) { printf("\033[%d;%dH", r, c); }

static void theme_sgr(void) {
    printf("\033[0m\033[38;5;%d;48;5;%dm", th_fg, th_bg);
}

static void hline(int r, const char *l, const char *m, const char *rr) {
    go(r, 1);
    fputs(l, stdout);
    for (int i = 0; i < cols - 2; i++) fputs(m, stdout);
    fputs(rr, stdout);
}

static int out_inner(void) { return rows - 7; }

static void draw_title(void) {
    const char *t = " PhoenixBoot Live System ";
    int pad = (cols - (int)strlen(t)) / 2;
    go(1, 1);
    printf("\033[7m%*s%s%*s\033[27m", pad, "", t, cols - pad - (int)strlen(t), "");
    theme_sgr();
}

static void draw_output(void) {
    int inner = out_inner();
    int w = cols - 4;
    int end = log_n - scroll_off;
    for (int i = 0; i < inner; i++) {
        int idx = end - inner + i;
        go(3 + i, 1);
        fputs(BV " ", stdout);
        if (idx >= 0 && idx >= log_n - MAX_LINES && idx < log_n) {
            int k = idx % MAX_LINES;
            if (logsty[k] == 1) fputs("\033[1m", stdout);
            if (logsty[k] == 2) fputs("\033[7m", stdout);
            printf("%-*.*s", w, w, logbuf[k]);
            fputs("\033[22;27m", stdout);
        } else {
            printf("%*s", w, "");
        }
        fputs(" " BV, stdout);
    }
}

static void draw_status(void) {
    struct sysinfo si;
    long up = 0;
    if (sysinfo(&si) == 0) up = si.uptime;
    char buf[256];
    snprintf(buf, sizeof(buf),
             " help: commands | PgUp/PgDn: scroll | Ctrl-L: redraw | up %ldm%02lds%s",
             up / 60, up % 60, scroll_off ? " | [scrolled]" : "");
    go(rows, 1);
    printf("\033[7m%-*.*s\033[27m", cols, cols, buf);
    theme_sgr();
}

static void draw_input(void) {
    int pl = (int)strlen(PROMPT);
    int avail = cols - 4 - pl;
    if (avail < 4) avail = 4;
    int off = (cur >= avail) ? cur - avail + 1 : 0;

    go(rows - 2, 1);
    fputs(BV " \033[1m" PROMPT "\033[22m", stdout);
    printf("%-*.*s", avail, avail, inbuf + off);
    fputs(" " BV, stdout);
    go(rows - 2, 3 + pl + (cur - off));
    fputs("\033[?25h", stdout);
    fflush(stdout);
}

void draw_frame(void) {
    query_size();
    fputs("\033[?25l", stdout);
    theme_sgr();
    fputs("\033[2J", stdout);
    draw_title();
    hline(2, TL, BH, TR);
    hline(rows - 4, BL, BH, BR);
    hline(rows - 3, TL, BH, TR);
    hline(rows - 1, BL, BH, BR);
    draw_output();
    draw_status();
    draw_input();
}

void redraw_dynamic(void) {
    fputs("\033[?25l", stdout);
    draw_output();
    draw_status();
    draw_input();
}

/* ------------------------------------------------------------------ */
/* Command dispatch (command table lives in commands.c)                */
/* ------------------------------------------------------------------ */
static void run_line(const char *line) {
    char copy[INPUT_MAX];
    char *argv[16], *tok, *sp;
    int argc = 0;

    log_add(LOG_ECHO, "# %s", line);
    strncpy(copy, line, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';
    for (tok = strtok_r(copy, " \t", &sp); tok && argc < 15; tok = strtok_r(NULL, " \t", &sp))
        argv[argc++] = tok;
    if (argc == 0) return;
    argv[argc] = NULL;

    for (int i = 0; cmds[i].name; i++) {
        if (strcmp(argv[0], cmds[i].name) == 0) {
            cmds[i].fn(argc, argv);
            return;
        }
    }
    log_add(LOG_ERROR, "unknown command: %s (try 'help')", argv[0]);
}

/* ------------------------------------------------------------------ */
/* Keyboard input                                                      */
/* ------------------------------------------------------------------ */
enum { K_UP = 1000, K_DOWN, K_RIGHT, K_LEFT, K_HOME, K_END, K_DEL, K_PGUP, K_PGDN, K_ESC };

static int read_byte(int timeout_ms) {
    if (timeout_ms >= 0) {
        struct pollfd p = { .fd = 0, .events = POLLIN };
        if (poll(&p, 1, timeout_ms) <= 0) return -2;
    }
    unsigned char c;
    ssize_t n = read(0, &c, 1);
    return (n == 1) ? c : -1;
}

static int read_key(void) {
    int c = read_byte(-1);
    if (c != 27) return c;

    int c1 = read_byte(50);
    if (c1 < 0) return K_ESC;
    if (c1 == 'O') {
        int c2 = read_byte(50);
        return c2 == 'H' ? K_HOME : c2 == 'F' ? K_END : K_ESC;
    }
    if (c1 != '[') return K_ESC;

    int c2 = read_byte(50);
    if (c2 >= '0' && c2 <= '9') {
        int c3 = read_byte(50);
        if (c3 == '~') {
            switch (c2) {
            case '1': case '7': return K_HOME;
            case '3': return K_DEL;
            case '4': case '8': return K_END;
            case '5': return K_PGUP;
            case '6': return K_PGDN;
            }
        }
        return K_ESC;
    }
    switch (c2) {
    case 'A': return K_UP;
    case 'B': return K_DOWN;
    case 'C': return K_RIGHT;
    case 'D': return K_LEFT;
    case 'H': return K_HOME;
    case 'F': return K_END;
    }
    return K_ESC;
}

static void set_input(const char *s) {
    strncpy(inbuf, s, INPUT_MAX - 1);
    inbuf[INPUT_MAX - 1] = '\0';
    inlen = cur = (int)strlen(inbuf);
}

static void scroll_by(int delta) {
    int total = log_n < MAX_LINES ? log_n : MAX_LINES;
    int max = total - out_inner();
    if (max < 0) max = 0;
    scroll_off += delta;
    if (scroll_off > max) scroll_off = max;
    if (scroll_off < 0) scroll_off = 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(void) {
    mkdir("/proc", 0755);
    mkdir("/sys", 0755);
    mkdir("/dev", 0755);

    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);

    setup_console();
    setvbuf(stdout, NULL, _IOFBF, 1 << 16);
    raw_mode();

    query_size();
    log_add(LOG_NORMAL, "Welcome to Minimal PhoenixBoot Live System!");
    log_add(LOG_NORMAL, "Type 'help' to list commands. Try: color blue");
    log_add(LOG_NORMAL, "%s", "");
    draw_frame();

    for (;;) {
        int k = read_key();
        if (k == -1) { sleep(1); continue; }   /* don't spin or exit PID 1 */
        if (k == -2) continue;

        switch (k) {
        case '\r': case '\n':
            if (inlen > 0) {
                if (hist_n == 0 || strcmp(hist[(hist_n - 1) % HIST_MAX], inbuf) != 0) {
                    strcpy(hist[hist_n % HIST_MAX], inbuf);
                    hist_n++;
                }
                char line[INPUT_MAX];
                strcpy(line, inbuf);
                inbuf[0] = '\0'; inlen = cur = 0; hist_pos = -1;
                run_line(line);
            }
            break;
        case 127: case 8:
            if (cur > 0) {
                memmove(inbuf + cur - 1, inbuf + cur, inlen - cur + 1);
                cur--; inlen--;
            }
            break;
        case K_DEL:
            if (cur < inlen) {
                memmove(inbuf + cur, inbuf + cur + 1, inlen - cur);
                inlen--;
            }
            break;
        case 3: case 21:                        /* Ctrl-C / Ctrl-U */
            inbuf[0] = '\0'; inlen = cur = 0; hist_pos = -1;
            break;
        case 12: draw_frame(); continue;        /* Ctrl-L */
        case 1: case K_HOME: cur = 0; break;
        case 5: case K_END:  cur = inlen; break;
        case K_LEFT:  if (cur > 0) cur--; break;
        case K_RIGHT: if (cur < inlen) cur++; break;
        case K_UP:
            if (hist_n > 0 && hist_pos < hist_n - 1 && hist_pos < HIST_MAX - 1) {
                if (hist_pos == -1) strcpy(draft, inbuf);
                hist_pos++;
                set_input(hist[(hist_n - 1 - hist_pos) % HIST_MAX]);
            }
            break;
        case K_DOWN:
            if (hist_pos >= 0) {
                hist_pos--;
                set_input(hist_pos == -1 ? draft : hist[(hist_n - 1 - hist_pos) % HIST_MAX]);
            }
            break;
        case K_PGUP: scroll_by(out_inner() / 2); break;
        case K_PGDN: scroll_by(-(out_inner() / 2)); break;
        default:
            if (k >= 32 && k < 127 && inlen < INPUT_MAX - 1) {
                memmove(inbuf + cur + 1, inbuf + cur, inlen - cur + 1);
                inbuf[cur++] = (char)k;
                inlen++;
            }
            break;
        }
        redraw_dynamic();
    }
}
