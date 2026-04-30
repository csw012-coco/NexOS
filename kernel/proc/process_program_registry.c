#include "kernel/internal/proc/process_program_registry_internal.h"
#include "lib/string.h"

static const struct process_program g_process_programs[] = {
    { "hello", "HELLO.ELF" },
    { "keydemo", "KEYDEMO.ELF" },
    { "memdemo", "MEMDEMO.ELF" },
    { "readdemo", "READDEMO.ELF" },
    { "yielddemo", "YIELDDEMO.ELF" },
    { "badptr", "BADPTR.ELF" },
    { "sleepdemo", "SLEEPDEMO.ELF" },
    { "cat", "CATDEMO.ELF" },
    { "ls", "LSDEMO.ELF" },
    { "wdemo", "WDEMO.ELF" },
    { "ush", "USH.ELF" },
};

const struct process_program *process_find_program_internal(const char *name) {
    uint32_t i;

    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < process_program_count(); i++) {
        if (streq(name, g_process_programs[i].name) || streq(name, g_process_programs[i].image_name)) {
            return &g_process_programs[i];
        }
    }
    return NULL;
}

const char *process_resolve_image_name(const char *name) {
    const struct process_program *program;

    if (name == NULL) {
        return NULL;
    }
    program = process_find_program_internal(name);
    return program != NULL ? program->image_name : name;
}

uint32_t process_program_count(void) {
    return (uint32_t)(sizeof(g_process_programs) / sizeof(g_process_programs[0]));
}

const char *process_program_name(uint32_t index) {
    return index >= process_program_count() ? NULL : g_process_programs[index].name;
}
