#include "user/apps/elf/ush/ush_vars_internal.h"

static int ush_function_find_local(const char *name) {
    uint32_t i;

    for (i = 0; i < USH_FUNCTION_MAX; i++) {
        if (g_ush_functions[i].used && ush_vars_streq(g_ush_functions[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

static int ush_function_alloc_local(void) {
    uint32_t i;

    for (i = 0; i < USH_FUNCTION_MAX; i++) {
        if (!g_ush_functions[i].used) {
            g_ush_functions[i].used = 1u;
            g_ush_functions[i].name[0] = '\0';
            g_ush_functions[i].body[0] = '\0';
            return (int)i;
        }
    }
    return -1;
}

static int ush_function_store_local(int slot, const char *name, const char *body) {
    if (slot < 0 || slot >= (int)USH_FUNCTION_MAX || !ush_var_name_valid_local(name)) {
        return 0;
    }
    if (ush_vars_strlen(name) > USH_VAR_NAME_MAX ||
        ush_vars_strlen(body != NULL ? body : "") > USH_FUNCTION_BODY_MAX) {
        return 0;
    }
    ush_vars_copy_line(g_ush_functions[slot].name, name, sizeof(g_ush_functions[slot].name));
    ush_vars_copy_line(g_ush_functions[slot].body,
                       body != NULL ? body : "",
                       sizeof(g_ush_functions[slot].body));
    return 1;
}

int ush_function_assign_local(const char *name, const char *body) {
    int slot = ush_function_find_local(name);

    if (slot < 0) {
        slot = ush_function_alloc_local();
    }
    if (slot < 0) {
        return 0;
    }
    return ush_function_store_local(slot, name, body);
}

const char *ush_function_lookup_local(const char *name) {
    int slot = ush_function_find_local(name);

    if (slot < 0) {
        return NULL;
    }
    return g_ush_functions[slot].body;
}

void ush_function_list_local(void) {
    uint32_t i;
    int listed = 0;

    for (i = 0; i < USH_FUNCTION_MAX; i++) {
        if (!g_ush_functions[i].used) {
            continue;
        }
        write_str(g_ush_functions[i].name);
        write_str(" { ");
        write_str(g_ush_functions[i].body);
        write_str("; }\n");
        listed = 1;
    }
    if (!listed) {
        write_str("<empty>\n");
    }
}
