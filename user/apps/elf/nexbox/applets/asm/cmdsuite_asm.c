#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"

enum {
    FASM_LINE_MAX = 255u,
    FASM_LABEL_MAX = 128u,
    FASM_NAME_MAX = 63u,
    FASM_EXPR_MAX = 127u,
    FASM_DATA_MAX_OPERANDS = 32u,
    FASM_FMT_ELF64 = 0u,
    FASM_FMT_BINARY = 1u,
    FASM_USER_ELF_BASE = 0x0000008000000000ull,
    FASM_USER_ELF_LIMIT = 0x0000008000400000ull,
    FASM_ELF_EHDR_SIZE = 64u,
    FASM_ELF_PHDR_SIZE = 56u
};

struct fasm_label {
    char name[FASM_NAME_MAX + 1];
    uint64_t value;
};

struct fasm_state {
    struct fasm_label labels[FASM_LABEL_MAX];
    uint32_t label_count;
    uint32_t format;
    uint32_t line_number;
    uint64_t origin;
    uint64_t offset;
    uint64_t entry_value;
    char entry_expr[FASM_EXPR_MAX + 1];
    uint8_t entry_defined;
    char error_text[96];
};

struct fasm_output {
    int fd;
};

struct fasm_elf64_ehdr {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed));

struct fasm_elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed));

static int fasm_eval_expression(const struct fasm_state *state,
                                const char *expr,
                                uint64_t current_value,
                                uint64_t *value_out);
static int fasm_process_data_directive(struct fasm_state *state,
                                       struct fasm_output *out,
                                       const char *rest,
                                       uint32_t unit_size,
                                       int allow_string);

static int fasm_is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static char *fasm_skip_spaces(char *text) {
    while (*text != '\0' && fasm_is_space(*text)) {
        text++;
    }
    return text;
}

static char *fasm_trim_line_local(char *text) {
    uint32_t len;

    text = fasm_skip_spaces(text);
    len = str_len_local(text);
    while (len != 0u && fasm_is_space(text[len - 1u])) {
        text[--len] = '\0';
    }
    return text;
}

static void fasm_strip_comment(char *text) {
    uint32_t i = 0;
    char quote = '\0';

    while (text[i] != '\0') {
        char ch = text[i];

        if (quote != '\0') {
            if (ch == '\\' && text[i + 1u] != '\0') {
                i += 2u;
                continue;
            }
            if (ch == quote) {
                quote = '\0';
            }
            i++;
            continue;
        }
        if (ch == '\'' || ch == '"') {
            quote = ch;
            i++;
            continue;
        }
        if (ch == ';') {
            text[i] = '\0';
            return;
        }
        i++;
    }
}

static int fasm_is_ident_start(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' || ch == '.' || ch == '$';
}

static int fasm_is_ident_char(char ch) {
    return fasm_is_ident_start(ch) || (ch >= '0' && ch <= '9');
}

static void fasm_set_error(struct fasm_state *state, const char *text) {
    if (state == NULL || state->error_text[0] != '\0') {
        return;
    }
    copy_line_local(state->error_text, text, sizeof(state->error_text));
}

static void fasm_set_error_with_operand(struct fasm_state *state, const char *prefix, const char *operand) {
    char message[sizeof(state->error_text)];

    if (state == NULL || state->error_text[0] != '\0') {
        return;
    }
    message[0] = '\0';
    if (prefix != NULL) {
        copy_line_local(message, prefix, sizeof(message));
    }
    if (operand != NULL && operand[0] != '\0') {
        uint32_t used = str_len_local(message);

        if (used + 1u < sizeof(message)) {
            copy_line_local(message + used, operand, sizeof(message) - used);
        }
    }
    copy_line_local(state->error_text, message, sizeof(state->error_text));
}

static int fasm_parse_identifier_token(const char *text, char *name_out, uint32_t name_size, uint32_t *len_out) {
    uint32_t len = 0;

    if (text == NULL || name_out == NULL || name_size == 0 || !fasm_is_ident_start(text[0])) {
        return 0;
    }
    while (fasm_is_ident_char(text[len])) {
        if (len + 1u >= name_size) {
            return 0;
        }
        name_out[len] = text[len];
        len++;
    }
    name_out[len] = '\0';
    if (len_out != NULL) {
        *len_out = len;
    }
    return 1;
}

static int fasm_lookup_label(const struct fasm_state *state, const char *name, uint64_t *value_out) {
    uint32_t i;

    if (state == NULL || name == NULL) {
        return 0;
    }
    for (i = 0; i < state->label_count; i++) {
        if (streq_ignore_case_local(state->labels[i].name, name)) {
            if (value_out != NULL) {
                *value_out = state->labels[i].value;
            }
            return 1;
        }
    }
    return 0;
}

static int fasm_define_label(struct fasm_state *state, const char *name, uint64_t value) {
    uint32_t i;

    if (state == NULL || name == NULL || name[0] == '\0') {
        return 0;
    }
    for (i = 0; i < state->label_count; i++) {
        if (streq_ignore_case_local(state->labels[i].name, name)) {
            if (state->labels[i].value != value) {
                fasm_set_error(state, "duplicate label");
                return 0;
            }
            return 1;
        }
    }
    if (state->label_count >= FASM_LABEL_MAX) {
        fasm_set_error(state, "too many labels");
        return 0;
    }
    copy_line_local(state->labels[state->label_count].name, name, sizeof(state->labels[state->label_count].name));
    state->labels[state->label_count].value = value;
    state->label_count++;
    return 1;
}

static int fasm_parse_constant_definition(const struct fasm_state *state,
                                          char *cursor,
                                          uint64_t current_value,
                                          char *name_out,
                                          uint32_t name_size,
                                          uint64_t *value_out) {
    uint32_t name_len = 0;
    char keyword[FASM_NAME_MAX + 1];
    uint32_t keyword_len = 0;

    if (state == NULL || cursor == NULL || name_out == NULL || value_out == NULL) {
        return 0;
    }
    if (!fasm_parse_identifier_token(cursor, name_out, name_size, &name_len)) {
        return 0;
    }
    cursor = fasm_trim_line_local(cursor + name_len);
    if (*cursor == '=') {
        cursor = fasm_trim_line_local(cursor + 1u);
        return fasm_eval_expression(state, cursor, current_value, value_out);
    }
    if (!fasm_parse_identifier_token(cursor, keyword, sizeof(keyword), &keyword_len) ||
        !streq_ignore_case_local(keyword, "equ")) {
        return 0;
    }
    cursor = fasm_trim_line_local(cursor + keyword_len);
    return fasm_eval_expression(state, cursor, current_value, value_out);
}

static int fasm_parse_label_definition(char **cursor_io, char *name_out, uint32_t name_size) {
    char *cursor = fasm_skip_spaces(*cursor_io);
    uint32_t len = 0;

    if (!fasm_parse_identifier_token(cursor, name_out, name_size, &len) || cursor[len] != ':') {
        return 0;
    }
    *cursor_io = cursor + len + 1u;
    return 1;
}

static int fasm_try_labeled_data_directive(struct fasm_state *state,
                                           struct fasm_output *out,
                                           const char *label_name,
                                           char *cursor) {
    char directive[FASM_NAME_MAX + 1];
    uint32_t directive_len = 0;

    if (state == NULL || label_name == NULL || cursor == NULL) {
        return 0;
    }
    if (!fasm_parse_identifier_token(cursor, directive, sizeof(directive), &directive_len)) {
        return 0;
    }
    cursor = fasm_trim_line_local(cursor + directive_len);

    if (!streq_ignore_case_local(directive, "db") && !streq_ignore_case_local(directive, "dw") &&
        !streq_ignore_case_local(directive, "dd") && !streq_ignore_case_local(directive, "dq")) {
        return 0;
    }
    if (!fasm_define_label(state, label_name, state->origin + state->offset)) {
        return -1;
    }
    if (streq_ignore_case_local(directive, "db")) {
        return fasm_process_data_directive(state, out, cursor, 1u, 1);
    }
    if (streq_ignore_case_local(directive, "dw")) {
        return fasm_process_data_directive(state, out, cursor, 2u, 0);
    }
    if (streq_ignore_case_local(directive, "dd")) {
        return fasm_process_data_directive(state, out, cursor, 4u, 0);
    }
    return fasm_process_data_directive(state, out, cursor, 8u, 0);
}

static int fasm_parse_char_literal(const char *text, uint32_t *consumed_out, uint8_t *value_out) {
    char quote;
    uint8_t value;
    uint32_t pos = 0;

    if (text == NULL || (text[0] != '\'' && text[0] != '"')) {
        return 0;
    }
    quote = text[pos++];
    if (text[pos] == '\\') {
        pos++;
        switch (text[pos]) {
            case 'n':
                value = '\n';
                break;
            case 'r':
                value = '\r';
                break;
            case 't':
                value = '\t';
                break;
            case '0':
                value = '\0';
                break;
            case '\\':
                value = '\\';
                break;
            case '\'':
                value = '\'';
                break;
            case '"':
                value = '"';
                break;
            default:
                value = (uint8_t)text[pos];
                break;
        }
        pos++;
    } else if (text[pos] == '\0' || text[pos] == quote) {
        return 0;
    } else {
        value = (uint8_t)text[pos++];
    }
    if (text[pos] != quote) {
        return 0;
    }
    pos++;
    if (consumed_out != NULL) {
        *consumed_out = pos;
    }
    if (value_out != NULL) {
        *value_out = value;
    }
    return 1;
}

static int fasm_eval_expression(const struct fasm_state *state,
                                const char *expr,
                                uint64_t current_value,
                                uint64_t *value_out) {
    const char *cursor = expr;
    int have_term = 0;
    int sign = 1;
    int64_t total = 0;

    if (expr == NULL || value_out == NULL) {
        return 0;
    }

    for (;;) {
        uint64_t term = 0;
        uint8_t ch_value;
        uint32_t consumed = 0;
        char name[FASM_NAME_MAX + 1];
        uint32_t name_len = 0;
        char *end = 0;
        unsigned long long number;

        while (*cursor != '\0' && fasm_is_space(*cursor)) {
            cursor++;
        }
        if (!have_term && (*cursor == '+' || *cursor == '-')) {
            sign = *cursor == '-' ? -1 : 1;
            cursor++;
            continue;
        }
        if (have_term) {
            if (*cursor == '\0') {
                break;
            }
            if (*cursor == '+') {
                sign = 1;
                cursor++;
            } else if (*cursor == '-') {
                sign = -1;
                cursor++;
            } else {
                return 0;
            }
            while (*cursor != '\0' && fasm_is_space(*cursor)) {
                cursor++;
            }
        }
        if (*cursor == '\0') {
            return 0;
        }

        if (*cursor == '$') {
            term = current_value;
            cursor++;
        } else if (fasm_parse_char_literal(cursor, &consumed, &ch_value)) {
            term = ch_value;
            cursor += consumed;
        } else if (fasm_parse_identifier_token(cursor, name, sizeof(name), &name_len)) {
            if (!fasm_lookup_label(state, name, &term)) {
                return 0;
            }
            cursor += name_len;
        } else {
            number = strtoull(cursor, &end, 0);
            if (end == cursor) {
                return 0;
            }
            term = (uint64_t)number;
            cursor = end;
        }

        total += sign > 0 ? (int64_t)term : -(int64_t)term;
        have_term = 1;
    }

    if (!have_term) {
        return 0;
    }
    *value_out = (uint64_t)total;
    return 1;
}

static int fasm_write_all(int fd, const void *data, uint32_t size) {
    uint32_t done = 0;

    while (done < size) {
        uint32_t written = write_fd((uint32_t)fd, (const char *)data + done, size - done);

        if (written == 0u) {
            return 0;
        }
        done += written;
    }
    return 1;
}

static int fasm_emit_bytes(struct fasm_state *state,
                           struct fasm_output *out,
                           const void *data,
                           uint32_t size) {
    if (out != NULL && !fasm_write_all(out->fd, data, size)) {
        fasm_set_error(state, "output write failed");
        return 0;
    }
    state->offset += size;
    return 1;
}

static int fasm_emit_u8(struct fasm_state *state, struct fasm_output *out, uint8_t value) {
    return fasm_emit_bytes(state, out, &value, 1u);
}

static int fasm_emit_u16(struct fasm_state *state, struct fasm_output *out, uint16_t value) {
    uint8_t data[2];

    data[0] = (uint8_t)(value & 0xffu);
    data[1] = (uint8_t)((value >> 8) & 0xffu);
    return fasm_emit_bytes(state, out, data, sizeof(data));
}

static int fasm_emit_u32(struct fasm_state *state, struct fasm_output *out, uint32_t value) {
    uint8_t data[4];

    data[0] = (uint8_t)(value & 0xffu);
    data[1] = (uint8_t)((value >> 8) & 0xffu);
    data[2] = (uint8_t)((value >> 16) & 0xffu);
    data[3] = (uint8_t)((value >> 24) & 0xffu);
    return fasm_emit_bytes(state, out, data, sizeof(data));
}

static int fasm_emit_u64(struct fasm_state *state, struct fasm_output *out, uint64_t value) {
    uint8_t data[8];
    uint32_t i;

    for (i = 0; i < 8u; i++) {
        data[i] = (uint8_t)((value >> (i * 8u)) & 0xffu);
    }
    return fasm_emit_bytes(state, out, data, sizeof(data));
}

static int fasm_split_operands(char *text, char *operands[], uint32_t max_operands) {
    uint32_t count = 0;
    char quote = '\0';
    int bracket_depth = 0;
    char *cursor = text;
    char *start;

    for (;;) {
        cursor = fasm_skip_spaces(cursor);
        if (*cursor == '\0') {
            return (int)count;
        }
        if (count >= max_operands) {
            return -1;
        }
        start = cursor;
        while (*cursor != '\0') {
            if (quote != '\0') {
                if (*cursor == '\\' && cursor[1] != '\0') {
                    cursor += 2;
                    continue;
                }
                if (*cursor == quote) {
                    quote = '\0';
                }
                cursor++;
                continue;
            }
            if (*cursor == '\'' || *cursor == '"') {
                quote = *cursor++;
                continue;
            }
            if (*cursor == '[') {
                bracket_depth++;
                cursor++;
                continue;
            }
            if (*cursor == ']') {
                if (bracket_depth > 0) {
                    bracket_depth--;
                }
                cursor++;
                continue;
            }
            if (bracket_depth == 0 && *cursor == ',') {
                break;
            }
            cursor++;
        }
        if (*cursor != '\0') {
            *cursor++ = '\0';
        }
        operands[count++] = fasm_trim_line_local(start);
    }
}

static int fasm_parse_register64(const char *text, uint32_t *reg_out) {
    static const char *const names[] = {
        "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
        "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15"};
    uint32_t i;

    if (text == NULL || reg_out == NULL) {
        return 0;
    }
    for (i = 0; i < 16u; i++) {
        if (streq_ignore_case_local(text, names[i])) {
            *reg_out = i;
            return 1;
        }
    }
    return 0;
}

static int fasm_emit_rex_modrm(struct fasm_state *state,
                               struct fasm_output *out,
                               uint8_t opcode,
                               uint32_t dst_reg,
                               uint32_t src_reg) {
    uint8_t rex = 0x48u | (uint8_t)(((src_reg >> 3u) & 1u) << 2u) | (uint8_t)((dst_reg >> 3u) & 1u);
    uint8_t modrm = 0xc0u | (uint8_t)((src_reg & 7u) << 3u) | (uint8_t)(dst_reg & 7u);

    return fasm_emit_u8(state, out, rex) &&
           fasm_emit_u8(state, out, opcode) &&
           fasm_emit_u8(state, out, modrm);
}

static int fasm_process_data_directive(struct fasm_state *state,
                                       struct fasm_output *out,
                                       const char *rest,
                                       uint32_t unit_size,
                                       int allow_string) {
    int sizing_only = out == NULL;
    char buffer[FASM_LINE_MAX + 1];
    char *operands[FASM_DATA_MAX_OPERANDS];
    int operand_count;
    int i;

    copy_line_local(buffer, rest, sizeof(buffer));
    operand_count = fasm_split_operands(buffer, operands, FASM_DATA_MAX_OPERANDS);
    if (operand_count <= 0) {
        fasm_set_error(state, "missing data operands");
        return 0;
    }

    for (i = 0; i < operand_count; i++) {
        char *operand = operands[i];

        if (allow_string && (operand[0] == '\'' || operand[0] == '"')) {
            char quote = operand[0];
            uint32_t pos = 1u;

            while (operand[pos] != '\0' && operand[pos] != quote) {
                uint8_t value;

                if (operand[pos] == '\\' && operand[pos + 1u] != '\0') {
                    pos++;
                    switch (operand[pos]) {
                        case 'n':
                            value = '\n';
                            break;
                        case 'r':
                            value = '\r';
                            break;
                        case 't':
                            value = '\t';
                            break;
                        case '0':
                            value = '\0';
                            break;
                        default:
                            value = (uint8_t)operand[pos];
                            break;
                    }
                    pos++;
                } else {
                    value = (uint8_t)operand[pos++];
                }
                if (!fasm_emit_u8(state, out, value)) {
                    return 0;
                }
            }
            if (operand[pos] != quote || operand[pos + 1u] != '\0') {
                fasm_set_error(state, "invalid string literal");
                return 0;
            }
            continue;
        }

        {
            uint64_t value = 0;

            if (!fasm_eval_expression(state, operand, state->origin + state->offset, &value) && !sizing_only) {
                fasm_set_error_with_operand(state, "invalid data expression: ", operand);
                return 0;
            }
            switch (unit_size) {
                case 1u:
                    if (!fasm_emit_u8(state, out, (uint8_t)value)) {
                        return 0;
                    }
                    break;
                case 2u:
                    if (!fasm_emit_u16(state, out, (uint16_t)value)) {
                        return 0;
                    }
                    break;
                case 4u:
                    if (!fasm_emit_u32(state, out, (uint32_t)value)) {
                        return 0;
                    }
                    break;
                case 8u:
                    if (!fasm_emit_u64(state, out, value)) {
                        return 0;
                    }
                    break;
                default:
                    fasm_set_error(state, "unsupported data size");
                    return 0;
            }
        }
    }

    return 1;
}

static int fasm_process_instruction(struct fasm_state *state,
                                    struct fasm_output *out,
                                    const char *name,
                                    char *rest) {
    int sizing_only = out == NULL;
    char *operands[3];
    int operand_count;

    if (streq_ignore_case_local(name, "ret")) {
        return fasm_emit_u8(state, out, 0xc3u);
    }
    if (streq_ignore_case_local(name, "syscall")) {
        return fasm_emit_u8(state, out, 0x0fu) && fasm_emit_u8(state, out, 0x05u);
    }
    if (streq_ignore_case_local(name, "nop")) {
        return fasm_emit_u8(state, out, 0x90u);
    }
    if (streq_ignore_case_local(name, "int3")) {
        return fasm_emit_u8(state, out, 0xccu);
    }
    if (streq_ignore_case_local(name, "hlt")) {
        return fasm_emit_u8(state, out, 0xf4u);
    }
    if (streq_ignore_case_local(name, "int")) {
        uint64_t value = 0;

        rest = fasm_trim_line_local(rest);
        if ((!fasm_eval_expression(state, rest, state->origin + state->offset, &value) && !sizing_only) ||
            (!sizing_only && value > 0xffu)) {
            fasm_set_error(state, "invalid int vector");
            return 0;
        }
        return fasm_emit_u8(state, out, 0xcdu) && fasm_emit_u8(state, out, (uint8_t)value);
    }
    if (streq_ignore_case_local(name, "jmp") || streq_ignore_case_local(name, "call") ||
        streq_ignore_case_local(name, "jz") || streq_ignore_case_local(name, "je") ||
        streq_ignore_case_local(name, "jnz") || streq_ignore_case_local(name, "jne")) {
        uint64_t target = 0;
        int64_t delta;
        uint64_t next_ip;

        rest = fasm_trim_line_local(rest);
        if (!fasm_eval_expression(state, rest, state->origin + state->offset, &target) && !sizing_only) {
            fasm_set_error(state, "invalid branch target");
            return 0;
        }
        next_ip = state->origin + state->offset +
                  ((streq_ignore_case_local(name, "jz") || streq_ignore_case_local(name, "je") ||
                    streq_ignore_case_local(name, "jnz") || streq_ignore_case_local(name, "jne"))
                       ? 6u
                       : 5u);
        delta = (int64_t)target - (int64_t)next_ip;
        if (!sizing_only && (delta < -2147483648ll || delta > 2147483647ll)) {
            fasm_set_error(state, "branch target out of range");
            return 0;
        }
        if (streq_ignore_case_local(name, "call")) {
            return fasm_emit_u8(state, out, 0xe8u) &&
                   fasm_emit_u32(state, out, (uint32_t)(int32_t)delta);
        }
        if (streq_ignore_case_local(name, "jmp")) {
            return fasm_emit_u8(state, out, 0xe9u) &&
                   fasm_emit_u32(state, out, (uint32_t)(int32_t)delta);
        }
        return fasm_emit_u8(state, out, 0x0fu) &&
               fasm_emit_u8(state, out,
                            (uint8_t)((streq_ignore_case_local(name, "jz") || streq_ignore_case_local(name, "je"))
                                          ? 0x84u
                                          : 0x85u)) &&
               fasm_emit_u32(state, out, (uint32_t)(int32_t)delta);
    }

    operand_count = fasm_split_operands(rest, operands, 3u);
    if (operand_count < 0) {
        fasm_set_error(state, "too many operands");
        return 0;
    }

    if (streq_ignore_case_local(name, "push") || streq_ignore_case_local(name, "pop")) {
        uint32_t reg;

        if (operand_count != 1 || !fasm_parse_register64(operands[0], &reg)) {
            fasm_set_error(state, "push/pop expects one register");
            return 0;
        }
        if (reg >= 8u && !fasm_emit_u8(state, out, 0x41u)) {
            return 0;
        }
        return fasm_emit_u8(state, out, (uint8_t)((streq_ignore_case_local(name, "push") ? 0x50u : 0x58u) +
                                                   (reg & 7u)));
    }

    if (streq_ignore_case_local(name, "mov")) {
        uint32_t dst_reg;
        uint32_t src_reg;
        uint64_t imm = 0;

        if (operand_count != 2 || !fasm_parse_register64(operands[0], &dst_reg)) {
            fasm_set_error(state, "mov expects register destination");
            return 0;
        }
        if (fasm_parse_register64(operands[1], &src_reg)) {
            return fasm_emit_rex_modrm(state, out, 0x89u, dst_reg, src_reg);
        }
        if (!fasm_eval_expression(state, operands[1], state->origin + state->offset, &imm) && !sizing_only) {
            fasm_set_error(state, "invalid mov source");
            return 0;
        }
        return fasm_emit_u8(state, out, (uint8_t)(0x48u | ((dst_reg >> 3u) & 1u))) &&
               fasm_emit_u8(state, out, (uint8_t)(0xb8u + (dst_reg & 7u))) &&
               fasm_emit_u64(state, out, imm);
    }

    if (streq_ignore_case_local(name, "xor")) {
        uint32_t dst_reg;
        uint32_t src_reg;

        if (operand_count != 2 || !fasm_parse_register64(operands[0], &dst_reg) ||
            !fasm_parse_register64(operands[1], &src_reg)) {
            fasm_set_error(state, "xor expects two registers");
            return 0;
        }
        return fasm_emit_rex_modrm(state, out, 0x31u, dst_reg, src_reg);
    }

    fasm_set_error(state, "unsupported instruction");
    return 0;
}

static int fasm_process_statement(struct fasm_state *state, struct fasm_output *out, char *line) {
    char name[FASM_NAME_MAX + 1];
    char const_name[FASM_NAME_MAX + 1];
    uint32_t name_len = 0;
    char *cursor = line;
    uint64_t const_value = 0;

    while (fasm_parse_label_definition(&cursor, name, sizeof(name))) {
        if (!fasm_define_label(state, name, state->origin + state->offset)) {
            return 0;
        }
        cursor = fasm_skip_spaces(cursor);
    }
    cursor = fasm_trim_line_local(cursor);
    if (*cursor == '\0') {
        return 1;
    }

    if (fasm_parse_constant_definition(state,
                                       cursor,
                                       state->origin + state->offset,
                                       const_name,
                                       sizeof(const_name),
                                       &const_value)) {
        return fasm_define_label(state, const_name, const_value);
    }

    if (!fasm_parse_identifier_token(cursor, name, sizeof(name), &name_len)) {
        fasm_set_error(state, "invalid statement");
        return 0;
    }
    cursor = fasm_trim_line_local(cursor + name_len);

    if (!streq_ignore_case_local(name, "format") && !streq_ignore_case_local(name, "org") &&
        !streq_ignore_case_local(name, "entry") && !streq_ignore_case_local(name, "bits") &&
        !streq_ignore_case_local(name, "use") && !streq_ignore_case_local(name, "use64") &&
        !streq_ignore_case_local(name, "section") && !streq_ignore_case_local(name, "segment") &&
        !streq_ignore_case_local(name, "global") && !streq_ignore_case_local(name, "public") &&
        !streq_ignore_case_local(name, "db") && !streq_ignore_case_local(name, "dw") &&
        !streq_ignore_case_local(name, "dd") && !streq_ignore_case_local(name, "dq")) {
        int labeled_data = fasm_try_labeled_data_directive(state, out, name, cursor);

        if (labeled_data != 0) {
            return labeled_data > 0;
        }
    }

    if (streq_ignore_case_local(name, "format")) {
        if (state->offset != 0u) {
            fasm_set_error(state, "format must be set before output");
            return 0;
        }
        if (streq_ignore_case_local(cursor, "binary")) {
            state->format = FASM_FMT_BINARY;
            return 1;
        }
        if (streq_ignore_case_local(cursor, "elf64")) {
            state->format = FASM_FMT_ELF64;
            return 1;
        }
        fasm_set_error(state, "unsupported format");
        return 0;
    }

    if (streq_ignore_case_local(name, "org")) {
        uint64_t value = 0;

        if (state->offset != 0u) {
            fasm_set_error(state, "org must be set before output");
            return 0;
        }
        if (!fasm_eval_expression(state, cursor, state->origin, &value)) {
            fasm_set_error(state, "invalid org expression");
            return 0;
        }
        state->origin = value;
        return 1;
    }

    if (streq_ignore_case_local(name, "entry")) {
        if (*cursor == '\0') {
            fasm_set_error(state, "missing entry expression");
            return 0;
        }
        copy_line_local(state->entry_expr, cursor, sizeof(state->entry_expr));
        state->entry_defined = 1u;
        return 1;
    }

    if (streq_ignore_case_local(name, "bits")) {
        if (!streq_ignore_case_local(cursor, "64")) {
            fasm_set_error(state, "only bits 64 is supported");
            return 0;
        }
        return 1;
    }

    if (streq_ignore_case_local(name, "use")) {
        if (!streq_ignore_case_local(cursor, "64")) {
            fasm_set_error(state, "only use 64 is supported");
            return 0;
        }
        return 1;
    }

    if (streq_ignore_case_local(name, "use64") || streq_ignore_case_local(name, "section") ||
        streq_ignore_case_local(name, "segment") || streq_ignore_case_local(name, "global") ||
        streq_ignore_case_local(name, "public")) {
        return 1;
    }

    if (streq_ignore_case_local(name, "db")) {
        return fasm_process_data_directive(state, out, cursor, 1u, 1);
    }
    if (streq_ignore_case_local(name, "dw")) {
        return fasm_process_data_directive(state, out, cursor, 2u, 0);
    }
    if (streq_ignore_case_local(name, "dd")) {
        return fasm_process_data_directive(state, out, cursor, 4u, 0);
    }
    if (streq_ignore_case_local(name, "dq")) {
        return fasm_process_data_directive(state, out, cursor, 8u, 0);
    }

    return fasm_process_instruction(state, out, name, cursor);
}

static int fasm_run_pass(struct fasm_state *state, int fd, struct fasm_output *out) {
    char raw_line[FASM_LINE_MAX + 1];
    uint32_t got;

    state->offset = 0;
    state->line_number = 0;
    while ((got = read_line((uint32_t)fd, raw_line, sizeof(raw_line))) != 0u) {
        char *line;

        (void)got;
        state->line_number++;
        fasm_strip_comment(raw_line);
        line = fasm_trim_line_local(raw_line);
        if (*line == '\0') {
            continue;
        }
        if (!fasm_process_statement(state, out, line)) {
            return 0;
        }
    }
    return 1;
}

static int fasm_derive_output_path(char *dst, uint32_t dst_size, const char *src, uint32_t format) {
    uint32_t len = str_len_local(src);
    uint32_t last_slash = 0xffffffffu;
    uint32_t last_dot = 0xffffffffu;
    uint32_t i;
    const char *ext = format == FASM_FMT_BINARY ? ".bin" : ".elf";
    uint32_t ext_len = str_len_local(ext);

    if (len + ext_len + 1u >= dst_size) {
        return 0;
    }
    copy_line_local(dst, src, dst_size);
    for (i = 0; i < len; i++) {
        if (src[i] == '/') {
            last_slash = i;
            last_dot = 0xffffffffu;
        } else if (src[i] == '.') {
            last_dot = i;
        }
    }
    if (last_dot != 0xffffffffu && last_dot > last_slash) {
        dst[last_dot] = '\0';
    }
    if (str_len_local(dst) + ext_len + 1u >= dst_size) {
        return 0;
    }
    copy_line_local(dst + str_len_local(dst), ext, dst_size - str_len_local(dst));
    return 1;
}

static int fasm_write_elf_header(struct fasm_state *state, struct fasm_output *out) {
    struct fasm_elf64_ehdr ehdr;
    struct fasm_elf64_phdr phdr;
    uint32_t i;

    for (i = 0; i < sizeof(ehdr.e_ident); i++) {
        ehdr.e_ident[i] = 0;
    }
    ehdr.e_ident[0] = 0x7fu;
    ehdr.e_ident[1] = 'E';
    ehdr.e_ident[2] = 'L';
    ehdr.e_ident[3] = 'F';
    ehdr.e_ident[4] = 2u;
    ehdr.e_ident[5] = 1u;
    ehdr.e_ident[6] = 1u;
    ehdr.e_type = 2u;
    ehdr.e_machine = 62u;
    ehdr.e_version = 1u;
    ehdr.e_entry = state->entry_value;
    ehdr.e_phoff = sizeof(ehdr);
    ehdr.e_shoff = 0u;
    ehdr.e_flags = 0u;
    ehdr.e_ehsize = sizeof(ehdr);
    ehdr.e_phentsize = sizeof(phdr);
    ehdr.e_phnum = 1u;
    ehdr.e_shentsize = 0u;
    ehdr.e_shnum = 0u;
    ehdr.e_shstrndx = 0u;

    phdr.p_type = 1u;
    phdr.p_flags = 7u;
    phdr.p_offset = sizeof(ehdr) + sizeof(phdr);
    phdr.p_vaddr = state->origin;
    phdr.p_paddr = state->origin;
    phdr.p_filesz = state->offset;
    phdr.p_memsz = state->offset;
    phdr.p_align = 0x1000u;

    return fasm_write_all(out->fd, &ehdr, sizeof(ehdr)) &&
           fasm_write_all(out->fd, &phdr, sizeof(phdr));
}

static int fasm_reopen_source(const char *path) {
    return cmd_open_resolved_path(path, 0);
}

int cmd_asm(int argc, char **argv) {
    struct fasm_state *state;
    struct fasm_output out;
    char output_path[CMD_PATH_MAX];
    int src_fd;
    int out_fd;
    int rc = 1;

    if (argc < 2 || argc > 3) {
        write_str("usage: asm <source.asm> [output]\n");
        write_str("subset: format elf64|binary, entry, org, labels, const/equ, bits 64, db/dw/dd/dq,\n");
        write_str("        mov/xor/push/pop/jmp/call/jz/jnz/int/nop/ret/syscall/int3/hlt\n");
        return argc == 1 ? 0 : 1;
    }

    state = (struct fasm_state *)calloc(1u, sizeof(*state));
    if (state == NULL) {
        write_err_str("asm: out of memory\n");
        return 1;
    }

    state->format = FASM_FMT_ELF64;
    state->origin = FASM_USER_ELF_BASE;
    state->entry_value = FASM_USER_ELF_BASE;

    src_fd = fasm_reopen_source(argv[1]);
    if (src_fd < 0) {
        write_err_str("asm: source open failed\n");
        goto out_free_state;
    }
    if (!fasm_run_pass(state, src_fd, NULL)) {
        close((uint32_t)src_fd);
        write_err_str("asm: line ");
        write_dec(state->line_number);
        write_err_str(": ");
        write_err_str(state->error_text[0] != '\0' ? state->error_text : "parse failed");
        write_err_str("\n");
        goto out_free_state;
    }
    close((uint32_t)src_fd);

    if (state->entry_defined) {
        if (!fasm_eval_expression(state, state->entry_expr, state->origin, &state->entry_value)) {
            write_err_str("asm: invalid entry expression\n");
            goto out_free_state;
        }
    } else {
        state->entry_value = state->origin;
    }

    if (state->format == FASM_FMT_ELF64) {
        if (state->origin < FASM_USER_ELF_BASE || state->origin + state->offset > FASM_USER_ELF_LIMIT) {
            write_err_str("asm: elf image outside NexOS user range\n");
            goto out_free_state;
        }
        if (state->entry_value < state->origin || state->entry_value >= state->origin + state->offset + 1u) {
            if (state->offset != 0u) {
                write_err_str("asm: entry outside output image\n");
                goto out_free_state;
            }
        }
    }

    if (argc >= 3) {
        copy_line_local(output_path, argv[2], sizeof(output_path));
    } else if (!fasm_derive_output_path(output_path, sizeof(output_path), argv[1], state->format)) {
        write_err_str("asm: could not derive output path\n");
        goto out_free_state;
    }

    out_fd = open(output_path, O_CREAT | O_TRUNC);
    if (out_fd < 0) {
        write_err_str("asm: output open failed\n");
        goto out_free_state;
    }
    out.fd = out_fd;

    if (state->format == FASM_FMT_ELF64 && !fasm_write_elf_header(state, &out)) {
        close((uint32_t)out_fd);
        write_err_str("asm: output write failed\n");
        goto out_free_state;
    }

    src_fd = fasm_reopen_source(argv[1]);
    if (src_fd < 0) {
        close((uint32_t)out_fd);
        write_err_str("asm: source reopen failed\n");
        goto out_free_state;
    }
    state->offset = 0;
    state->line_number = 0;
    state->error_text[0] = '\0';
    if (!fasm_run_pass(state, src_fd, &out)) {
        close((uint32_t)src_fd);
        close((uint32_t)out_fd);
        write_err_str("asm: line ");
        write_dec(state->line_number);
        write_err_str(": ");
        write_err_str(state->error_text[0] != '\0' ? state->error_text : "emit failed");
        write_err_str("\n");
        goto out_free_state;
    }
    close((uint32_t)src_fd);
    close((uint32_t)out_fd);

    write_str("asm: wrote ");
    write_str(output_path);
    write_str(" (");
    write_dec((uint32_t)state->offset);
    write_str(state->format == FASM_FMT_ELF64 ? " bytes of code, elf64)\n" : " bytes, binary)\n");
    rc = 0;

out_free_state:
    free(state);
    return rc;
}
