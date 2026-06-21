#include "user/apps/elf/ncc/ncc.h"

static int ncc_derive_output(char *output, uint32_t output_size, const char *source) {
    uint32_t length = strlen(source);
    uint32_t slash = 0u;
    uint32_t dot = length;

    for (uint32_t i = 0u; i < length; i++) {
        if (source[i] == '/') {
            slash = i + 1u;
            dot = length;
        } else if (source[i] == '.') {
            dot = i;
        }
    }
    if (dot < slash) dot = length;
    if (dot + 1u > output_size) {
        return 0;
    }
    memcpy(output, source, dot);
    output[dot] = '\0';
    return output[0] != '\0';
}

static void ncc_usage(void) {
    write_err_str("usage: ncc <source.c> [-o output]\n");
    write_err_str("initial subset: functions/prototypes, globals, structs, typedef, enum,\n");
    write_err_str("                arrays/initializers, switch, ?:, preprocessor, static link\n");
}

int main(int argc, char **argv) {
    struct ncc_program program;
    struct ncc_object *objects;
    uint32_t object_count = 0u;
    const char *source = NULL;
    const char *output_arg = NULL;
    char output[NCC_PATH_MAX + 1];
    const char *crt0_path;
    const char *libc_start_path;
    const char *nlibc_path;
    char error[192];
    int rc = 1;

    memset(&program, 0, sizeof(program));
    error[0] = '\0';
    objects = calloc(NCC_OBJECT_MAX, sizeof(*objects));
    if (objects == NULL) {
        write_err_str("ncc: out of memory\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (++i >= argc) {
                ncc_usage();
                return 1;
            }
            output_arg = argv[i];
        } else if (argv[i][0] == '-') {
            ncc_usage();
            goto out;
        } else if (source == NULL) {
            source = argv[i];
        } else {
            ncc_usage();
            goto out;
        }
    }
    if (source == NULL) {
        ncc_usage();
        goto out;
    }
    if (output_arg != NULL) {
        ncc_copy_text(output, sizeof(output), output_arg);
    } else if (!ncc_derive_output(output, sizeof(output), source)) {
        write_err_str("ncc: could not derive output path\n");
        goto out;
    }
    if (!ncc_parse_file(source, &program, error, sizeof(error))) {
        eprintf("ncc: parse: %s\n", error);
        goto out;
    }
    if (!ncc_codegen_program(&program,
                             &objects[object_count],
                             error,
                             sizeof(error))) {
        eprintf("ncc: codegen: %s\n", error);
        goto out;
    }
    object_count++;
    {
        int probe = open("/system/devel/lib/crt0.o", O_RDONLY);

        if (probe < 0) {
            crt0_path = "/nxfs/system/devel/lib/crt0.o";
            libc_start_path = "/nxfs/system/devel/lib/libc_start.o";
            nlibc_path = "/nxfs/system/devel/lib/libnlibc.a";
        } else {
            close(probe);
            crt0_path = "/system/devel/lib/crt0.o";
            libc_start_path = "/system/devel/lib/libc_start.o";
            nlibc_path = "/system/devel/lib/libnlibc.a";
        }
    }
    if (!ncc_load_elf_object(crt0_path,
                             &objects[object_count],
                             error,
                             sizeof(error))) {
        eprintf("ncc: link: %s\n", error);
        goto out;
    }
    object_count++;
    if (!ncc_load_elf_object(libc_start_path,
                             &objects[object_count],
                             error,
                             sizeof(error))) {
        eprintf("ncc: link: %s\n", error);
        goto out;
    }
    object_count++;
    if (!ncc_load_archive(nlibc_path,
                          objects,
                          NCC_OBJECT_MAX,
                          &object_count,
                          error,
                          sizeof(error))) {
        eprintf("ncc: link: %s\n", error);
        goto out;
    }
    if (!ncc_link_executable(output, objects, object_count, error, sizeof(error))) {
        eprintf("ncc: link: %s\n", error);
        goto out;
    }
    printf("ncc: wrote %s\n", output);
    rc = 0;

out:
    ncc_program_destroy(&program);
    for (uint32_t i = 0u; i < object_count; i++) {
        ncc_object_destroy(&objects[i]);
    }
    free(objects);
    return rc;
}
