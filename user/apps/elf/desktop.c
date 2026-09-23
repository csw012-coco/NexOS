#include <nlibc.h>
#include <nexos/system.h>

enum {
    DESKTOP_TIMEOUT = 200u,
    DESKTOP_MIN_WIDTH = 320u,
    DESKTOP_MIN_HEIGHT = 200u,
    DESKTOP_SERVER_CAPS = SYS_PROC_CAP_DISPLAY | SYS_PROC_CAP_INPUT
};

#if defined(__i386__)
#define DESKTOP_SERVER_COMMAND "/cmd/prism32"
#else
#define DESKTOP_SERVER_COMMAND "/cmd/prism"
#endif

static int desktop_connect_with_retry(struct prism_client *client,
                                      uint32_t attempts) {
    int rc = -1;

    for (uint32_t i = 0u; i < attempts; i++) {
        rc = prism_client_connect(client);
        if (rc == 0) {
            return 0;
        }
        sleep(2u);
    }
    return rc;
}

static int desktop_spawn_server_with_caps(void) {
    uint32_t original_caps = 0u;
    uint32_t restore_mask;
    int rc;

    (void)capability_spawn_clear();
    rc = capability_get(&original_caps);
    if (rc != 0) {
        return rc;
    }
    rc = capability_auth_grant(DESKTOP_SERVER_CAPS, "");
    if (rc < 0) {
        return rc;
    }
    rc = capability_spawn_set(DESKTOP_SERVER_CAPS);
    if (rc < 0) {
        (void)capability_spawn_clear();
        restore_mask = DESKTOP_SERVER_CAPS & ~original_caps;
        if (restore_mask != 0u) {
            (void)capability_drop(restore_mask);
        }
        return rc;
    }
    rc = spawn(DESKTOP_SERVER_COMMAND, SYS_SPAWN_AUTO, SYS_SPAWN_BACKGROUND);
    (void)capability_spawn_clear();
    restore_mask = DESKTOP_SERVER_CAPS & ~original_caps;
    if (restore_mask != 0u) {
        (void)capability_drop(restore_mask);
    }
    return rc;
}

static int desktop_connect_or_start_server(struct prism_client *client) {
    int rc = desktop_connect_with_retry(client, 4u);

    if (rc == 0) {
        return 0;
    }
    printf("desktop: start server command=%s\n", DESKTOP_SERVER_COMMAND);
    rc = desktop_spawn_server_with_caps();
    if (rc <= 0) {
        eprintf("desktop: server start failed rc=%d\n", rc);
        return rc != 0 ? rc : -1;
    }
    return desktop_connect_with_retry(client, 40u);
}

static void desktop_put_pixel(struct prism_surface *surface,
                              uint32_t x,
                              uint32_t y,
                              uint32_t rgb) {
    if (x >= surface->width || y >= surface->height) {
        return;
    }
    surface->pixels[y * (surface->pitch / sizeof(uint32_t)) + x] = rgb;
}

static void desktop_fill_rect(struct prism_surface *surface,
                              uint32_t x,
                              uint32_t y,
                              uint32_t width,
                              uint32_t height,
                              uint32_t rgb) {
    uint32_t x_end = x + width;
    uint32_t y_end = y + height;

    if (x_end < x || x_end > surface->width) {
        x_end = surface->width;
    }
    if (y_end < y || y_end > surface->height) {
        y_end = surface->height;
    }
    for (uint32_t py = y; py < y_end; py++) {
        for (uint32_t px = x; px < x_end; px++) {
            desktop_put_pixel(surface, px, py, rgb);
        }
    }
}

static void desktop_draw_rect(struct prism_surface *surface,
                              uint32_t x,
                              uint32_t y,
                              uint32_t width,
                              uint32_t height,
                              uint32_t rgb) {
    if (width == 0u || height == 0u) {
        return;
    }
    desktop_fill_rect(surface, x, y, width, 1u, rgb);
    desktop_fill_rect(surface, x, y + height - 1u, width, 1u, rgb);
    desktop_fill_rect(surface, x, y, 1u, height, rgb);
    desktop_fill_rect(surface, x + width - 1u, y, 1u, height, rgb);
}

static void desktop_paint(struct prism_surface *surface) {
    uint32_t panel_y = surface->height > 34u ? surface->height - 34u : 0u;

    for (uint32_t y = 0u; y < surface->height; y++) {
        uint32_t shade = surface->height > 1u ? y * 34u / surface->height : 0u;
        uint32_t rgb = (0x0bu << 16) | ((0x13u + shade) << 8) | (0x24u + shade);

        for (uint32_t x = 0u; x < surface->width; x++) {
            uint32_t glow = surface->width > 1u ? x * 18u / surface->width : 0u;
            desktop_put_pixel(surface, x, y, rgb + glow);
        }
    }

    desktop_fill_rect(surface, 0u, 0u, surface->width, 28u, 0x111827u);
    desktop_fill_rect(surface, 14u, 9u, 58u, 10u, 0x22c55eu);
    desktop_fill_rect(surface, 86u, 9u, 96u, 10u, 0x38bdf8u);
    desktop_fill_rect(surface, 198u, 9u, 54u, 10u, 0xf59e0bu);

    desktop_fill_rect(surface, 0u, panel_y, surface->width, surface->height - panel_y, 0x0f172au);
    desktop_fill_rect(surface, 16u, panel_y + 9u, 70u, 16u, 0x1f2937u);
    desktop_draw_rect(surface, 16u, panel_y + 9u, 70u, 16u, 0x64748bu);
    desktop_fill_rect(surface, 100u, panel_y + 9u, 46u, 16u, 0x334155u);
    desktop_fill_rect(surface, 158u, panel_y + 9u, 46u, 16u, 0x334155u);

    desktop_fill_rect(surface, 30u, 54u, 52u, 42u, 0xe5e7ebu);
    desktop_fill_rect(surface, 36u, 60u, 40u, 8u, 0x94a3b8u);
    desktop_fill_rect(surface, 36u, 74u, 30u, 8u, 0xcbd5e1u);
    desktop_draw_rect(surface, 30u, 54u, 52u, 42u, 0x0f172au);

    desktop_fill_rect(surface, 30u, 118u, 52u, 42u, 0xf8fafcu);
    desktop_fill_rect(surface, 36u, 124u, 20u, 20u, 0x38bdf8u);
    desktop_fill_rect(surface, 60u, 124u, 16u, 8u, 0x94a3b8u);
    desktop_fill_rect(surface, 60u, 136u, 16u, 8u, 0xcbd5e1u);
    desktop_draw_rect(surface, 30u, 118u, 52u, 42u, 0x0f172au);
}

int main(void) {
    struct prism_client client;
    struct prism_surface surface;
    struct prism_rect actual;
    prism_kernel_display_info display;
    char surface_name[PRISM_SURFACE_NAME_MAX + 1u];
    uint32_t window_id = 0u;
    uint32_t surface_width;
    uint32_t surface_height;
    int rc;

    memset(&client, 0, sizeof(client));
    memset(&surface, 0, sizeof(surface));
    memset(&display, 0, sizeof(display));

    rc = desktop_connect_or_start_server(&client);
    if (rc != 0) {
        eprintf("desktop: connect failed rc=%d\n", rc);
        return 1;
    }
    printf("desktop: connect queue=%s\n",
           prism_client_server_queue_name(&client));
    rc = prism_client_hello(&client, DESKTOP_TIMEOUT);
    if (rc != 0) {
        eprintf("desktop: hello failed rc=%d\n", rc);
        prism_client_disconnect(&client);
        return 1;
    }

    if (prism_kernel_display_info_query(&display) != 0 ||
        display.width < DESKTOP_MIN_WIDTH ||
        display.height < DESKTOP_MIN_HEIGHT) {
        eprintf("desktop: display unavailable\n");
        prism_client_disconnect(&client);
        return 1;
    }
    rc = prism_client_create_desktop_window(&client,
                                            &window_id,
                                            &actual,
                                            DESKTOP_TIMEOUT);
    if (rc != 0 || window_id == 0u) {
        eprintf("desktop: create failed rc=%d id=%u\n", rc, window_id);
        prism_client_disconnect(&client);
        return 1;
    }

    surface_width = actual.width;
    surface_height = actual.height;
    (void)snprintf(surface_name,
                   sizeof(surface_name),
                   "desktop.%u",
                   (uint32_t)getpid());
    rc = prism_client_create_surface(&surface,
                                     surface_name,
                                     surface_width,
                                     surface_height);
    if (rc != 0) {
        eprintf("desktop: surface failed rc=%d\n", rc);
        (void)prism_client_destroy_window(&client, window_id, DESKTOP_TIMEOUT);
        prism_client_disconnect(&client);
        return 1;
    }

    desktop_paint(&surface);
    rc = prism_client_attach_surface(&client, window_id, &surface, DESKTOP_TIMEOUT);
    if (rc == 0) {
        rc = prism_client_present_surface(&client,
                                          window_id,
                                          &surface,
                                          DESKTOP_TIMEOUT);
    }
    if (rc != 0) {
        eprintf("desktop: present failed rc=%d\n", rc);
        prism_client_destroy_surface(&surface);
        (void)prism_client_destroy_window(&client, window_id, DESKTOP_TIMEOUT);
        prism_client_disconnect(&client);
        return 1;
    }

    printf("desktop: online window=%u surface=%ux%u\n",
           window_id,
           surface.width,
           surface.height);
    for (;;) {
        sleep(100u);
    }
}
