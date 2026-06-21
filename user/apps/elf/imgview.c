#include "user/libc/include/nlibc.h"
#include "user/libc/include/ctype.h"

enum {
    IMGVIEW_DEFAULT_WIDTH = 640,
    IMGVIEW_DEFAULT_HEIGHT = 480,
    IMGVIEW_MAX_WIDTH = 1920,
    IMGVIEW_CHUNK_PIXELS = 256
};

static int read_exact(int fd, void *buffer, uint32_t bytes) {
    uint8_t *out = (uint8_t *)buffer;
    uint32_t offset = 0;

    while (offset < bytes) {
        ssize_t got = read(fd, out + offset, bytes - offset);

        if (got < 0) {
            return -1;
        }
        if (got == 0) {
            return 0;
        }
        offset += (uint32_t)got;
    }
    return 1;
}

struct imgview_image {
    const char *path;
    uint32_t width;
    uint32_t height;
    long data_offset;
    int ppm;
};

static uint32_t parse_dim(const char *text) {
    unsigned long value = strtoul(text, NULL, 10);

    if (value == 0ul || value > 65535ul) {
        return 0;
    }
    return (uint32_t)value;
}

static int read_byte(int fd, char *ch) {
    ssize_t got = read(fd, ch, 1u);

    return got == 1 ? 1 : got == 0 ? 0 : -1;
}

static int ppm_next_token(int fd, char *token, uint32_t token_size) {
    uint32_t len = 0;
    char ch;
    int rc;

    if (token_size == 0u) {
        return 0;
    }
    for (;;) {
        rc = read_byte(fd, &ch);
        if (rc <= 0) {
            return 0;
        }
        if (ch == '#') {
            do {
                rc = read_byte(fd, &ch);
                if (rc <= 0) {
                    return 0;
                }
            } while (ch != '\n' && ch != '\r');
            continue;
        }
        if (!isspace((unsigned char)ch)) {
            break;
        }
    }

    for (;;) {
        if (len + 1u >= token_size) {
            return 0;
        }
        token[len++] = ch;
        rc = read_byte(fd, &ch);
        if (rc < 0) {
            return 0;
        }
        if (rc == 0 || isspace((unsigned char)ch)) {
            token[len] = '\0';
            return 1;
        }
        if (ch == '#') {
            do {
                rc = read_byte(fd, &ch);
                if (rc <= 0) {
                    return 0;
                }
            } while (ch != '\n' && ch != '\r');
            token[len] = '\0';
            return 1;
        }
    }
}

static int ppm_probe(struct imgview_image *image) {
    char magic[2];
    char token[24];
    uint32_t maxval;
    int fd = open(image->path, O_RDONLY);

    if (fd < 0) {
        eprintf("imgview: open failed: %s\n", image->path);
        return -1;
    }
    if (read_exact(fd, magic, sizeof(magic)) != 1) {
        close((uint32_t)fd);
        return 0;
    }
    if (magic[0] != 'P' || magic[1] != '6') {
        close((uint32_t)fd);
        return 0;
    }
    if (!ppm_next_token(fd, token, sizeof(token))) {
        eprintf("imgview: invalid PPM width\n");
        close((uint32_t)fd);
        return -1;
    }
    image->width = parse_dim(token);
    if (!ppm_next_token(fd, token, sizeof(token))) {
        eprintf("imgview: invalid PPM height\n");
        close((uint32_t)fd);
        return -1;
    }
    image->height = parse_dim(token);
    if (!ppm_next_token(fd, token, sizeof(token))) {
        eprintf("imgview: invalid PPM maxval\n");
        close((uint32_t)fd);
        return -1;
    }
    maxval = parse_dim(token);
    if (image->width == 0u || image->height == 0u || maxval != 255u) {
        eprintf("imgview: unsupported PPM header\n");
        close((uint32_t)fd);
        return -1;
    }
    image->data_offset = lseek(fd, 0, SEEK_CUR);
    image->ppm = 1;
    close((uint32_t)fd);
    return 1;
}

static int draw_rgb24(const struct imgview_image *image, int32_t dst_x, int32_t dst_y) {
    uint8_t rgb[IMGVIEW_CHUNK_PIXELS * 3u];
    uint32_t pixels[IMGVIEW_CHUNK_PIXELS];
    int fd;

    fd = open(image->path, O_RDONLY);
    if (fd < 0) {
        eprintf("imgview: open failed: %s\n", image->path);
        return -1;
    }
    if (image->data_offset > 0 && lseek(fd, image->data_offset, SEEK_SET) < 0) {
        eprintf("imgview: seek failed\n");
        close((uint32_t)fd);
        return -1;
    }

    for (uint32_t y = 0; y < image->height; y++) {
        for (uint32_t x = 0; x < image->width;) {
            uint32_t chunk = image->width - x;
            int rc;

            if (chunk > IMGVIEW_CHUNK_PIXELS) {
                chunk = IMGVIEW_CHUNK_PIXELS;
            }
            rc = read_exact(fd, rgb, chunk * 3u);
            if (rc <= 0) {
                eprintf("imgview: short read at row %u\n", y);
                close((uint32_t)fd);
                return -1;
            }
            for (uint32_t i = 0; i < chunk; i++) {
                uint32_t r = rgb[i * 3u + 0u];
                uint32_t g = rgb[i * 3u + 1u];
                uint32_t b = rgb[i * 3u + 2u];

                pixels[i] = (r << 16) | (g << 8) | b;
            }
            if (gfx_blit(pixels,
                         chunk * sizeof(pixels[0]),
                         dst_x + (int32_t)x,
                         dst_y + (int32_t)y,
                         chunk,
                         1u) != 0) {
                eprintf("imgview: render failed at row %u\n", y);
                close((uint32_t)fd);
                return -1;
            }
            x += chunk;
        }
    }

    close((uint32_t)fd);
    return 0;
}

static void draw_checker(uint32_t screen_w, uint32_t screen_h) {
    (void)screen_w;
    (void)screen_h;
    gfx_clear(0x0f172au);
}

static void wait_for_exit(void) {
    struct syscall_gui_event event;

    if (gui_input_grab() != 0) {
        sleep(250u);
        return;
    }

    for (;;) {
        while (gui_poll_event(&event) == SYS_GUI_EVENT_READY) {
            if (event.type == SYS_GUI_EVENT_KEY && event.pressed) {
                (void)gui_input_release();
                return;
            }
        }
        sleep(2u);
    }
}

int main(int argc, char **argv) {
    struct imgview_image image;
    clear();

    struct syscall_gfx_info info;
    int32_t x;
    int32_t y;

    image.path = "/a.rgb";
    image.width = IMGVIEW_DEFAULT_WIDTH;
    image.height = IMGVIEW_DEFAULT_HEIGHT;
    image.data_offset = 0;
    image.ppm = 0;

    if (argc >= 2) {
        image.path = argv[1];
    }
    if (argc >= 4) {
        image.width = parse_dim(argv[2]);
        image.height = parse_dim(argv[3]);
        if (image.width == 0u || image.height == 0u) {
            eprintf("usage: imgview [file.ppm | rgb24-file width height]\n");
            return 1;
        }
    } else if (argc == 2) {
        int probe = ppm_probe(&image);

        if (probe < 0) {
            return 1;
        }
        if (probe == 0) {
            eprintf("usage: imgview [file.ppm | rgb24-file width height]\n");
            return 1;
        }
    } else if (argc != 1) {
        eprintf("usage: imgview [file.ppm | rgb24-file width height]\n");
        return 1;
    }

    if (image.width > IMGVIEW_MAX_WIDTH) {
        eprintf("imgview: image width too large\n");
        return 1;
    }
    if (gfx_info(&info) != 0 || info.width == 0u || info.height == 0u) {
        eprintf("imgview: framebuffer graphics unavailable\n");
        return 1;
    }

    x = info.width > image.width ? (int32_t)((info.width - image.width) / 2u) : 0;
    y = info.height > image.height ? (int32_t)((info.height - image.height) / 2u) : 0;
    draw_checker(info.width, info.height);
    if (draw_rgb24(&image, x, y) != 0 || gfx_present() != 0) {
        return 1;
    }

    printf("imgview: showing %s (%ux%u%s), press any key to exit\n",
           image.path,
           image.width,
           image.height,
           image.ppm ? " PPM" : "");
    wait_for_exit();
    return 0;
}
