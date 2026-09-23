#include <nlibc.h>
#include <nexos/system.h>

enum {
    PRISM_SMOKE_TIMEOUT = 200u,
    PRISM_SMOKE_SURFACE_WIDTH = 208u,
    PRISM_SMOKE_SURFACE_HEIGHT = 92u,
    PRISM_SMOKE_CAP_DISPLAY = 1u << 7,
    PRISM_SMOKE_CAP_INPUT = 1u << 8,
    PRISM_SMOKE_SERVER_CAPS = PRISM_SMOKE_CAP_DISPLAY | PRISM_SMOKE_CAP_INPUT
};

#if defined(__i386__)
#define PRISM_SMOKE_SERVER_COMMAND "/cmd/prism32"
#else
#define PRISM_SMOKE_SERVER_COMMAND "/cmd/prism"
#endif

static int wants_shutdown(int argc, char **argv) {
    return argc >= 2 && strcmp(argv[1], "--shutdown") == 0;
}

static int replace_surface_for_resize(struct prism_client *client,
                                      uint32_t window_id,
                                      struct prism_surface *surface,
                                      const struct prism_event *event,
                                      const char *tag,
                                      uint32_t tint,
                                      uint32_t *sequence);

static void print_window(const char *label,
                         uint32_t id,
                         const struct prism_rect *rect) {
    printf("prismsmoke: %s id=%u rect=%d,%d %ux%u\n",
           label,
           id,
           rect->x,
           rect->y,
           rect->width,
           rect->height);
}

static void print_event(const struct prism_event *event) {
    if (event->type == PRISM_EVENT_FOCUS) {
        printf("prismsmoke: event focus window=%u focused=%u\n",
               event->window_id,
               event->pressed ? 1u : 0u);
    } else if (event->type == PRISM_EVENT_CLOSE_REQUEST) {
        printf("prismsmoke: event close-request window=%u\n", event->window_id);
    } else if (event->type == PRISM_EVENT_MOVE) {
        printf("prismsmoke: event move window=%u rect=%d,%d %ux%u content=%ux%u\n",
               event->window_id,
               event->rect.x,
               event->rect.y,
               event->rect.width,
               event->rect.height,
               event->content_width,
               event->content_height);
    } else if (event->type == PRISM_EVENT_RESIZE) {
        printf("prismsmoke: event resize window=%u rect=%d,%d %ux%u content=%ux%u\n",
               event->window_id,
               event->rect.x,
               event->rect.y,
               event->rect.width,
               event->rect.height,
               event->content_width,
               event->content_height);
    } else if (event->type == PRISM_EVENT_KEY && event->pressed) {
        printf("prismsmoke: event key window=%u key=%u ascii=%d repeat=%u mods=%u%u%u\n",
               event->window_id,
               event->keycode,
               (int)event->ascii,
               event->repeat ? 1u : 0u,
               event->shift ? 1u : 0u,
               event->ctrl ? 1u : 0u,
               event->alt ? 1u : 0u);
    } else if (event->type == PRISM_EVENT_MOUSE &&
               (event->pressed || event->released)) {
        printf("prismsmoke: event mouse window=%u pos=%d,%d buttons=%u press=%u release=%u\n",
               event->window_id,
               event->x,
               event->y,
               event->buttons,
               event->button_pressed,
               event->button_released);
    }
}

static void drain_events(struct prism_client *client,
                         uint32_t first_id,
                         uint32_t second_id,
                         struct prism_surface *surface_first,
                         struct prism_surface *surface_second,
                         int *first_closed,
                         int *second_closed,
                         uint32_t *surface_sequence) {
    struct prism_event event;

    for (;;) {
        int rc = prism_client_poll_event(client, &event);

        if (rc <= 0) {
            return;
        }
        print_event(&event);
        if (event.type == PRISM_EVENT_CLOSE_REQUEST) {
            if (event.window_id == first_id && !*first_closed) {
                if (prism_client_destroy_window(client,
                                                first_id,
                                                PRISM_SMOKE_TIMEOUT) == 0) {
                    *first_closed = 1;
                }
            } else if (event.window_id == second_id && !*second_closed) {
                if (prism_client_destroy_window(client,
                                                second_id,
                                                PRISM_SMOKE_TIMEOUT) == 0) {
                    *second_closed = 1;
                }
            }
        } else if (event.type == PRISM_EVENT_RESIZE) {
            int resize_rc = 0;

            if (event.window_id == first_id && !*first_closed) {
                resize_rc = replace_surface_for_resize(client,
                                                       first_id,
                                                       surface_first,
                                                       &event,
                                                       "a",
                                                       0x203860u,
                                                       surface_sequence);
            } else if (event.window_id == second_id && !*second_closed) {
                resize_rc = replace_surface_for_resize(client,
                                                       second_id,
                                                       surface_second,
                                                       &event,
                                                       "b",
                                                       0x402020u,
                                                       surface_sequence);
            }
            if (resize_rc != 0) {
                eprintf("prismsmoke: resize surface failed rc=%d\n",
                        resize_rc);
            }
        }
    }
}

static void paint_surface(struct prism_surface *surface, uint32_t tint) {
    for (uint32_t y = 0u; y < surface->height; y++) {
        for (uint32_t x = 0u; x < surface->width; x++) {
            uint32_t stripe = ((x / 12u) + (y / 12u)) & 1u;
            uint32_t red = ((tint >> 16) & 0xffu) + x * 48u / surface->width;
            uint32_t green = ((tint >> 8) & 0xffu) + y * 48u / surface->height;
            uint32_t blue = (tint & 0xffu) + (stripe ? 48u : 8u);

            if (red > 255u) {
                red = 255u;
            }
            if (green > 255u) {
                green = 255u;
            }
            if (blue > 255u) {
                blue = 255u;
            }
            surface->pixels[y * (surface->pitch / sizeof(uint32_t)) + x] =
                (red << 16) | (green << 8) | blue;
        }
    }
}

static int replace_surface_for_resize(struct prism_client *client,
                                      uint32_t window_id,
                                      struct prism_surface *surface,
                                      const struct prism_event *event,
                                      const char *tag,
                                      uint32_t tint,
                                      uint32_t *sequence) {
    struct prism_surface next;
    char name[PRISM_SURFACE_NAME_MAX + 1u];
    int rc;

    if (!prism_event_needs_surface_resize(event, surface)) {
        return 0;
    }
    memset(&next, 0, sizeof(next));
    (void)snprintf(name,
                   sizeof(name),
                   "prism.s.%u.%s.%u",
                   (uint32_t)getpid(),
                   tag,
                   ++*sequence);
    rc = prism_client_create_surface(&next,
                                     name,
                                     event->content_width,
                                     event->content_height);
    if (rc != 0) {
        return rc;
    }
    paint_surface(&next, tint);
    rc = prism_client_attach_resized_surface(client,
                                             window_id,
                                             &next,
                                             event,
                                             PRISM_SMOKE_TIMEOUT);
    if (rc != 0) {
        prism_client_destroy_surface(&next);
        return rc;
    }
    prism_client_destroy_surface(surface);
    *surface = next;
    return prism_client_present_surface(client,
                                        window_id,
                                        surface,
                                        PRISM_SMOKE_TIMEOUT);
}

static int connect_with_retry(struct prism_client *client, uint32_t attempts) {
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

static int spawn_server_with_caps(void) {
    uint32_t original_caps = 0u;
    uint32_t restore_mask;
    int rc;

    (void)capability_spawn_clear();
    rc = capability_get(&original_caps);
    if (rc != 0) {
        return rc;
    }
    rc = capability_auth_grant(PRISM_SMOKE_SERVER_CAPS, "");
    if (rc < 0) {
        return rc;
    }
    rc = capability_spawn_set(PRISM_SMOKE_SERVER_CAPS);
    if (rc < 0) {
        (void)capability_spawn_clear();
        restore_mask = PRISM_SMOKE_SERVER_CAPS & ~original_caps;
        if (restore_mask != 0u) {
            (void)capability_drop(restore_mask);
        }
        return rc;
    }
    rc = spawn(PRISM_SMOKE_SERVER_COMMAND, SYS_SPAWN_AUTO, SYS_SPAWN_BACKGROUND);
    (void)capability_spawn_clear();
    restore_mask = PRISM_SMOKE_SERVER_CAPS & ~original_caps;
    if (restore_mask != 0u) {
        (void)capability_drop(restore_mask);
    }
    return rc;
}

static int connect_or_start_server(struct prism_client *client) {
    int rc = connect_with_retry(client, 4u);

    if (rc == 0) {
        return 0;
    }
    printf("prismsmoke: start server command=%s\n", PRISM_SMOKE_SERVER_COMMAND);
    rc = spawn_server_with_caps();
    if (rc <= 0) {
        eprintf("prismsmoke: server start failed rc=%d\n", rc);
        return rc != 0 ? rc : -1;
    }
    return connect_with_retry(client, 40u);
}

int main(int argc, char **argv) {
    struct prism_client client;
    struct prism_surface surface_first;
    struct prism_surface surface_second;
    struct prism_rect first;
    struct prism_rect second;
    struct prism_rect actual_first;
    struct prism_rect actual_second;
    uint32_t first_id = 0u;
    uint32_t second_id = 0u;
    char first_surface_name[PRISM_SURFACE_NAME_MAX + 1u];
    char second_surface_name[PRISM_SURFACE_NAME_MAX + 1u];
    char queue_name[PRISM_SERVER_QUEUE_MAX + 1u];
    int rc;
    int ok = 0;
    int first_closed = 0;
    int second_closed = 0;
    uint32_t surface_sequence = 0u;

    memset(&surface_first, 0, sizeof(surface_first));
    memset(&surface_second, 0, sizeof(surface_second));
    if (prism_server_queue_for_current_tty(queue_name, sizeof(queue_name)) != 0) {
        prism_client_copy_text(queue_name, sizeof(queue_name), PRISM_SERVER_QUEUE);
    }
    printf("prismsmoke: connect queue=%s\n", queue_name);
    rc = connect_or_start_server(&client);
    if (rc != 0) {
        eprintf("prismsmoke: connect failed rc=%d\n", rc);
        return 1;
    }

    rc = prism_client_hello(&client, PRISM_SMOKE_TIMEOUT);
    if (rc != 0) {
        eprintf("prismsmoke: hello failed rc=%d\n", rc);
        goto out;
    }

    first.x = 80;
    first.y = 72;
    first.width = 260u;
    first.height = 150u;
    rc = prism_client_create_window(&client,
                                    &first,
                                    "Smoke A",
                                    &first_id,
                                    &actual_first,
                                    PRISM_SMOKE_TIMEOUT);
    if (rc != 0 || first_id == 0u) {
        eprintf("prismsmoke: create A failed rc=%d id=%u\n", rc, first_id);
        goto out;
    }
    print_window("created A", first_id, &actual_first);
    (void)snprintf(first_surface_name,
                   sizeof(first_surface_name),
                   "prism.s.%u.a",
                   (uint32_t)getpid());
    rc = prism_client_create_surface(&surface_first,
                                     first_surface_name,
                                     PRISM_SMOKE_SURFACE_WIDTH,
                                     PRISM_SMOKE_SURFACE_HEIGHT);
    if (rc != 0) {
        eprintf("prismsmoke: surface A failed rc=%d\n", rc);
        goto out;
    }
    paint_surface(&surface_first, 0x203860u);
    rc = prism_client_attach_surface(&client,
                                     first_id,
                                     &surface_first,
                                     PRISM_SMOKE_TIMEOUT);
    if (rc != 0) {
        eprintf("prismsmoke: attach A failed rc=%d\n", rc);
        goto out;
    }

    second.x = 180;
    second.y = 128;
    second.width = 240u;
    second.height = 132u;
    rc = prism_client_create_window(&client,
                                    &second,
                                    "Smoke B",
                                    &second_id,
                                    &actual_second,
                                    PRISM_SMOKE_TIMEOUT);
    if (rc != 0 || second_id == 0u) {
        eprintf("prismsmoke: create B failed rc=%d id=%u\n", rc, second_id);
        goto out;
    }
    print_window("created B", second_id, &actual_second);
    (void)snprintf(second_surface_name,
                   sizeof(second_surface_name),
                   "prism.s.%u.b",
                   (uint32_t)getpid());
    rc = prism_client_create_surface(&surface_second,
                                     second_surface_name,
                                     PRISM_SMOKE_SURFACE_WIDTH,
                                     PRISM_SMOKE_SURFACE_HEIGHT);
    if (rc != 0) {
        eprintf("prismsmoke: surface B failed rc=%d\n", rc);
        goto out;
    }
    paint_surface(&surface_second, 0x402020u);
    rc = prism_client_attach_surface(&client,
                                     second_id,
                                     &surface_second,
                                     PRISM_SMOKE_TIMEOUT);
    if (rc != 0) {
        eprintf("prismsmoke: attach B failed rc=%d\n", rc);
        goto out;
    }

    rc = prism_client_present_surface(&client,
                                      first_id,
                                      &surface_first,
                                      PRISM_SMOKE_TIMEOUT);
    if (rc != 0) {
        eprintf("prismsmoke: present A failed rc=%d\n", rc);
        goto out;
    }
    rc = prism_client_present_surface(&client,
                                      second_id,
                                      &surface_second,
                                      PRISM_SMOKE_TIMEOUT);
    if (rc != 0) {
        eprintf("prismsmoke: present B failed rc=%d\n", rc);
        goto out;
    }
    rc = prism_client_move_window(&client,
                                  first_id,
                                  actual_first.x + 16,
                                  actual_first.y + 12,
                                  PRISM_SMOKE_TIMEOUT);
    if (rc != 0) {
        eprintf("prismsmoke: move A failed rc=%d\n", rc);
        goto out;
    }
    rc = prism_client_resize_window(&client,
                                    second_id,
                                    second.width + 32u,
                                    second.height + 24u,
                                    PRISM_SMOKE_TIMEOUT);
    if (rc != 0) {
        eprintf("prismsmoke: resize B failed rc=%d\n", rc);
        goto out;
    }

    for (uint32_t i = 0u; i < 40u; i++) {
        drain_events(&client,
                     first_id,
                     second_id,
                     &surface_first,
                     &surface_second,
                     &first_closed,
                     &second_closed,
                     &surface_sequence);
        sleep(2u);
    }

    if (!first_closed) {
        rc = prism_client_destroy_window(&client, first_id, PRISM_SMOKE_TIMEOUT);
        if (rc != 0) {
            eprintf("prismsmoke: destroy A failed rc=%d\n", rc);
            goto out;
        }
    }
    if (!second_closed) {
        rc = prism_client_destroy_window(&client, second_id, PRISM_SMOKE_TIMEOUT);
        if (rc != 0) {
            eprintf("prismsmoke: destroy B failed rc=%d\n", rc);
            goto out;
        }
    }

    if (wants_shutdown(argc, argv)) {
        rc = prism_client_shutdown(&client, PRISM_SMOKE_TIMEOUT);
        if (rc != 0) {
            eprintf("prismsmoke: shutdown failed rc=%d\n", rc);
            goto out;
        }
    }

    ok = 1;

out:
    prism_client_destroy_surface(&surface_first);
    prism_client_destroy_surface(&surface_second);
    prism_client_disconnect(&client);
    printf("prismsmoke: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
