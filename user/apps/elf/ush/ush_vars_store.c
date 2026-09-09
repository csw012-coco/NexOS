#include "user/apps/elf/ush/ush_vars_internal.h"

struct ush_var_entry g_ush_vars[USH_VAR_MAX];

static int ush_var_find_local(const char *name) {
    uint32_t i;

    for (i = 0; i < USH_VAR_MAX; i++) {
        if (g_ush_vars[i].used && ush_vars_streq(g_ush_vars[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

const char *ush_var_lookup_local(const char *name) {
    int slot;
    char *env_value;

    if (name == NULL || name[0] == '\0') {
        return NULL;
    }
    slot = ush_var_find_local(name);
    if (slot >= 0) {
        return g_ush_vars[slot].value;
    }
    env_value = getenv(name);
    return env_value != NULL ? env_value : NULL;
}

static int ush_var_alloc_local(void) {
    uint32_t i;

    for (i = 0; i < USH_VAR_MAX; i++) {
        if (!g_ush_vars[i].used) {
            g_ush_vars[i].used = 1u;
            g_ush_vars[i].name[0] = '\0';
            g_ush_vars[i].value[0] = '\0';
            return (int)i;
        }
    }
    return -1;
}

static int ush_var_store_local(int slot, const char *name, const char *value) {
    if (slot < 0 || slot >= (int)USH_VAR_MAX || !ush_var_name_valid_local(name)) {
        return 0;
    }
    if (ush_vars_strlen(name) > USH_VAR_NAME_MAX ||
        ush_vars_strlen(value != NULL ? value : "") > USH_VAR_VALUE_MAX) {
        return 0;
    }
    ush_vars_copy_line(g_ush_vars[slot].name, name, sizeof(g_ush_vars[slot].name));
    ush_vars_copy_line(g_ush_vars[slot].value, value != NULL ? value : "", sizeof(g_ush_vars[slot].value));
    return 1;
}

void ush_var_clear_slot_local(int slot) {
    if (slot < 0 || slot >= (int)USH_VAR_MAX) {
        return;
    }
    g_ush_vars[slot].used = 0u;
    g_ush_vars[slot].name[0] = '\0';
    g_ush_vars[slot].value[0] = '\0';
}

int ush_var_assign_local(const char *name, const char *value, int exported_if_new) {
    int slot = ush_var_find_local(name);
    char *env_value = getenv(name);

    if (slot >= 0) {
        if (!ush_var_store_local(slot, name, value)) {
            return 0;
        }
        return 1;
    }
    if (env_value != NULL || exported_if_new) {
        return setenv(name, value != NULL ? value : "", 1) == 0;
    }
    slot = ush_var_alloc_local();
    if (slot < 0) {
        return 0;
    }
    if (!ush_var_store_local(slot, name, value)) {
        ush_var_clear_slot_local(slot);
        return 0;
    }
    return 1;
}

int ush_var_export_local(const char *name, const char *value) {
    int slot;
    const char *current_value;

    if (value != NULL) {
        slot = ush_var_find_local(name);
        if (slot >= 0) {
            ush_var_clear_slot_local(slot);
        }
        return setenv(name, value, 1) == 0;
    }

    slot = ush_var_find_local(name);
    if (slot >= 0) {
        current_value = g_ush_vars[slot].value;
        if (setenv(name, current_value, 1) != 0) {
            return 0;
        }
        ush_var_clear_slot_local(slot);
        return 1;
    }

    current_value = getenv(name);
    if (current_value != NULL) {
        return 1;
    }
    return setenv(name, "", 1) == 0;
}

void ush_var_list_local(int exported_only) {
    uint32_t i = 0;
    int listed = 0;

    (void)exported_only;
    while (environ != NULL && environ[i] != NULL) {
        write_str(environ[i]);
        write_str("\n");
        listed = 1;
        i++;
    }
    if (!listed) {
        write_str("<empty>\n");
    }
}

void ush_var_list_shell_local(void) {
    uint32_t i;
    int listed = 0;

    for (i = 0; i < USH_VAR_MAX; i++) {
        if (!g_ush_vars[i].used) {
            continue;
        }
        write_str(g_ush_vars[i].name);
        write_str("=");
        write_str(g_ush_vars[i].value);
        write_str("\n");
        listed = 1;
    }
    if (!listed) {
        write_str("<empty>\n");
    }
}

int ush_parse_assignment_local(const char *text,
                               char *name,
                               uint32_t name_size,
                               char *value,
                               uint32_t value_size) {
    char buffer[USH_LINE_MAX + 1];
    uint32_t pos = 0;
    uint32_t eq = 0xffffffffu;
    uint32_t value_pos = 0;

    if (text == NULL || name == NULL || value == NULL || name_size == 0 || value_size == 0) {
        return 0;
    }
    ush_vars_copy_line(buffer, text, sizeof(buffer));
    ush_vars_trim_in_place(buffer);
    if (buffer[0] == '\0') {
        return 0;
    }
    while (buffer[pos] != '\0') {
        if (buffer[pos] == '=') {
            eq = pos;
            break;
        }
        pos++;
    }
    if (eq == 0xffffffffu || eq == 0u || eq + 1u > sizeof(buffer)) {
        return 0;
    }
    buffer[eq] = '\0';
    ush_vars_trim_in_place(buffer);
    if (!ush_var_name_valid_local(buffer)) {
        return 0;
    }
    ush_vars_copy_line(name, buffer, name_size);
    while (text[value_pos] != '\0') {
        if (text[value_pos] == '=') {
            value_pos++;
            break;
        }
        value_pos++;
    }
    ush_vars_copy_line(value, text + value_pos, value_size);
    return 1;
}
