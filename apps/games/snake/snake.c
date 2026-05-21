/*
 * Snake Game for Toriginal OS
 * A classic snake game implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BOARD_WIDTH 40
#define BOARD_HEIGHT 20
#define MAX_SNAKE_LEN 400

/* Snake segment */
typedef struct {
    int x, y;
} Point;

/* Game state */
typedef struct {
    Point snake[MAX_SNAKE_LEN];
    int snake_len;
    Point food;
    int score;
    int game_over;
    int direction;  /* 0=up, 1=down, 2=left, 3=right */
    int next_direction;
} Game;

Game game;

void init_game() {
    srand(time(NULL));
    
    /* Initialize snake */
    game.snake_len = 3;
    game.snake[0].x = BOARD_WIDTH / 2;
    game.snake[0].y = BOARD_HEIGHT / 2;
    game.snake[1].x = BOARD_WIDTH / 2 - 1;
    game.snake[1].y = BOARD_HEIGHT / 2;
    game.snake[2].x = BOARD_WIDTH / 2 - 2;
    game.snake[2].y = BOARD_HEIGHT / 2;
    
    /* Initialize food */
    game.food.x = rand() % BOARD_WIDTH;
    game.food.y = rand() % BOARD_HEIGHT;
    
    game.score = 0;
    game.game_over = 0;
    game.direction = 3;  /* Start moving right */
    game.next_direction = 3;
}

void draw_board() {
    system("clear");
    printf("╔");
    for (int i = 0; i < BOARD_WIDTH; i++) printf("═");
    printf("╗\n");
    
    for (int y = 0; y < BOARD_HEIGHT; y++) {
        printf("║");
        for (int x = 0; x < BOARD_WIDTH; x++) {
            int is_snake = 0;
            int is_head = 0;
            
            /* Check if position is snake */
            for (int i = 0; i < game.snake_len; i++) {
                if (game.snake[i].x == x && game.snake[i].y == y) {
                    is_snake = 1;
                    if (i == 0) is_head = 1;
                    break;
                }
            }
            
            if (is_head) {
                printf("◉");  /* Head */
            } else if (is_snake) {
                printf("●");  /* Body */
            } else if (game.food.x == x && game.food.y == y) {
                printf("✳");  /* Food */
            } else {
                printf(" ");
            }
        }
        printf("║\n");
    }
    
    printf("╚");
    for (int i = 0; i < BOARD_WIDTH; i++) printf("═");
    printf("╝\n");
    
    printf("Score: %d | Length: %d\n", game.score, game.snake_len);
    printf("Controls: W=Up, S=Down, A=Left, D=Right, Q=Quit\n");
}

void update_game() {
    /* Update direction */
    game.direction = game.next_direction;
    
    /* Calculate new head position */
    Point new_head = game.snake[0];
    
    switch (game.direction) {
        case 0: new_head.y--; break;  /* Up */
        case 1: new_head.y++; break;  /* Down */
        case 2: new_head.x--; break;  /* Left */
        case 3: new_head.x++; break;  /* Right */
    }
    
    /* Check collision with walls */
    if (new_head.x < 0 || new_head.x >= BOARD_WIDTH ||
        new_head.y < 0 || new_head.y >= BOARD_HEIGHT) {
        game.game_over = 1;
        return;
    }
    
    /* Check collision with self */
    for (int i = 0; i < game.snake_len; i++) {
        if (new_head.x == game.snake[i].x && new_head.y == game.snake[i].y) {
            game.game_over = 1;
            return;
        }
    }
    
    /* Move snake */
    for (int i = game.snake_len; i > 0; i--) {
        game.snake[i] = game.snake[i - 1];
    }
    game.snake[0] = new_head;
    
    /* Check if food eaten */
    if (new_head.x == game.food.x && new_head.y == game.food.y) {
        game.score += 10;
        game.snake_len++;
        
        /* New food */
        game.food.x = rand() % BOARD_WIDTH;
        game.food.y = rand() % BOARD_HEIGHT;
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    printf("\n");
    printf("╔════════════════════════════════╗\n");
    printf("║     TORIGINAL OS - SNAKE GAME  ║\n");
    printf("╚════════════════════════════════╝\n\n");
    
    printf("Controls:\n");
    printf("  W - Move Up\n");
    printf("  S - Move Down\n");
    printf("  A - Move Left\n");
    printf("  D - Move Right\n");
    printf("  Q - Quit\n\n");
    
    printf("Press Enter to start...");
    getchar();
    
    init_game();
    
    int tick = 0;
    while (!game.game_over) {
        draw_board();
        
        /* Simple input handling (non-blocking would be better) */
        /* For now, just update every tick */
        
        tick++;
        if (tick % 3 == 0) {  /* Update every 3 iterations */
            update_game();
        }
        
        /* Simulate delay */
        for (int i = 0; i < 100000000; i++) {
            __asm__("nop");
        }
    }
    
    draw_board();
    printf("\n");
    printf("╔════════════════════════════════╗\n");
    printf("║         GAME OVER!             ║\n");
    printf("║     Final Score: %d            ║\n", game.score);
    printf("║     Length: %d                 ║\n", game.snake_len);
    printf("╚════════════════════════════════╝\n\n");
    
    return 0;
}
