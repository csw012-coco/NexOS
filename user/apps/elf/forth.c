#include <stddef.h>
#include <stdint.h>

#include "user/libc/include/fcntl.h"
#include "user/libc/include/file.h"
#include "user/libc/include/stdio.h"
#include "user/libc/include/stdlib.h"
#include "user/libc/include/string.h"
#include "user/libc/include/unistd.h"
#include "user/libc/include/nexos/string.h"
#include "user/libc/include/nexos/system.h"

typedef int64_t forth_cell;

enum {
    FORTH_STACK_MAX = 64u,
    FORTH_WORD_MAX = 96u,
    FORTH_NAME_MAX = 24u,
    FORTH_TOKEN_MAX = 64u,
    FORTH_OP_MAX = 768u,
    FORTH_STRING_POOL_SIZE = 4096u,
    FORTH_LINE_MAX = 256u,
    FORTH_FILE_MAX = 16384u,
    FORTH_RECURSION_MAX = 32u
};

enum forth_token_type {
    FORTH_TOKEN_END = 0,
    FORTH_TOKEN_WORD,
    FORTH_TOKEN_PRINT_STRING
};

enum forth_word_kind {
    FORTH_WORD_BUILTIN = 0,
    FORTH_WORD_USER
};

enum forth_op_type {
    FORTH_OP_PUSH = 0,
    FORTH_OP_CALL,
    FORTH_OP_PRINT
};

struct forth_state;

typedef int (*forth_builtin_fn)(struct forth_state *state);

struct forth_op {
    uint8_t type;
    uint16_t word_index;
    uint16_t string_offset;
    uint16_t string_length;
    forth_cell value;
};

struct forth_word {
    char name[FORTH_NAME_MAX];
    uint8_t kind;
    forth_builtin_fn builtin;
    uint16_t first_op;
    uint16_t op_count;
};

struct forth_state {
    forth_cell stack[FORTH_STACK_MAX];
    uint32_t sp;
    struct forth_word words[FORTH_WORD_MAX];
    uint32_t word_count;
    struct forth_op ops[FORTH_OP_MAX];
    uint32_t op_count;
    char strings[FORTH_STRING_POOL_SIZE];
    uint32_t string_count;
    int running;
};

static int forth_is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

static int forth_streq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static char forth_lower(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

static int forth_name_eq(const char *a, const char *b) {
    uint32_t i = 0u;

    if (a == 0 || b == 0) {
        return 0;
    }
    while (a[i] != '\0' && b[i] != '\0') {
        if (forth_lower(a[i]) != forth_lower(b[i])) {
            return 0;
        }
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static void forth_copy_text(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0u;

    if (dst == 0 || dst_size == 0u) {
        return;
    }
    if (src == 0) {
        dst[0] = '\0';
        return;
    }
    while (src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int forth_push(struct forth_state *state, forth_cell value) {
    if (state->sp >= FORTH_STACK_MAX) {
        eprintf("forth: stack overflow\n");
        return -1;
    }
    state->stack[state->sp++] = value;
    return 0;
}

static int forth_pop(struct forth_state *state, forth_cell *out) {
    if (state->sp == 0u) {
        eprintf("forth: stack underflow\n");
        return -1;
    }
    state->sp--;
    if (out != 0) {
        *out = state->stack[state->sp];
    }
    return 0;
}

static int forth_need(struct forth_state *state, uint32_t count) {
    if (state->sp < count) {
        eprintf("forth: stack underflow\n");
        return -1;
    }
    return 0;
}

static int forth_parse_number(const char *token, forth_cell *out) {
    const char *text = token;
    char *end = 0;
    int negative = 0;
    unsigned long long value;

    if (token == 0 || token[0] == '\0') {
        return 0;
    }
    if (*text == '-') {
        negative = 1;
        text++;
    } else if (*text == '+') {
        text++;
    }
    if (*text == '\0') {
        return 0;
    }
    value = strtoull(text, &end, 0);
    if (end == text || *end != '\0') {
        return 0;
    }
    if (out != 0) {
        *out = negative ? -(forth_cell)value : (forth_cell)value;
    }
    return 1;
}

static int forth_next_token(const char **cursor,
                            char *token,
                            uint32_t token_size,
                            int *type_out) {
    const char *p;
    uint32_t len = 0u;

    if (cursor == 0 || *cursor == 0 || token == 0 || token_size == 0u || type_out == 0) {
        return 0;
    }
    p = *cursor;
    for (;;) {
        while (forth_is_space(*p)) {
            p++;
        }
        if (*p == '\\') {
            while (*p != '\0' && *p != '\n') {
                p++;
            }
            continue;
        }
        if (*p == '(') {
            p++;
            while (*p != '\0' && *p != ')') {
                p++;
            }
            if (*p == ')') {
                p++;
            }
            continue;
        }
        break;
    }
    if (*p == '\0') {
        *cursor = p;
        token[0] = '\0';
        *type_out = FORTH_TOKEN_END;
        return 0;
    }
    if (p[0] == '.' && p[1] == '"') {
        p += 2;
        if (forth_is_space(*p)) {
            p++;
        }
        while (*p != '\0' && *p != '"' && len + 1u < token_size) {
            token[len++] = *p++;
        }
        token[len] = '\0';
        while (*p != '\0' && *p != '"') {
            p++;
        }
        if (*p == '"') {
            p++;
        }
        *cursor = p;
        *type_out = FORTH_TOKEN_PRINT_STRING;
        return 1;
    }
    while (*p != '\0' && !forth_is_space(*p) && len + 1u < token_size) {
        token[len++] = *p++;
    }
    token[len] = '\0';
    while (*p != '\0' && !forth_is_space(*p)) {
        p++;
    }
    *cursor = p;
    *type_out = FORTH_TOKEN_WORD;
    return 1;
}

static int forth_find_word(struct forth_state *state, const char *name) {
    int32_t i;

    if (state == 0 || name == 0) {
        return -1;
    }
    i = (int32_t)state->word_count - 1;
    while (i >= 0) {
        if (forth_name_eq(state->words[i].name, name)) {
            return i;
        }
        i--;
    }
    return -1;
}

static int forth_add_builtin(struct forth_state *state, const char *name, forth_builtin_fn fn) {
    struct forth_word *word;

    if (state->word_count >= FORTH_WORD_MAX) {
        return -1;
    }
    word = &state->words[state->word_count++];
    forth_copy_text(word->name, sizeof(word->name), name);
    word->kind = FORTH_WORD_BUILTIN;
    word->builtin = fn;
    word->first_op = 0u;
    word->op_count = 0u;
    return 0;
}

static int forth_add_string(struct forth_state *state,
                            const char *text,
                            uint16_t *offset_out,
                            uint16_t *length_out) {
    uint32_t len = 0u;
    uint32_t offset;

    while (text[len] != '\0') {
        len++;
    }
    if (state->string_count + len + 1u > FORTH_STRING_POOL_SIZE) {
        eprintf("forth: string pool full\n");
        return -1;
    }
    offset = state->string_count;
    for (uint32_t i = 0u; i < len; i++) {
        state->strings[offset + i] = text[i];
    }
    state->strings[offset + len] = '\0';
    state->string_count += len + 1u;
    *offset_out = (uint16_t)offset;
    *length_out = (uint16_t)len;
    return 0;
}

static int forth_add_op(struct forth_state *state, const struct forth_op *op) {
    if (state->op_count >= FORTH_OP_MAX) {
        eprintf("forth: word body space full\n");
        return -1;
    }
    state->ops[state->op_count++] = *op;
    return 0;
}

static int forth_execute_word(struct forth_state *state, uint32_t word_index, uint32_t depth);

static int forth_execute_op(struct forth_state *state, const struct forth_op *op, uint32_t depth) {
    if (op->type == FORTH_OP_PUSH) {
        return forth_push(state, op->value);
    }
    if (op->type == FORTH_OP_PRINT) {
        const char *text = state->strings + op->string_offset;

        (void)write(STDOUT_FILENO, text, op->string_length);
        return 0;
    }
    if (op->type == FORTH_OP_CALL) {
        return forth_execute_word(state, op->word_index, depth + 1u);
    }
    eprintf("forth: bad op\n");
    return -1;
}

static int forth_execute_word(struct forth_state *state, uint32_t word_index, uint32_t depth) {
    struct forth_word *word;

    if (word_index >= state->word_count) {
        eprintf("forth: bad word index\n");
        return -1;
    }
    if (depth > FORTH_RECURSION_MAX) {
        eprintf("forth: recursion limit\n");
        return -1;
    }
    word = &state->words[word_index];
    if (word->kind == FORTH_WORD_BUILTIN) {
        return word->builtin(state);
    }
    for (uint32_t i = 0u; i < word->op_count; i++) {
        if (forth_execute_op(state, &state->ops[word->first_op + i], depth) < 0) {
            return -1;
        }
        if (!state->running) {
            return 0;
        }
    }
    return 0;
}

static int forth_compile_definition(struct forth_state *state, const char **cursor) {
    char name[FORTH_NAME_MAX];
    char token[FORTH_TOKEN_MAX];
    int token_type;
    uint32_t op_start;
    uint32_t string_start;
    uint32_t op_count;
    int existing;

    if (!forth_next_token(cursor, name, sizeof(name), &token_type) || token_type != FORTH_TOKEN_WORD) {
        eprintf("forth: ':' expects a word name\n");
        return -1;
    }
    existing = forth_find_word(state, name);
    if (existing >= 0 && state->words[existing].kind == FORTH_WORD_BUILTIN) {
        eprintf("forth: cannot redefine builtin '%s'\n", name);
        return -1;
    }
    op_start = state->op_count;
    string_start = state->string_count;
    for (;;) {
        struct forth_op op;
        forth_cell value;
        int word_index;

        if (!forth_next_token(cursor, token, sizeof(token), &token_type)) {
            eprintf("forth: definition '%s' missing ';'\n", name);
            state->op_count = op_start;
            state->string_count = string_start;
            return -1;
        }
        if (token_type == FORTH_TOKEN_WORD && forth_streq(token, ";")) {
            break;
        }
        op.type = FORTH_OP_PUSH;
        op.word_index = 0u;
        op.string_offset = 0u;
        op.string_length = 0u;
        op.value = 0;
        if (token_type == FORTH_TOKEN_PRINT_STRING) {
            op.type = FORTH_OP_PRINT;
            if (forth_add_string(state, token, &op.string_offset, &op.string_length) < 0 ||
                forth_add_op(state, &op) < 0) {
                state->op_count = op_start;
                state->string_count = string_start;
                return -1;
            }
            continue;
        }
        if (forth_parse_number(token, &value)) {
            op.type = FORTH_OP_PUSH;
            op.value = value;
            if (forth_add_op(state, &op) < 0) {
                state->op_count = op_start;
                state->string_count = string_start;
                return -1;
            }
            continue;
        }
        if (forth_streq(token, ":")) {
            eprintf("forth: nested definitions are not supported\n");
            state->op_count = op_start;
            state->string_count = string_start;
            return -1;
        }
        word_index = forth_find_word(state, token);
        if (word_index < 0) {
            eprintf("forth: unknown word in definition: %s\n", token);
            state->op_count = op_start;
            state->string_count = string_start;
            return -1;
        }
        op.type = FORTH_OP_CALL;
        op.word_index = (uint16_t)word_index;
        if (forth_add_op(state, &op) < 0) {
            state->op_count = op_start;
            state->string_count = string_start;
            return -1;
        }
    }
    op_count = state->op_count - op_start;
    if (existing >= 0) {
        state->words[existing].first_op = (uint16_t)op_start;
        state->words[existing].op_count = (uint16_t)op_count;
        return 0;
    }
    if (state->word_count >= FORTH_WORD_MAX) {
        eprintf("forth: dictionary full\n");
        state->op_count = op_start;
        state->string_count = string_start;
        return -1;
    }
    forth_copy_text(state->words[state->word_count].name, FORTH_NAME_MAX, name);
    state->words[state->word_count].kind = FORTH_WORD_USER;
    state->words[state->word_count].builtin = 0;
    state->words[state->word_count].first_op = (uint16_t)op_start;
    state->words[state->word_count].op_count = (uint16_t)op_count;
    state->word_count++;
    return 0;
}

static int forth_eval_text(struct forth_state *state, const char *text) {
    const char *cursor = text;
    char token[FORTH_TOKEN_MAX];
    int token_type;

    while (state->running && forth_next_token(&cursor, token, sizeof(token), &token_type)) {
        forth_cell value;
        int word_index;

        if (token_type == FORTH_TOKEN_PRINT_STRING) {
            (void)write(STDOUT_FILENO, token, strlen(token));
            continue;
        }
        if (forth_streq(token, ":")) {
            if (forth_compile_definition(state, &cursor) < 0) {
                return -1;
            }
            continue;
        }
        if (forth_streq(token, ";")) {
            eprintf("forth: stray ';'\n");
            return -1;
        }
        if (forth_parse_number(token, &value)) {
            if (forth_push(state, value) < 0) {
                return -1;
            }
            continue;
        }
        word_index = forth_find_word(state, token);
        if (word_index < 0) {
            eprintf("forth: unknown word: %s\n", token);
            return -1;
        }
        if (forth_execute_word(state, (uint32_t)word_index, 0u) < 0) {
            return -1;
        }
    }
    return 0;
}

static int forth_binop_add(struct forth_state *state) {
    forth_cell a;
    forth_cell b;

    if (forth_pop(state, &b) < 0 || forth_pop(state, &a) < 0) {
        return -1;
    }
    return forth_push(state, a + b);
}

static int forth_binop_sub(struct forth_state *state) {
    forth_cell a;
    forth_cell b;

    if (forth_pop(state, &b) < 0 || forth_pop(state, &a) < 0) {
        return -1;
    }
    return forth_push(state, a - b);
}

static int forth_binop_mul(struct forth_state *state) {
    forth_cell a;
    forth_cell b;

    if (forth_pop(state, &b) < 0 || forth_pop(state, &a) < 0) {
        return -1;
    }
    return forth_push(state, a * b);
}

static int forth_binop_div(struct forth_state *state) {
    forth_cell a;
    forth_cell b;

    if (forth_pop(state, &b) < 0 || forth_pop(state, &a) < 0) {
        return -1;
    }
    if (b == 0) {
        eprintf("forth: division by zero\n");
        return -1;
    }
    return forth_push(state, a / b);
}

static int forth_binop_mod(struct forth_state *state) {
    forth_cell a;
    forth_cell b;

    if (forth_pop(state, &b) < 0 || forth_pop(state, &a) < 0) {
        return -1;
    }
    if (b == 0) {
        eprintf("forth: modulo by zero\n");
        return -1;
    }
    return forth_push(state, a % b);
}

static int forth_word_dup(struct forth_state *state) {
    if (forth_need(state, 1u) < 0) {
        return -1;
    }
    return forth_push(state, state->stack[state->sp - 1u]);
}

static int forth_word_drop(struct forth_state *state) {
    forth_cell ignored;

    return forth_pop(state, &ignored);
}

static int forth_word_swap(struct forth_state *state) {
    forth_cell top;

    if (forth_need(state, 2u) < 0) {
        return -1;
    }
    top = state->stack[state->sp - 1u];
    state->stack[state->sp - 1u] = state->stack[state->sp - 2u];
    state->stack[state->sp - 2u] = top;
    return 0;
}

static int forth_word_over(struct forth_state *state) {
    if (forth_need(state, 2u) < 0) {
        return -1;
    }
    return forth_push(state, state->stack[state->sp - 2u]);
}

static int forth_word_rot(struct forth_state *state) {
    forth_cell a;
    forth_cell b;
    forth_cell c;

    if (forth_need(state, 3u) < 0) {
        return -1;
    }
    a = state->stack[state->sp - 3u];
    b = state->stack[state->sp - 2u];
    c = state->stack[state->sp - 1u];
    state->stack[state->sp - 3u] = b;
    state->stack[state->sp - 2u] = c;
    state->stack[state->sp - 1u] = a;
    return 0;
}

static int forth_word_dot(struct forth_state *state) {
    forth_cell value;

    if (forth_pop(state, &value) < 0) {
        return -1;
    }
    printf("%lld ", (long long)value);
    return 0;
}

static int forth_word_dots(struct forth_state *state) {
    printf("<%u> ", state->sp);
    for (uint32_t i = 0u; i < state->sp; i++) {
        printf("%lld ", (long long)state->stack[i]);
    }
    return 0;
}

static int forth_word_cr(struct forth_state *state) {
    (void)state;
    printf("\n");
    return 0;
}

static int forth_word_emit(struct forth_state *state) {
    forth_cell value;
    char ch;

    if (forth_pop(state, &value) < 0) {
        return -1;
    }
    ch = (char)value;
    (void)write(STDOUT_FILENO, &ch, 1u);
    return 0;
}

static int forth_word_depth(struct forth_state *state) {
    return forth_push(state, (forth_cell)state->sp);
}

static int forth_word_equal(struct forth_state *state) {
    forth_cell a;
    forth_cell b;

    if (forth_pop(state, &b) < 0 || forth_pop(state, &a) < 0) {
        return -1;
    }
    return forth_push(state, a == b ? -1 : 0);
}

static int forth_word_less(struct forth_state *state) {
    forth_cell a;
    forth_cell b;

    if (forth_pop(state, &b) < 0 || forth_pop(state, &a) < 0) {
        return -1;
    }
    return forth_push(state, a < b ? -1 : 0);
}

static int forth_word_greater(struct forth_state *state) {
    forth_cell a;
    forth_cell b;

    if (forth_pop(state, &b) < 0 || forth_pop(state, &a) < 0) {
        return -1;
    }
    return forth_push(state, a > b ? -1 : 0);
}

static int forth_word_zero_equal(struct forth_state *state) {
    forth_cell value;

    if (forth_pop(state, &value) < 0) {
        return -1;
    }
    return forth_push(state, value == 0 ? -1 : 0);
}

static int forth_word_and(struct forth_state *state) {
    forth_cell a;
    forth_cell b;

    if (forth_pop(state, &b) < 0 || forth_pop(state, &a) < 0) {
        return -1;
    }
    return forth_push(state, a & b);
}

static int forth_word_or(struct forth_state *state) {
    forth_cell a;
    forth_cell b;

    if (forth_pop(state, &b) < 0 || forth_pop(state, &a) < 0) {
        return -1;
    }
    return forth_push(state, a | b);
}

static int forth_word_xor(struct forth_state *state) {
    forth_cell a;
    forth_cell b;

    if (forth_pop(state, &b) < 0 || forth_pop(state, &a) < 0) {
        return -1;
    }
    return forth_push(state, a ^ b);
}

static int forth_word_invert(struct forth_state *state) {
    forth_cell value;

    if (forth_pop(state, &value) < 0) {
        return -1;
    }
    return forth_push(state, ~value);
}

static int forth_word_words(struct forth_state *state) {
    for (uint32_t i = 0u; i < state->word_count; i++) {
        printf("%s ", state->words[i].name);
    }
    printf("\n");
    return 0;
}

static int forth_word_ticks(struct forth_state *state) {
    return forth_push(state, (forth_cell)ticks());
}

static int forth_word_sleep(struct forth_state *state) {
    forth_cell value;

    if (forth_pop(state, &value) < 0) {
        return -1;
    }
    if (value < 0) {
        value = 0;
    }
    sleep((uint32_t)value);
    return 0;
}

static int forth_word_clear(struct forth_state *state) {
    (void)state;
    clear();
    return 0;
}

static int forth_word_help(struct forth_state *state) {
    (void)state;
    printf("NexForth words: + - * / mod dup drop swap over rot . .s cr emit depth\n");
    printf("               = < > 0= and or xor invert words ticks sleep clear bye\n");
    printf("Define: : square dup * ;   Print string: .\" hello\"   Comment: \\ text\n");
    return 0;
}

static int forth_word_bye(struct forth_state *state) {
    state->running = 0;
    return 0;
}

static void forth_init(struct forth_state *state) {
    state->sp = 0u;
    state->word_count = 0u;
    state->op_count = 0u;
    state->string_count = 0u;
    state->running = 1;

    (void)forth_add_builtin(state, "+", forth_binop_add);
    (void)forth_add_builtin(state, "-", forth_binop_sub);
    (void)forth_add_builtin(state, "*", forth_binop_mul);
    (void)forth_add_builtin(state, "/", forth_binop_div);
    (void)forth_add_builtin(state, "mod", forth_binop_mod);
    (void)forth_add_builtin(state, "dup", forth_word_dup);
    (void)forth_add_builtin(state, "drop", forth_word_drop);
    (void)forth_add_builtin(state, "swap", forth_word_swap);
    (void)forth_add_builtin(state, "over", forth_word_over);
    (void)forth_add_builtin(state, "rot", forth_word_rot);
    (void)forth_add_builtin(state, ".", forth_word_dot);
    (void)forth_add_builtin(state, ".s", forth_word_dots);
    (void)forth_add_builtin(state, "cr", forth_word_cr);
    (void)forth_add_builtin(state, "emit", forth_word_emit);
    (void)forth_add_builtin(state, "depth", forth_word_depth);
    (void)forth_add_builtin(state, "=", forth_word_equal);
    (void)forth_add_builtin(state, "<", forth_word_less);
    (void)forth_add_builtin(state, ">", forth_word_greater);
    (void)forth_add_builtin(state, "0=", forth_word_zero_equal);
    (void)forth_add_builtin(state, "and", forth_word_and);
    (void)forth_add_builtin(state, "or", forth_word_or);
    (void)forth_add_builtin(state, "xor", forth_word_xor);
    (void)forth_add_builtin(state, "invert", forth_word_invert);
    (void)forth_add_builtin(state, "words", forth_word_words);
    (void)forth_add_builtin(state, "ticks", forth_word_ticks);
    (void)forth_add_builtin(state, "sleep", forth_word_sleep);
    (void)forth_add_builtin(state, "clear", forth_word_clear);
    (void)forth_add_builtin(state, "help", forth_word_help);
    (void)forth_add_builtin(state, "bye", forth_word_bye);
}

static int forth_eval_file(struct forth_state *state, const char *path) {
    char *file_buffer;
    int fd;
    uint32_t total = 0u;

    file_buffer = (char *)malloc(FORTH_FILE_MAX);
    if (file_buffer == 0) {
        eprintf("forth: out of memory\n");
        return -1;
    }
    fd = open(path, 0);
    if (fd < 0) {
        free(file_buffer);
        eprintf("forth: open failed: %s\n", path);
        return -1;
    }
    while (total + 1u < FORTH_FILE_MAX) {
        ssize_t got = read(fd, file_buffer + total, FORTH_FILE_MAX - total - 1u);

        if (got < 0) {
            close((uint32_t)fd);
            free(file_buffer);
            eprintf("forth: read failed: %s\n", path);
            return -1;
        }
        if (got == 0) {
            break;
        }
        total += (uint32_t)got;
    }
    close((uint32_t)fd);
    if (total + 1u >= FORTH_FILE_MAX) {
        free(file_buffer);
        eprintf("forth: file too large: %s\n", path);
        return -1;
    }
    file_buffer[total] = '\0';
    {
        int rc = forth_eval_text(state, file_buffer);

        free(file_buffer);
        return rc;
    }
}

static int forth_repl(struct forth_state *state) {
    char line[FORTH_LINE_MAX];

    printf("NexForth 0.1  (help lists words, bye exits)\n");
    while (state->running) {
        uint32_t got;

        printf("nforth> ");
        got = read_line(STDIN_FILENO, line, sizeof(line));
        if (got == 0u) {
            printf("\n");
            break;
        }
        trim_line(line);
        if (line[0] == '\0') {
            continue;
        }
        if (forth_eval_text(state, line) < 0) {
            printf("\n");
            continue;
        }
        if (state->running) {
            printf(" ok\n");
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    struct forth_state *state = (struct forth_state *)calloc(1u, sizeof(*state));
    int rc = 0;

    if (state == 0) {
        eprintf("forth: out of memory\n");
        return 1;
    }
    forth_init(state);
    if (argc >= 3 && forth_streq(argv[1], "-e")) {
        if (forth_eval_text(state, argv[2]) < 0) {
            rc = 1;
        } else {
            printf("\n");
        }
        free(state);
        return rc;
    }
    if (argc > 1) {
        for (int i = 1; i < argc && state->running; i++) {
            if (forth_eval_file(state, argv[i]) < 0) {
                rc = 1;
                break;
            }
        }
        free(state);
        return rc;
    }
    rc = forth_repl(state);
    free(state);
    return rc;
}
