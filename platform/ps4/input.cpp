// Native ScePad input routed through the engine's existing SDL event handler.
#include <SDL.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <stdio.h>
#include <stdlib.h>

extern bool ss_ps4_gameplay_input();
extern "C" void ss_ps4_load_modules(void);

void ss_ps4_poll_pad()
{
    static int handle = -1;
    static Uint32 previousTime = 0, nextOpen = 0;
    static bool keys[SDL_NUM_SCANCODES] = {};
    static float remainderX = 0, remainderY = 0;
    const Uint32 now = SDL_GetTicks();
    const Uint32 elapsed = now - previousTime;
    if (elapsed < 8) return;
    previousTime = now;
    if (handle < 0 && Sint32(now - nextOpen) >= 0) {
        nextOpen = now + 1000;
        ss_ps4_load_modules();
        sceUserServiceInitialize(nullptr);
        int user = -1;
        if (sceUserServiceGetInitialUser(&user) >= 0) {
            scePadInit();
            handle = scePadOpen(user, ORBIS_PAD_PORT_TYPE_STANDARD, 0, nullptr);
            if (uint32_t(handle) == ORBIS_PAD_ERROR_ALREADY_OPENED)
                handle = scePadGetHandle(user, ORBIS_PAD_PORT_TYPE_STANDARD, 0);
            if (handle <= 0) handle = -1;
            else fprintf(stderr, "PS4 PAD ready handle=%d\n", handle);
        }
    }
    OrbisPadData pad = {};
    const bool active = handle >= 0 && scePadReadState(handle, &pad) >= 0 && pad.connected;
    const bool game = ss_ps4_gameplay_input();
    bool next[SDL_NUM_SCANCODES] = {};
    if (active) {
        next[SDL_SCANCODE_ESCAPE] = pad.buttons & ORBIS_PAD_BUTTON_OPTIONS;
        next[game ? SDL_SCANCODE_SPACE : SDL_SCANCODE_RETURN] = pad.buttons & ORBIS_PAD_BUTTON_CROSS;
        next[game ? SDL_SCANCODE_F : SDL_SCANCODE_ESCAPE] |= bool(pad.buttons & ORBIS_PAD_BUTTON_CIRCLE);
        next[SDL_SCANCODE_UP] = pad.buttons & ORBIS_PAD_BUTTON_UP;
        next[SDL_SCANCODE_DOWN] = pad.buttons & ORBIS_PAD_BUTTON_DOWN;
        next[SDL_SCANCODE_LEFT] = pad.buttons & ORBIS_PAD_BUTTON_LEFT;
        next[SDL_SCANCODE_RIGHT] = pad.buttons & ORBIS_PAD_BUTTON_RIGHT;
        next[game ? SDL_SCANCODE_W : SDL_SCANCODE_UP] |= pad.leftStick.y < 96;
        next[game ? SDL_SCANCODE_S : SDL_SCANCODE_DOWN] |= pad.leftStick.y > 160;
        next[game ? SDL_SCANCODE_A : SDL_SCANCODE_LEFT] |= pad.leftStick.x < 96;
        next[game ? SDL_SCANCODE_D : SDL_SCANCODE_RIGHT] |= pad.leftStick.x > 160;
        if (game) {
            next[SDL_SCANCODE_F6] = pad.buttons & ORBIS_PAD_BUTTON_TOUCH_PAD;
            next[SDL_SCANCODE_LSHIFT] = pad.buttons & ORBIS_PAD_BUTTON_L3;
#ifdef SS_PS4_TSE
            // TSE's extra weapons need scope adjustment and the Serious Bomb.
            next[SDL_SCANCODE_UP] = next[SDL_SCANCODE_DOWN] = false;
            next[SDL_SCANCODE_X] = pad.buttons & ORBIS_PAD_BUTTON_UP;
            next[SDL_SCANCODE_Z] = pad.buttons & ORBIS_PAD_BUTTON_DOWN;
            next[SDL_SCANCODE_0] = pad.buttons & ORBIS_PAD_BUTTON_R3;
#endif
            next[SDL_SCANCODE_LCTRL] = (pad.buttons & ORBIS_PAD_BUTTON_R2) || pad.analogButtons.r2 >= 32;
            next[SDL_SCANCODE_RETURN] = (pad.buttons & ORBIS_PAD_BUTTON_L2) || pad.analogButtons.l2 >= 32;
            next[SDL_SCANCODE_LALT] = pad.buttons & ORBIS_PAD_BUTTON_SQUARE;
            next[SDL_SCANCODE_LEFTBRACKET] = pad.buttons & ORBIS_PAD_BUTTON_L1;
            next[SDL_SCANCODE_RIGHTBRACKET] = pad.buttons & ORBIS_PAD_BUTTON_R1;
            next[SDL_SCANCODE_H] = pad.buttons & ORBIS_PAD_BUTTON_TRIANGLE;
            int x = int(pad.rightStick.x) - 128, y = int(pad.rightStick.y) - 128;
            if (abs(x) < 24) x = 0;
            if (abs(y) < 24) y = 0;
            const float dt = (elapsed > 50 ? 50 : elapsed) / 1000.0f;
            remainderX += x / 128.0f * 500.0f * dt;
            remainderY += y / 128.0f * 500.0f * dt;
            SDL_Event motion = {};
            motion.type = SDL_MOUSEMOTION;
            motion.motion.xrel = int(remainderX);
            motion.motion.yrel = int(remainderY);
            remainderX -= motion.motion.xrel;
            remainderY -= motion.motion.yrel;
            if (motion.motion.xrel || motion.motion.yrel) SDL_PushEvent(&motion);
        }
    }
    if (!active || !game) remainderX = remainderY = 0;
    for (int k = 0; k < SDL_NUM_SCANCODES; ++k) {
        if (keys[k] == next[k]) continue;
        SDL_Event event = {};
        event.type = next[k] ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.state = next[k] ? SDL_PRESSED : SDL_RELEASED;
        event.key.keysym.scancode = SDL_Scancode(k);
        event.key.keysym.sym = SDL_GetKeyFromScancode(SDL_Scancode(k));
        if (SDL_PushEvent(&event) == 1) {
            keys[k] = next[k];
            fprintf(stderr, "PS4 PAD scancode=%d down=%d game=%d\n", k, int(next[k]), int(game));
        }
    }
}
