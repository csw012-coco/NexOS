#include "drivers/audio/ac97.h"
#include "drivers/audio/hda.h"
#include "lib/string.h"

static struct ac97_status g_i386_published_ac97_status;
static struct hda_status g_i386_published_hda_status;

int ac97_init(void) {
    return 0;
}

int ac97_query_status(struct ac97_status *out) {
    if (out == 0) {
        return 0;
    }
    *out = g_i386_published_ac97_status;
    return g_i386_published_ac97_status.present != 0u;
}

int ac97_publish_status(const struct ac97_status *status) {
    if (status == 0) {
        return 0;
    }
    g_i386_published_ac97_status = *status;
    return 1;
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
