#include "drivers/audio/ac97.h"
#include "drivers/audio/hda.h"
#include "drivers/net/rtl8139.h"
#include "lib/string.h"

static struct hda_status g_i386_published_hda_status;

int ac97_init(void) {
    return 0;
}

int ac97_query_status(struct ac97_status *out) {
    if (out == 0) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    return 0;
}

int hda_init(void) {
    return 0;
}

int hda_query_status(struct hda_status *out) {
    if (out == 0) {
        return 0;
    }
    *out = g_i386_published_hda_status;
    return g_i386_published_hda_status.present != 0u;
}

int hda_i386_publish_status(const struct hda_status *status) {
    if (status == 0) {
        return 0;
    }
    g_i386_published_hda_status = *status;
    return 1;
}

int rtl8139_init(void) {
    return 0;
}

int rtl8139_query_status(struct rtl8139_status *out) {
    if (out == 0) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    return 0;
}

int rtl8139_send_frame(const uint8_t *data, uint32_t bytes) {
    (void)data;
    (void)bytes;
    return 0;
}

int rtl8139_send_test_frame(void) {
    return 0;
}

int rtl8139_receive_packet(struct rtl8139_rx_packet *out) {
    if (out == 0) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    return 0;
}

int rtl8139_handle_irq(uint8_t irq_line) {
    (void)irq_line;
    return 0;
}
