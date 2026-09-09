#include "user/apps/elf/ush/ush_vars_internal.h"

static int ush_alias_find_local(const char *name) {
    uint32_t i;

    for (i = 0; i < USH_VAR_MAX; i++) {
        if (g_ush_aliases[i].used && ush_vars_streq(g_ush_aliases[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

static int ush_alias_alloc_local(void) {
    uint32_t i;

    for (i = 0; i < USH_VAR_MAX; i++) {
        if (!g_ush_aliases[i].used) {
            g_ush_aliases[i].used = 1u;
            g_ush_aliases[i].name[0] = '\0';
            g_ush_aliases[i].value[0] = '\0';
            return (int)i;
        }
    }
    return -1;
}

static int ush_alias_store_local(int slot, const char *name, const char *value) {
    if (slot < 0 || slot >= (int)USH_VAR_MAX || !ush_var_name_valid_local(name)) {
        return 0;
    }
    if (ush_vars_strlen(name) > USH_VAR_NAME_MAX ||
        ush_vars_strlen(value != NULL ? value : "") > USH_LINE_MAX) {
        return 0;
    }
    ush_vars_copy_line(g_ush_aliases[slot].name, name, sizeof(g_ush_aliases[slot].name));
    ush_vars_copy_line(g_ush_aliases[slot].value,
                       value != NULL ? value : "",
                       sizeof(g_ush_aliases[slot].value));
    return 1;
}

const char *ush_alias_lookup_local(const char *name) {
    int slot = ush_alias_find_local(name);

    if (slot < 0) {
        return NULL;
    }
    return g_ush_aliases[slot].value;
}

int ush_alias_assign_local(const char *name, const char *value) {
    int slot = ush_alias_find_local(name);

    if (slot < 0) {
        slot = ush_alias_alloc_local();
    }
    if (slot < 0) {
        return 0;
    }
    return ush_alias_store_local(slot, name, value);
}

void ush_alias_list_local(void) {
    uint32_t i;
    int listed = 0;

    for (i = 0; i < USH_VAR_MAX; i++) {
        if (!g_ush_aliases[i].used) {
            continue;
        }
        write_str("alias ");
        write_str(g_ush_aliases[i].name);
        write_str("=");
        write_str(g_ush_aliases[i].value);
        write_str("\n");
        listed = 1;
    }
    if (!listed) {
        write_str("<empty>\n");
    }
}
