#include "user/apps/elf/ncc/ncc.h"

enum {
    NCC_R_X86_64_64 = 1,
    NCC_R_X86_64_PC32 = 2,
    NCC_R_X86_64_PLT32 = 4
};

static int ncc_buffer_reserve(struct ncc_buffer *buffer, uint32_t extra) {
    uint32_t needed = buffer->size + extra;
    uint32_t capacity;
    uint8_t *data;

    if (needed <= buffer->capacity) {
        return 1;
    }
    capacity = buffer->capacity != 0u ? buffer->capacity : 256u;
    while (capacity < needed) {
        capacity *= 2u;
    }
    data = realloc(buffer->data, capacity);
    if (data == NULL) {
        return 0;
    }
    buffer->data = data;
    buffer->capacity = capacity;
    return 1;
}

static int ncc_emit_bytes(struct ncc_codegen *codegen, const void *data, uint32_t size) {
    struct ncc_buffer *buffer = &codegen->object.sections[0].bytes;

    if (!ncc_buffer_reserve(buffer, size)) {
        ncc_copy_text(codegen->error, sizeof(codegen->error), "code buffer exhausted");
        return 0;
    }
    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    codegen->object.sections[0].size = buffer->size;
    return 1;
}

static int ncc_emit_u8(struct ncc_codegen *codegen, uint8_t value) {
    return ncc_emit_bytes(codegen, &value, 1u);
}

static int ncc_emit_u32(struct ncc_codegen *codegen, uint32_t value) {
    return ncc_emit_bytes(codegen, &value, 4u);
}

static int ncc_emit_u64(struct ncc_codegen *codegen, uint64_t value) {
    return ncc_emit_bytes(codegen, &value, 8u);
}

static uint32_t ncc_text_offset(const struct ncc_codegen *codegen) {
    return codegen->object.sections[0].bytes.size;
}

static uint32_t ncc_section_offset(const struct ncc_codegen *codegen, uint32_t section) {
    return codegen->object.sections[section].bytes.size;
}

static void ncc_patch_i32(struct ncc_codegen *codegen, uint32_t offset, int32_t value) {
    memcpy(codegen->object.sections[0].bytes.data + offset, &value, sizeof(value));
}

static int ncc_add_symbol(struct ncc_codegen *codegen,
                          const char *name,
                          int32_t section,
                          uint64_t value,
                          uint8_t global) {
    struct ncc_symbol *symbol;

    if (codegen->object.symbol_count >= NCC_SYMBOL_MAX) {
        ncc_copy_text(codegen->error, sizeof(codegen->error), "too many symbols");
        return -1;
    }
    symbol = &codegen->object.symbols[codegen->object.symbol_count];
    memset(symbol, 0, sizeof(*symbol));
    ncc_copy_text(symbol->name, sizeof(symbol->name), name);
    symbol->section = section;
    symbol->value = value;
    symbol->global = global;
    return (int)codegen->object.symbol_count++;
}

static int ncc_find_symbol(const struct ncc_codegen *codegen, const char *name) {
    for (uint32_t i = 0u; i < codegen->object.symbol_count; i++) {
        if (strcmp(codegen->object.symbols[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int ncc_get_or_add_undefined(struct ncc_codegen *codegen, const char *name) {
    int index = ncc_find_symbol(codegen, name);

    if (index >= 0) {
        return index;
    }
    return ncc_add_symbol(codegen, name, -1, 0u, 1u);
}

static int ncc_add_reloc(struct ncc_codegen *codegen,
                         int32_t section,
                         uint64_t offset,
                         uint32_t type,
                         uint32_t symbol,
                         int64_t addend) {
    struct ncc_reloc *reloc;

    if (codegen->object.reloc_count >= NCC_RELOC_MAX) {
        ncc_copy_text(codegen->error, sizeof(codegen->error), "too many relocations");
        return 0;
    }
    reloc = &codegen->object.relocs[codegen->object.reloc_count++];
    reloc->section = section;
    reloc->offset = offset;
    reloc->type = type;
    reloc->symbol = symbol;
    reloc->addend = addend;
    return 1;
}

static int ncc_emit_jump(struct ncc_codegen *codegen, uint8_t conditional) {
    uint32_t patch;

    if (conditional) {
        const uint8_t op[] = {0x0f, 0x84};
        if (!ncc_emit_bytes(codegen, op, sizeof(op))) return -1;
    } else if (!ncc_emit_u8(codegen, 0xe9)) {
        return -1;
    }
    patch = ncc_text_offset(codegen);
    if (!ncc_emit_u32(codegen, 0u)) {
        return -1;
    }
    return (int)patch;
}

static void ncc_patch_jump(struct ncc_codegen *codegen, int patch, uint32_t target) {
    ncc_patch_i32(codegen, (uint32_t)patch, (int32_t)(target - ((uint32_t)patch + 4u)));
}

static int ncc_codegen_expression(struct ncc_codegen *codegen, struct ncc_node *node);

static uint32_t ncc_codegen_value_size(struct ncc_type type) {
    if (type.pointer_depth != 0u) {
        return 8u;
    }
    if (type.struct_type != NULL) {
        return type.struct_type->size;
    }
    return type.base_size;
}

static uint32_t ncc_codegen_element_size(struct ncc_type type) {
    if (type.array_length != 0u) {
        if (type.pointer_depth != 0u) {
            return 8u;
        }
        if (type.struct_type != NULL) {
            return type.struct_type->size;
        }
        return type.base_size;
    }
    if (type.pointer_depth > 1u) {
        return 8u;
    }
    if (type.pointer_depth == 1u && type.struct_type != NULL) {
        return type.struct_type->size;
    }
    return type.base_size;
}

static uint32_t ncc_codegen_type_size(struct ncc_type type) {
    uint32_t size = ncc_codegen_value_size(type);

    return type.array_length != 0u ? size * type.array_length : size;
}

static uint32_t ncc_codegen_type_align(struct ncc_type type) {
    uint32_t size;

    if (type.array_length != 0u) {
        type.array_length = 0u;
    }
    if (type.pointer_depth != 0u) {
        return 8u;
    }
    if (type.struct_type != NULL) {
        return type.struct_type->align != 0u ? type.struct_type->align : 1u;
    }
    size = ncc_codegen_value_size(type);
    return size >= 8u ? 8u : (size != 0u ? size : 1u);
}

static int ncc_emit_section_zeros(struct ncc_codegen *codegen,
                                  uint32_t section,
                                  uint32_t size) {
    struct ncc_buffer *buffer = &codegen->object.sections[section].bytes;

    if (!ncc_buffer_reserve(buffer, size)) {
        ncc_copy_text(codegen->error, sizeof(codegen->error), "data buffer exhausted");
        return 0;
    }
    memset(buffer->data + buffer->size, 0, size);
    buffer->size += size;
    codegen->object.sections[section].size = buffer->size;
    return 1;
}

static int ncc_codegen_globals(struct ncc_codegen *codegen, struct ncc_global *global) {
    while (global != NULL) {
        uint32_t size = ncc_codegen_type_size(global->type);
        uint32_t align = ncc_codegen_type_align(global->type);
        uint32_t section_index = global->has_initializer ? 2u : 3u;
        struct ncc_section *section = &codegen->object.sections[section_index];
        uint32_t padding;
        uint32_t offset;
        int symbol;

        if (global->extern_only) {
            if (ncc_find_symbol(codegen, global->name) < 0 &&
                ncc_get_or_add_undefined(codegen, global->name) < 0) {
                return 0;
            }
            global = global->next;
            continue;
        }
        if (size == 0u || align == 0u) {
            ncc_copy_text(codegen->error, sizeof(codegen->error), "invalid global variable type");
            return 0;
        }
        if (ncc_find_symbol(codegen, global->name) >= 0) {
            ncc_copy_text(codegen->error, sizeof(codegen->error), "duplicate global symbol");
            return 0;
        }
        padding = (uint32_t)(ncc_align_up(section->size, align) - section->size);
        if (section_index == 2u) {
            if (!ncc_emit_section_zeros(codegen, section_index, padding)) {
                return 0;
            }
            offset = ncc_section_offset(codegen, section_index);
            if (!ncc_emit_section_zeros(codegen, section_index, size)) {
                return 0;
            }
            if (global->has_initializer) {
                if (global->initializer_data != NULL &&
                    global->initializer_size == size) {
                    memcpy(section->bytes.data + offset,
                           global->initializer_data,
                           size);
                } else {
                    uint64_t value = (uint64_t)global->initial_value;
                    uint32_t value_size = ncc_codegen_value_size(global->type);

                    memcpy(section->bytes.data + offset,
                           &value,
                           value_size < size ? value_size : size);
                }
            }
        } else {
            section->size += padding;
            offset = (uint32_t)section->size;
            section->size += size;
        }
        symbol = ncc_add_symbol(codegen, global->name, (int32_t)section_index, offset, 1u);
        if (symbol < 0) {
            return 0;
        }
        codegen->object.symbols[symbol].size = size;
        global = global->next;
    }
    return 1;
}

static int ncc_codegen_address(struct ncc_codegen *codegen, struct ncc_node *node) {
    if (node == NULL) {
        return 0;
    }
    switch (node->kind) {
        case NCC_NODE_VARIABLE: {
            if (node->is_global) {
                const uint8_t op[] = {0x48, 0xb8};
                uint32_t reloc_offset;
                int symbol;

                if (!ncc_emit_bytes(codegen, op, sizeof(op))) {
                    return 0;
                }
                reloc_offset = ncc_text_offset(codegen);
                if (!ncc_emit_u64(codegen, 0u)) {
                    return 0;
                }
                symbol = ncc_find_symbol(codegen, node->name);
                if (symbol < 0) {
                    symbol = ncc_get_or_add_undefined(codegen, node->name);
                }
                return symbol >= 0 &&
                       ncc_add_reloc(codegen,
                                     0,
                                     reloc_offset,
                                     NCC_R_X86_64_64,
                                     (uint32_t)symbol,
                                     0);
            }
            const uint8_t op[] = {0x48, 0x8d, 0x85};

            return ncc_emit_bytes(codegen, op, sizeof(op)) &&
                   ncc_emit_u32(codegen, (uint32_t)-node->stack_offset);
        }
        case NCC_NODE_DEREFERENCE:
            return ncc_codegen_expression(codegen, node->lhs);
        case NCC_NODE_INDEX: {
            uint32_t element_size = ncc_codegen_element_size(node->lhs->type);
            const uint8_t pop_r10[] = {0x41, 0x5a};
            const uint8_t add[] = {0x4c, 0x01, 0xd0};

            if (!ncc_codegen_expression(codegen, node->lhs) ||
                !ncc_emit_u8(codegen, 0x50) ||
                !ncc_codegen_expression(codegen, node->rhs)) {
                return 0;
            }
            if (element_size != 1u) {
                const uint8_t multiply[] = {0x48, 0x69, 0xc0};

                if (!ncc_emit_bytes(codegen, multiply, sizeof(multiply)) ||
                    !ncc_emit_u32(codegen, element_size)) {
                    return 0;
                }
            }
            return ncc_emit_bytes(codegen, pop_r10, sizeof(pop_r10)) &&
                   ncc_emit_bytes(codegen, add, sizeof(add));
        }
        case NCC_NODE_MEMBER: {
            if (!ncc_codegen_address(codegen, node->lhs)) {
                return 0;
            }
            if (node->field_offset != 0u) {
                const uint8_t add[] = {0x48, 0x05};

                return ncc_emit_bytes(codegen, add, sizeof(add)) &&
                       ncc_emit_u32(codegen, node->field_offset);
            }
            return 1;
        }
        default:
            ncc_copy_text(codegen->error, sizeof(codegen->error), "expression has no address");
            return 0;
    }
}

static int ncc_codegen_load_address(struct ncc_codegen *codegen, struct ncc_type type) {
    if (ncc_codegen_value_size(type) == 1u) {
        const uint8_t op[] = {0x0f, 0xb6, 0x00};

        return ncc_emit_bytes(codegen, op, sizeof(op));
    }
    {
        const uint8_t op[] = {0x48, 0x8b, 0x00};

        return ncc_emit_bytes(codegen, op, sizeof(op));
    }
}

static int ncc_codegen_store_r10(struct ncc_codegen *codegen, struct ncc_type type) {
    if (ncc_codegen_value_size(type) == 1u) {
        const uint8_t op[] = {0x41, 0x88, 0x02};

        return ncc_emit_bytes(codegen, op, sizeof(op));
    }
    {
        const uint8_t op[] = {0x49, 0x89, 0x02};

        return ncc_emit_bytes(codegen, op, sizeof(op));
    }
}

static int ncc_codegen_adjust_rax(struct ncc_codegen *codegen,
                                  int subtract,
                                  uint32_t delta) {
    if (subtract) {
        const uint8_t op[] = {0x48, 0x2d};

        return ncc_emit_bytes(codegen, op, sizeof(op)) &&
               ncc_emit_u32(codegen, delta);
    }
    {
        const uint8_t op[] = {0x48, 0x05};

        return ncc_emit_bytes(codegen, op, sizeof(op)) &&
               ncc_emit_u32(codegen, delta);
    }
}

static int ncc_codegen_increment(struct ncc_codegen *codegen,
                                 struct ncc_node *node,
                                 int decrement,
                                 int post) {
    uint32_t delta = (node->lhs->type.pointer_depth != 0u ||
                      node->lhs->type.array_length != 0u)
                         ? ncc_codegen_element_size(node->lhs->type)
                         : 1u;

    if (!ncc_codegen_address(codegen, node->lhs) ||
        !ncc_emit_u8(codegen, 0x50) ||
        !ncc_codegen_load_address(codegen, node->lhs->type)) {
        return 0;
    }
    if (post) {
        const uint8_t pop_r11_pop_r10_push_r11[] = {
            0x41, 0x5b,
            0x41, 0x5a,
            0x41, 0x53
        };

        return ncc_emit_u8(codegen, 0x50) &&
               ncc_codegen_adjust_rax(codegen, decrement, delta) &&
               ncc_emit_bytes(codegen,
                              pop_r11_pop_r10_push_r11,
                              sizeof(pop_r11_pop_r10_push_r11)) &&
               ncc_codegen_store_r10(codegen, node->lhs->type) &&
               ncc_emit_u8(codegen, 0x58);
    }
    return ncc_codegen_adjust_rax(codegen, decrement, delta) &&
           ncc_emit_bytes(codegen, (const uint8_t[]){0x41, 0x5a}, 2u) &&
           ncc_codegen_store_r10(codegen, node->lhs->type);
}

static int ncc_codegen_compound_assign(struct ncc_codegen *codegen,
                                       struct ncc_node *node,
                                       int subtract) {
    const uint8_t pop_r10[] = {0x41, 0x5a};

    if (!ncc_codegen_address(codegen, node->lhs) ||
        !ncc_emit_u8(codegen, 0x50) ||
        !ncc_codegen_load_address(codegen, node->lhs->type) ||
        !ncc_emit_u8(codegen, 0x50) ||
        !ncc_codegen_expression(codegen, node->rhs) ||
        !ncc_emit_bytes(codegen, pop_r10, sizeof(pop_r10))) {
        return 0;
    }
    if ((node->lhs->type.pointer_depth != 0u ||
         node->lhs->type.array_length != 0u) &&
        node->rhs->type.pointer_depth == 0u &&
        node->rhs->type.array_length == 0u) {
        uint32_t element_size = ncc_codegen_element_size(node->lhs->type);

        if (element_size != 1u) {
            const uint8_t multiply[] = {0x48, 0x69, 0xc0};

            if (!ncc_emit_bytes(codegen, multiply, sizeof(multiply)) ||
                !ncc_emit_u32(codegen, element_size)) {
                return 0;
            }
        }
    }
    if (subtract) {
        const uint8_t op[] = {0x49, 0x29, 0xc2, 0x4c, 0x89, 0xd0};

        if (!ncc_emit_bytes(codegen, op, sizeof(op))) {
            return 0;
        }
    } else {
        const uint8_t op[] = {0x4c, 0x01, 0xd0};

        if (!ncc_emit_bytes(codegen, op, sizeof(op))) {
            return 0;
        }
    }
    return ncc_emit_bytes(codegen, pop_r10, sizeof(pop_r10)) &&
           ncc_codegen_store_r10(codegen, node->lhs->type);
}

static int ncc_codegen_bit_operation(struct ncc_codegen *codegen,
                                     enum ncc_node_kind kind) {
    switch (kind) {
        case NCC_NODE_BIT_AND:
        case NCC_NODE_AND_ASSIGN:
            return ncc_emit_bytes(codegen,
                                  (const uint8_t[]){0x4c, 0x21, 0xd0},
                                  3u);
        case NCC_NODE_BIT_OR:
        case NCC_NODE_OR_ASSIGN:
            return ncc_emit_bytes(codegen,
                                  (const uint8_t[]){0x4c, 0x09, 0xd0},
                                  3u);
        case NCC_NODE_BIT_XOR:
        case NCC_NODE_XOR_ASSIGN:
            return ncc_emit_bytes(codegen,
                                  (const uint8_t[]){0x4c, 0x31, 0xd0},
                                  3u);
        case NCC_NODE_SHL:
        case NCC_NODE_SHL_ASSIGN:
            return ncc_emit_bytes(codegen,
                                  (const uint8_t[]){
                                      0x48, 0x89, 0xc1,
                                      0x4c, 0x89, 0xd0,
                                      0x48, 0xd3, 0xe0
                                  },
                                  9u);
        case NCC_NODE_SHR:
        case NCC_NODE_SHR_ASSIGN:
            return ncc_emit_bytes(codegen,
                                  (const uint8_t[]){
                                      0x48, 0x89, 0xc1,
                                      0x4c, 0x89, 0xd0,
                                      0x48, 0xd3, 0xf8
                                  },
                                  9u);
        default:
            return 0;
    }
}

static int ncc_codegen_bit_compound_assign(struct ncc_codegen *codegen,
                                           struct ncc_node *node) {
    if (!ncc_codegen_address(codegen, node->lhs) ||
        !ncc_emit_u8(codegen, 0x50) ||
        !ncc_codegen_load_address(codegen, node->lhs->type) ||
        !ncc_emit_u8(codegen, 0x50) ||
        !ncc_codegen_expression(codegen, node->rhs) ||
        !ncc_emit_bytes(codegen, (const uint8_t[]){0x41, 0x5a}, 2u) ||
        !ncc_codegen_bit_operation(codegen, node->kind) ||
        !ncc_emit_bytes(codegen, (const uint8_t[]){0x41, 0x5a}, 2u)) {
        return 0;
    }
    return ncc_codegen_store_r10(codegen, node->lhs->type);
}

static int ncc_codegen_call(struct ncc_codegen *codegen, struct ncc_node *node) {
    static const uint8_t pop_register[][2] = {
        {0x5f, 0x00},
        {0x5e, 0x00},
        {0x5a, 0x00},
        {0x59, 0x00},
        {0x41, 0x58},
        {0x41, 0x59}
    };
    int symbol;
    uint32_t reloc_offset;

    for (uint32_t i = 0u; i < node->arg_count; i++) {
        if (!ncc_codegen_expression(codegen, node->args[i]) ||
            !ncc_emit_u8(codegen, 0x50)) {
            return 0;
        }
    }
    for (uint32_t i = node->arg_count; i > 0u; i--) {
        uint32_t index = i - 1u;
        uint32_t bytes = index < 4u ? 1u : 2u;

        if (!ncc_emit_bytes(codegen, pop_register[index], bytes)) {
            return 0;
        }
    }
    {
        const uint8_t clear_varargs[] = {0x31, 0xc0};
        if (!ncc_emit_bytes(codegen, clear_varargs, sizeof(clear_varargs)) ||
            !ncc_emit_u8(codegen, 0xe8)) {
            return 0;
        }
    }
    reloc_offset = ncc_text_offset(codegen);
    if (!ncc_emit_u32(codegen, 0u)) {
        return 0;
    }
    symbol = ncc_get_or_add_undefined(codegen, node->name);
    return symbol >= 0 &&
           ncc_add_reloc(codegen,
                         0,
                         reloc_offset,
                         NCC_R_X86_64_PLT32,
                         (uint32_t)symbol,
                         -4);
}

static int ncc_codegen_binary(struct ncc_codegen *codegen, struct ncc_node *node) {
    const uint8_t pop_r10[] = {0x41, 0x5a};

    if (!ncc_codegen_expression(codegen, node->lhs) ||
        !ncc_emit_u8(codegen, 0x50) ||
        !ncc_codegen_expression(codegen, node->rhs) ||
        !ncc_emit_bytes(codegen, pop_r10, sizeof(pop_r10))) {
        return 0;
    }
    switch (node->kind) {
        case NCC_NODE_ADD: {
            if (node->lhs->type.pointer_depth != 0u ||
                node->lhs->type.array_length != 0u) {
                uint32_t element_size = ncc_codegen_element_size(node->lhs->type);

                if (element_size != 1u) {
                    const uint8_t multiply[] = {0x48, 0x69, 0xc0};
                    if (!ncc_emit_bytes(codegen, multiply, sizeof(multiply)) ||
                        !ncc_emit_u32(codegen, element_size)) return 0;
                }
            } else if (node->rhs->type.pointer_depth != 0u ||
                       node->rhs->type.array_length != 0u) {
                uint32_t element_size = ncc_codegen_element_size(node->rhs->type);

                if (element_size != 1u) {
                    const uint8_t multiply[] = {0x4d, 0x69, 0xd2};
                    if (!ncc_emit_bytes(codegen, multiply, sizeof(multiply)) ||
                        !ncc_emit_u32(codegen, element_size)) return 0;
                }
            }
            const uint8_t op[] = {0x4c, 0x01, 0xd0};
            return ncc_emit_bytes(codegen, op, sizeof(op));
        }
        case NCC_NODE_SUB: {
            int lhs_pointer = node->lhs->type.pointer_depth != 0u ||
                              node->lhs->type.array_length != 0u;
            int rhs_pointer = node->rhs->type.pointer_depth != 0u ||
                              node->rhs->type.array_length != 0u;

            if (lhs_pointer && !rhs_pointer) {
                uint32_t element_size = ncc_codegen_element_size(node->lhs->type);

                if (element_size != 1u) {
                    const uint8_t multiply[] = {0x48, 0x69, 0xc0};
                    if (!ncc_emit_bytes(codegen, multiply, sizeof(multiply)) ||
                        !ncc_emit_u32(codegen, element_size)) return 0;
                }
            }
            {
                const uint8_t op[] = {0x49, 0x29, 0xc2, 0x4c, 0x89, 0xd0};

                if (!ncc_emit_bytes(codegen, op, sizeof(op))) return 0;
            }
            if (lhs_pointer && rhs_pointer) {
                uint32_t element_size = ncc_codegen_element_size(node->lhs->type);

                if (element_size != 1u) {
                    const uint8_t divide[] = {
                        0x48, 0x99,
                        0x49, 0xbb
                    };
                    const uint8_t idiv_r11[] = {0x49, 0xf7, 0xfb};

                    return ncc_emit_bytes(codegen, divide, sizeof(divide)) &&
                           ncc_emit_u64(codegen, element_size) &&
                           ncc_emit_bytes(codegen, idiv_r11, sizeof(idiv_r11));
                }
            }
            return 1;
        }
        case NCC_NODE_MUL: {
            const uint8_t op[] = {0x49, 0x89, 0xc3, 0x4c, 0x89, 0xd0, 0x49, 0x0f, 0xaf, 0xc3};
            return ncc_emit_bytes(codegen, op, sizeof(op));
        }
        case NCC_NODE_DIV: {
            const uint8_t op[] = {0x49, 0x89, 0xc3, 0x4c, 0x89, 0xd0, 0x48, 0x99, 0x49, 0xf7, 0xfb};
            return ncc_emit_bytes(codegen, op, sizeof(op));
        }
        case NCC_NODE_MOD: {
            const uint8_t op[] = {
                0x49, 0x89, 0xc3,
                0x4c, 0x89, 0xd0,
                0x48, 0x99,
                0x49, 0xf7, 0xfb,
                0x48, 0x89, 0xd0
            };
            return ncc_emit_bytes(codegen, op, sizeof(op));
        }
        case NCC_NODE_BIT_AND:
        case NCC_NODE_BIT_OR:
        case NCC_NODE_BIT_XOR:
        case NCC_NODE_SHL:
        case NCC_NODE_SHR:
            return ncc_codegen_bit_operation(codegen, node->kind);
        case NCC_NODE_EQ:
        case NCC_NODE_NE:
        case NCC_NODE_LT:
        case NCC_NODE_LE: {
            uint8_t condition = node->kind == NCC_NODE_EQ ? 0x94 :
                                node->kind == NCC_NODE_NE ? 0x95 :
                                node->kind == NCC_NODE_LT ? 0x9c : 0x9e;
            const uint8_t compare[] = {0x49, 0x39, 0xc2, 0x0f};
            const uint8_t extend[] = {0xc0, 0x48, 0x0f, 0xb6, 0xc0};

            return ncc_emit_bytes(codegen, compare, sizeof(compare)) &&
                   ncc_emit_u8(codegen, condition) &&
                   ncc_emit_bytes(codegen, extend, sizeof(extend));
        }
        default:
            return 0;
    }
}

static int ncc_codegen_logical(struct ncc_codegen *codegen, struct ncc_node *node) {
    const uint8_t test[] = {0x48, 0x85, 0xc0};
    const uint8_t mov_one[] = {0x48, 0xc7, 0xc0, 0x01, 0x00, 0x00, 0x00};
    const uint8_t xor_zero[] = {0x31, 0xc0};

    if (node->kind == NCC_NODE_LOGICAL_AND) {
        int lhs_false_patch;
        int rhs_false_patch;
        int end_patch;

        if (!ncc_codegen_expression(codegen, node->lhs) ||
            !ncc_emit_bytes(codegen, test, sizeof(test))) {
            return 0;
        }
        lhs_false_patch = ncc_emit_jump(codegen, 1u);
        if (lhs_false_patch < 0 ||
            !ncc_codegen_expression(codegen, node->rhs) ||
            !ncc_emit_bytes(codegen, test, sizeof(test))) {
            return 0;
        }
        rhs_false_patch = ncc_emit_jump(codegen, 1u);
        if (rhs_false_patch < 0 ||
            !ncc_emit_bytes(codegen, mov_one, sizeof(mov_one))) {
            return 0;
        }
        end_patch = ncc_emit_jump(codegen, 0u);
        if (end_patch < 0) {
            return 0;
        }
        ncc_patch_jump(codegen, lhs_false_patch, ncc_text_offset(codegen));
        ncc_patch_jump(codegen, rhs_false_patch, ncc_text_offset(codegen));
        if (!ncc_emit_bytes(codegen, xor_zero, sizeof(xor_zero))) {
            return 0;
        }
        ncc_patch_jump(codegen, end_patch, ncc_text_offset(codegen));
        return 1;
    }
    {
        int lhs_false_patch;
        int rhs_false_patch;
        int lhs_true_end_patch;
        int rhs_true_end_patch;

        if (!ncc_codegen_expression(codegen, node->lhs) ||
            !ncc_emit_bytes(codegen, test, sizeof(test))) {
            return 0;
        }
        lhs_false_patch = ncc_emit_jump(codegen, 1u);
        if (lhs_false_patch < 0 ||
            !ncc_emit_bytes(codegen, mov_one, sizeof(mov_one))) {
            return 0;
        }
        lhs_true_end_patch = ncc_emit_jump(codegen, 0u);
        if (lhs_true_end_patch < 0) {
            return 0;
        }
        ncc_patch_jump(codegen, lhs_false_patch, ncc_text_offset(codegen));
        if (!ncc_codegen_expression(codegen, node->rhs) ||
            !ncc_emit_bytes(codegen, test, sizeof(test))) {
            return 0;
        }
        rhs_false_patch = ncc_emit_jump(codegen, 1u);
        if (rhs_false_patch < 0 ||
            !ncc_emit_bytes(codegen, mov_one, sizeof(mov_one))) {
            return 0;
        }
        rhs_true_end_patch = ncc_emit_jump(codegen, 0u);
        if (rhs_true_end_patch < 0) {
            return 0;
        }
        ncc_patch_jump(codegen, rhs_false_patch, ncc_text_offset(codegen));
        if (!ncc_emit_bytes(codegen, xor_zero, sizeof(xor_zero))) {
            return 0;
        }
        ncc_patch_jump(codegen, lhs_true_end_patch, ncc_text_offset(codegen));
        ncc_patch_jump(codegen, rhs_true_end_patch, ncc_text_offset(codegen));
        return 1;
    }
}

static int ncc_codegen_conditional(struct ncc_codegen *codegen,
                                   struct ncc_node *node) {
    const uint8_t test[] = {0x48, 0x85, 0xc0};
    int false_patch;
    int end_patch;

    if (!ncc_codegen_expression(codegen, node->condition) ||
        !ncc_emit_bytes(codegen, test, sizeof(test))) {
        return 0;
    }
    false_patch = ncc_emit_jump(codegen, 1u);
    if (false_patch < 0 ||
        !ncc_codegen_expression(codegen, node->then_node)) {
        return 0;
    }
    end_patch = ncc_emit_jump(codegen, 0u);
    if (end_patch < 0) {
        return 0;
    }
    ncc_patch_jump(codegen, false_patch, ncc_text_offset(codegen));
    if (!ncc_codegen_expression(codegen, node->else_node)) {
        return 0;
    }
    ncc_patch_jump(codegen, end_patch, ncc_text_offset(codegen));
    return 1;
}

static int ncc_codegen_expression(struct ncc_codegen *codegen, struct ncc_node *node) {
    if (node == NULL) {
        return 0;
    }
    switch (node->kind) {
        case NCC_NODE_NUMBER: {
            const uint8_t op[] = {0x48, 0xb8};
            return ncc_emit_bytes(codegen, op, sizeof(op)) &&
                   ncc_emit_u64(codegen, (uint64_t)node->value);
        }
        case NCC_NODE_STRING: {
            struct ncc_section *rodata = &codegen->object.sections[1];
            char symbol_name[NCC_NAME_MAX + 1];
            int symbol;
            uint32_t offset = rodata->bytes.size;
            const uint8_t op[] = {0x48, 0xb8};
            uint32_t reloc_offset;

            if (!ncc_buffer_reserve(&rodata->bytes, node->string_length)) {
                return 0;
            }
            memcpy(rodata->bytes.data + rodata->bytes.size,
                   node->string_value,
                   node->string_length);
            rodata->bytes.size += node->string_length;
            rodata->size = rodata->bytes.size;
            snprintf(symbol_name, sizeof(symbol_name), ".Lstr%u", codegen->string_index++);
            symbol = ncc_add_symbol(codegen, symbol_name, 1, offset, 0u);
            if (symbol < 0 || !ncc_emit_bytes(codegen, op, sizeof(op))) {
                return 0;
            }
            reloc_offset = ncc_text_offset(codegen);
            return ncc_emit_u64(codegen, 0u) &&
                   ncc_add_reloc(codegen,
                                 0,
                                 reloc_offset,
                                 NCC_R_X86_64_64,
                                 (uint32_t)symbol,
                                 0);
        }
        case NCC_NODE_VARIABLE:
            if (node->type.array_length != 0u) {
                return ncc_codegen_address(codegen, node);
            }
            return ncc_codegen_address(codegen, node) &&
                   ncc_codegen_load_address(codegen, node->type);
        case NCC_NODE_ASSIGN:
            return ncc_codegen_address(codegen, node->lhs) &&
                   ncc_emit_u8(codegen, 0x50) &&
                   ncc_codegen_expression(codegen, node->rhs) &&
                   ncc_emit_bytes(codegen, (const uint8_t[]){0x41, 0x5a}, 2u) &&
                   ncc_codegen_store_r10(codegen, node->lhs->type);
        case NCC_NODE_ADD_ASSIGN:
            return ncc_codegen_compound_assign(codegen, node, 0);
        case NCC_NODE_SUB_ASSIGN:
            return ncc_codegen_compound_assign(codegen, node, 1);
        case NCC_NODE_AND_ASSIGN:
        case NCC_NODE_OR_ASSIGN:
        case NCC_NODE_XOR_ASSIGN:
        case NCC_NODE_SHL_ASSIGN:
        case NCC_NODE_SHR_ASSIGN:
            return ncc_codegen_bit_compound_assign(codegen, node);
        case NCC_NODE_PRE_INC:
            return ncc_codegen_increment(codegen, node, 0, 0);
        case NCC_NODE_PRE_DEC:
            return ncc_codegen_increment(codegen, node, 1, 0);
        case NCC_NODE_POST_INC:
            return ncc_codegen_increment(codegen, node, 0, 1);
        case NCC_NODE_POST_DEC:
            return ncc_codegen_increment(codegen, node, 1, 1);
        case NCC_NODE_ADDRESS:
            return ncc_codegen_address(codegen, node->lhs);
        case NCC_NODE_DEREFERENCE:
        case NCC_NODE_INDEX:
            return ncc_codegen_address(codegen, node) &&
                   ncc_codegen_load_address(codegen, node->type);
        case NCC_NODE_MEMBER:
            if (node->type.array_length != 0u) {
                return ncc_codegen_address(codegen, node);
            }
            return ncc_codegen_address(codegen, node) &&
                   ncc_codegen_load_address(codegen, node->type);
        case NCC_NODE_CALL:
            return ncc_codegen_call(codegen, node);
        case NCC_NODE_NEG: {
            const uint8_t op[] = {0x48, 0xf7, 0xd8};
            return ncc_codegen_expression(codegen, node->lhs) &&
                   ncc_emit_bytes(codegen, op, sizeof(op));
        }
        case NCC_NODE_NOT: {
            const uint8_t op[] = {0x48, 0x83, 0xf8, 0x00, 0x0f, 0x94, 0xc0, 0x48, 0x0f, 0xb6, 0xc0};
            return ncc_codegen_expression(codegen, node->lhs) &&
                   ncc_emit_bytes(codegen, op, sizeof(op));
        }
        case NCC_NODE_BIT_NOT: {
            const uint8_t op[] = {0x48, 0xf7, 0xd0};
            return ncc_codegen_expression(codegen, node->lhs) &&
                   ncc_emit_bytes(codegen, op, sizeof(op));
        }
        case NCC_NODE_ADD:
        case NCC_NODE_SUB:
        case NCC_NODE_MUL:
        case NCC_NODE_DIV:
        case NCC_NODE_MOD:
        case NCC_NODE_BIT_AND:
        case NCC_NODE_BIT_OR:
        case NCC_NODE_BIT_XOR:
        case NCC_NODE_SHL:
        case NCC_NODE_SHR:
        case NCC_NODE_EQ:
        case NCC_NODE_NE:
        case NCC_NODE_LT:
        case NCC_NODE_LE:
            return ncc_codegen_binary(codegen, node);
        case NCC_NODE_LOGICAL_AND:
        case NCC_NODE_LOGICAL_OR:
            return ncc_codegen_logical(codegen, node);
        case NCC_NODE_CONDITIONAL:
            return ncc_codegen_conditional(codegen, node);
        default:
            return 0;
    }
}

struct ncc_loop_codegen {
    struct ncc_loop_codegen *parent;
    uint8_t accept_continue;
    uint32_t break_patches[128];
    uint32_t break_count;
    uint32_t continue_patches[128];
    uint32_t continue_count;
};

static int ncc_loop_add_jump(struct ncc_codegen *codegen,
                             struct ncc_loop_codegen *loop,
                             int is_continue) {
    uint32_t *patches;
    uint32_t *count;
    int patch;

    while (is_continue && loop != NULL && !loop->accept_continue) {
        loop = loop->parent;
    }
    if (loop == NULL) {
        ncc_copy_text(codegen->error,
                      sizeof(codegen->error),
                      is_continue ? "continue outside loop" : "break outside loop");
        return 0;
    }
    patches = is_continue ? loop->continue_patches : loop->break_patches;
    count = is_continue ? &loop->continue_count : &loop->break_count;
    if (*count >= 128u) {
        ncc_copy_text(codegen->error, sizeof(codegen->error), "too many loop jumps");
        return 0;
    }
    patch = ncc_emit_jump(codegen, 0u);
    if (patch < 0) {
        return 0;
    }
    patches[(*count)++] = (uint32_t)patch;
    return 1;
}

static void ncc_loop_patch_jumps(struct ncc_codegen *codegen,
                                 const uint32_t *patches,
                                 uint32_t count,
                                 uint32_t target) {
    for (uint32_t i = 0u; i < count; i++) {
        ncc_patch_jump(codegen, (int)patches[i], target);
    }
}

static int ncc_codegen_statement(struct ncc_codegen *codegen,
                                 struct ncc_node *node,
                                 uint32_t *return_patches,
                                 uint32_t *return_count,
                                 struct ncc_loop_codegen *loop) {
    while (node != NULL) {
        switch (node->kind) {
            case NCC_NODE_BLOCK:
                if (!ncc_codegen_statement(codegen,
                                           node->lhs,
                                           return_patches,
                                           return_count,
                                           loop)) {
                    return 0;
                }
                break;
            case NCC_NODE_RETURN: {
                int patch;

                if (!ncc_codegen_expression(codegen, node->lhs) ||
                    *return_count >= 128u) {
                    return 0;
                }
                patch = ncc_emit_jump(codegen, 0u);
                if (patch < 0) {
                    return 0;
                }
                return_patches[(*return_count)++] = (uint32_t)patch;
                break;
            }
            case NCC_NODE_EXPR_STMT:
                if (!ncc_codegen_expression(codegen, node->lhs)) {
                    return 0;
                }
                break;
            case NCC_NODE_IF: {
                int false_patch;
                int end_patch = -1;

                if (!ncc_codegen_expression(codegen, node->condition)) return 0;
                {
                    const uint8_t test[] = {0x48, 0x85, 0xc0};
                    if (!ncc_emit_bytes(codegen, test, sizeof(test))) return 0;
                }
                false_patch = ncc_emit_jump(codegen, 1u);
                if (false_patch < 0 ||
                    !ncc_codegen_statement(codegen,
                                           node->then_node,
                                           return_patches,
                                           return_count,
                                           loop)) {
                    return 0;
                }
                if (node->else_node != NULL) {
                    end_patch = ncc_emit_jump(codegen, 0u);
                }
                ncc_patch_jump(codegen, false_patch, ncc_text_offset(codegen));
                if (node->else_node != NULL &&
                    !ncc_codegen_statement(codegen,
                                           node->else_node,
                                           return_patches,
                                           return_count,
                                           loop)) {
                    return 0;
                }
                if (end_patch >= 0) {
                    ncc_patch_jump(codegen, end_patch, ncc_text_offset(codegen));
                }
                break;
            }
            case NCC_NODE_WHILE: {
                struct ncc_loop_codegen child_loop;
                uint32_t loop_start = ncc_text_offset(codegen);
                uint32_t loop_end;
                int exit_patch;
                int back_patch;

                memset(&child_loop, 0, sizeof(child_loop));
                child_loop.parent = loop;
                child_loop.accept_continue = 1u;
                if (!ncc_codegen_expression(codegen, node->condition)) return 0;
                {
                    const uint8_t test[] = {0x48, 0x85, 0xc0};
                    if (!ncc_emit_bytes(codegen, test, sizeof(test))) return 0;
                }
                exit_patch = ncc_emit_jump(codegen, 1u);
                if (exit_patch < 0 ||
                    !ncc_codegen_statement(codegen,
                                           node->then_node,
                                           return_patches,
                                           return_count,
                                           &child_loop)) {
                    return 0;
                }
                ncc_loop_patch_jumps(codegen,
                                     child_loop.continue_patches,
                                     child_loop.continue_count,
                                     loop_start);
                back_patch = ncc_emit_jump(codegen, 0u);
                if (back_patch < 0) return 0;
                ncc_patch_jump(codegen, back_patch, loop_start);
                loop_end = ncc_text_offset(codegen);
                ncc_patch_jump(codegen, exit_patch, loop_end);
                ncc_loop_patch_jumps(codegen,
                                     child_loop.break_patches,
                                     child_loop.break_count,
                                     loop_end);
                break;
            }
            case NCC_NODE_FOR: {
                struct ncc_loop_codegen child_loop;
                uint32_t condition_start;
                uint32_t update_start;
                uint32_t loop_end;
                int exit_patch;
                int back_patch;

                memset(&child_loop, 0, sizeof(child_loop));
                child_loop.parent = loop;
                child_loop.accept_continue = 1u;
                if (!ncc_codegen_statement(codegen,
                                           node->lhs,
                                           return_patches,
                                           return_count,
                                           loop)) {
                    return 0;
                }
                condition_start = ncc_text_offset(codegen);
                if (!ncc_codegen_expression(codegen, node->condition)) return 0;
                {
                    const uint8_t test[] = {0x48, 0x85, 0xc0};
                    if (!ncc_emit_bytes(codegen, test, sizeof(test))) return 0;
                }
                exit_patch = ncc_emit_jump(codegen, 1u);
                if (exit_patch < 0 ||
                    !ncc_codegen_statement(codegen,
                                           node->then_node,
                                           return_patches,
                                           return_count,
                                           &child_loop)) {
                    return 0;
                }
                update_start = ncc_text_offset(codegen);
                ncc_loop_patch_jumps(codegen,
                                     child_loop.continue_patches,
                                     child_loop.continue_count,
                                     update_start);
                if (node->rhs != NULL && !ncc_codegen_expression(codegen, node->rhs)) {
                    return 0;
                }
                back_patch = ncc_emit_jump(codegen, 0u);
                if (back_patch < 0) return 0;
                ncc_patch_jump(codegen, back_patch, condition_start);
                loop_end = ncc_text_offset(codegen);
                ncc_patch_jump(codegen, exit_patch, loop_end);
                ncc_loop_patch_jumps(codegen,
                                     child_loop.break_patches,
                                     child_loop.break_count,
                                     loop_end);
                break;
            }
            case NCC_NODE_SWITCH: {
                struct ncc_loop_codegen switch_scope;
                struct ncc_node *label;
                struct ncc_node *case_nodes[128];
                uint32_t case_patches[128];
                uint32_t case_count = 0u;
                int default_dispatch_patch;
                int default_found = 0;
                uint32_t switch_end;

                memset(&switch_scope, 0, sizeof(switch_scope));
                switch_scope.parent = loop;
                if (!ncc_codegen_expression(codegen, node->condition) ||
                    !ncc_emit_bytes(codegen,
                                    (const uint8_t[]){0x49, 0x89, 0xc3},
                                    3u)) {
                    return 0;
                }
                for (label = node->then_node; label != NULL; label = label->next) {
                    int patch;

                    if (label->kind != NCC_NODE_CASE) {
                        continue;
                    }
                    if (case_count >= 128u ||
                        !ncc_emit_bytes(codegen,
                                        (const uint8_t[]){0x49, 0xba},
                                        2u) ||
                        !ncc_emit_u64(codegen, (uint64_t)label->value) ||
                        !ncc_emit_bytes(codegen,
                                        (const uint8_t[]){0x4d, 0x39, 0xd3},
                                        3u)) {
                        return 0;
                    }
                    patch = ncc_emit_jump(codegen, 1u);
                    if (patch < 0) {
                        return 0;
                    }
                    case_nodes[case_count] = label;
                    case_patches[case_count++] = (uint32_t)patch;
                }
                default_dispatch_patch = ncc_emit_jump(codegen, 0u);
                if (default_dispatch_patch < 0) {
                    return 0;
                }
                for (label = node->then_node; label != NULL; label = label->next) {
                    uint32_t body_start = ncc_text_offset(codegen);

                    if (label->kind == NCC_NODE_CASE) {
                        for (uint32_t i = 0u; i < case_count; i++) {
                            if (case_nodes[i] == label) {
                                ncc_patch_jump(codegen,
                                               (int)case_patches[i],
                                               body_start);
                                break;
                            }
                        }
                    } else if (label->kind == NCC_NODE_DEFAULT) {
                        ncc_patch_jump(codegen,
                                       default_dispatch_patch,
                                       body_start);
                        default_found = 1;
                    } else {
                        return 0;
                    }
                    if (!ncc_codegen_statement(codegen,
                                               label->lhs,
                                               return_patches,
                                               return_count,
                                               &switch_scope)) {
                        return 0;
                    }
                }
                switch_end = ncc_text_offset(codegen);
                if (!default_found) {
                    ncc_patch_jump(codegen,
                                   default_dispatch_patch,
                                   switch_end);
                }
                ncc_loop_patch_jumps(codegen,
                                     switch_scope.break_patches,
                                     switch_scope.break_count,
                                     switch_end);
                break;
            }
            case NCC_NODE_BREAK:
                if (!ncc_loop_add_jump(codegen, loop, 0)) {
                    return 0;
                }
                break;
            case NCC_NODE_CONTINUE:
                if (!ncc_loop_add_jump(codegen, loop, 1)) {
                    return 0;
                }
                break;
            default:
                return 0;
        }
        node = node->next;
    }
    return 1;
}

static struct ncc_local *ncc_function_local(struct ncc_function *function, const char *name) {
    struct ncc_local *local = function->locals;

    while (local != NULL) {
        if (strcmp(local->name, name) == 0) return local;
        local = local->next;
    }
    return NULL;
}

static int ncc_codegen_function(struct ncc_codegen *codegen, struct ncc_function *function) {
    static const uint8_t stores[][3] = {
        {0x48, 0x89, 0xbd},
        {0x48, 0x89, 0xb5},
        {0x48, 0x89, 0x95},
        {0x48, 0x89, 0x8d},
        {0x4c, 0x89, 0x85},
        {0x4c, 0x89, 0x8d}
    };
    static const uint8_t char_stores[][3] = {
        {0x40, 0x88, 0xbd},
        {0x40, 0x88, 0xb5},
        {0x40, 0x88, 0x95},
        {0x40, 0x88, 0x8d},
        {0x44, 0x88, 0x85},
        {0x44, 0x88, 0x8d}
    };
    uint32_t return_patches[128];
    uint32_t return_count = 0u;
    uint32_t frame_size = (uint32_t)ncc_align_up(function->stack_size, 16u);
    int symbol;
    const uint8_t prologue[] = {0x55, 0x48, 0x89, 0xe5, 0x48, 0x81, 0xec};
    const uint8_t zero_return[] = {0x31, 0xc0};
    const uint8_t epilogue[] = {0xc9, 0xc3};

    symbol = ncc_find_symbol(codegen, function->name);
    if (symbol >= 0 && codegen->object.symbols[symbol].section >= 0) {
        ncc_copy_text(codegen->error, sizeof(codegen->error), "duplicate function");
        return 0;
    }
    if (symbol < 0) {
        symbol = ncc_add_symbol(codegen,
                                function->name,
                                0,
                                ncc_text_offset(codegen),
                                1u);
    } else {
        codegen->object.symbols[symbol].section = 0;
        codegen->object.symbols[symbol].value = ncc_text_offset(codegen);
    }
    if (symbol < 0 ||
        !ncc_emit_bytes(codegen, prologue, sizeof(prologue)) ||
        !ncc_emit_u32(codegen, frame_size)) {
        return 0;
    }
    for (uint32_t i = 0u; i < function->param_count; i++) {
        struct ncc_local *local = ncc_function_local(function, function->params[i]);
        const uint8_t *store;

        store = local != NULL &&
                        local->type.pointer_depth == 0u &&
                        local->type.base_size == 1u
                    ? char_stores[i]
                    : stores[i];
        if (local == NULL ||
            !ncc_emit_bytes(codegen, store, sizeof(stores[i])) ||
            !ncc_emit_u32(codegen, (uint32_t)-local->stack_offset)) {
            return 0;
        }
    }
    if (!ncc_codegen_statement(codegen,
                               function->body,
                               return_patches,
                               &return_count,
                               NULL) ||
        !ncc_emit_bytes(codegen, zero_return, sizeof(zero_return))) {
        return 0;
    }
    {
        uint32_t epilogue_offset = ncc_text_offset(codegen);
        for (uint32_t i = 0u; i < return_count; i++) {
            ncc_patch_jump(codegen, (int)return_patches[i], epilogue_offset);
        }
    }
    return ncc_emit_bytes(codegen, epilogue, sizeof(epilogue));
}

void ncc_object_destroy(struct ncc_object *object) {
    if (object == NULL) {
        return;
    }
    if (object->owned_image == NULL && object->sections != NULL) {
        for (uint32_t i = 0u; i < object->section_count; i++) {
            free(object->sections[i].bytes.data);
        }
    }
    free(object->owned_image);
    free(object->sections);
    free(object->symbols);
    free(object->relocs);
    memset(object, 0, sizeof(*object));
}

int ncc_codegen_program(struct ncc_program *program,
                        struct ncc_object *object_out,
                        char *error,
                        uint32_t error_size) {
    struct ncc_codegen codegen;
    struct ncc_function *function;

    memset(&codegen, 0, sizeof(codegen));
    codegen.object.sections = calloc(4u, sizeof(*codegen.object.sections));
    codegen.object.symbols = calloc(NCC_SYMBOL_MAX, sizeof(*codegen.object.symbols));
    codegen.object.relocs = calloc(NCC_RELOC_MAX, sizeof(*codegen.object.relocs));
    if (codegen.object.sections == NULL ||
        codegen.object.symbols == NULL ||
        codegen.object.relocs == NULL) {
        ncc_object_destroy(&codegen.object);
        ncc_copy_text(error, error_size, "out of memory");
        return 0;
    }
    codegen.object.section_count = 4u;
    codegen.object.sections[0].kind = NCC_SEC_TEXT;
    codegen.object.sections[0].align = 16u;
    codegen.object.sections[1].kind = NCC_SEC_RODATA;
    codegen.object.sections[1].align = 8u;
    codegen.object.sections[2].kind = NCC_SEC_DATA;
    codegen.object.sections[2].align = 8u;
    codegen.object.sections[3].kind = NCC_SEC_BSS;
    codegen.object.sections[3].align = 8u;
    ncc_copy_text(codegen.object.name, sizeof(codegen.object.name), "<compiled>");
    if (!ncc_codegen_globals(&codegen, program->globals)) {
        ncc_copy_text(error,
                      error_size,
                      codegen.error[0] != '\0' ? codegen.error : "global generation failed");
        ncc_object_destroy(&codegen.object);
        return 0;
    }
    function = program->functions;
    while (function != NULL) {
        if (!function->prototype_only &&
            !ncc_codegen_function(&codegen, function)) {
            ncc_copy_text(error,
                          error_size,
                          codegen.error[0] != '\0' ? codegen.error : "code generation failed");
            ncc_object_destroy(&codegen.object);
            return 0;
        }
        function = function->next;
    }
    *object_out = codegen.object;
    return 1;
}
