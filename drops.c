#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_rotozoom.h>
#include <SDL_gfxPrimitives.h>
#include <SDL_ttf.h>
#include <SDL_mixer.h>
#include <SDL_framerate.h>

#ifdef _PSP_FW_VERSION
#include <pspkernel.h>
#include <pspsdk.h>
#include <psputility.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#define MODULE_NAME "Main"
PSP_MODULE_INFO(MODULE_NAME, 0, 1, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_HEAP_SIZE_MAX();
#endif

#define WIDTH 480
#define HEIGHT 272
#define BPP 32
#define BLACK 0x000000ff
#define WHITE 0xf4f3d7ff

#define R(rgba) ((rgba & 0xff000000) >> 24)
#define G(rgba) ((rgba & 0x00ff0000) >> 16)
#define B(rgba) ((rgba & 0x0000ff00) >> 8)

#define TINT_COLOR 0x00000088
#define BG_COLOR 0x363636ff
#define PLAYER_COLOR 0xfd63aaff
#define PLAYER_FORCE_COLOR 0xfd0e7cff
#define PLAYER_TURBO_COLOR 0xffe273ff
#define FORCE_FIELD_COLOR 0xffffff80

#ifdef _PSP_FW_VERSION
#define random lrand48
#endif

#ifdef _PSP_FW_VERSION
enum {
    PSP_BUTTON_CROSS,
    PSP_BUTTON_TRIANGLE,
    PSP_BUTTON_SQUARE,
    PSP_BUTTON_CIRCLE,
    PSP_BUTTON_L,
    PSP_BUTTON_R,
    PSP_BUTTON_DOWN,
    PSP_BUTTON_LEFT,
    PSP_BUTTON_UP,
    PSP_BUTTON_RIGHT,
    PSP_BUTTON_SELECT,
    PSP_BUTTON_START,
};
#else
// Only valid for my joypad
enum {
    PSP_BUTTON_CROSS = 2,
    PSP_BUTTON_CIRCLE = 1,
    PSP_BUTTON_SQUARE = 3,
    PSP_BUTTON_TRIANGLE = 0,
    PSP_BUTTON_L = 6,
    PSP_BUTTON_R = 7,
    PSP_BUTTON_DOWN,
    PSP_BUTTON_LEFT,
    PSP_BUTTON_UP,
    PSP_BUTTON_RIGHT,
    PSP_BUTTON_SELECT = 8,
    PSP_BUTTON_START = 11
};
#endif

typedef enum FX {
    FX_PIXELATE,
} FX;

typedef struct JoystickState {
    int buttons[12];
    int analog_x;
    int analog_y;
} JoystickState;

enum BonusState {
    BONUS_STATE_INACTIVE = 0,
    BONUS_STATE_GROWING,
    BONUS_STATE_ACTIVE,
    BONUS_STATE_DYING
};

enum BonusType {
    BONUS_TYPE_NONE = -1,
    BONUS_TYPE_TURBO,
    BONUS_TYPE_FREEZE,
    BONUS_TYPE_REPEL,
    BONUS_TYPE_BOMB,
    BONUS_TYPE_NUM
};

typedef struct Bonus {
    enum BonusState state;
    enum BonusType type;
    int x, y;
    int size;
    int grown_size;
} Bonus;

enum DropState {
    DROP_STATE_INACTIVE = 0,
    DROP_STATE_GROWING,
    DROP_STATE_ACTIVE,
    DROP_STATE_DYING
};

typedef struct Drop {
    enum DropState state;
    int x, y;
    int size;
    int grown_size;
} Drop;

enum EnemyState {
    ENEMY_STATE_INACTIVE = 0,
    ENEMY_STATE_ACTIVE
};

typedef struct Enemy {
    int state;
    int x, y;
} Enemy;

typedef struct Player {
    int x, y;
    int size;
    int life;
    int turbo;
    int speed;
    int force_field;
    int energy;
    int points;
    Uint32 hit;
    Uint32 berzerk;
    int berzerk_field;
    enum BonusType bonus;
    Uint32 bonus_start;
} Player;

typedef struct Hardware {
    SDL_Joystick *joystick;
    JoystickState joystick_state;
    SDL_Surface *screen;
    TTF_Font *big_font;
    TTF_Font *medium_font;
    SDL_Surface *happy_face;
    SDL_Surface *paused_face;
    SDL_Surface *game_over_face;
    SDL_Surface *background;
    Uint32 bonus_colors[BONUS_TYPE_NUM];
} Hardware;

enum GameState {
    GAME_STATE_START_SCREEN,
    GAME_STATE_PLAYING,
    GAME_STATE_PAUSED,
    GAME_STATE_OVER
};

typedef struct Game {
    int state;
    int level;
    Drop drops[50];
    Enemy enemies[50];
    Player player;
    Bonus bonus;
    Uint32 last_enemy_timestamp;
    Uint32 ticks, last_start;
} Game;

Game game;
Hardware hardware;

void render_world();

int collide(int x1, int y1, int size1, int x2, int y2, int size2){
    int sqd = (x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2);
    return sqd < (size1 + size2) * (size1 + size2);
}

int keep_inside(int v, int min, int max){
    if (v < min)
        return min;
    if (v > max)
        return max;
    return v;
}

void apply_fx(FX fx, void *params){
    SDL_Surface *mini, *maxi;
    switch (fx){
    case FX_PIXELATE:
        mini = zoomSurface(hardware.screen, 0.125, 0.125, 1);
        maxi = zoomSurface(mini, 8, 8, 0);
        SDL_FreeSurface(mini);
        SDL_BlitSurface(maxi, NULL, hardware.screen, NULL);
        SDL_FreeSurface(maxi);
        break;
    }
}

void stop_clock(){
    game.ticks += SDL_GetTicks() - game.last_start;
    game.last_start = 0;
}

void start_clock(){
    game.last_start = SDL_GetTicks();
}

Uint32 get_clock(){
    if (game.state == GAME_STATE_PLAYING){
        return game.ticks + SDL_GetTicks() - game.last_start;
    }
    else {
        return game.ticks;
    }
}

void update_joy_state(){
    JoystickState *joystick_state = &hardware.joystick_state;
    SDL_Joystick *joystick = hardware.joystick;
    memset(joystick_state->buttons, 0, sizeof(joystick_state->buttons));
    joystick_state->analog_x = joystick_state->analog_y = 0;

    SDL_JoystickUpdate();

    joystick_state->buttons[PSP_BUTTON_TRIANGLE] = SDL_JoystickGetButton(joystick, 0);
    joystick_state->buttons[PSP_BUTTON_CIRCLE] = SDL_JoystickGetButton(joystick, 1);
    joystick_state->buttons[PSP_BUTTON_CROSS] = SDL_JoystickGetButton(joystick, 2);
    joystick_state->buttons[PSP_BUTTON_SQUARE] = SDL_JoystickGetButton(joystick, 3);

    joystick_state->buttons[PSP_BUTTON_L] = SDL_JoystickGetButton(joystick, 4);
    joystick_state->buttons[PSP_BUTTON_R] = SDL_JoystickGetButton(joystick, 5);

    joystick_state->buttons[PSP_BUTTON_START] = SDL_JoystickGetButton(joystick, 11);

#ifdef _PSP_FW_VERSION
    joystick_state->buttons[PSP_BUTTON_DOWN] = SDL_JoystickGetButton(joystick, 6);
    joystick_state->buttons[PSP_BUTTON_LEFT] = SDL_JoystickGetButton(joystick, 7);
    joystick_state->buttons[PSP_BUTTON_UP] = SDL_JoystickGetButton(joystick, 8);
    joystick_state->buttons[PSP_BUTTON_RIGHT] = SDL_JoystickGetButton(joystick, 9);

    joystick_state->buttons[PSP_BUTTON_SELECT] = SDL_JoystickGetButton(joystick, 10);
#else
    joystick_state->buttons[PSP_BUTTON_L] = SDL_JoystickGetButton(joystick, 6);
    joystick_state->buttons[PSP_BUTTON_R] = SDL_JoystickGetButton(joystick, 7);

    joystick_state->buttons[PSP_BUTTON_SELECT] = SDL_JoystickGetButton(joystick, 8);
#endif
    joystick_state->analog_x = (SDL_JoystickGetAxis(joystick, 0) / 256) + 128;
    joystick_state->analog_y = (SDL_JoystickGetAxis(joystick, 1) / 256) + 128;
}

void print(SDL_Surface *dst, int x, int y, TTF_Font *font, char *text, Uint32 rgba){
    SDL_Rect pos;
    SDL_Surface *src;
    SDL_Color color = { R(rgba), G(rgba), B(rgba) };
    pos.x = x;
    pos.y = y;
    src = TTF_RenderText_Blended(font, text, color);
    SDL_BlitSurface(src, NULL, dst, &pos);
    SDL_FreeSurface(src);
}

// Draw centered text
void print_center(SDL_Surface *dst, TTF_Font *font, char *text, Uint32 rgba){
    int width, height;
    TTF_SizeText(font, text, &width, &height);
    print(hardware.screen, (WIDTH - width) / 2, (HEIGHT - height) / 2, font, text, rgba);
}

// Draw centered text with a logo
void print_with_logo(SDL_Surface *dst, TTF_Font *font, char *text, SDL_Surface *logo){
    SDL_Rect pos;
    int width, height;

    TTF_SizeText(font, text, &width, &height);
    pos.x = (WIDTH - width) / 2 - 40;
    pos.y = (HEIGHT - height) / 2 - (30 - height) / 2;
    print(hardware.screen, (WIDTH - width) / 2, (HEIGHT - height) / 2, font, text, WHITE);
    SDL_BlitSurface(logo, NULL, hardware.screen, &pos);
    rectangleColor(hardware.screen, pos.x - 1, pos.y - 1, pos.x + 30, pos.y + 30, WHITE);
}

void fill_circle(int x, int y, int r, Uint32 rgba){
    filledCircleColor(hardware.screen, x, y, r, rgba);
    aacircleColor(hardware.screen, x, y, r, rgba);
}

int can_berzerk(){
    return game.player.energy > 500;
}

void reset_game(){
    int i;
    game.level = 1;
    game.player.x = WIDTH / 2;
    game.player.y = HEIGHT / 2;
    game.player.life = 5;
    game.player.size = 10;
    game.player.speed = 2;
    game.player.turbo = 0;
    game.player.energy = 0;
    game.player.force_field = 0;
    game.player.points = 0;
    game.player.hit = 0;
    game.player.bonus = BONUS_TYPE_NONE;
    game.player.bonus_start = 0;
    for (i = 0; i < 50; i++){
        game.drops[i].state = DROP_STATE_INACTIVE;
    }
    for (i = 0; i < 50; i++){
        game.enemies[i].state = ENEMY_STATE_INACTIVE;
    }
    game.bonus.state = BONUS_STATE_INACTIVE;
    game.state = GAME_STATE_START_SCREEN;
    game.last_enemy_timestamp = 0;
    game.ticks = 0;
    game.last_start = 0;
}

void quit(){
    render_world();
    apply_fx(FX_PIXELATE, NULL);
    boxColor(hardware.screen, 0, 0, WIDTH, HEIGHT, TINT_COLOR);
    print_center(hardware.screen, hardware.big_font, "Shutting down...", WHITE);
    SDL_Flip(hardware.screen);
    SDL_Quit();
#ifdef _PSP_FW_VERSION
    sceKernelExitGame();
#else
    exit(0);
#endif
}

void init(){
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO) == -1)
        quit();
    SDL_ShowCursor(SDL_DISABLE);
    SDL_WM_SetCaption("drops", NULL);

    hardware.joystick = SDL_JoystickOpen(0);
    SDL_JoystickEventState(SDL_ENABLE);

    hardware.screen = SDL_SetVideoMode(WIDTH, HEIGHT, BPP, SDL_HWSURFACE | SDL_ANYFORMAT | SDL_DOUBLEBUF);
    if (hardware.screen == NULL)
        quit();

    if (TTF_Init() == -1)
        quit();
    hardware.big_font = TTF_OpenFont("media/DroidSans.ttf", 20);
    hardware.medium_font = TTF_OpenFont("media/DroidSans.ttf", 12);

    hardware.game_over_face = IMG_Load("media/gameover.png");
    hardware.happy_face = IMG_Load("media/happy.png");
    hardware.paused_face = IMG_Load("media/paused.png");

    hardware.background = IMG_Load("media/bg.png");

    hardware.bonus_colors[BONUS_TYPE_TURBO] = PLAYER_TURBO_COLOR;
    hardware.bonus_colors[BONUS_TYPE_FREEZE] = WHITE;
    hardware.bonus_colors[BONUS_TYPE_BOMB] = BLACK;
    hardware.bonus_colors[BONUS_TYPE_REPEL] = 0x00C7FBff;

    reset_game();
}

void draw_clock(){
    int width, height;
    char msg[256];
    snprintf(msg, 256, "%d", get_clock());
    TTF_SizeText(hardware.big_font, msg, &width, &height);
    print(hardware.screen, 10, 10, hardware.medium_font, msg, WHITE);
}

void render_world(){
    char msg[256];
    int width, height, i, x, y;
    Uint32 color = 0;

#ifdef _PSP_FW_VERSION
    SDL_FillRect(hardware.screen, NULL, SDL_MapRGB(hardware.screen->format, R(BG_COLOR), G(BG_COLOR), B(BG_COLOR)));
#else
    SDL_BlitSurface(hardware.background, NULL, hardware.screen, NULL);
#endif

    for (i = 0; i < 50; i++){
        if (game.drops[i].state){
            switch (game.drops[i].state){
                case DROP_STATE_ACTIVE: color = 0x019875ff; break;
                case DROP_STATE_GROWING:
                case DROP_STATE_DYING: color = 0xa6c780ff; break;
                default: break;
            }
            if (game.player.berzerk){
                x = game.drops[i].x + random() % 4;
                y = game.drops[i].y + random() % 4;
                fill_circle(x, y, game.drops[i].size, color);
            }
            else {
                fill_circle(game.drops[i].x, game.drops[i].y, game.drops[i].size, color);
            }
        }
    }

    for (i = 0; i < 50; i++){
        if (game.enemies[i].state){
            fill_circle(game.enemies[i].x, game.enemies[i].y, 2, WHITE);
        }
    }

    if (game.player.berzerk)
        fill_circle(game.player.x, game.player.y, game.player.size + game.player.berzerk_field, FORCE_FIELD_COLOR);
    else if (game.player.force_field)
        fill_circle(game.player.x, game.player.y, game.player.size + game.player.force_field, FORCE_FIELD_COLOR);

    if (game.player.bonus == BONUS_TYPE_BOMB){
        int bomb_radius = (get_clock() - game.player.bonus_start) / 10;
        fill_circle(game.bonus.x, game.bonus.y, game.bonus.grown_size + bomb_radius, FORCE_FIELD_COLOR);
    }

    if (game.bonus.state != BONUS_STATE_INACTIVE){
        x = game.bonus.x + random() % 4;
        y = game.bonus.y + random() % 4;
        fill_circle(x, y, game.bonus.size, hardware.bonus_colors[game.bonus.type]);
    }

    x = game.player.x;
    y = game.player.y;
    if (game.player.hit && (get_clock() - game.player.hit < 1000)){
        color = WHITE;
    }
    else if (game.player.force_field){
        color = PLAYER_FORCE_COLOR;
    }
    else if (game.player.berzerk){
        color = BLACK;
        x = game.player.x + random() % 3 - 1;
        y = game.player.y + random() % 3 - 1;
    }
    else if (game.player.turbo){
        color = PLAYER_TURBO_COLOR;
    }
    else {
        color = PLAYER_COLOR;
    }
    fill_circle(x, y, game.player.size, color);

    snprintf(msg, 256, "LEVEL %d", game.level);
    TTF_SizeText(hardware.medium_font, msg, &width, &height);
    print(hardware.screen, 10, 10, hardware.medium_font, msg, WHITE);

    snprintf(msg, 256, "%d", game.player.life);
    TTF_SizeText(hardware.big_font, msg, &width, &height);
    print(hardware.screen, WIDTH - 100, 10, hardware.big_font, msg, PLAYER_COLOR);

    if (can_berzerk()){
        x = WIDTH - 110 + random() % 3 - 1;
        y = 22 + random() % 3 - 1;
        fill_circle(x, y, 7, BLACK);
    }
    else {
        fill_circle(WIDTH - 110, 22, 4, PLAYER_COLOR);
    }

    snprintf(msg, 256, "%d", game.player.points);
    TTF_SizeText(hardware.big_font, msg, &width, &height);
    print(hardware.screen, WIDTH - width - 10, 10, hardware.big_font, msg, WHITE);
}

void redraw(){
    char msg[256];
    switch (game.state){
    case GAME_STATE_START_SCREEN:
        render_world();
        apply_fx(FX_PIXELATE, NULL);
        boxColor(hardware.screen, 0, 0, WIDTH, HEIGHT, TINT_COLOR);
        print_with_logo(hardware.screen, hardware.big_font, "Press START to play", hardware.happy_face);
        break;
    case GAME_STATE_PAUSED:
        render_world();
        apply_fx(FX_PIXELATE, NULL);
        boxColor(hardware.screen, 0, 0, WIDTH, HEIGHT, TINT_COLOR);
        print_with_logo(hardware.screen, hardware.big_font, "Paused", hardware.paused_face);
        break;
    case GAME_STATE_PLAYING:
        render_world();
        break;
    case GAME_STATE_OVER:
        render_world();
        apply_fx(FX_PIXELATE, NULL);
        boxColor(hardware.screen, 0, 0, WIDTH, HEIGHT, TINT_COLOR);
        snprintf(msg, 256, "You scored %d points, and I'M DEAD!", game.player.points);
        print_with_logo(hardware.screen, hardware.big_font, msg, hardware.game_over_face);
        break;
    }
    SDL_Flip(hardware.screen);
}

void update_game(){
    int dx = 0, dy = 0, i;
    int max_active_drops_count = keep_inside(20 - (game.level / 2), 5, 50);
    int drops_to_activate, active_drops_count = 0;
    int max_active_enemies_count = keep_inside(10 + game.level * 2, 0, 50);
    int active_enemies_count = 0;
    Uint32 berzerk_duration;

    // Did we reach the end of the bonus ?
    if (game.player.bonus != BONUS_TYPE_NONE){
        Uint32 bonus_duration = get_clock() - game.player.bonus_start;
        if (bonus_duration > 3000){
            game.player.bonus = BONUS_TYPE_NONE;
        }
    }

    if (can_berzerk() && hardware.joystick_state.buttons[PSP_BUTTON_TRIANGLE] && !game.player.berzerk){
        game.player.energy = 0;
        game.player.berzerk = get_clock();
        game.player.berzerk_field = 0;
        return;
    }

    if (game.player.berzerk){
        berzerk_duration = get_clock() - game.player.berzerk;
        game.player.berzerk_field = berzerk_duration / 4;
        if (berzerk_duration > 1500){
            game.player.berzerk = 0;
        }
    }

    // let the drops grow or die
    for (i = 0; i < 50; i++){
        if (game.drops[i].state == DROP_STATE_GROWING){
            game.drops[i].size++;
            if (game.drops[i].size >= game.drops[i].grown_size){
                game.drops[i].state = DROP_STATE_ACTIVE;
                game.drops[i].size = game.drops[i].grown_size;
            }
        }
        else if (game.drops[i].state == DROP_STATE_DYING){
            game.drops[i].size--;
            if (game.drops[i].size <= 1){
                game.drops[i].state = DROP_STATE_INACTIVE;
            }
        }
    }

    if (game.bonus.state == BONUS_STATE_GROWING){
        game.bonus.size++;
        if (game.bonus.size >= game.bonus.grown_size){
            game.bonus.state = BONUS_STATE_ACTIVE;
            game.bonus.size = game.bonus.grown_size;
        }
    }
    else if (game.bonus.state == BONUS_STATE_DYING){
        game.bonus.size--;
        if (game.bonus.size <= 1){
            game.bonus.state = BONUS_STATE_INACTIVE;
        }
    }


    // Add drops if maximum not reached
    for (i = 0; i < 50; i++){
        active_drops_count += game.drops[i].state != DROP_STATE_INACTIVE;
    }
    if (active_drops_count < max_active_drops_count){
        drops_to_activate = max_active_drops_count - active_drops_count;
        for (i = 0; drops_to_activate && i < 50; i++){
            if (game.drops[i].state == DROP_STATE_INACTIVE){
                game.drops[i].state = DROP_STATE_GROWING;
                game.drops[i].grown_size = 5 + (random() % (30 - game.level));
                game.drops[i].size = 1;
                game.drops[i].x = game.drops[i].grown_size + (random() % (WIDTH - 2 * game.drops[i].grown_size));
                game.drops[i].y = game.drops[i].grown_size + (random() % (HEIGHT - 2 * game.drops[i].grown_size));
                drops_to_activate--;
            }
        }
    }

    // Do we absorb a drop ?
    for (i = 0; i < 50; i++){
        if (game.drops[i].state != DROP_STATE_INACTIVE && game.drops[i].state != DROP_STATE_DYING){
            if (collide(game.player.x, game.player.y, game.player.size, game.drops[i].x, game.drops[i].y, game.drops[i].size)){
                game.player.points += game.drops[i].size;
                game.player.energy += game.drops[i].size;
                game.drops[i].state = DROP_STATE_DYING;
            }
        }
    }

    // Did we absorb the bonus ?
    if (game.bonus.state != BONUS_STATE_INACTIVE && game.bonus.state != BONUS_STATE_DYING){
        if (collide(game.player.x, game.player.y, game.player.size, game.bonus.x, game.bonus.y, game.bonus.size)){
            game.player.bonus = game.bonus.type;
            game.player.bonus_start = get_clock();
            game.bonus.state = BONUS_STATE_DYING;
        }
    }

    // Did the Bomb Bonus explode ?
    if (game.player.bonus == BONUS_TYPE_BOMB){
        int bomb_radius = (get_clock() - game.player.bonus_start) / 10;
        for (i = 0; i < 50; i++){
            if (game.enemies[i].state && collide(game.bonus.x, game.bonus.y, game.bonus.grown_size + bomb_radius, game.enemies[i].x, game.enemies[i].y, 2)){
                game.enemies[i].state = ENEMY_STATE_INACTIVE;
            }
        }
    }

    // Do we need to interact with enemies ?
    for (i = 0; i < 50; i++){
        if (game.enemies[i].state){
            // We get hurt if we collide with enemies..
            if (collide(game.player.x, game.player.y, game.player.size, game.enemies[i].x, game.enemies[i].y, 2)){
                game.player.hit = get_clock();
                game.player.life--;
                game.enemies[i].state = ENEMY_STATE_INACTIVE;
                if (game.player.life == 0){
                    game.state = GAME_STATE_OVER;
                    stop_clock();
                    return;
                }
            }
            // ..unless we GO BERZERK
            else if (game.player.berzerk && collide(game.player.x, game.player.y, game.player.size +  + (game.player.berzerk ? game.player.berzerk_field : 0), game.enemies[i].x, game.enemies[i].y, 2)){
                game.enemies[i].state = ENEMY_STATE_INACTIVE;
            }
            // ..unless we USE THE FORCE
            else if (game.player.force_field && collide(game.player.x, game.player.y, game.player.size + game.player.force_field, game.enemies[i].x, game.enemies[i].y, 2)){
                game.enemies[i].state = ENEMY_STATE_INACTIVE;
            }
        }
    }

    // Make the Enemies chase us
    if (!game.player.berzerk && game.player.bonus != BONUS_TYPE_FREEZE){
        for (i = 0; i < 50; i++){
            if (game.enemies[i].state){
                int enemy_speed = ((game.player.bonus != BONUS_TYPE_REPEL) * 2 - 1) * (random() % 2 + 1);
                if (game.enemies[i].x > game.player.x)
                    game.enemies[i].x -= enemy_speed;
                if (game.enemies[i].y > game.player.y)
                    game.enemies[i].y -= enemy_speed;
                if (game.enemies[i].x < game.player.x)
                    game.enemies[i].x += enemy_speed;
                if (game.enemies[i].y < game.player.y)
                    game.enemies[i].y += enemy_speed;
            }
        }
    }

    // Add Enemies every 0.5s unless max reached
    if (!game.player.berzerk){
        for (i = 0; i < 50; i++){
            active_enemies_count += game.enemies[i].state != ENEMY_STATE_INACTIVE;
        }
        if ((get_clock() - game.last_enemy_timestamp > 500) && active_enemies_count < max_active_enemies_count){
            for (i = 0; i < 50; i++){
                if (game.enemies[i].state == ENEMY_STATE_INACTIVE){
                    game.enemies[i].state = ENEMY_STATE_ACTIVE;
                    game.enemies[i].x = random() % 2 ? 10 : WIDTH - 10;
                    game.enemies[i].y = random() % 2 ? 10 : HEIGHT - 10;
                    game.last_enemy_timestamp = get_clock();
                    break;
                }
            }
        }
    }

    // Use turbo ?
    game.player.speed = 2;
    game.player.turbo = 0;
    if (game.player.bonus == BONUS_TYPE_TURBO
        || hardware.joystick_state.buttons[PSP_BUTTON_CIRCLE]
        || hardware.joystick_state.buttons[PSP_BUTTON_R]){
        if (game.player.bonus != BONUS_TYPE_TURBO && game.player.energy > 0){
            --game.player.energy;
            game.player.speed = 4;
            game.player.turbo = 1;
        }
        if (game.player.bonus == BONUS_TYPE_TURBO){
            game.player.speed = 4;
            game.player.turbo = 1;
        }
    }

    // Move
    if (hardware.joystick_state.analog_x < 120)
       dx = -game.player.speed;
    if (hardware.joystick_state.analog_x > 130)
       dx = game.player.speed;
    if (hardware.joystick_state.analog_y < 120)
       dy = -game.player.speed;
    if (hardware.joystick_state.analog_y > 130)
       dy = game.player.speed;
    if (!game.player.berzerk){
        game.player.x += dx;
        game.player.y += dy;
        game.player.x = keep_inside(game.player.x, game.player.size, WIDTH - game.player.size);
        game.player.y = keep_inside(game.player.y, game.player.size, HEIGHT - game.player.size);
    }

    // USE THE FORCE ?
    if (hardware.joystick_state.buttons[PSP_BUTTON_CROSS] && game.player.energy){
        --game.player.energy;
        game.player.force_field++;
    }
    else {
        game.player.force_field--;
    }
    game.player.force_field = keep_inside(game.player.force_field, 0, 20);


    // Next Level ?
    if (game.player.points > (game.level << 1) * 500){
        game.level = keep_inside(game.level + 1, 1, 20);
        // Add Bonus
        game.bonus.state = BONUS_STATE_GROWING;
        game.bonus.type = random() % BONUS_TYPE_NUM;
        game.bonus.grown_size = 10;
        game.bonus.size = 1;
        game.bonus.x = game.bonus.grown_size + (random() % (WIDTH - 2 * game.bonus.grown_size));
        game.bonus.y = game.bonus.grown_size + (random() % (HEIGHT - 2 * game.bonus.grown_size));
    }
}

void loop(){
    FPSmanager fps_manager;

    SDL_initFramerate(&fps_manager);
    SDL_setFramerate(&fps_manager, 60);

    redraw();

    while (1){
        SDL_Event event;
        int up_event;
        up_event = 0;
        if (SDL_PollEvent(&event)){
            if (event.type == SDL_QUIT){
                quit();
            }
            else if (event.type == SDL_JOYBUTTONUP){
               up_event = 1;
            }
        }
        update_joy_state();
        if (hardware.joystick_state.buttons[PSP_BUTTON_L] && hardware.joystick_state.buttons[PSP_BUTTON_R]){
            quit();
            return;
        }

        switch (game.state){
        case GAME_STATE_START_SCREEN:
            if (up_event && (event.jbutton.button == PSP_BUTTON_START || event.jbutton.button == PSP_BUTTON_CROSS)){
                game.state = GAME_STATE_PLAYING;
                start_clock();
                redraw();
            }
            break;
        case GAME_STATE_PLAYING:
            if (up_event && event.jbutton.button == PSP_BUTTON_START){
                game.state = GAME_STATE_PAUSED;
                stop_clock();
            }
            else {
                update_game();
            }
            redraw();
            break;
        case GAME_STATE_PAUSED:
            if (up_event && (event.jbutton.button == PSP_BUTTON_START || event.jbutton.button == PSP_BUTTON_CROSS)){
                game.state = GAME_STATE_PLAYING;
                start_clock();
                redraw();
            }
            break;
        case GAME_STATE_OVER:
            if (up_event && (event.jbutton.button == PSP_BUTTON_START)){
                reset_game();
                redraw();
            }
            break;
        }
        SDL_framerateDelay(&fps_manager);
    }
}

#ifdef _PSP_FW_VERSION
int exit_callback(int arg1, int arg2, void *common){
    quit();
    return 0;
}

int CallbackThread(SceSize args, void *argp){
    int cbid;
    cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

int SetupCallbacks(void){
    int thid = 0;
    thid = sceKernelCreateThread("update_thread", CallbackThread, 0x11, 0xFA0, PSP_THREAD_ATTR_USER, 0);

    if (thid >= 0)
        sceKernelStartThread(thid, 0, 0);

    return thid;
}

#define BUF_WIDTH (512)
#define PIXEL_SIZE (4)
#define FRAME_SIZE (BUF_WIDTH * HEIGHT * PIXEL_SIZE)
#define SCR_WIDTH WIDTH
#define SCR_HEIGHT HEIGHT

static unsigned int __attribute__((aligned(16))) list[262144];

static void SetupGu()
{
    sceGuInit();

    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, (void*)0, BUF_WIDTH);
    sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, (void*)0x88000, BUF_WIDTH);
    sceGuDepthBuffer((void*)0x110000, BUF_WIDTH);
    sceGuOffset(2048 - (SCR_WIDTH/2), 2048 - (SCR_HEIGHT/2));
    sceGuViewport(2048, 2048, SCR_WIDTH, SCR_HEIGHT);
    sceGuDepthRange(0xc350, 0x2710);
    sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuFrontFace(GU_CW);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_CULL_FACE);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuFinish();
    sceGuSync(0, 0);

    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}
#endif

int main(int argc, char *argv[])
{
#ifdef _PSP_FW_VERSION
    SetupCallbacks();
    SetupGu();
#endif
    init();
    loop();
    return 0;
}

