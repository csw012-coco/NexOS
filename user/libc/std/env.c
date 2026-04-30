#include "user/libc/include/nlibc.h"

enum {
    NLIBC_ENV_MAX = 16u,
    NLIBC_ENV_TEXT_MAX = NOS_PATH_BUFFER_SIZE
};

static char g_env_storage[NLIBC_ENV_MAX][NLIBC_ENV_TEXT_MAX];
static char *g_env_slots[NLIBC_ENV_MAX + 1u];
static uint32_t g_env_count = 0;

char **environ = g_env_slots;

static int env_name_match_local(const char *entry, const char *name) {
    uint32_t i = 0;

    if (entry == 0 || name == 0) {
        return 0;
    }
    while (name[i] != '\0') {
        if (entry[i] != name[i]) {
            return 0;
        }
        i++;
    }
    return entry[i] == '=';
}

static int env_name_valid_local(const char *name) {
    uint32_t i = 0;

    if (name == 0 || name[0] == '\0') {
        return 0;
    }
    while (name[i] != '\0') {
        if (name[i] == '=') {
            return 0;
        }
        i++;
    }
    return 1;
}

static int env_find_slot_local(const char *name) {
    uint32_t i;

    for (i = 0; i < g_env_count; i++) {
        if (env_name_match_local(g_env_storage[i], name)) {
            return (int)i;
        }
    }
    return -1;
}

static int env_store_pair_local(uint32_t slot, const char *name, const char *value) {
    uint32_t pos = 0;
    uint32_t i = 0;

    if (slot >= NLIBC_ENV_MAX || !env_name_valid_local(name)) {
        return -1;
    }
    while (name[pos] != '\0') {
        if (pos + 1u >= sizeof(g_env_storage[slot])) {
            return -1;
        }
        g_env_storage[slot][pos] = name[pos];
        pos++;
    }
    if (pos + 2u > sizeof(g_env_storage[slot])) {
        return -1;
    }
    g_env_storage[slot][pos++] = '=';
    while (value != 0 && value[i] != '\0') {
        if (pos + 1u >= sizeof(g_env_storage[slot])) {
            return -1;
        }
        g_env_storage[slot][pos++] = value[i++];
    }
    g_env_storage[slot][pos] = '\0';
    g_env_slots[slot] = g_env_storage[slot];
    return 0;
}

static int env_store_raw_local(uint32_t slot, const char *entry) {
    uint32_t i = 0;

    if (slot >= NLIBC_ENV_MAX || entry == 0 || entry[0] == '\0') {
        return -1;
    }
    while (entry[i] != '\0') {
        if (i + 1u >= sizeof(g_env_storage[slot])) {
            return -1;
        }
        g_env_storage[slot][i] = entry[i];
        i++;
    }
    g_env_storage[slot][i] = '\0';
    g_env_slots[slot] = g_env_storage[slot];
    return 0;
}

void __libc_env_init(char **envp) {
    g_env_count = 0;
    g_env_slots[0] = 0;
    while (envp != 0 && envp[g_env_count] != 0 && g_env_count < NLIBC_ENV_MAX) {
        if (env_store_raw_local(g_env_count, envp[g_env_count]) != 0) {
            break;
        }
        g_env_count++;
    }
    g_env_slots[g_env_count] = 0;
}

char *getenv(const char *name) {
    int slot = env_find_slot_local(name);

    if (slot < 0) {
        return 0;
    }
    return g_env_storage[slot] + strlen(name) + 1u;
}

int setenv(const char *name, const char *value, int overwrite) {
    int slot;

    if (!env_name_valid_local(name)) {
        return -1;
    }
    slot = env_find_slot_local(name);
    if (slot >= 0 && !overwrite) {
        return 0;
    }
    if (slot < 0) {
        if (g_env_count >= NLIBC_ENV_MAX) {
            return -1;
        }
        slot = (int)g_env_count++;
    }
    if (env_store_pair_local((uint32_t)slot, name, value != 0 ? value : "") != 0) {
        if ((uint32_t)slot + 1u == g_env_count && slot >= 0 && g_env_slots[slot] == 0) {
            g_env_count--;
        }
        return -1;
    }
    g_env_slots[g_env_count] = 0;
    return 0;
}

int unsetenv(const char *name) {
    uint32_t i;
    int slot = env_find_slot_local(name);

    if (slot < 0) {
        return 0;
    }
    for (i = (uint32_t)slot; i + 1u < g_env_count; i++) {
        strlcpy(g_env_storage[i], sizeof(g_env_storage[i]), g_env_storage[i + 1u]);
        g_env_slots[i] = g_env_storage[i];
    }
    if (g_env_count != 0) {
        g_env_count--;
    }
    g_env_storage[g_env_count][0] = '\0';
    g_env_slots[g_env_count] = 0;
    return 0;
}

int putenv(char *string) {
    uint32_t i = 0;
    char name[NOS_NAME_BUFFER_SIZE];

    if (string == 0 || string[0] == '\0') {
        return -1;
    }
    while (string[i] != '\0' && string[i] != '=') {
        if (i + 1u >= sizeof(name)) {
            return -1;
        }
        name[i] = string[i];
        i++;
    }
    if (string[i] != '=') {
        return -1;
    }
    name[i] = '\0';
    return setenv(name, string + i + 1u, 1);
}
