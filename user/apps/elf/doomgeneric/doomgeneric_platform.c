#include "user/libc/include/nlibc.h"
#include "doomgeneric/doomgeneric.h"
#include "doomgeneric/doomkeys.h"
#include "doomgeneric/i_system.h"

static struct syscall_gfx_info g_doom_gfx;
static struct syscall_gui_event_cursor g_doom_cursor;
static int32_t g_doom_x;
static int32_t g_doom_y;
static int g_doom_input_grabbed;

static void doom_release_input(void) {
    if (g_doom_input_grabbed) {
        (void)gui_input_release();
        g_doom_input_grabbed = 0;
    }
}

static unsigned char doom_key_from_event(const struct syscall_gui_event *event) {
    if (event->keycode >= SYS_KEY_A && event->keycode <= SYS_KEY_Z) {
        return (unsigned char)('a' + event->keycode - SYS_KEY_A);
    }
    if (event->keycode >= SYS_KEY_0 && event->keycode <= SYS_KEY_9) {
        return (unsigned char)('0' + event->keycode - SYS_KEY_0);
    }
    switch (event->keycode) {
        case SYS_KEY_ESC: return KEY_ESCAPE;
        case SYS_KEY_TAB: return KEY_TAB;
        case SYS_KEY_ENTER: return KEY_ENTER;
        case SYS_KEY_BACKSPACE: return KEY_BACKSPACE;
        case SYS_KEY_SPACE: return KEY_USE;
        case SYS_KEY_HOME: return KEY_HOME;
        case SYS_KEY_END: return KEY_END;
        case SYS_KEY_DELETE: return KEY_DEL;
        case SYS_KEY_PAGE_UP: return KEY_PGUP;
        case SYS_KEY_PAGE_DOWN: return KEY_PGDN;
        case SYS_KEY_F1: return KEY_F1;
        case SYS_KEY_F2: return KEY_F2;
        case SYS_KEY_F3: return KEY_F3;
        case SYS_KEY_UP: return KEY_UPARROW;
        case SYS_KEY_DOWN: return KEY_DOWNARROW;
        case SYS_KEY_LEFT: return KEY_LEFTARROW;
        case SYS_KEY_RIGHT: return KEY_RIGHTARROW;
        case SYS_KEY_LEFT_SHIFT:
        case SYS_KEY_RIGHT_SHIFT:
            return KEY_RSHIFT;
        case SYS_KEY_LEFT_CTRL:
        case SYS_KEY_RIGHT_CTRL:
            return KEY_FIRE;
        case SYS_KEY_LEFT_ALT:
        case SYS_KEY_RIGHT_ALT:
            return KEY_RALT;
        case SYS_KEY_CAPS_LOCK: return KEY_CAPSLOCK;
        case SYS_KEY_NUM_LOCK: return KEY_NUMLOCK;
        case SYS_KEY_SCROLL_LOCK: return KEY_SCRLCK;
        case SYS_KEY_MINUS: return '-';
        case SYS_KEY_EQUAL: return '=';
        case SYS_KEY_LEFT_BRACKET: return '[';
        case SYS_KEY_RIGHT_BRACKET: return ']';
        case SYS_KEY_BACKSLASH: return '\\';
        case SYS_KEY_SEMICOLON: return ';';
        case SYS_KEY_APOSTROPHE: return '\'';
        case SYS_KEY_GRAVE: return '`';
        case SYS_KEY_COMMA: return ',';
        case SYS_KEY_PERIOD: return '.';
        case SYS_KEY_SLASH: return '/';
        default:
            return event->ascii != '\0' ? (unsigned char)event->ascii : 0;
    }
}

void DG_Init(void) {
    if (gfx_info(&g_doom_gfx) != 0 ||
        g_doom_gfx.width < DOOMGENERIC_RESX ||
        g_doom_gfx.height < DOOMGENERIC_RESY) {
        I_Error("doomgeneric: requires at least %ux%u graphics",
                DOOMGENERIC_RESX,
                DOOMGENERIC_RESY);
    }
    if (gui_input_grab() != 0) {
        I_Error("doomgeneric: could not acquire input focus");
    }
    g_doom_input_grabbed = 1;
    (void)gui_event_cursor_init(&g_doom_cursor);
    I_AtExit(doom_release_input, true);

    g_doom_x = ((int32_t)g_doom_gfx.width - DOOMGENERIC_RESX) / 2;
    g_doom_y = ((int32_t)g_doom_gfx.height - DOOMGENERIC_RESY) / 2;
    gfx_clear(0);
    gfx_present();
}

void DG_DrawFrame(void) {
    if (gfx_blit(DG_ScreenBuffer,
                 DOOMGENERIC_RESX * sizeof(*DG_ScreenBuffer),
                 g_doom_x,
                 g_doom_y,
                 DOOMGENERIC_RESX,
                 DOOMGENERIC_RESY) != 0 ||
        gfx_present() != 0) {
        I_Error("doomgeneric: graphics update failed");
    }
}

void DG_SleepMs(uint32_t ms) {
    uint32_t timer_ticks = (ms + 9u) / 10u;

    if (timer_ticks == 0u) {
        yield();
    } else {
        sleep(timer_ticks);
    }
}

uint32_t DG_GetTicksMs(void) {
    return ticks() * 10u;
}

int DG_GetKey(int *pressed, unsigned char *doom_key) {
    struct syscall_gui_event event;

    while (gui_poll_event_with_cursor(&g_doom_cursor, &event) == SYS_GUI_EVENT_READY) {
        unsigned char key;

        if (event.type != SYS_GUI_EVENT_KEY) {
            continue;
        }
        key = doom_key_from_event(&event);
        if (key == 0) {
            continue;
        }
        *pressed = event.pressed != 0;
        *doom_key = key;
        return 1;
    }
    return 0;
}

void DG_SetWindowTitle(const char *title) {
    (void)title;
}
