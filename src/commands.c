/*
 * commands.c - all built-in shell commands except "color" (see color.c).
 *
 * To add a command: write a cmd_xxx(int argc, char **argv) function and
 * add a line to the cmds[] table at the bottom.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/reboot.h>
#include "ui.h"

static void cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    log_add(LOG_NORMAL, "Available commands:");
    for (int i = 0; cmds[i].name; i++)
        log_add(LOG_NORMAL, "  %-9s %s", cmds[i].name, cmds[i].help);
}

static void cmd_clear(int argc, char **argv) {
    (void)argc; (void)argv;
    log_clear();
}

static void cmd_echo(int argc, char **argv) {
    char buf[INPUT_MAX] = "";
    for (int i = 1; i < argc; i++) {
        if (i > 1) strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, argv[i], sizeof(buf) - strlen(buf) - 1);
    }
    log_add(LOG_NORMAL, "%s", buf);
}

static void cmd_uname(int argc, char **argv) {
    (void)argc; (void)argv;
    struct utsname u;
    if (uname(&u) < 0) { log_add(LOG_ERROR, "uname: %s", strerror(errno)); return; }
    log_add(LOG_NORMAL, "%s %s %s %s", u.sysname, u.nodename, u.release, u.machine);
}

static void cmd_uptime(int argc, char **argv) {
    (void)argc; (void)argv;
    struct sysinfo si;
    if (sysinfo(&si) < 0) { log_add(LOG_ERROR, "uptime: %s", strerror(errno)); return; }
    long s = si.uptime;
    log_add(LOG_NORMAL, "up %ldh %02ldm %02lds", s / 3600, (s / 60) % 60, s % 60);
}

static void cmd_free(int argc, char **argv) {
    (void)argc; (void)argv;
    struct sysinfo si;
    if (sysinfo(&si) < 0) { log_add(LOG_ERROR, "free: %s", strerror(errno)); return; }
    unsigned long u = si.mem_unit ? si.mem_unit : 1;
    unsigned long tot = si.totalram * u >> 20, fr = si.freeram * u >> 20;
    log_add(LOG_NORMAL, "total %lu MiB   used %lu MiB   free %lu MiB   procs %u",
            tot, tot - fr, fr, si.procs);
}

static void cmd_date(int argc, char **argv) {
    (void)argc; (void)argv;
    time_t t = time(NULL);
    char b[64];
    strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S UTC", gmtime(&t));
    log_add(LOG_NORMAL, "%s", b);
}

static void cmd_ls(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "/";
    struct dirent **list;
    int n = scandir(path, &list, NULL, alphasort);
    if (n < 0) { log_add(LOG_ERROR, "ls: %s: %s", path, strerror(errno)); return; }

    char line[LINE_LEN] = "";
    int w = cols - 4;
    for (int i = 0; i < n; i++) {
        const char *nm = list[i]->d_name;
        if (strcmp(nm, ".") && strcmp(nm, "..")) {
            char item[300];
            snprintf(item, sizeof(item), "%s%s  ", nm, list[i]->d_type == DT_DIR ? "/" : "");
            if ((int)(strlen(line) + strlen(item)) > w) {
                log_add(LOG_NORMAL, "%s", line);
                line[0] = '\0';
            }
            strncat(line, item, sizeof(line) - strlen(line) - 1);
        }
        free(list[i]);
    }
    free(list);
    if (line[0]) log_add(LOG_NORMAL, "%s", line);
}

static void cmd_cat(int argc, char **argv) {
    if (argc < 2) { log_add(LOG_ERROR, "usage: cat <file>"); return; }
    FILE *f = fopen(argv[1], "r");
    if (!f) { log_add(LOG_ERROR, "cat: %s: %s", argv[1], strerror(errno)); return; }
    char line[LINE_LEN];
    int n = 0;
    while (fgets(line, sizeof(line), f) && n++ < 500)
        log_add(LOG_NORMAL, "%s", line);
    fclose(f);
}

static void cmd_redraw(int argc, char **argv) {
    (void)argc; (void)argv;
    draw_frame();
}

static void cmd_reboot(int argc, char **argv) {
    (void)argc; (void)argv;
    log_add(LOG_NORMAL, "Rebooting...");
    redraw_dynamic();
    sync();
    reboot(RB_AUTOBOOT);
    log_add(LOG_ERROR, "reboot failed: %s", strerror(errno));
}

static void cmd_poweroff(int argc, char **argv) {
    (void)argc; (void)argv;
    log_add(LOG_NORMAL, "Powering off...");
    redraw_dynamic();
    sync();
    reboot(RB_POWER_OFF);
    log_add(LOG_ERROR, "poweroff failed: %s", strerror(errno));
}

const struct cmd cmds[] = {
    {"help",     "show this list",                       cmd_help},
    {"color",    "change screen color: color <bg> [fg]", cmd_color},   /* color.c */
    
    {"cave_exp", "Light Illumination", cmd_cave_exp},   /* cave_exp.c */
    {"keylock", "Simple Keylock Demo", cmd_keylock},   /* keylock.c */
    {"esp32_fsm", "ESP32 FSM Demo", cmd_esp32fsm},   /* esp32fsm.c */
    {"rainheart", "A heart in the rain", cmd_rainheart},   /* rainheart.c */
    {"kick", "Run towards Goal, Score the Shot", cmd_kick},   /* kick.c */
    {"grades", "Try your luck on enrolling something", cmd_grades},   /* grades.c */
    
    {"clear",    "clear the output window",              cmd_clear},
    {"echo",     "print text",                           cmd_echo},
    {"uname",    "system information",                   cmd_uname},
    {"uptime",   "time since boot",                      cmd_uptime},
    {"free",     "memory usage",                         cmd_free},
    {"date",     "current date/time (UTC)",              cmd_date},
    {"ls",       "list a directory",                     cmd_ls},
    {"cat",      "print a file",                         cmd_cat},
    {"redraw",   "redraw the screen",                    cmd_redraw},
    {"reboot",   "restart the machine",                  cmd_reboot},
    {"poweroff", "shut down the machine",                cmd_poweroff},
    {NULL, NULL, NULL}
};
