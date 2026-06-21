#include "user/apps/elf/ncc/ncc.h"

static int ncc_is_alpha(char ch) {
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') ||
           ch == '_';
}

static int ncc_is_digit(char ch) {
    return ch >= '0' && ch <= '9';
}

static int ncc_is_alnum(char ch) {
    return ncc_is_alpha(ch) || ncc_is_digit(ch);
}

struct ncc_macro {
    char name[NCC_NAME_MAX + 1];
    char body[NCC_MACRO_BODY_MAX + 1];
};

struct ncc_preprocessor {
    struct ncc_macro macros[NCC_MACRO_MAX];
    uint32_t macro_count;
    char included[NCC_INCLUDE_MAX][NCC_PATH_MAX + 1];
    uint32_t include_count;
};

static uint8_t ncc_hex_digit(char ch) {
    if (ch >= '0' && ch <= '9') return (uint8_t)(ch - '0');
    if (ch >= 'a' && ch <= 'f') return (uint8_t)(ch - 'a' + 10);
    if (ch >= 'A' && ch <= 'F') return (uint8_t)(ch - 'A' + 10);
    return 0xffu;
}

static char ncc_escape_char(struct ncc_lexer *lexer) {
    char ch;

    if (lexer->offset >= lexer->length) {
        return 0;
    }
    ch = lexer->source[lexer->offset++];
    switch (ch) {
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        case '0': return '\0';
        case '\\': return '\\';
        case '\'': return '\'';
        case '"': return '"';
        default: return ch;
    }
}

static void ncc_set_error(struct ncc_lexer *lexer, const char *text) {
    if (lexer->error[0] == '\0') {
        snprintf(lexer->error, sizeof(lexer->error), "line %u: %s", lexer->line, text);
    }
}

static int ncc_file_exists(const char *path) {
    int fd;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    close(fd);
    return 1;
}

static void ncc_path_directory(char *dst, uint32_t dst_size, const char *path) {
    uint32_t slash = 0u;
    uint32_t length = path != NULL ? (uint32_t)strlen(path) : 0u;

    for (uint32_t i = 0u; i < length; i++) {
        if (path[i] == '/') {
            slash = i + 1u;
        }
    }
    if (slash == 0u) {
        ncc_copy_text(dst, dst_size, ".");
        return;
    }
    if (slash >= dst_size) {
        slash = dst_size - 1u;
    }
    memcpy(dst, path, slash);
    dst[slash] = '\0';
}

static int ncc_resolve_include(struct ncc_lexer *lexer,
                               const char *current_path,
                               const char *name,
                               int system_header,
                               char *path,
                               uint32_t path_size) {
    if (!system_header) {
        char directory[NCC_PATH_MAX + 1];

        ncc_path_directory(directory, sizeof(directory), current_path);
        if (strcmp(directory, ".") == 0) {
            snprintf(path, path_size, "%s", name);
        } else {
            snprintf(path, path_size, "%s%s", directory, name);
        }
        if (ncc_file_exists(path)) {
            return 1;
        }
        snprintf(path, path_size, "%s", name);
        if (ncc_file_exists(path)) {
            return 1;
        }
        snprintf(path, path_size, "/system/devel/include/%s", name);
        if (ncc_file_exists(path)) {
            return 1;
        }
        snprintf(path, path_size, "/nxfs/system/devel/include/%s", name);
        if (ncc_file_exists(path)) {
            return 1;
        }
    } else {
        snprintf(path, path_size, "/system/devel/include/%s", name);
        if (ncc_file_exists(path)) {
            return 1;
        }
        snprintf(path, path_size, "/nxfs/system/devel/include/%s", name);
        if (ncc_file_exists(path)) {
            return 1;
        }
        snprintf(path, path_size, "user/libc/include/%s", name);
        if (ncc_file_exists(path)) {
            return 1;
        }
    }
    snprintf(lexer->error,
             sizeof(lexer->error),
             "line %u: include not found: %s",
             lexer->line,
             name);
    return 0;
}

static int ncc_include_seen(struct ncc_preprocessor *preprocessor, const char *path) {
    for (uint32_t i = 0u; i < preprocessor->include_count; i++) {
        if (strcmp(preprocessor->included[i], path) == 0) {
            return 1;
        }
    }
    if (preprocessor->include_count < NCC_INCLUDE_MAX) {
        ncc_copy_text(preprocessor->included[preprocessor->include_count],
                      sizeof(preprocessor->included[0]),
                      path);
        preprocessor->include_count++;
    }
    return 0;
}

static int ncc_system_header_exports_declarations(const char *name) {
    return strcmp(name, "stdint.h") == 0 ||
           strcmp(name, "stddef.h") == 0 ||
           strcmp(name, "stdbool.h") == 0 ||
           strcmp(name, "limits.h") == 0 ||
           strcmp(name, "inttypes.h") == 0 ||
           strcmp(name, "sys/types.h") == 0;
}

static int ncc_skip_directive(struct ncc_lexer *lexer) {
    char word[24];
    uint32_t word_len = 0u;

    lexer->offset++;
    while (lexer->offset < lexer->length &&
           (lexer->source[lexer->offset] == ' ' || lexer->source[lexer->offset] == '\t')) {
        lexer->offset++;
    }
    while (lexer->offset < lexer->length &&
           ncc_is_alpha(lexer->source[lexer->offset]) &&
           word_len + 1u < sizeof(word)) {
        word[word_len++] = lexer->source[lexer->offset++];
    }
    word[word_len] = '\0';
    if (strcmp(word, "include") == 0) {
        char name[NCC_PATH_MAX + 1];
        char path[NCC_PATH_MAX + 1];
        uint32_t name_len = 0u;
        char closing;
        int system_header;

        while (lexer->offset < lexer->length &&
               (lexer->source[lexer->offset] == ' ' || lexer->source[lexer->offset] == '\t')) {
            lexer->offset++;
        }
        if (lexer->offset >= lexer->length ||
            (lexer->source[lexer->offset] != '<' && lexer->source[lexer->offset] != '"')) {
            ncc_set_error(lexer, "invalid include directive");
            return 0;
        }
        system_header = lexer->source[lexer->offset] == '<';
        closing = system_header ? '>' : '"';
        lexer->offset++;
        while (lexer->offset < lexer->length &&
               lexer->source[lexer->offset] != closing &&
               lexer->source[lexer->offset] != '\n') {
            if (name_len + 1u < sizeof(name)) {
                name[name_len++] = lexer->source[lexer->offset];
            }
            lexer->offset++;
        }
        name[name_len] = '\0';
        if (lexer->offset >= lexer->length || lexer->source[lexer->offset] != closing) {
            ncc_set_error(lexer, "unterminated include path");
            return 0;
        }
        if (!ncc_resolve_include(lexer,
                                 ".",
                                 name,
                                 system_header,
                                 path,
                                 sizeof(path))) {
            return 0;
        }
    }
    while (lexer->offset < lexer->length && lexer->source[lexer->offset] != '\n') {
        lexer->offset++;
    }
    return 1;
}

static const char *ncc_find_macro(const struct ncc_macro *macros,
                                  uint32_t macro_count,
                                  const char *name,
                                  uint32_t name_len) {
    for (uint32_t i = 0u; i < macro_count; i++) {
        if (strlen(macros[i].name) == name_len &&
            strncmp(macros[i].name, name, name_len) == 0) {
            return macros[i].body;
        }
    }
    return NULL;
}

static int ncc_add_macro(struct ncc_lexer *lexer,
                         struct ncc_macro *macros,
                         uint32_t *macro_count,
                         const char *name,
                         uint32_t name_len,
                         const char *body,
                         uint32_t body_len) {
    struct ncc_macro *macro;

    if (name_len == 0u || name_len > NCC_NAME_MAX) {
        ncc_set_error(lexer, "invalid macro name");
        return 0;
    }
    for (uint32_t i = 0u; i < *macro_count; i++) {
        if (strlen(macros[i].name) == name_len &&
            strncmp(macros[i].name, name, name_len) == 0) {
            macro = &macros[i];
            memset(macro, 0, sizeof(*macro));
            memcpy(macro->name, name, name_len);
            if (body_len > NCC_MACRO_BODY_MAX) body_len = NCC_MACRO_BODY_MAX;
            memcpy(macro->body, body, body_len);
            return 1;
        }
    }
    if (*macro_count >= NCC_MACRO_MAX) {
        ncc_set_error(lexer, "too many macros");
        return 0;
    }
    macro = &macros[(*macro_count)++];
    memset(macro, 0, sizeof(*macro));
    memcpy(macro->name, name, name_len);
    if (body_len > NCC_MACRO_BODY_MAX) body_len = NCC_MACRO_BODY_MAX;
    memcpy(macro->body, body, body_len);
    return 1;
}

static int ncc_buffer_append_char(struct ncc_lexer *lexer,
                                  char **dst,
                                  uint32_t *size,
                                  uint32_t *capacity,
                                  char ch) {
    if (*size + 1u >= *capacity) {
        uint32_t new_capacity = *capacity != 0u ? *capacity * 2u : 256u;
        char *new_dst;

        while (*size + 1u >= new_capacity) {
            new_capacity *= 2u;
        }
        new_dst = realloc(*dst, new_capacity);
        if (new_dst == NULL) {
            ncc_set_error(lexer, "out of memory during preprocessing");
            return 0;
        }
        *dst = new_dst;
        *capacity = new_capacity;
    }
    (*dst)[(*size)++] = ch;
    (*dst)[*size] = '\0';
    return 1;
}

static int ncc_buffer_append_text(struct ncc_lexer *lexer,
                                  char **dst,
                                  uint32_t *size,
                                  uint32_t *capacity,
                                  const char *text) {
    while (text != NULL && *text != '\0') {
        if (!ncc_buffer_append_char(lexer, dst, size, capacity, *text++)) {
            return 0;
        }
    }
    return 1;
}

static int ncc_buffer_append_expanded_text(struct ncc_lexer *lexer,
                                           struct ncc_preprocessor *preprocessor,
                                           char **dst,
                                           uint32_t *size,
                                           uint32_t *capacity,
                                           const char *text,
                                           uint32_t depth) {
    uint32_t offset = 0u;

    if (depth > 32u) {
        ncc_set_error(lexer, "macro expansion is too deep");
        return 0;
    }
    while (text != NULL && text[offset] != '\0') {
        if (ncc_is_alpha(text[offset])) {
            uint32_t start = offset;
            const char *macro;

            while (ncc_is_alnum(text[offset])) {
                offset++;
            }
            macro = ncc_find_macro(preprocessor->macros,
                                   preprocessor->macro_count,
                                   text + start,
                                   offset - start);
            if (macro != NULL) {
                if (!ncc_buffer_append_expanded_text(lexer,
                                                     preprocessor,
                                                     dst,
                                                     size,
                                                     capacity,
                                                     macro,
                                                     depth + 1u)) {
                    return 0;
                }
            } else {
                while (start < offset) {
                    if (!ncc_buffer_append_char(lexer,
                                                dst,
                                                size,
                                                capacity,
                                                text[start++])) {
                        return 0;
                    }
                }
            }
            continue;
        }
        if (text[offset] == '"' || text[offset] == '\'') {
            char quote = text[offset++];

            if (!ncc_buffer_append_char(lexer, dst, size, capacity, quote)) {
                return 0;
            }
            while (text[offset] != '\0') {
                char ch = text[offset++];

                if (!ncc_buffer_append_char(lexer, dst, size, capacity, ch)) {
                    return 0;
                }
                if (ch == '\\' && text[offset] != '\0') {
                    if (!ncc_buffer_append_char(lexer,
                                                dst,
                                                size,
                                                capacity,
                                                text[offset++])) {
                        return 0;
                    }
                } else if (ch == quote) {
                    break;
                }
            }
            continue;
        }
        if (!ncc_buffer_append_char(lexer,
                                    dst,
                                    size,
                                    capacity,
                                    text[offset++])) {
            return 0;
        }
    }
    return 1;
}

static int ncc_preprocess_source(struct ncc_lexer *lexer,
                                 struct ncc_preprocessor *preprocessor,
                                 const char *current_path,
                                 char *source,
                                 uint32_t length,
                                 char **out_source,
                                 uint32_t *out_length,
                                 uint32_t include_depth) {
    char *output = NULL;
    uint32_t output_size = 0u;
    uint32_t output_capacity = 0u;
    uint32_t offset = 0u;
    int line_start = 1;

    if (include_depth > NCC_INCLUDE_DEPTH_MAX) {
        ncc_set_error(lexer, "include nesting is too deep");
        return 0;
    }
    while (offset < length) {
        char ch = source[offset];

        if (line_start) {
            uint32_t directive = offset;

            while (directive < length &&
                   (source[directive] == ' ' || source[directive] == '\t')) {
                directive++;
            }
            if (directive < length && source[directive] == '#') {
                char word[24];
                uint32_t word_len = 0u;

                offset = directive + 1u;
                while (offset < length && (source[offset] == ' ' || source[offset] == '\t')) {
                    offset++;
                }
                while (offset < length &&
                       ncc_is_alpha(source[offset]) &&
                       word_len + 1u < sizeof(word)) {
                    word[word_len++] = source[offset++];
                }
                word[word_len] = '\0';
                while (offset < length && (source[offset] == ' ' || source[offset] == '\t')) {
                    offset++;
                }
                if (strcmp(word, "include") == 0) {
                    char name[NCC_PATH_MAX + 1];
                    char path[NCC_PATH_MAX + 1];
                    uint32_t name_len = 0u;
                    char closing;
                    int system_header;

                    if (offset >= length || (source[offset] != '<' && source[offset] != '"')) {
                        ncc_set_error(lexer, "invalid include directive");
                        free(output);
                        return 0;
                    }
                    system_header = source[offset] == '<';
                    closing = system_header ? '>' : '"';
                    offset++;
                    while (offset < length && source[offset] != closing && source[offset] != '\n') {
                        if (name_len + 1u < sizeof(name)) {
                            name[name_len++] = source[offset];
                        }
                        offset++;
                    }
                    name[name_len] = '\0';
                    if (offset >= length || source[offset] != closing) {
                        ncc_set_error(lexer, "unterminated include path");
                        free(output);
                        return 0;
                    }
                    if (!ncc_resolve_include(lexer,
                                             current_path,
                                             name,
                                             system_header,
                                             path,
                                             sizeof(path))) {
                        free(output);
                        return 0;
                    }
                    if (!ncc_include_seen(preprocessor, path)) {
                        uint8_t *include_source;
                        uint32_t include_size;
                        char *include_output;
                        uint32_t include_output_size;

                        if (!ncc_read_file(path, &include_source, &include_size)) {
                            ncc_set_error(lexer, "could not read include file");
                            free(output);
                            return 0;
                        }
                        if (!ncc_preprocess_source(lexer,
                                                   preprocessor,
                                                   path,
                                                   (char *)include_source,
                                                   include_size,
                                                   &include_output,
                                                   &include_output_size,
                                                   include_depth + 1u)) {
                            free(include_source);
                            free(output);
                            return 0;
                        }
                        free(include_source);
                        /*
                         * Simple SDK type headers can now contribute typedef
                         * declarations. Larger nlibc headers still contribute
                         * macros only until every declaration form they use is
                         * supported by ncc.
                         */
                        if ((!system_header ||
                             ncc_system_header_exports_declarations(name)) &&
                            !ncc_buffer_append_text(lexer,
                                                    &output,
                                                    &output_size,
                                                    &output_capacity,
                                                    include_output)) {
                            free(include_output);
                            free(output);
                            return 0;
                        }
                        free(include_output);
                    }
                } else if (strcmp(word, "define") == 0) {
                    uint32_t name_start = offset;
                    uint32_t name_len;
                    uint32_t body_start;
                    uint32_t body_end;

                    if (offset >= length || !ncc_is_alpha(source[offset])) {
                        ncc_set_error(lexer, "invalid define directive");
                        free(output);
                        return 0;
                    }
                    while (offset < length && ncc_is_alnum(source[offset])) {
                        offset++;
                    }
                    name_len = offset - name_start;
                    if (offset < length && source[offset] == '(') {
                        while (offset < length && source[offset] != '\n') {
                            offset++;
                        }
                        continue;
                    }
                    while (offset < length && (source[offset] == ' ' || source[offset] == '\t')) {
                        offset++;
                    }
                    body_start = offset;
                    while (offset < length && source[offset] != '\n') {
                        offset++;
                    }
                    body_end = offset;
                    while (body_end > body_start &&
                           (source[body_end - 1u] == ' ' || source[body_end - 1u] == '\t' ||
                            source[body_end - 1u] == '\r')) {
                        body_end--;
                    }
                    if (!ncc_add_macro(lexer,
                                       preprocessor->macros,
                                       &preprocessor->macro_count,
                                       source + name_start,
                                       name_len,
                                       source + body_start,
                                       body_end - body_start)) {
                        free(output);
                        return 0;
                    }
                }
                while (offset < length && source[offset] != '\n') {
                    offset++;
                }
                if (offset < length && source[offset] == '\n') {
                    if (!ncc_buffer_append_char(lexer,
                                                &output,
                                                &output_size,
                                                &output_capacity,
                                                '\n')) {
                        free(output);
                        return 0;
                    }
                    offset++;
                }
                line_start = 1;
                continue;
            }
        }

        if (ncc_is_alpha(ch)) {
            uint32_t start = offset;
            const char *macro;

            while (offset < length && ncc_is_alnum(source[offset])) {
                offset++;
            }
            macro = ncc_find_macro(preprocessor->macros,
                                   preprocessor->macro_count,
                                   source + start,
                                   offset - start);
            if (macro != NULL) {
                if (!ncc_buffer_append_expanded_text(lexer,
                                                     preprocessor,
                                                     &output,
                                                     &output_size,
                                                     &output_capacity,
                                                     macro,
                                                     0u)) {
                    free(output);
                    return 0;
                }
            } else {
                for (uint32_t i = start; i < offset; i++) {
                    if (!ncc_buffer_append_char(lexer,
                                                &output,
                                                &output_size,
                                                &output_capacity,
                                                source[i])) {
                        free(output);
                        return 0;
                    }
                }
            }
            line_start = 0;
            continue;
        }

        if (ch == '"' || ch == '\'') {
            char quote = ch;

            if (!ncc_buffer_append_char(lexer, &output, &output_size, &output_capacity, ch)) {
                free(output);
                return 0;
            }
            offset++;
            while (offset < length) {
                ch = source[offset++];
                if (!ncc_buffer_append_char(lexer, &output, &output_size, &output_capacity, ch)) {
                    free(output);
                    return 0;
                }
                if (ch == '\\' && offset < length) {
                    ch = source[offset++];
                    if (!ncc_buffer_append_char(lexer,
                                                &output,
                                                &output_size,
                                                &output_capacity,
                                                ch)) {
                        free(output);
                        return 0;
                    }
                    continue;
                }
                if (ch == quote) {
                    break;
                }
                if (ch == '\n') {
                    lexer->line++;
                    line_start = 1;
                    break;
                }
            }
            continue;
        }

        if (ch == '/' && offset + 1u < length && source[offset + 1u] == '/') {
            while (offset < length && source[offset] != '\n') {
                if (!ncc_buffer_append_char(lexer,
                                            &output,
                                            &output_size,
                                            &output_capacity,
                                            source[offset++])) {
                    free(output);
                    return 0;
                }
            }
            continue;
        }

        if (!ncc_buffer_append_char(lexer, &output, &output_size, &output_capacity, ch)) {
            free(output);
            return 0;
        }
        offset++;
        if (ch == '\n') {
            lexer->line++;
            line_start = 1;
        } else if (ch != ' ' && ch != '\t' && ch != '\r') {
            line_start = 0;
        }
    }
    *out_source = output;
    *out_length = output_size;
    return 1;
}

static int ncc_skip_space(struct ncc_lexer *lexer) {
    int line_start = lexer->offset == 0u;

    for (;;) {
        char ch;

        if (lexer->offset >= lexer->length) {
            return 1;
        }
        ch = lexer->source[lexer->offset];
        if (ch == ' ' || ch == '\t' || ch == '\r') {
            lexer->offset++;
            continue;
        }
        if (ch == '\n') {
            lexer->offset++;
            lexer->line++;
            line_start = 1;
            continue;
        }
        if (line_start && ch == '#') {
            if (!ncc_skip_directive(lexer)) {
                return 0;
            }
            continue;
        }
        if (ch == '/' && lexer->offset + 1u < lexer->length &&
            lexer->source[lexer->offset + 1u] == '/') {
            lexer->offset += 2u;
            while (lexer->offset < lexer->length && lexer->source[lexer->offset] != '\n') {
                lexer->offset++;
            }
            continue;
        }
        if (ch == '/' && lexer->offset + 1u < lexer->length &&
            lexer->source[lexer->offset + 1u] == '*') {
            lexer->offset += 2u;
            while (lexer->offset + 1u < lexer->length) {
                if (lexer->source[lexer->offset] == '\n') {
                    lexer->line++;
                }
                if (lexer->source[lexer->offset] == '*' &&
                    lexer->source[lexer->offset + 1u] == '/') {
                    lexer->offset += 2u;
                    break;
                }
                lexer->offset++;
            }
            continue;
        }
        return 1;
    }
}

int ncc_lexer_init(struct ncc_lexer *lexer, const char *path) {
    uint8_t *source;
    uint32_t size;
    char *preprocessed;
    uint32_t preprocessed_size;
    struct ncc_preprocessor *preprocessor;

    memset(lexer, 0, sizeof(*lexer));
    if (!ncc_read_file(path, &source, &size) || size > NCC_SOURCE_MAX) {
        ncc_copy_text(lexer->error, sizeof(lexer->error), "could not read source");
        return 0;
    }
    lexer->line = 1u;
    preprocessor = calloc(1u, sizeof(*preprocessor));
    if (preprocessor == NULL) {
        free(source);
        ncc_copy_text(lexer->error, sizeof(lexer->error), "out of memory");
        return 0;
    }
    if (!ncc_preprocess_source(lexer,
                               preprocessor,
                               path,
                               (char *)source,
                               size,
                               &preprocessed,
                               &preprocessed_size,
                               0u)) {
        free(preprocessor);
        free(source);
        return 0;
    }
    free(preprocessor);
    free(source);
    lexer->source = preprocessed;
    lexer->length = preprocessed_size;
    lexer->line = 1u;
    return ncc_lexer_next(lexer);
}

void ncc_lexer_destroy(struct ncc_lexer *lexer) {
    if (lexer != NULL) {
        free(lexer->source);
        lexer->source = NULL;
    }
}

int ncc_token_is(const struct ncc_token *token, const char *text) {
    return token != NULL && text != NULL && strcmp(token->text, text) == 0;
}

int ncc_lexer_next(struct ncc_lexer *lexer) {
    struct ncc_token *token = &lexer->token;
    char ch;

    memset(token, 0, sizeof(*token));
    if (!ncc_skip_space(lexer)) {
        return 0;
    }
    token->line = lexer->line;
    if (lexer->offset >= lexer->length) {
        token->kind = NCC_TOK_EOF;
        return 1;
    }
    ch = lexer->source[lexer->offset];
    if (ncc_is_alpha(ch)) {
        uint32_t len = 0u;

        token->kind = NCC_TOK_IDENT;
        while (lexer->offset < lexer->length &&
               ncc_is_alnum(lexer->source[lexer->offset])) {
            if (len + 1u < sizeof(token->text)) {
                token->text[len++] = lexer->source[lexer->offset];
            }
            lexer->offset++;
        }
        token->text[len] = '\0';
        return 1;
    }
    if (ncc_is_digit(ch)) {
        uint32_t base = 10u;
        uint64_t value = 0u;

        token->kind = NCC_TOK_NUMBER;
        if (ch == '0' && lexer->offset + 1u < lexer->length &&
            (lexer->source[lexer->offset + 1u] == 'x' ||
             lexer->source[lexer->offset + 1u] == 'X')) {
            base = 16u;
            lexer->offset += 2u;
        }
        while (lexer->offset < lexer->length) {
            uint8_t digit = base == 16u
                                ? ncc_hex_digit(lexer->source[lexer->offset])
                                : (ncc_is_digit(lexer->source[lexer->offset])
                                       ? (uint8_t)(lexer->source[lexer->offset] - '0')
                                       : 0xffu);
            if (digit >= base) {
                break;
            }
            value = value * base + digit;
            lexer->offset++;
        }
        while (lexer->offset < lexer->length &&
               (lexer->source[lexer->offset] == 'u' ||
                lexer->source[lexer->offset] == 'U' ||
                lexer->source[lexer->offset] == 'l' ||
                lexer->source[lexer->offset] == 'L')) {
            lexer->offset++;
        }
        token->value = value;
        snprintf(token->text, sizeof(token->text), "%llu", (unsigned long long)value);
        return 1;
    }
    if (ch == '"' || ch == '\'') {
        char quote = ch;
        uint32_t len = 0u;

        lexer->offset++;
        token->kind = quote == '"' ? NCC_TOK_STRING : NCC_TOK_CHAR;
        while (lexer->offset < lexer->length && lexer->source[lexer->offset] != quote) {
            char value;

            if (lexer->source[lexer->offset] == '\n') {
                ncc_set_error(lexer, "newline in literal");
                return 0;
            }
            if (lexer->source[lexer->offset] == '\\') {
                lexer->offset++;
                value = ncc_escape_char(lexer);
            } else {
                value = lexer->source[lexer->offset++];
            }
            if (len + 1u < sizeof(token->text)) {
                token->text[len++] = value;
            }
        }
        if (lexer->offset >= lexer->length) {
            ncc_set_error(lexer, "unterminated literal");
            return 0;
        }
        lexer->offset++;
        token->text[len] = '\0';
        token->value = len != 0u ? (uint8_t)token->text[0] : 0u;
        return 1;
    }
    token->kind = NCC_TOK_PUNCT;
    if (lexer->offset + 2u < lexer->length) {
        char next = lexer->source[lexer->offset + 1u];
        char third = lexer->source[lexer->offset + 2u];

        if (((ch == '<' && next == '<') ||
             (ch == '>' && next == '>')) &&
            third == '=') {
            token->text[0] = ch;
            token->text[1] = next;
            token->text[2] = third;
            token->text[3] = '\0';
            lexer->offset += 3u;
            return 1;
        }
    }
    if (lexer->offset + 1u < lexer->length) {
        char next = lexer->source[lexer->offset + 1u];

        if ((ch == '=' && next == '=') ||
            (ch == '!' && next == '=') ||
            (ch == '<' && next == '=') ||
            (ch == '>' && next == '=') ||
            (ch == '+' && next == '+') ||
            (ch == '-' && next == '-') ||
            (ch == '-' && next == '>') ||
            (ch == '+' && next == '=') ||
            (ch == '-' && next == '=') ||
            (ch == '&' && next == '=') ||
            (ch == '|' && next == '=') ||
            (ch == '^' && next == '=') ||
            (ch == '<' && next == '<') ||
            (ch == '>' && next == '>') ||
            (ch == '&' && next == '&') ||
            (ch == '|' && next == '|')) {
            token->text[0] = ch;
            token->text[1] = next;
            token->text[2] = '\0';
            lexer->offset += 2u;
            return 1;
        }
    }
    token->text[0] = ch;
    token->text[1] = '\0';
    lexer->offset++;
    return 1;
}
