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
#define BG_COLOR SDL_MapRGB(hardware.screen->format, 0x55, 0x55, 0x44)
#define BLACK SDL_MapRGB(hardware.screen->format, 0, 0, 0)
#define WHITE SDL_MapRGB(hardware.screen->format, 255, 255, 255)


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

typedef struct JoystickState {
    int buttons[12];
    int analog_x;
    int analog_y;
} JoystickState;

typedef struct Player {
    int x, y;
    int size;
    int life;
    int using_turbo;
    int speed;
    int force_field;
    int energy_left;
    int points;
    int last_hurt;
} Player;

enum DropState {
    DROP_INACTIVE = 0,
    DROP_GROWING,
    DROP_ACTIVE,
    DROP_DYING
};

typedef struct Drop {
    int state;
    int x, y;
    int size;
    int grown_size;
} Drop;

enum EnemyState {
    ENEMY_INACTIVE = 0,
    ENEMY_ACTIVE
};

typedef struct Enemy {
    int state;
    int x, y;
} Enemy;

typedef struct Hardware {
    SDL_Joystick *joystick;
    JoystickState joystick_state;
    SDL_Surface *screen;
    TTF_Font *big_font;
    TTF_Font *medium_font;
} Hardware;

enum GameState {
    NO_GAME,
    GAME_PLAYING,
    GAME_PAUSED,
    GAME_OVER
};

typedef struct Game {
    int state;
    Drop drops[50];
    Enemy enemies[50];
    Player player;
    Uint32 last_enemy_timestamp;
} Game;

Game game;
Hardware hardware;

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
    joystick_state->buttons[PSP_BUTTON_DOWN] = SDL_JoystickGetButton(joystick, 6)
    joystick_state->buttons[PSP_BUTTON_LEFT] = SDL_JoystickGetButton(joystick, 7)
    joystick_state->buttons[PSP_BUTTON_UP] = SDL_JoystickGetButton(joystick, 8)
    joystick_state->buttons[PSP_BUTTON_RIGHT] = SDL_JoystickGetButton(joystick, 9)

    joystick_state->buttons[PSP_BUTTON_SELECT] = SDL_JoystickGetButton(joystick, 10);
#else
    joystick_state->buttons[PSP_BUTTON_L] = SDL_JoystickGetButton(joystick, 6);
    joystick_state->buttons[PSP_BUTTON_R] = SDL_JoystickGetButton(joystick, 7);

    joystick_state->buttons[PSP_BUTTON_SELECT] = SDL_JoystickGetButton(joystick, 8);
#endif
    joystick_state->analog_x = (SDL_JoystickGetAxis(joystick, 0) / 256) + 128;
    joystick_state->analog_y = (SDL_JoystickGetAxis(joystick, 1) / 256) + 128;
}

void print(SDL_Surface *dst, int x, int y, TTF_Font *font, char *text, int r, int g, int b){
    SDL_Rect pos;
    SDL_Surface *src;
    SDL_Color color = {r, g, b};
    if (font == NULL) return;
    pos.x = x;
    pos.y = y;
    src = TTF_RenderText_Blended(font, text, color);
    SDL_BlitSurface(src, NULL, dst, &pos);
    SDL_FreeSurface(src);
}

void print_center(SDL_Surface *dst, TTF_Font *font, char *text, int r, int g, int b){
    int width, height;
    TTF_SizeText(font, text, &width, &height);
    print(hardware.screen, (WIDTH - width) / 2, (HEIGHT - height) / 2, font, text, r, g, b);
}

void reset_game(){
    int i;
    game.player.x = WIDTH / 2;
    game.player.y = HEIGHT / 2;
    game.player.life = 5;
    game.player.size = 10;
    game.player.speed = 2;
    game.player.using_turbo = 0;
    game.player.energy_left = 0;
    game.player.force_field = 0;
    game.player.points = 0;
    game.player.last_hurt = 0;
    for (i = 0; i < 50; i++){
        game.drops[i].state = DROP_INACTIVE;
    }
    for (i = 0; i < 50; i++){
        game.enemies[i].state = ENEMY_INACTIVE;
    }
    game.state = NO_GAME;
    game.last_enemy_timestamp = 0;
}

void quit(){
    boxRGBA(hardware.screen, 0, 0, WIDTH, HEIGHT, 0, 0, 0, 200);
    print_center(hardware.screen, hardware.big_font, "Shutting down...", 255, 255, 255);
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
    hardware.joystick = SDL_JoystickOpen(0);
    SDL_JoystickEventState(SDL_ENABLE);
    if (TTF_Init() == -1)
        quit();
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) < 0)
        quit();

    SDL_ShowCursor(SDL_DISABLE);

    hardware.screen = SDL_SetVideoMode(WIDTH, HEIGHT, BPP, SDL_HWSURFACE | SDL_ANYFORMAT | SDL_DOUBLEBUF);
    if (hardware.screen == NULL)
        quit();

    hardware.big_font = TTF_OpenFont("DroidSans.ttf", 20);
    hardware.medium_font = TTF_OpenFont("DroidSans.ttf", 12);

    reset_game();
}

void render_world(){
    char msg[256];
    int width, height, i;

    SDL_FillRect(hardware.screen, NULL, BG_COLOR);
    for (i = 0; i < 50; i++){
        int color = SDL_MapRGB(hardware.screen->format, 0, 0, 0);
        if (game.drops[i].state){
            switch (game.drops[i].state){
                case DROP_GROWING: color = SDL_MapRGB(hardware.screen->format, 0xff, 0xbb, 0xbb); break;
                case DROP_ACTIVE: color = SDL_MapRGB(hardware.screen->format, 0xff, 0x75, 0x75); break;
                case DROP_DYING: color = SDL_MapRGB(hardware.screen->format, 0xd0, 0xbc, 0xfe); break;
            }
            filledCircleColor(hardware.screen, game.drops[i].x, game.drops[i].y, game.drops[i].size, color);
            aacircleColor(hardware.screen, game.drops[i].x, game.drops[i].y, game.drops[i].size, color);
        }
    }

    for (i = 0; i < 50; i++){
        if (game.enemies[i].state){
            filledCircleRGBA(hardware.screen, game.enemies[i].x, game.enemies[i].y, 2, 0xff, 0xff, 0xff, 255);
            aacircleRGBA(hardware.screen, game.enemies[i].x, game.enemies[i].y, 2, 0xff, 0xff, 0xff, 255);
        }
    }

    filledCircleRGBA(hardware.screen, game.player.x, game.player.y, game.player.size + game.player.force_field, 0xff, 0xff, 0xff, 128);

    if (game.player.last_hurt && (SDL_GetTicks() - game.player.last_hurt < 1000)){
        filledCircleRGBA(hardware.screen, game.player.x, game.player.y, game.player.size, 0xff, 0xff, 0xff, 255);
        aacircleRGBA(hardware.screen, game.player.x, game.player.y, game.player.size, 0xff, 0xff, 0xff, 255);
    }
    else {
        filledCircleRGBA(hardware.screen, game.player.x, game.player.y, game.player.size, 0xff, 0xbb, 0xbb, 255);
        aacircleRGBA(hardware.screen, game.player.x, game.player.y, game.player.size, 0xff, 0xbb, 0xbb, 255);
    }

/*
    if (game.player.using_turbo){
        strcpy(msg, "TURBO");
        TTF_SizeText(hardware.medium_font, msg, &width, &height);
        print(hardware.screen, 10, 10, hardware.medium_font, msg, 255, 255, 255);
    }
*/

    snprintf(msg, 256, "%d", game.player.life);
    TTF_SizeText(hardware.big_font, msg, &width, &height);
    print(hardware.screen, WIDTH - 100, 10, hardware.big_font, msg, 0xff, 0xbb, 0xbb);

    snprintf(msg, 256, "%d", game.player.points);
    TTF_SizeText(hardware.big_font, msg, &width, &height);
    print(hardware.screen, WIDTH - width - 10, 10, hardware.big_font, msg, 255, 255, 255);
}

void display(){
    switch (game.state){
    case NO_GAME:
        SDL_FillRect(hardware.screen, NULL, BLACK);
        print_center(hardware.screen, hardware.big_font, "Press START to play", 255, 255, 255);
        break;
    case GAME_PAUSED:
        render_world();
        boxRGBA(hardware.screen, 0, 0, WIDTH, HEIGHT, 0, 0, 0, 200);
        print_center(hardware.screen, hardware.big_font, "Paused", 255, 255, 255);
        break;
    case GAME_PLAYING:
        render_world();
        break;
    case GAME_OVER:
        render_world();
        boxRGBA(hardware.screen, 0, 0, WIDTH, HEIGHT, 0, 0, 0, 200);
        print_center(hardware.screen, hardware.big_font, "GAME OVER", 255, 255, 255);
        break;
    }
    SDL_Flip(hardware.screen);
}

void update_game(){
    int dx = 0, dy = 0, i;
    int max_active_drops_count = 10, drops_to_activate, active_drops_count = 0;
    int max_active_enemies_count = 50, active_enemies_count = 0;

    // let the drops grow or die
    for (i = 0; i < 50; i++){
        if (game.drops[i].state == DROP_GROWING){
            game.drops[i].size++;
            if (game.drops[i].size >= game.drops[i].grown_size){
                game.drops[i].state = DROP_ACTIVE;
                game.drops[i].size = game.drops[i].grown_size;
            }
        }
        else if (game.drops[i].state == DROP_DYING){
            game.drops[i].size--;
            if (game.drops[i].size <= 1){
                game.drops[i].state = DROP_INACTIVE;
            }
        }
    }

    // Add drops if maximum not reached
    for (i = 0; i < 50; i++){
        active_drops_count += game.drops[i].state != DROP_INACTIVE;
    }
    drops_to_activate = max_active_drops_count - active_drops_count;
    for (i = 0; drops_to_activate && i < 50; i++){
        if (game.drops[i].state == DROP_INACTIVE){
            game.drops[i].state = DROP_GROWING;
            game.drops[i].grown_size = 5 + (random() % 30);
            game.drops[i].size = 1;
            game.drops[i].x = game.drops[i].grown_size + (random() % (WIDTH - 2 * game.drops[i].grown_size));
            game.drops[i].y = game.drops[i].grown_size + (random() % (HEIGHT - 2 * game.drops[i].grown_size));
            drops_to_activate--;
        }
    }

    // Do we absorb a drop ?
    for (i = 0; i < 50; i++){
        if (game.drops[i].state != DROP_INACTIVE && game.drops[i].state != DROP_DYING){
            if (collide(game.player.x, game.player.y, game.player.size, game.drops[i].x, game.drops[i].y, game.drops[i].size)){
                game.player.points += game.drops[i].size;
                game.player.energy_left += game.drops[i].size;
                game.drops[i].state = DROP_DYING;
            }
        }
    }

    // Do we need to interact with enemies ?
    for (i = 0; i < 50; i++){
        if (game.enemies[i].state){
            // We get hurt if we collide with enemies..
            if (collide(game.player.x, game.player.y, game.player.size, game.enemies[i].x, game.enemies[i].y, 2)){
                game.player.last_hurt = SDL_GetTicks();
                game.player.life--;
                game.enemies[i].state = ENEMY_INACTIVE;
                if (game.player.life == 0){
                    game.state = GAME_OVER;
                    return;
                }
            }
            // ..unless we USE THE FORCE
            else if (game.player.force_field && collide(game.player.x, game.player.y, game.player.size + game.player.force_field, game.enemies[i].x, game.enemies[i].y, 2)){
                game.enemies[i].state = ENEMY_INACTIVE;
            }
        }
    }

    // Make the Enemies chase us
    for (i = 0; i < 50; i++){
        if (game.enemies[i].state){
            if (game.enemies[i].x > game.player.x)
                game.enemies[i].x--;
            if (game.enemies[i].y > game.player.y)
                game.enemies[i].y--;
            if (game.enemies[i].x < game.player.x)
                game.enemies[i].x++;
            if (game.enemies[i].y < game.player.y)
                game.enemies[i].y++;
        }
    }

    // Add Enemies every 0.5s unless max reached
    for (i = 0; i < 50; i++){
        active_enemies_count += game.enemies[i].state != ENEMY_INACTIVE;
    }
    if ((SDL_GetTicks() - game.last_enemy_timestamp > 500) && active_enemies_count < max_active_enemies_count){
        for (i = 0; i < 50; i++){
            if (game.enemies[i].state == ENEMY_INACTIVE){
                game.enemies[i].state = ENEMY_ACTIVE;
                game.enemies[i].x = random() % 2 ? 10 : WIDTH - 10;
                game.enemies[i].y = random() % 2 ? 10 : HEIGHT - 10;
                game.last_enemy_timestamp = SDL_GetTicks();
                break;
            }
        }
    }

    // Use turbo ?
    game.player.speed = 2;
    game.player.using_turbo = 0;
    if (hardware.joystick_state.buttons[PSP_BUTTON_CIRCLE]){
        if (game.player.energy_left >= 0){
            game.player.energy_left--;
            game.player.speed = 4;
            game.player.using_turbo = 1;
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
    game.player.x += dx;
    game.player.y += dy;
    game.player.x = keep_inside(game.player.x, game.player.size, WIDTH - game.player.size);
    game.player.y = keep_inside(game.player.y, game.player.size, HEIGHT - game.player.size);

    // USE THE FORCE ?
    if (hardware.joystick_state.buttons[PSP_BUTTON_CROSS] && game.player.energy_left){
        game.player.energy_left--;
        game.player.force_field++;
    }
    else {
        game.player.force_field--;
    }
    game.player.force_field = keep_inside(game.player.force_field, 0, 20);
}

void loop(){
    SDL_Event event;
    FPSmanager fps_manager;

    SDL_initFramerate(&fps_manager);
    SDL_setFramerate(&fps_manager, 60);

    while (1){
        int up_event = 0;
        display();
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
        case NO_GAME:
            if (up_event && (event.jbutton.button == PSP_BUTTON_START || event.jbutton.button == PSP_BUTTON_CROSS))
               game.state = GAME_PLAYING;
            break;
        case GAME_PLAYING:
            if (up_event && event.jbutton.button == PSP_BUTTON_START)
               game.state = GAME_PAUSED;
            update_game();
            break;
        case GAME_PAUSED:
            if (up_event && (event.jbutton.button == PSP_BUTTON_START || event.jbutton.button == PSP_BUTTON_CROSS))
               game.state = GAME_PLAYING;
            break;
        case GAME_OVER:
            if (up_event && (event.jbutton.button == PSP_BUTTON_START)){
               reset_game();
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

