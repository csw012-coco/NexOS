#pragma once

#include "user/apps/elf/ush/ush_shared.h"

struct ush_var_entry {
    uint8_t used;
    char name[USH_VAR_NAME_MAX + 1];
    char value[USH_VAR_VALUE_MAX + 1];
};

struct ush_alias_entry {
    uint8_t used;
    char name[USH_VAR_NAME_MAX + 1];
    char value[USH_LINE_MAX + 1];
};

struct ush_function_entry {
    uint8_t used;
    char name[USH_VAR_NAME_MAX + 1];
    char body[USH_FUNCTION_BODY_MAX + 1];
};

struct ush_script_args_state {
    uint32_t argc;
    char argv[10][USH_VAR_VALUE_MAX + 1];
    char joined[USH_VAR_VALUE_MAX + 1];
    char count[12];
};

extern struct ush_var_entry g_ush_vars[USH_VAR_MAX];
extern struct ush_alias_entry g_ush_aliases[USH_VAR_MAX];
extern struct ush_function_entry g_ush_functions[USH_FUNCTION_MAX];
extern struct ush_script_args_state g_ush_script_args;

uint32_t ush_vars_strlen(const char *text);
void ush_vars_copy_line(char *dst, const char *src, uint32_t max_len);
int ush_vars_streq(const char *a, const char *b);
int ush_vars_is_space(char ch);
void ush_vars_trim_in_place(char *text);
int ush_var_name_char_local(char ch, int first);
const char *ush_alias_lookup_local(const char *name);
const char *ush_special_var_lookup_local(const char *name);
const char *ush_var_lookup_local(const char *name);
void ush_var_clear_slot_local(int slot);
void ush_sync_pwd_var_local(const char *cwd);
