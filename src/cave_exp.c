#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "ui.h"

/* Forward declaration */
void cmd_cave_exp(int argc, char **argv);

/* Game state */
static struct {
    int player_x, player_y;
    int room_width, room_height;
    int debug_mode;     /* "p" key enables full illumination */
} game_state = {0};

/* Lighting parameters */
#define LIGHT_RADIUS 5
#define DARK_COLOR 16       /* Very dark gray/black from 256-color palette */
#define LIGHT_COLOR 7       /* White */
#define WALL_COLOR 8        /* Gray for borders */

static void game_init(void) {
    game_state.room_width = cols;
    game_state.room_height = rows - 2;  /* Leave room for UI at bottom */
    
    /* Start player in center */
    game_state.player_x = game_state.room_width / 2;
    game_state.player_y = game_state.room_height / 2;
    game_state.debug_mode = 0;
}

static void game_cleanup(void) {
    /* No dynamic allocation, so nothing to free */
}

static int is_illuminated(int x, int y) {
    /* In debug mode, everything is lit */
    if (game_state.debug_mode) return 1;
    
    /* Check if tile is within light radius of player */
    int dx = x - game_state.player_x;
    int dy = y - game_state.player_y;
    int dist_sq = dx * dx + dy * dy;
    return dist_sq <= (LIGHT_RADIUS * LIGHT_RADIUS);
}

static void game_draw(void) {
    /* Clear screen and draw each tile */
    for (int y = 0; y < game_state.room_height; y++) {
        for (int x = 0; x < game_state.room_width; x++) {
            int is_wall = (x == 0 || x == game_state.room_width - 1 ||
                          y == 0 || y == game_state.room_height - 1);
            int is_player = (x == game_state.player_x && y == game_state.player_y);
            int lit = is_illuminated(x, y);
            
            /* Determine what to draw */
            char ch = ' ';
            int fg = 0, bg = DARK_COLOR;
            
            if (is_player) {
                /* Player always visible */
                ch = '#';
                bg = LIGHT_COLOR;
                fg = 0;
            } else if (lit) {
                /* Illuminated tile */
                if (is_wall) {
                    ch = '#';
                    bg = WALL_COLOR;
                    fg = 0;
                } else {
                    ch = '.';
                    bg = 0;  /* Black floor */
                    fg = 8;  /* Gray dot */
                }
            } else {
                /* Dark tile */
                ch = ' ';
                bg = DARK_COLOR;
            }
            
            /* Output the cell at position (x, y) */
            printf("\033[%d;%dH", y + 1, x + 1);  /* Cursor to (y+1, x+1) */
            printf("\033[38;5;%dm\033[48;5;%dm%c\033[0m", fg, bg, ch);
        }
    }
    
    /* Draw UI at bottom */
    printf("\033[%d;1H", game_state.room_height + 1);
    printf("\033[48;5;0m\033[38;5;7m");
    printf("Position: (%d,%d) | Move: WASD | Debug: P | Quit: Q", 
           game_state.player_x, game_state.player_y);
    printf("\033[0m");
    fflush(stdout);
}

static void game_move_player(int dx, int dy) {
    int new_x = game_state.player_x + dx;
    int new_y = game_state.player_y + dy;
    
    /* Clamp to room bounds (allow movement into walls for simplicity) */
    if (new_x < 0) new_x = 0;
    if (new_x >= game_state.room_width) new_x = game_state.room_width - 1;
    if (new_y < 0) new_y = 0;
    if (new_y >= game_state.room_height) new_y = game_state.room_height - 1;
    
    game_state.player_x = new_x;
    game_state.player_y = new_y;
}

static void game_handle_input(int ch) {
    switch (ch) {
        case 'w': case 'W':
            game_move_player(0, -1);
            break;
        case 's': case 'S':
            game_move_player(0, 1);
            break;
        case 'a': case 'A':
            game_move_player(-1, 0);
            break;
        case 'd': case 'D':
            game_move_player(1, 0);
            break;
        case 'p': case 'P':
            game_state.debug_mode = !game_state.debug_mode;
            break;
        case 'q': case 'Q':
            return;  /* Exit game loop */
    }
}

void cmd_cave_exp(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    log_add(LOG_NORMAL, "Starting cave explore...");
    log_add(LOG_NORMAL, "Move: W/A/S/D | Debug: P | Quit: Q");
    
    game_init();
    
    /* Main game loop */
    while (1) {
        game_draw();
        
        /* Get input (non-blocking would be better, but this works for testing) */
        int ch = getchar();
        if (ch != EOF) {
            game_handle_input(ch);
            if (ch == 'q' || ch == 'Q') break;  /* Exit on Q */
        }
    }
    
    game_cleanup();
}

