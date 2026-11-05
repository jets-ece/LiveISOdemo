#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include "ui.h"

void cmd_esp32fsm(int argc, char **argv);

#define DEV_PATH "/dev/ttyUSB0"
#define LOG_MAX 12
#define LINE_MAX_LEN 80
#define FSM_INPUT_MAX 60
#define STATE_MAX 24

enum { MODE_NORMAL, MODE_TYPE };

static struct {
    int width, height, running;
    int state;
    char input[FSM_INPUT_MAX + 1];
    int input_len;
} g = {0};

/* Persistent across runs */
static int serial_fd = -1;
static char fsm_state[STATE_MAX] = "UNKNOWN";
static char reply_log[LOG_MAX][LINE_MAX_LEN];
static int reply_count = 0;

/* Forward declarations */
static void game_init(void);
static void game_draw(void);
static void game_handle_input(int ch);
static void game_cleanup(void);
static void send_line(const char *s);
static void read_reply(void);

static void log_reply(const char *s) {
    if (reply_count == LOG_MAX) {
        memmove(reply_log[0], reply_log[1], (LOG_MAX - 1) * LINE_MAX_LEN);
        reply_count--;
    }
    snprintf(reply_log[reply_count], LINE_MAX_LEN, "%s", s);
    reply_count++;
}

static int lower_key(int ch) {
    return (ch >= 'A' && ch <= 'Z') ? ch + ('a' - 'A') : ch;
}

static void enter_normal(void) {
    g.state = MODE_NORMAL;
    g.input_len = 0;
    g.input[0] = '\0';
}

static void enter_type(void) {
    g.state = MODE_TYPE;
    g.input_len = 0;
    g.input[0] = '\0';
}

static void send_line(const char *s) {
    char buf[LINE_MAX_LEN];
    if (serial_fd < 0) {
        log_reply("Not connected (press o)");
        return;
    }
    snprintf(buf, sizeof buf, "> %s", s);
    log_reply(buf);

    int len = snprintf(buf, sizeof buf, "%s\n", s);
    ssize_t written = write(serial_fd, buf, len);
    (void)written;
}

/* Returns 1 with a line in buf, or 0 on timeout/error/no port. */
static int read_line(char *buf, int n) {
    int len = 0;
    char c = 0;
    if (serial_fd < 0) {
        buf[0] = '\0';
        return 0;
    }
    while (len < n - 1) {
        ssize_t r = read(serial_fd, &c, 1);
        if (r <= 0) break; /* Read timeout (VTIME) or error */
        if (c == '\r') continue;
        if (c == '\n') break;
        buf[len++] = c;
    }
    buf[len] = '\0';
    return (len > 0);
}

/* Reads lines until a STATE line arrives, logging everything seen. */
static void read_reply(void) {
    char line[LINE_MAX_LEN];
    char out[LINE_MAX_LEN];
    int timeouts = 0;
    int lines = 0;
    while (timeouts < 3 && lines < 10) {
        if (!read_line(line, sizeof line)) {
            timeouts++;
            continue;
        }
        if (line[0] == '\0') {
            continue;
        }
        lines++;
        snprintf(out, sizeof out, "< %s", line);
        log_reply(out);
        if (strncmp(line, "STATE ", 6) == 0) {
            snprintf(fsm_state, sizeof fsm_state, "%s", line + 6);
            return;
        }
    }
}

static void port_open(void) {
    if (serial_fd >= 0) {
        return;
    }

    serial_fd = open(DEV_PATH, O_RDWR | O_NOCTTY | O_NDELAY);
    if (serial_fd < 0) {
        log_reply("Could not open " DEV_PATH);
        return;
    }

    /* Configure line control directly via termios */
    struct termios options;
    tcgetattr(serial_fd, &options);

    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);

    /* 8N1 raw mode */
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_cflag |= (CLOCAL | CREAD);

    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_oflag &= ~OPOST;

    /* Read timeout = 2.0s (20 * 0.1s) */
    options.c_cc[VMIN]  = 0;
    options.c_cc[VTIME] = 20;

    tcflush(serial_fd, TCIFLUSH);
    tcsetattr(serial_fd, TCSANOW, &options);

    /* Restore blocking reads with VTIME timeout */
    fcntl(serial_fd, F_SETFL, 0);

    log_reply("Port opened, syncing...");
    send_line("STATUS");
    read_reply();
}

static void put_at(int row, int col, const char *s) {
    int r = row;
    int c = col;
    int avail;
    if (r < 1) {
        r = 1;
    }
    if (r > rows) {
        r = rows;
    }
    if (c < 1) {
        c = 1;
    }
    avail = cols - c + 1;
    if (avail < 1) {
        return;
    }
    printf("\033[%d;%dH%.*s", r, c, avail, s);
}

static void put_center(int row, const char *s) {
    int x = (g.width - (int)strlen(s)) / 2 + 1;
    if (x < 1) {
        x = 1;
    }
    put_at(row, x, s);
}

static int state_color(void) {
    if (strcmp(fsm_state, "RUNNING") == 0) {
        return 32;
    }
    if (strcmp(fsm_state, "PAUSED") == 0) {
        return 33;
    }
    if (strcmp(fsm_state, "ERROR") == 0) {
        return 31;
    }
    if (strcmp(fsm_state, "IDLE") == 0) {
        return 36;
    }
    return 37;
}

static void game_init(void) {
    g.width = cols;
    g.height = rows - 2;
    g.running = 1;
    enter_normal();
    printf("\033[?25l");
    fflush(stdout);
    if (serial_fd < 0) {
        port_open();
    }
}

static void game_draw(void) {
    char buf[160];
    int max_lines;
    int first;
    int i;

    printf("\033[H\033[2J\033[0m");

    printf("\033[1;36m");
    put_center(1, "ESP32 FSM Console");
    printf("\033[0m");

    snprintf(buf, sizeof buf, "Link: %s", serial_fd >= 0 ? DEV_PATH : "disconnected");
    put_at(3, 3, buf);

    put_at(4, 3, "FSM state: ");
    printf("\033[1;%dm%s\033[0m", state_color(), fsm_state);

    put_at(6, 3, "Replies:");
    max_lines = g.height - 7;
    if (max_lines < 0) {
        max_lines = 0;
    }
    first = reply_count - max_lines;
    if (first < 0) {
        first = 0;
    }
    for (i = first; i < reply_count; i++) {
        put_at(7 + (i - first), 3, reply_log[i]);
    }

    if (g.state == MODE_TYPE) {
        snprintf(buf, sizeof buf, "> %s_", g.input);
        put_at(rows - 1, 3, buf);
        printf("\033[%d;1H\033[7m %s \033[0m",
               rows, "Enter:send  Esc:cancel  Backspace:delete");
    } else {
        put_at(rows - 1, 3, "Press c to type a raw command line");
        printf("\033[%d;1H\033[7m %s \033[0m",
               rows, "s:start p:pause r:resume x:reset t:status c:type o:connect q:quit");
    }

    fflush(stdout);
}

static void game_handle_input(int ch) {
    switch (g.state) {
    case MODE_NORMAL:
        switch (lower_key(ch)) {
        case 's':
            send_line("START");
            read_reply();
            break;
        case 'p':
            send_line("PAUSE");
            read_reply();
            break;
        case 'r':
            send_line("RESUME");
            read_reply();
            break;
        case 'x':
            send_line("RESET");
            read_reply();
            break;
        case 't':
            send_line("STATUS");
            read_reply();
            break;
        case 'c':
            enter_type();
            break;
        case 'o':
            port_open();
            break;
        case 'q':
        case 'e':
            g.running = 0;
            break;
        default:
            break;
        }
        break;
    case MODE_TYPE:
        if (ch == '\n' || ch == '\r') {
            if (g.input_len > 0) {
                send_line(g.input);
                read_reply();
            }
            enter_normal();
        } else if (ch == 27) {
            enter_normal();
        } else if (ch == 127 || ch == 8) {
            if (g.input_len > 0) {
                g.input_len--;
                g.input[g.input_len] = '\0';
            }
        } else if (ch >= 32 && ch < 127 && g.input_len < FSM_INPUT_MAX) {
            g.input[g.input_len++] = (char)ch;
            g.input[g.input_len] = '\0';
        }
        break;
    default:
        enter_normal();
        break;
    }
}

static void game_cleanup(void) {
    printf("\033[0m\033[?25h\033[2J\033[H");
    fflush(stdout);
}

void cmd_esp32fsm(int argc, char **argv) {
    (void)argc;
    (void)argv;
    log_add(LOG_NORMAL, "Starting esp32fsm...");
    game_init();
    while (g.running) {
        game_draw();
        int ch = getchar();
        if (ch == EOF) {
            break;
        }
        game_handle_input(ch);
    }
    game_cleanup();
}
