#include "user/apps/elf/ncc/ncc.h"

static void ncc_parser_error(struct ncc_parser *parser, const char *text) {
    if (parser->error[0] == '\0') {
        snprintf(parser->error,
                 sizeof(parser->error),
                 "line %u: %s near '%s'",
                 parser->lexer.token.line,
                 text,
                 parser->lexer.token.text);
    }
}

static int ncc_next(struct ncc_parser *parser) {
    if (!ncc_lexer_next(&parser->lexer)) {
        ncc_copy_text(parser->error, sizeof(parser->error), parser->lexer.error);
        return 0;
    }
    return 1;
}

static int ncc_consume(struct ncc_parser *parser, const char *text) {
    if (!ncc_token_is(&parser->lexer.token, text)) {
        return 0;
    }
    return ncc_next(parser);
}

static int ncc_expect(struct ncc_parser *parser, const char *text) {
    if (!ncc_token_is(&parser->lexer.token, text)) {
        ncc_parser_error(parser, "expected token");
        return 0;
    }
    return ncc_next(parser);
}

static int ncc_is_builtin_type_keyword(const struct ncc_token *token) {
    return ncc_token_is(token, "void") ||
           ncc_token_is(token, "char") ||
           ncc_token_is(token, "short") ||
           ncc_token_is(token, "int") ||
           ncc_token_is(token, "long") ||
           ncc_token_is(token, "signed") ||
           ncc_token_is(token, "unsigned") ||
           ncc_token_is(token, "const") ||
           ncc_token_is(token, "volatile") ||
           ncc_token_is(token, "struct") ||
           ncc_token_is(token, "enum");
}

static struct ncc_typedef *ncc_find_typedef(struct ncc_parser *parser,
                                            const char *name) {
    struct ncc_typedef *alias = parser != NULL && parser->program != NULL
                                    ? parser->program->typedefs
                                    : NULL;

    while (alias != NULL) {
        if (strcmp(alias->name, name) == 0) {
            return alias;
        }
        alias = alias->next;
    }
    return NULL;
}

static int ncc_is_type_name(struct ncc_parser *parser) {
    return ncc_is_builtin_type_keyword(&parser->lexer.token) ||
           (parser->lexer.token.kind == NCC_TOK_IDENT &&
            ncc_find_typedef(parser, parser->lexer.token.text) != NULL);
}

static uint32_t ncc_type_size(const struct ncc_type *type) {
    uint32_t element_size;

    if (type == NULL) {
        return 0u;
    }
    element_size = type->pointer_depth != 0u
                       ? 8u
                       : (type->struct_type != NULL ? type->struct_type->size : type->base_size);
    return type->array_length != 0u ? element_size * type->array_length : element_size;
}

static uint32_t ncc_type_align(struct ncc_type type) {
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
    size = ncc_type_size(&type);
    return size >= 8u ? 8u : (size != 0u ? size : 1u);
}

static struct ncc_struct_type *ncc_find_struct(struct ncc_parser *parser, const char *name) {
    struct ncc_struct_type *type = parser != NULL && parser->program != NULL
                                       ? parser->program->structs
                                       : NULL;

    while (type != NULL) {
        if (strcmp(type->name, name) == 0) {
            return type;
        }
        type = type->next;
    }
    return NULL;
}

static struct ncc_struct_field *ncc_find_struct_field(struct ncc_struct_type *type,
                                                      const char *name) {
    struct ncc_struct_field *field = type != NULL ? type->fields : NULL;

    while (field != NULL) {
        if (strcmp(field->name, name) == 0) {
            return field;
        }
        field = field->next;
    }
    return NULL;
}

static struct ncc_struct_type *ncc_add_struct_type(struct ncc_parser *parser,
                                                  const char *name) {
    struct ncc_struct_type *type;

    if (parser == NULL || parser->program == NULL ||
        ncc_find_struct(parser, name) != NULL) {
        ncc_parser_error(parser, "duplicate struct type");
        return NULL;
    }
    type = calloc(1u, sizeof(*type));
    if (type == NULL) {
        ncc_parser_error(parser, "out of memory");
        return NULL;
    }
    ncc_copy_text(type->name, sizeof(type->name), name);
    type->align = 1u;
    type->next = parser->program->structs;
    parser->program->structs = type;
    return type;
}

static struct ncc_type ncc_type_decay(struct ncc_type type) {
    if (type.array_length != 0u) {
        type.array_length = 0u;
        type.pointer_depth++;
    }
    return type;
}

static struct ncc_type ncc_type_dereference(struct ncc_type type) {
    type.array_length = 0u;
    if (type.pointer_depth != 0u) {
        type.pointer_depth--;
    }
    return type;
}

static int ncc_type_is_pointer(struct ncc_type type) {
    return type.pointer_depth != 0u || type.array_length != 0u;
}

static int ncc_parse_type(struct ncc_parser *parser, struct ncc_type *type_out) {
    struct ncc_type type;
    int consumed = 0;

    memset(&type, 0, sizeof(type));
    type.base_size = 8u;
    if (parser->lexer.token.kind == NCC_TOK_IDENT) {
        struct ncc_typedef *alias = ncc_find_typedef(parser,
                                                     parser->lexer.token.text);

        if (alias != NULL) {
            type = alias->type;
            consumed = 1;
            if (!ncc_next(parser)) {
                return 0;
            }
        }
    }
    while (ncc_is_builtin_type_keyword(&parser->lexer.token)) {
        int was_struct = ncc_token_is(&parser->lexer.token, "struct");
        int was_enum = ncc_token_is(&parser->lexer.token, "enum");

        if (ncc_token_is(&parser->lexer.token, "char")) {
            type.base_size = 1u;
        }
        consumed = 1;
        if (!ncc_next(parser)) {
            return 0;
        }
        if (was_struct && parser->lexer.token.kind == NCC_TOK_IDENT) {
            struct ncc_struct_type *struct_type = ncc_find_struct(parser,
                                                                  parser->lexer.token.text);

            if (struct_type == NULL) {
                ncc_parser_error(parser, "unknown struct type");
                return 0;
            }
            type.struct_type = struct_type;
            type.base_size = 0u;
            if (!ncc_next(parser)) {
                return 0;
            }
            break;
        }
        if (was_enum) {
            type.base_size = 8u;
            if (parser->lexer.token.kind == NCC_TOK_IDENT &&
                !ncc_token_is(&parser->lexer.token, "const") &&
                !ncc_token_is(&parser->lexer.token, "volatile")) {
                if (!ncc_next(parser)) {
                    return 0;
                }
            }
            break;
        }
    }
    while (ncc_consume(parser, "*")) {
        consumed = 1;
        type.pointer_depth++;
    }
    if (consumed && type_out != NULL) {
        *type_out = type;
    }
    return consumed;
}

static struct ncc_node *ncc_new_node(enum ncc_node_kind kind) {
    struct ncc_node *node = calloc(1u, sizeof(*node));

    if (node != NULL) {
        node->kind = kind;
    }
    return node;
}

static struct ncc_node *ncc_new_binary(enum ncc_node_kind kind,
                                       struct ncc_node *lhs,
                                       struct ncc_node *rhs) {
    struct ncc_node *node = ncc_new_node(kind);

    if (node != NULL) {
        node->lhs = lhs;
        node->rhs = rhs;
    }
    return node;
}

static struct ncc_local *ncc_find_local(struct ncc_parser *parser, const char *name) {
    struct ncc_local *local = parser->current_function != NULL
                                  ? parser->current_function->locals
                                  : NULL;

    while (local != NULL) {
        if (strcmp(local->name, name) == 0) {
            return local;
        }
        local = local->next;
    }
    return NULL;
}

static struct ncc_global *ncc_find_global(struct ncc_parser *parser, const char *name) {
    struct ncc_global *global = parser != NULL && parser->program != NULL
                                    ? parser->program->globals
                                    : NULL;

    while (global != NULL) {
        if (strcmp(global->name, name) == 0) {
            return global;
        }
        global = global->next;
    }
    return NULL;
}

static struct ncc_enum_constant *ncc_find_enum_constant(struct ncc_parser *parser,
                                                        const char *name) {
    struct ncc_enum_constant *constant =
        parser != NULL && parser->program != NULL
            ? parser->program->enum_constants
            : NULL;

    while (constant != NULL) {
        if (strcmp(constant->name, name) == 0) {
            return constant;
        }
        constant = constant->next;
    }
    return NULL;
}

static struct ncc_global *ncc_add_global(struct ncc_parser *parser,
                                         const char *name,
                                         struct ncc_type type,
                                         int64_t initial_value,
                                         int has_initializer,
                                         uint8_t *initializer_data,
                                         uint32_t initializer_size) {
    struct ncc_global *global;

    if (parser == NULL || parser->program == NULL ||
        ncc_find_global(parser, name) != NULL) {
        ncc_parser_error(parser, "duplicate global variable");
        return NULL;
    }
    global = calloc(1u, sizeof(*global));
    if (global == NULL) {
        ncc_parser_error(parser, "out of memory");
        return NULL;
    }
    ncc_copy_text(global->name, sizeof(global->name), name);
    global->type = type;
    global->initial_value = initial_value;
    global->initializer_data = initializer_data;
    global->initializer_size = initializer_size;
    global->has_initializer = has_initializer ? 1u : 0u;
    global->next = parser->program->globals;
    parser->program->globals = global;
    return global;
}

static struct ncc_local *ncc_add_local(struct ncc_parser *parser,
                                       const char *name,
                                       struct ncc_type type) {
    struct ncc_local *local;
    uint32_t size;
    uint32_t align;

    if (parser->current_function == NULL || ncc_find_local(parser, name) != NULL) {
        ncc_parser_error(parser, "duplicate local variable");
        return NULL;
    }
    local = calloc(1u, sizeof(*local));
    if (local == NULL) {
        ncc_parser_error(parser, "out of memory");
        return NULL;
    }
    ncc_copy_text(local->name, sizeof(local->name), name);
    size = ncc_type_size(&type);
    align = ncc_type_align(type);
    if (size == 0u || align == 0u) {
        free(local);
        ncc_parser_error(parser, "invalid local variable type");
        return NULL;
    }
    parser->current_function->stack_size =
        (uint32_t)ncc_align_up(parser->current_function->stack_size, align);
    parser->current_function->stack_size += size;
    local->stack_offset = (int32_t)parser->current_function->stack_size;
    local->type = type;
    local->next = parser->current_function->locals;
    parser->current_function->locals = local;
    return local;
}

static struct ncc_node *ncc_parse_expression(struct ncc_parser *parser);
static struct ncc_node *ncc_parse_assignment(struct ncc_parser *parser);

static struct ncc_node *ncc_new_sizeof_number(uint32_t size) {
    struct ncc_node *node = ncc_new_node(NCC_NODE_NUMBER);

    if (node != NULL) {
        node->value = (int64_t)size;
        node->type.base_size = 8u;
    }
    return node;
}

static int ncc_node_is_assignable(struct ncc_node *node) {
    if (node == NULL) {
        return 0;
    }
    return node->kind == NCC_NODE_VARIABLE ||
           node->kind == NCC_NODE_DEREFERENCE ||
           node->kind == NCC_NODE_INDEX ||
           node->kind == NCC_NODE_MEMBER;
}

static int ncc_require_assignable(struct ncc_parser *parser, struct ncc_node *node) {
    if (!ncc_node_is_assignable(node)) {
        ncc_parser_error(parser, "assignment target is not assignable");
        return 0;
    }
    if (node->type.array_length != 0u) {
        ncc_parser_error(parser, "array is not assignable");
        return 0;
    }
    return 1;
}

static struct ncc_node *ncc_parse_primary(struct ncc_parser *parser) {
    struct ncc_token token = parser->lexer.token;
    struct ncc_node *node;

    if (ncc_consume(parser, "(")) {
        node = ncc_parse_expression(parser);
        if (node == NULL || !ncc_expect(parser, ")")) {
            return NULL;
        }
        return node;
    }
    if (token.kind == NCC_TOK_NUMBER || token.kind == NCC_TOK_CHAR) {
        node = ncc_new_node(NCC_NODE_NUMBER);
        if (node == NULL) {
            return NULL;
        }
        node->value = (int64_t)token.value;
        node->type.base_size = 8u;
        if (!ncc_next(parser)) {
            free(node);
            return NULL;
        }
        return node;
    }
    if (token.kind == NCC_TOK_STRING) {
        node = ncc_new_node(NCC_NODE_STRING);
        if (node == NULL) {
            return NULL;
        }
        node->string_value = strdup(token.text);
        node->string_length = strlen(token.text) + 1u;
        node->type.base_size = 1u;
        node->type.pointer_depth = 1u;
        if (node->string_value == NULL || !ncc_next(parser)) {
            free(node->string_value);
            free(node);
            return NULL;
        }
        return node;
    }
    if (token.kind == NCC_TOK_IDENT) {
        char name[NCC_NAME_MAX + 1];

        ncc_copy_text(name, sizeof(name), token.text);
        if (!ncc_next(parser)) {
            return NULL;
        }
        if (ncc_consume(parser, "(")) {
            node = ncc_new_node(NCC_NODE_CALL);
            if (node == NULL) {
                return NULL;
            }
            ncc_copy_text(node->name, sizeof(node->name), name);
            node->type.base_size = 8u;
            if (!ncc_token_is(&parser->lexer.token, ")")) {
                for (;;) {
                    if (node->arg_count >= NCC_ARG_MAX) {
                        ncc_parser_error(parser, "too many call arguments");
                        return NULL;
                    }
                    node->args[node->arg_count] = ncc_parse_expression(parser);
                    if (node->args[node->arg_count] == NULL) {
                        return NULL;
                    }
                    node->arg_count++;
                    if (!ncc_consume(parser, ",")) {
                        break;
                    }
                }
            }
            if (!ncc_expect(parser, ")")) {
                return NULL;
            }
            return node;
        }
        {
            struct ncc_local *local = ncc_find_local(parser, name);

            if (local == NULL) {
                struct ncc_global *global = ncc_find_global(parser, name);
                struct ncc_enum_constant *constant =
                    ncc_find_enum_constant(parser, name);

                if (global != NULL) {
                    node = ncc_new_node(NCC_NODE_VARIABLE);
                    if (node == NULL) {
                        return NULL;
                    }
                    node->is_global = 1u;
                    node->type = global->type;
                    ncc_copy_text(node->name, sizeof(node->name), name);
                    return node;
                }
                if (constant != NULL) {
                    node = ncc_new_node(NCC_NODE_NUMBER);
                    if (node == NULL) {
                        return NULL;
                    }
                    node->value = constant->value;
                    node->type.base_size = 8u;
                    return node;
                }
                ncc_parser_error(parser, "unknown variable");
                return NULL;
            }
            node = ncc_new_node(NCC_NODE_VARIABLE);
            if (node == NULL) {
                return NULL;
            }
            node->stack_offset = local->stack_offset;
            node->type = local->type;
            ncc_copy_text(node->name, sizeof(node->name), name);
            return node;
        }
    }
    ncc_parser_error(parser, "expected expression");
    return NULL;
}

static struct ncc_node *ncc_parse_postfix(struct ncc_parser *parser) {
    struct ncc_node *node = ncc_parse_primary(parser);

    while (node != NULL) {
        if (ncc_consume(parser, "[")) {
            struct ncc_node *index = ncc_new_node(NCC_NODE_INDEX);

            if (index == NULL) {
                return NULL;
            }
            index->lhs = node;
            index->rhs = ncc_parse_expression(parser);
            if (index->rhs == NULL || !ncc_expect(parser, "]")) {
                return NULL;
            }
            if (!ncc_type_is_pointer(node->type)) {
                ncc_parser_error(parser, "subscripted value is not an array or pointer");
                return NULL;
            }
            index->type = ncc_type_dereference(ncc_type_decay(node->type));
            node = index;
        } else if (ncc_consume(parser, ".")) {
            struct ncc_struct_field *field;
            struct ncc_node *member;

            if (node->type.pointer_depth != 0u || node->type.struct_type == NULL) {
                ncc_parser_error(parser, "member access requires a struct value");
                return NULL;
            }
            if (parser->lexer.token.kind != NCC_TOK_IDENT) {
                ncc_parser_error(parser, "expected struct field name");
                return NULL;
            }
            field = ncc_find_struct_field(node->type.struct_type, parser->lexer.token.text);
            if (field == NULL) {
                ncc_parser_error(parser, "unknown struct field");
                return NULL;
            }
            member = ncc_new_node(NCC_NODE_MEMBER);
            if (member == NULL) {
                return NULL;
            }
            member->lhs = node;
            member->type = field->type;
            member->field_offset = field->offset;
            ncc_copy_text(member->name, sizeof(member->name), parser->lexer.token.text);
            if (!ncc_next(parser)) {
                return NULL;
            }
            node = member;
        } else if (ncc_consume(parser, "->")) {
            struct ncc_type target_type = ncc_type_dereference(ncc_type_decay(node->type));
            struct ncc_struct_field *field;
            struct ncc_node *deref;
            struct ncc_node *member;

            if (node->type.pointer_depth == 0u || target_type.struct_type == NULL) {
                ncc_parser_error(parser, "arrow access requires a struct pointer");
                return NULL;
            }
            if (parser->lexer.token.kind != NCC_TOK_IDENT) {
                ncc_parser_error(parser, "expected struct field name");
                return NULL;
            }
            field = ncc_find_struct_field(target_type.struct_type, parser->lexer.token.text);
            if (field == NULL) {
                ncc_parser_error(parser, "unknown struct field");
                return NULL;
            }
            deref = ncc_new_node(NCC_NODE_DEREFERENCE);
            member = ncc_new_node(NCC_NODE_MEMBER);
            if (deref == NULL || member == NULL) {
                return NULL;
            }
            deref->lhs = node;
            deref->type = target_type;
            member->lhs = deref;
            member->type = field->type;
            member->field_offset = field->offset;
            ncc_copy_text(member->name, sizeof(member->name), parser->lexer.token.text);
            if (!ncc_next(parser)) {
                return NULL;
            }
            node = member;
        } else if (ncc_consume(parser, "++")) {
            struct ncc_type type = node->type;

            if (!ncc_require_assignable(parser, node)) {
                return NULL;
            }
            node = ncc_new_binary(NCC_NODE_POST_INC, node, NULL);
            if (node != NULL) node->type = type;
        } else if (ncc_consume(parser, "--")) {
            struct ncc_type type = node->type;

            if (!ncc_require_assignable(parser, node)) {
                return NULL;
            }
            node = ncc_new_binary(NCC_NODE_POST_DEC, node, NULL);
            if (node != NULL) node->type = type;
        } else {
            break;
        }
    }
    return node;
}

static struct ncc_node *ncc_parse_unary(struct ncc_parser *parser) {
    if (ncc_consume(parser, "sizeof")) {
        if (ncc_consume(parser, "(")) {
            if (ncc_is_type_name(parser)) {
                struct ncc_type type;

                if (!ncc_parse_type(parser, &type) ||
                    !ncc_expect(parser, ")")) {
                    return NULL;
                }
                return ncc_new_sizeof_number(ncc_type_size(&type));
            }
            {
                struct ncc_node *expr = ncc_parse_expression(parser);

                if (expr == NULL || !ncc_expect(parser, ")")) {
                    return NULL;
                }
                return ncc_new_sizeof_number(ncc_type_size(&expr->type));
            }
        }
        {
            struct ncc_node *expr = ncc_parse_unary(parser);

            if (expr == NULL) {
                return NULL;
            }
            return ncc_new_sizeof_number(ncc_type_size(&expr->type));
        }
    }
    if (ncc_consume(parser, "++")) {
        struct ncc_node *target = ncc_parse_unary(parser);
        struct ncc_type type;

        if (!ncc_require_assignable(parser, target)) {
            return NULL;
        }
        type = target->type;
        target = ncc_new_binary(NCC_NODE_PRE_INC, target, NULL);
        if (target != NULL) target->type = type;
        return target;
    }
    if (ncc_consume(parser, "--")) {
        struct ncc_node *target = ncc_parse_unary(parser);
        struct ncc_type type;

        if (!ncc_require_assignable(parser, target)) {
            return NULL;
        }
        type = target->type;
        target = ncc_new_binary(NCC_NODE_PRE_DEC, target, NULL);
        if (target != NULL) target->type = type;
        return target;
    }
    if (ncc_consume(parser, "+")) {
        return ncc_parse_unary(parser);
    }
    if (ncc_consume(parser, "-")) {
        struct ncc_node *node = ncc_new_node(NCC_NODE_NEG);

        if (node != NULL) {
            node->lhs = ncc_parse_unary(parser);
            node->type.base_size = 8u;
        }
        return node;
    }
    if (ncc_consume(parser, "!")) {
        struct ncc_node *node = ncc_new_node(NCC_NODE_NOT);

        if (node != NULL) {
            node->lhs = ncc_parse_unary(parser);
            node->type.base_size = 8u;
        }
        return node;
    }
    if (ncc_consume(parser, "~")) {
        struct ncc_node *node = ncc_new_node(NCC_NODE_BIT_NOT);

        if (node != NULL) {
            node->lhs = ncc_parse_unary(parser);
            node->type.base_size = 8u;
        }
        return node;
    }
    if (ncc_consume(parser, "&")) {
        struct ncc_node *node = ncc_new_node(NCC_NODE_ADDRESS);

        if (node != NULL) {
            node->lhs = ncc_parse_unary(parser);
            if (node->lhs != NULL) {
                node->type = node->lhs->type;
                node->type.array_length = 0u;
                node->type.pointer_depth++;
            }
        }
        return node;
    }
    if (ncc_consume(parser, "*")) {
        struct ncc_node *node = ncc_new_node(NCC_NODE_DEREFERENCE);

        if (node != NULL) {
            node->lhs = ncc_parse_unary(parser);
            if (node->lhs != NULL) {
                if (!ncc_type_is_pointer(node->lhs->type)) {
                    ncc_parser_error(parser, "cannot dereference non-pointer");
                    return NULL;
                }
                node->type = ncc_type_dereference(ncc_type_decay(node->lhs->type));
            }
        }
        return node;
    }
    return ncc_parse_postfix(parser);
}

static struct ncc_node *ncc_parse_mul(struct ncc_parser *parser) {
    struct ncc_node *node = ncc_parse_unary(parser);

    while (node != NULL) {
        if (ncc_consume(parser, "*")) {
            node = ncc_new_binary(NCC_NODE_MUL, node, ncc_parse_unary(parser));
        } else if (ncc_consume(parser, "/")) {
            node = ncc_new_binary(NCC_NODE_DIV, node, ncc_parse_unary(parser));
        } else if (ncc_consume(parser, "%")) {
            node = ncc_new_binary(NCC_NODE_MOD, node, ncc_parse_unary(parser));
        } else {
            break;
        }
        if (node != NULL) {
            node->type.base_size = 8u;
        }
    }
    return node;
}

static struct ncc_node *ncc_parse_add(struct ncc_parser *parser) {
    struct ncc_node *node = ncc_parse_mul(parser);

    while (node != NULL) {
        if (ncc_consume(parser, "+")) {
            node = ncc_new_binary(NCC_NODE_ADD, node, ncc_parse_mul(parser));
        } else if (ncc_consume(parser, "-")) {
            node = ncc_new_binary(NCC_NODE_SUB, node, ncc_parse_mul(parser));
        } else {
            break;
        }
        if (node != NULL) {
            int lhs_pointer = ncc_type_is_pointer(node->lhs->type);
            int rhs_pointer = ncc_type_is_pointer(node->rhs->type);

            if (node->kind == NCC_NODE_ADD && lhs_pointer && rhs_pointer) {
                ncc_parser_error(parser, "cannot add two pointers");
                return NULL;
            }
            if (node->kind == NCC_NODE_SUB && !lhs_pointer && rhs_pointer) {
                ncc_parser_error(parser, "cannot subtract a pointer from an integer");
                return NULL;
            }
            if (node->kind == NCC_NODE_SUB && lhs_pointer && rhs_pointer) {
                node->type.base_size = 8u;
            } else if (lhs_pointer) {
                node->type = ncc_type_decay(node->lhs->type);
            } else if (rhs_pointer) {
                node->type = ncc_type_decay(node->rhs->type);
            } else {
                node->type.base_size = 8u;
            }
        }
    }
    return node;
}

static struct ncc_node *ncc_parse_shift(struct ncc_parser *parser) {
    struct ncc_node *node = ncc_parse_add(parser);

    while (node != NULL) {
        if (ncc_consume(parser, "<<")) {
            node = ncc_new_binary(NCC_NODE_SHL, node, ncc_parse_add(parser));
        } else if (ncc_consume(parser, ">>")) {
            node = ncc_new_binary(NCC_NODE_SHR, node, ncc_parse_add(parser));
        } else {
            break;
        }
        if (node != NULL) node->type.base_size = 8u;
    }
    return node;
}

static struct ncc_node *ncc_parse_relational(struct ncc_parser *parser) {
    struct ncc_node *node = ncc_parse_shift(parser);

    while (node != NULL) {
        if (ncc_consume(parser, "<")) {
            node = ncc_new_binary(NCC_NODE_LT, node, ncc_parse_shift(parser));
        } else if (ncc_consume(parser, "<=")) {
            node = ncc_new_binary(NCC_NODE_LE, node, ncc_parse_shift(parser));
        } else if (ncc_consume(parser, ">")) {
            node = ncc_new_binary(NCC_NODE_LT, ncc_parse_shift(parser), node);
        } else if (ncc_consume(parser, ">=")) {
            node = ncc_new_binary(NCC_NODE_LE, ncc_parse_shift(parser), node);
        } else {
            break;
        }
        if (node != NULL) node->type.base_size = 8u;
    }
    return node;
}

static struct ncc_node *ncc_parse_equality(struct ncc_parser *parser) {
    struct ncc_node *node = ncc_parse_relational(parser);

    while (node != NULL) {
        if (ncc_consume(parser, "==")) {
            node = ncc_new_binary(NCC_NODE_EQ, node, ncc_parse_relational(parser));
        } else if (ncc_consume(parser, "!=")) {
            node = ncc_new_binary(NCC_NODE_NE, node, ncc_parse_relational(parser));
        } else {
            break;
        }
        if (node != NULL) node->type.base_size = 8u;
    }
    return node;
}

static struct ncc_node *ncc_parse_bit_and(struct ncc_parser *parser) {
    struct ncc_node *node = ncc_parse_equality(parser);

    while (node != NULL && ncc_consume(parser, "&")) {
        node = ncc_new_binary(NCC_NODE_BIT_AND, node, ncc_parse_equality(parser));
        if (node != NULL) node->type.base_size = 8u;
    }
    return node;
}

static struct ncc_node *ncc_parse_bit_xor(struct ncc_parser *parser) {
    struct ncc_node *node = ncc_parse_bit_and(parser);

    while (node != NULL && ncc_consume(parser, "^")) {
        node = ncc_new_binary(NCC_NODE_BIT_XOR, node, ncc_parse_bit_and(parser));
        if (node != NULL) node->type.base_size = 8u;
    }
    return node;
}

static struct ncc_node *ncc_parse_bit_or(struct ncc_parser *parser) {
    struct ncc_node *node = ncc_parse_bit_xor(parser);

    while (node != NULL && ncc_consume(parser, "|")) {
        node = ncc_new_binary(NCC_NODE_BIT_OR, node, ncc_parse_bit_xor(parser));
        if (node != NULL) node->type.base_size = 8u;
    }
    return node;
}

static struct ncc_node *ncc_parse_logical_and(struct ncc_parser *parser) {
    struct ncc_node *node = ncc_parse_bit_or(parser);

    while (node != NULL && ncc_consume(parser, "&&")) {
        node = ncc_new_binary(NCC_NODE_LOGICAL_AND, node, ncc_parse_bit_or(parser));
        if (node != NULL) node->type.base_size = 8u;
    }
    return node;
}

static struct ncc_node *ncc_parse_logical_or(struct ncc_parser *parser) {
    struct ncc_node *node = ncc_parse_logical_and(parser);

    while (node != NULL && ncc_consume(parser, "||")) {
        node = ncc_new_binary(NCC_NODE_LOGICAL_OR, node, ncc_parse_logical_and(parser));
        if (node != NULL) node->type.base_size = 8u;
    }
    return node;
}

static int ncc_conditional_type(struct ncc_parser *parser,
                                struct ncc_node *then_node,
                                struct ncc_node *else_node,
                                struct ncc_type *type_out) {
    int then_pointer = ncc_type_is_pointer(then_node->type);
    int else_pointer = ncc_type_is_pointer(else_node->type);

    if (then_pointer || else_pointer) {
        if (!then_pointer || !else_pointer) {
            ncc_parser_error(parser, "conditional pointer branches must have matching types");
            return 0;
        }
        if (then_node->type.pointer_depth != else_node->type.pointer_depth ||
            then_node->type.struct_type != else_node->type.struct_type) {
            ncc_parser_error(parser, "conditional pointer types do not match");
            return 0;
        }
        *type_out = ncc_type_decay(then_node->type);
        return 1;
    }
    memset(type_out, 0, sizeof(*type_out));
    type_out->base_size =
        then_node->type.base_size > else_node->type.base_size
            ? then_node->type.base_size
            : else_node->type.base_size;
    if (type_out->base_size == 0u) {
        type_out->base_size = 8u;
    }
    return 1;
}

static struct ncc_node *ncc_parse_conditional(struct ncc_parser *parser) {
    struct ncc_node *condition = ncc_parse_logical_or(parser);
    struct ncc_node *node;

    if (condition == NULL || !ncc_consume(parser, "?")) {
        return condition;
    }
    node = ncc_new_node(NCC_NODE_CONDITIONAL);
    if (node == NULL) {
        return NULL;
    }
    node->condition = condition;
    node->then_node = ncc_parse_assignment(parser);
    if (node->then_node == NULL || !ncc_expect(parser, ":")) {
        return NULL;
    }
    node->else_node = ncc_parse_conditional(parser);
    if (node->else_node == NULL ||
        !ncc_conditional_type(parser,
                              node->then_node,
                              node->else_node,
                              &node->type)) {
        return NULL;
    }
    return node;
}

static struct ncc_node *ncc_parse_assignment(struct ncc_parser *parser) {
    struct ncc_node *node = ncc_parse_conditional(parser);

    if (node != NULL &&
        (ncc_token_is(&parser->lexer.token, "=") ||
         ncc_token_is(&parser->lexer.token, "+=") ||
         ncc_token_is(&parser->lexer.token, "-=") ||
         ncc_token_is(&parser->lexer.token, "&=") ||
         ncc_token_is(&parser->lexer.token, "|=") ||
         ncc_token_is(&parser->lexer.token, "^=") ||
         ncc_token_is(&parser->lexer.token, "<<=") ||
         ncc_token_is(&parser->lexer.token, ">>="))) {
        enum ncc_node_kind kind = NCC_NODE_ASSIGN;

        if (ncc_token_is(&parser->lexer.token, "+=")) {
            kind = NCC_NODE_ADD_ASSIGN;
        } else if (ncc_token_is(&parser->lexer.token, "-=")) {
            kind = NCC_NODE_SUB_ASSIGN;
        } else if (ncc_token_is(&parser->lexer.token, "&=")) {
            kind = NCC_NODE_AND_ASSIGN;
        } else if (ncc_token_is(&parser->lexer.token, "|=")) {
            kind = NCC_NODE_OR_ASSIGN;
        } else if (ncc_token_is(&parser->lexer.token, "^=")) {
            kind = NCC_NODE_XOR_ASSIGN;
        } else if (ncc_token_is(&parser->lexer.token, "<<=")) {
            kind = NCC_NODE_SHL_ASSIGN;
        } else if (ncc_token_is(&parser->lexer.token, ">>=")) {
            kind = NCC_NODE_SHR_ASSIGN;
        }
        if (!ncc_require_assignable(parser, node)) {
            return NULL;
        }
        if (!ncc_next(parser)) {
            return NULL;
        }
        {
            struct ncc_type type = node->type;
            struct ncc_node *rhs = ncc_parse_assignment(parser);
            int lhs_pointer = ncc_type_is_pointer(type);
            int rhs_pointer = rhs != NULL && ncc_type_is_pointer(rhs->type);

            if (rhs == NULL) {
                return NULL;
            }
            if ((kind == NCC_NODE_ADD_ASSIGN || kind == NCC_NODE_SUB_ASSIGN) &&
                rhs_pointer) {
                if (lhs_pointer) {
                    ncc_parser_error(parser, "compound pointer assignment needs an integer rhs");
                } else {
                    ncc_parser_error(parser, "cannot assign pointer arithmetic result to integer");
                }
                return NULL;
            }
            if (kind != NCC_NODE_ASSIGN &&
                kind != NCC_NODE_ADD_ASSIGN &&
                kind != NCC_NODE_SUB_ASSIGN &&
                (lhs_pointer || rhs_pointer)) {
                ncc_parser_error(parser, "bitwise compound assignment requires integers");
                return NULL;
            }
            node = ncc_new_binary(kind, node, rhs);
            if (node != NULL) node->type = type;
        }
    }
    return node;
}

static struct ncc_node *ncc_parse_expression(struct ncc_parser *parser) {
    return ncc_parse_assignment(parser);
}

static struct ncc_node *ncc_parse_statement(struct ncc_parser *parser);

static void ncc_append_nodes(struct ncc_node ***tail_io,
                             struct ncc_node *nodes) {
    struct ncc_node **tail = *tail_io;

    *tail = nodes;
    while (*tail != NULL) {
        tail = &(*tail)->next;
    }
    *tail_io = tail;
}

static struct ncc_node *ncc_clone_lvalue(struct ncc_node *node) {
    struct ncc_node *copy;

    if (node == NULL) {
        return NULL;
    }
    copy = ncc_new_node(node->kind);
    if (copy == NULL) {
        return NULL;
    }
    copy->value = node->value;
    copy->stack_offset = node->stack_offset;
    copy->field_offset = node->field_offset;
    copy->is_global = node->is_global;
    copy->type = node->type;
    ncc_copy_text(copy->name, sizeof(copy->name), node->name);
    copy->lhs = ncc_clone_lvalue(node->lhs);
    copy->rhs = ncc_clone_lvalue(node->rhs);
    return copy;
}

static struct ncc_node *ncc_initializer_target_index(struct ncc_node *base,
                                                     struct ncc_type element_type,
                                                     uint32_t index) {
    struct ncc_node *node = ncc_new_node(NCC_NODE_INDEX);
    struct ncc_node *number = ncc_new_node(NCC_NODE_NUMBER);

    if (node == NULL || number == NULL) {
        return NULL;
    }
    number->value = index;
    number->type.base_size = 8u;
    node->lhs = ncc_clone_lvalue(base);
    node->rhs = number;
    node->type = element_type;
    return node;
}

static struct ncc_node *ncc_initializer_target_member(struct ncc_node *base,
                                                      struct ncc_struct_field *field) {
    struct ncc_node *node = ncc_new_node(NCC_NODE_MEMBER);

    if (node == NULL) {
        return NULL;
    }
    node->lhs = ncc_clone_lvalue(base);
    node->field_offset = field->offset;
    node->type = field->type;
    ncc_copy_text(node->name, sizeof(node->name), field->name);
    return node;
}

static struct ncc_node *ncc_initializer_assignment(struct ncc_node *target,
                                                   struct ncc_node *value) {
    struct ncc_node *statement = ncc_new_node(NCC_NODE_EXPR_STMT);

    if (statement == NULL || target == NULL || value == NULL) {
        return NULL;
    }
    statement->lhs = ncc_new_binary(NCC_NODE_ASSIGN, target, value);
    if (statement->lhs != NULL) {
        statement->lhs->type = target->type;
    }
    return statement;
}

static struct ncc_node *ncc_zero_initializer(struct ncc_node *target,
                                             struct ncc_type type) {
    struct ncc_node *head = NULL;
    struct ncc_node **tail = &head;

    if (type.array_length != 0u) {
        struct ncc_type element = type;

        element.array_length = 0u;
        for (uint32_t i = 0u; i < type.array_length; i++) {
            ncc_append_nodes(&tail,
                             ncc_zero_initializer(
                                 ncc_initializer_target_index(target, element, i),
                                 element));
        }
        return head;
    }
    if (type.pointer_depth == 0u && type.struct_type != NULL) {
        for (struct ncc_struct_field *field = type.struct_type->fields;
             field != NULL;
             field = field->next) {
            ncc_append_nodes(&tail,
                             ncc_zero_initializer(
                                 ncc_initializer_target_member(target, field),
                                 field->type));
        }
        return head;
    }
    {
        struct ncc_node *zero = ncc_new_node(NCC_NODE_NUMBER);

        if (zero != NULL) {
            zero->type.base_size = 8u;
        }
        return ncc_initializer_assignment(target, zero);
    }
}

static struct ncc_node *ncc_parse_local_initializer(struct ncc_parser *parser,
                                                    struct ncc_node *target,
                                                    struct ncc_type type) {
    struct ncc_node *head = NULL;
    struct ncc_node **tail = &head;

    if (type.array_length != 0u) {
        struct ncc_type element = type;
        uint32_t index = 0u;

        element.array_length = 0u;
        if (element.base_size == 1u &&
            element.pointer_depth == 0u &&
            element.struct_type == NULL &&
            parser->lexer.token.kind == NCC_TOK_STRING) {
            uint32_t length = (uint32_t)strlen(parser->lexer.token.text) + 1u;

            if (length > type.array_length) {
                ncc_parser_error(parser, "string initializer is too long");
                return NULL;
            }
            for (uint32_t i = 0u; i < length; i++) {
                struct ncc_node *value = ncc_new_node(NCC_NODE_NUMBER);

                value->value = (uint8_t)parser->lexer.token.text[i];
                value->type.base_size = 8u;
                ncc_append_nodes(&tail,
                                 ncc_initializer_assignment(
                                     ncc_initializer_target_index(target, element, i),
                                     value));
            }
            return ncc_next(parser) ? head : NULL;
        }
        if (!ncc_expect(parser, "{")) {
            return NULL;
        }
        while (!ncc_token_is(&parser->lexer.token, "}") &&
               index < type.array_length) {
            ncc_append_nodes(&tail,
                             ncc_parse_local_initializer(
                                 parser,
                                 ncc_initializer_target_index(target, element, index),
                                 element));
            index++;
            if (!ncc_consume(parser, ",")) {
                break;
            }
        }
        if (!ncc_expect(parser, "}")) {
            return NULL;
        }
        return head;
    }
    if (type.pointer_depth == 0u && type.struct_type != NULL) {
        struct ncc_struct_field *field = type.struct_type->fields;

        if (!ncc_expect(parser, "{")) {
            return NULL;
        }
        while (!ncc_token_is(&parser->lexer.token, "}") && field != NULL) {
            ncc_append_nodes(&tail,
                             ncc_parse_local_initializer(
                                 parser,
                                 ncc_initializer_target_member(target, field),
                                 field->type));
            field = field->next;
            if (!ncc_consume(parser, ",")) {
                break;
            }
        }
        if (!ncc_expect(parser, "}")) {
            return NULL;
        }
        return head;
    }
    if (ncc_consume(parser, "{")) {
        struct ncc_node *value = ncc_parse_expression(parser);

        if (value == NULL || !ncc_consume(parser, "}") ) {
            return NULL;
        }
        return ncc_initializer_assignment(target, value);
    }
    return ncc_initializer_assignment(target, ncc_parse_assignment(parser));
}

static int ncc_parse_case_constant(struct ncc_parser *parser, int64_t *value_out) {
    int negative = 0;
    int64_t value;

    if (ncc_consume(parser, "-")) {
        negative = 1;
    } else {
        (void)ncc_consume(parser, "+");
    }
    if (parser->lexer.token.kind == NCC_TOK_NUMBER ||
        parser->lexer.token.kind == NCC_TOK_CHAR) {
        value = (int64_t)parser->lexer.token.value;
        if (!ncc_next(parser)) {
            return 0;
        }
    } else if (parser->lexer.token.kind == NCC_TOK_IDENT) {
        struct ncc_enum_constant *constant =
            ncc_find_enum_constant(parser, parser->lexer.token.text);

        if (constant == NULL) {
            ncc_parser_error(parser, "case value must be an integer constant");
            return 0;
        }
        value = constant->value;
        if (!ncc_next(parser)) {
            return 0;
        }
    } else {
        ncc_parser_error(parser, "case value must be an integer constant");
        return 0;
    }
    *value_out = negative ? -value : value;
    return 1;
}

static struct ncc_node *ncc_parse_switch_body(struct ncc_parser *parser) {
    struct ncc_node *first = NULL;
    struct ncc_node **case_tail = &first;
    struct ncc_node *current_case = NULL;
    struct ncc_node **statement_tail = NULL;
    int saw_default = 0;

    while (!ncc_token_is(&parser->lexer.token, "}") &&
           parser->lexer.token.kind != NCC_TOK_EOF) {
        if (ncc_token_is(&parser->lexer.token, "case") ||
            ncc_token_is(&parser->lexer.token, "default")) {
            int is_default = ncc_token_is(&parser->lexer.token, "default");
            struct ncc_node *label =
                ncc_new_node(is_default ? NCC_NODE_DEFAULT : NCC_NODE_CASE);

            if (label == NULL || !ncc_next(parser)) {
                return NULL;
            }
            if (is_default) {
                if (saw_default) {
                    ncc_parser_error(parser, "duplicate default label");
                    return NULL;
                }
                saw_default = 1;
            } else if (!ncc_parse_case_constant(parser, &label->value)) {
                return NULL;
            }
            if (!ncc_expect(parser, ":")) {
                return NULL;
            }
            for (struct ncc_node *scan = first; scan != NULL; scan = scan->next) {
                if (!is_default && scan->kind == NCC_NODE_CASE &&
                    scan->value == label->value) {
                    ncc_parser_error(parser, "duplicate case value");
                    return NULL;
                }
            }
            *case_tail = label;
            case_tail = &label->next;
            current_case = label;
            statement_tail = &label->lhs;
            continue;
        }
        if (current_case == NULL) {
            ncc_parser_error(parser, "statement before first case label");
            return NULL;
        }
        {
            struct ncc_node *statement = ncc_parse_statement(parser);

            if (statement == NULL) {
                return NULL;
            }
            *statement_tail = statement;
            while (*statement_tail != NULL) {
                statement_tail = &(*statement_tail)->next;
            }
        }
    }
    if (!ncc_expect(parser, "}")) {
        return NULL;
    }
    return first;
}

static struct ncc_node *ncc_parse_block_after_open(struct ncc_parser *parser) {
    struct ncc_node *block = ncc_new_node(NCC_NODE_BLOCK);
    struct ncc_node **tail;

    if (block == NULL) {
        return NULL;
    }
    tail = &block->lhs;
    while (!ncc_token_is(&parser->lexer.token, "}") &&
           parser->lexer.token.kind != NCC_TOK_EOF) {
        struct ncc_node *statement = ncc_parse_statement(parser);

        if (statement == NULL) {
            return NULL;
        }
        *tail = statement;
        while (*tail != NULL) {
            tail = &(*tail)->next;
        }
    }
    if (!ncc_expect(parser, "}")) {
        return NULL;
    }
    return block;
}

static struct ncc_node *ncc_parse_declaration(struct ncc_parser *parser) {
    char name[NCC_NAME_MAX + 1];
    struct ncc_type type;
    struct ncc_local *local;
    struct ncc_node *statement;
    int unsized_array = 0;

    if (!ncc_parse_type(parser, &type) || parser->lexer.token.kind != NCC_TOK_IDENT) {
        ncc_parser_error(parser, "invalid local declaration");
        return NULL;
    }
    ncc_copy_text(name, sizeof(name), parser->lexer.token.text);
    if (!ncc_next(parser)) {
        return NULL;
    }
    if (ncc_consume(parser, "[")) {
        if (ncc_consume(parser, "]")) {
            unsized_array = 1;
        } else if (parser->lexer.token.kind != NCC_TOK_NUMBER ||
                   parser->lexer.token.value == 0u ||
                   parser->lexer.token.value > 65535u) {
            ncc_parser_error(parser, "array length must be a positive constant");
            return NULL;
        } else {
            type.array_length = (uint32_t)parser->lexer.token.value;
            if (!ncc_next(parser) || !ncc_expect(parser, "]")) {
                return NULL;
            }
        }
    }
    if (unsized_array) {
        if (!ncc_token_is(&parser->lexer.token, "=")) {
            ncc_parser_error(parser, "unsized array requires an initializer");
            return NULL;
        }
        if (type.base_size != 1u ||
            type.pointer_depth != 0u ||
            type.struct_type != NULL) {
            ncc_parser_error(parser, "only char[] string size inference is supported");
            return NULL;
        }
        {
            struct ncc_token saved = parser->lexer.token;
            uint32_t saved_offset = parser->lexer.offset;
            uint32_t saved_line = parser->lexer.line;

            if (!ncc_next(parser) ||
                parser->lexer.token.kind != NCC_TOK_STRING) {
                ncc_parser_error(parser, "char[] requires a string initializer");
                return NULL;
            }
            type.array_length = (uint32_t)strlen(parser->lexer.token.text) + 1u;
            parser->lexer.token = saved;
            parser->lexer.offset = saved_offset;
            parser->lexer.line = saved_line;
        }
    }
    local = ncc_add_local(parser, name, type);
    if (local == NULL) {
        return NULL;
    }
    statement = ncc_new_node(NCC_NODE_EXPR_STMT);
    if (statement == NULL) {
        return NULL;
    }
    if (ncc_consume(parser, "=")) {
        struct ncc_node *variable = ncc_new_node(NCC_NODE_VARIABLE);
        struct ncc_node *head = NULL;
        struct ncc_node **tail = &head;

        variable->stack_offset = local->stack_offset;
        variable->type = local->type;
        if (type.array_length != 0u ||
            (type.pointer_depth == 0u && type.struct_type != NULL)) {
            ncc_append_nodes(&tail,
                             ncc_zero_initializer(ncc_clone_lvalue(variable), type));
        }
        ncc_append_nodes(&tail,
                         ncc_parse_local_initializer(parser, variable, type));
        free(statement);
        statement = head;
    } else {
        statement->lhs = ncc_new_node(NCC_NODE_NUMBER);
        if (statement->lhs != NULL) statement->lhs->type.base_size = 8u;
    }
    if (statement == NULL || !ncc_expect(parser, ";")) {
        return NULL;
    }
    return statement;
}

static struct ncc_node *ncc_parse_statement(struct ncc_parser *parser) {
    struct ncc_node *node;

    if (ncc_consume(parser, "{")) {
        return ncc_parse_block_after_open(parser);
    }
    if (ncc_token_is(&parser->lexer.token, "return")) {
        if (!ncc_next(parser)) {
            return NULL;
        }
        node = ncc_new_node(NCC_NODE_RETURN);
        if (node == NULL) {
            return NULL;
        }
        node->lhs = ncc_parse_expression(parser);
        if (node->lhs == NULL || !ncc_expect(parser, ";")) {
            return NULL;
        }
        return node;
    }
    if (ncc_token_is(&parser->lexer.token, "if")) {
        if (!ncc_next(parser) || !ncc_expect(parser, "(")) {
            return NULL;
        }
        node = ncc_new_node(NCC_NODE_IF);
        if (node == NULL) {
            return NULL;
        }
        node->condition = ncc_parse_expression(parser);
        if (node->condition == NULL || !ncc_expect(parser, ")")) {
            return NULL;
        }
        node->then_node = ncc_parse_statement(parser);
        if (node->then_node == NULL) {
            return NULL;
        }
        if (ncc_token_is(&parser->lexer.token, "else")) {
            if (!ncc_next(parser)) {
                return NULL;
            }
            node->else_node = ncc_parse_statement(parser);
        }
        return node;
    }
    if (ncc_token_is(&parser->lexer.token, "while")) {
        if (!ncc_next(parser) || !ncc_expect(parser, "(")) {
            return NULL;
        }
        node = ncc_new_node(NCC_NODE_WHILE);
        if (node == NULL) {
            return NULL;
        }
        node->condition = ncc_parse_expression(parser);
        if (node->condition == NULL || !ncc_expect(parser, ")")) {
            return NULL;
        }
        parser->loop_depth++;
        node->then_node = ncc_parse_statement(parser);
        parser->loop_depth--;
        return node;
    }
    if (ncc_token_is(&parser->lexer.token, "for")) {
        if (!ncc_next(parser) || !ncc_expect(parser, "(")) {
            return NULL;
        }
        node = ncc_new_node(NCC_NODE_FOR);
        if (node == NULL) {
            return NULL;
        }
        if (ncc_is_type_name(parser)) {
            node->lhs = ncc_parse_declaration(parser);
        } else {
            node->lhs = ncc_new_node(NCC_NODE_EXPR_STMT);
            if (node->lhs == NULL) {
                return NULL;
            }
            if (ncc_consume(parser, ";")) {
                node->lhs->lhs = ncc_new_node(NCC_NODE_NUMBER);
                if (node->lhs->lhs != NULL) node->lhs->lhs->type.base_size = 8u;
            } else {
                node->lhs->lhs = ncc_parse_expression(parser);
                if (node->lhs->lhs == NULL || !ncc_expect(parser, ";")) {
                    return NULL;
                }
            }
        }
        if (node->lhs == NULL) {
            return NULL;
        }
        if (ncc_consume(parser, ";")) {
            node->condition = ncc_new_node(NCC_NODE_NUMBER);
            if (node->condition != NULL) {
                node->condition->value = 1;
                node->condition->type.base_size = 8u;
            }
        } else {
            node->condition = ncc_parse_expression(parser);
            if (node->condition == NULL || !ncc_expect(parser, ";")) {
                return NULL;
            }
        }
        if (!ncc_token_is(&parser->lexer.token, ")")) {
            node->rhs = ncc_parse_expression(parser);
            if (node->rhs == NULL) {
                return NULL;
            }
        }
        if (!ncc_expect(parser, ")")) {
            return NULL;
        }
        parser->loop_depth++;
        node->then_node = ncc_parse_statement(parser);
        parser->loop_depth--;
        return node->then_node != NULL ? node : NULL;
    }
    if (ncc_token_is(&parser->lexer.token, "switch")) {
        if (!ncc_next(parser) || !ncc_expect(parser, "(")) {
            return NULL;
        }
        node = ncc_new_node(NCC_NODE_SWITCH);
        if (node == NULL) {
            return NULL;
        }
        node->condition = ncc_parse_expression(parser);
        if (node->condition == NULL ||
            !ncc_expect(parser, ")") ||
            !ncc_expect(parser, "{")) {
            return NULL;
        }
        parser->switch_depth++;
        node->then_node = ncc_parse_switch_body(parser);
        parser->switch_depth--;
        if (node->then_node == NULL && parser->error[0] != '\0') {
            return NULL;
        }
        return node;
    }
    if (ncc_token_is(&parser->lexer.token, "break") ||
        ncc_token_is(&parser->lexer.token, "continue")) {
        int is_break = ncc_token_is(&parser->lexer.token, "break");

        if ((!is_break && parser->loop_depth == 0u) ||
            (is_break && parser->loop_depth == 0u && parser->switch_depth == 0u)) {
            ncc_parser_error(parser,
                             is_break ? "break outside loop" : "continue outside loop");
            return NULL;
        }
        if (!ncc_next(parser) || !ncc_expect(parser, ";")) {
            return NULL;
        }
        return ncc_new_node(is_break ? NCC_NODE_BREAK : NCC_NODE_CONTINUE);
    }
    if (ncc_is_type_name(parser)) {
        return ncc_parse_declaration(parser);
    }
    node = ncc_new_node(NCC_NODE_EXPR_STMT);
    if (node == NULL) {
        return NULL;
    }
    if (ncc_consume(parser, ";")) {
        node->lhs = ncc_new_node(NCC_NODE_NUMBER);
        return node;
    }
    node->lhs = ncc_parse_expression(parser);
    if (node->lhs == NULL || !ncc_expect(parser, ";")) {
        return NULL;
    }
    return node;
}

static int ncc_skip_top_level_declaration(struct ncc_parser *parser) {
    uint32_t depth = 0u;

    while (parser->lexer.token.kind != NCC_TOK_EOF) {
        if (ncc_token_is(&parser->lexer.token, "(")) depth++;
        if (ncc_token_is(&parser->lexer.token, ")") && depth != 0u) depth--;
        if (ncc_token_is(&parser->lexer.token, ";") && depth == 0u) {
            return ncc_next(parser);
        }
        if (!ncc_next(parser)) {
            return 0;
        }
    }
    return 1;
}

static int ncc_parse_global_constant(struct ncc_parser *parser, int64_t *value_out) {
    int negative = 0;
    int64_t value;

    if (ncc_consume(parser, "-")) {
        negative = 1;
    } else {
        (void)ncc_consume(parser, "+");
    }
    if (parser->lexer.token.kind == NCC_TOK_NUMBER ||
        parser->lexer.token.kind == NCC_TOK_CHAR) {
        value = (int64_t)parser->lexer.token.value;
        if (!ncc_next(parser)) {
            return 0;
        }
    } else if (parser->lexer.token.kind == NCC_TOK_IDENT) {
        struct ncc_enum_constant *constant =
            ncc_find_enum_constant(parser, parser->lexer.token.text);

        if (constant == NULL) {
            ncc_parser_error(parser, "global initializer must be constant");
            return 0;
        }
        value = constant->value;
        if (!ncc_next(parser)) {
            return 0;
        }
    } else {
        ncc_parser_error(parser, "global initializer must be constant");
        return 0;
    }
    *value_out = negative ? -value : value;
    return 1;
}

static int ncc_parse_global_initializer_data(struct ncc_parser *parser,
                                             struct ncc_type type,
                                             uint8_t *data,
                                             uint32_t data_size) {
    if (type.array_length != 0u) {
        struct ncc_type element = type;
        uint32_t element_size;
        uint32_t index = 0u;

        element.array_length = 0u;
        element_size = ncc_type_size(&element);
        if (element.base_size == 1u &&
            element.pointer_depth == 0u &&
            element.struct_type == NULL &&
            parser->lexer.token.kind == NCC_TOK_STRING) {
            uint32_t length = (uint32_t)strlen(parser->lexer.token.text) + 1u;

            if (length > data_size) {
                ncc_parser_error(parser, "string initializer is too long");
                return 0;
            }
            memcpy(data, parser->lexer.token.text, length);
            return ncc_next(parser);
        }
        if (!ncc_expect(parser, "{")) {
            return 0;
        }
        while (!ncc_token_is(&parser->lexer.token, "}") &&
               index < type.array_length) {
            if (!ncc_parse_global_initializer_data(parser,
                                                   element,
                                                   data + index * element_size,
                                                   element_size)) {
                return 0;
            }
            index++;
            if (!ncc_consume(parser, ",")) {
                break;
            }
        }
        return ncc_expect(parser, "}");
    }
    if (type.pointer_depth == 0u && type.struct_type != NULL) {
        struct ncc_struct_field *field = type.struct_type->fields;

        if (!ncc_expect(parser, "{")) {
            return 0;
        }
        while (!ncc_token_is(&parser->lexer.token, "}") && field != NULL) {
            uint32_t field_size = ncc_type_size(&field->type);

            if (field->offset + field_size > data_size ||
                !ncc_parse_global_initializer_data(parser,
                                                   field->type,
                                                   data + field->offset,
                                                   field_size)) {
                return 0;
            }
            field = field->next;
            if (!ncc_consume(parser, ",")) {
                break;
            }
        }
        return ncc_expect(parser, "}");
    }
    {
        int64_t value;
        uint32_t size = ncc_type_size(&type);

        if (ncc_consume(parser, "{")) {
            if (!ncc_parse_global_constant(parser, &value) ||
                !ncc_expect(parser, "}")) {
                return 0;
            }
        } else if (!ncc_parse_global_constant(parser, &value)) {
            return 0;
        }
        if (size > data_size) {
            return 0;
        }
        memcpy(data, &value, size);
        return 1;
    }
}

static int ncc_parse_global_variable(struct ncc_parser *parser,
                                     const char *name,
                                     struct ncc_type type) {
    int64_t initial_value = 0;
    int has_initializer = 0;
    uint8_t *initializer_data = NULL;
    uint32_t initializer_size = 0u;
    int unsized_array = 0;

    if (ncc_consume(parser, "[")) {
        if (ncc_consume(parser, "]")) {
            unsized_array = 1;
        } else if (parser->lexer.token.kind != NCC_TOK_NUMBER ||
                   parser->lexer.token.value == 0u ||
                   parser->lexer.token.value > 65535u) {
            ncc_parser_error(parser, "array length must be a positive constant");
            return 0;
        } else {
            type.array_length = (uint32_t)parser->lexer.token.value;
            if (!ncc_next(parser) || !ncc_expect(parser, "]")) {
                return 0;
            }
        }
    }
    if (ncc_consume(parser, "=")) {
        if (unsized_array) {
            if (type.base_size != 1u ||
                type.pointer_depth != 0u ||
                type.struct_type != NULL ||
                parser->lexer.token.kind != NCC_TOK_STRING) {
                ncc_parser_error(parser, "only char[] string size inference is supported");
                return 0;
            }
            type.array_length = (uint32_t)strlen(parser->lexer.token.text) + 1u;
        }
        initializer_size = ncc_type_size(&type);
        initializer_data = calloc(1u, initializer_size);
        if (initializer_data == NULL) {
            ncc_parser_error(parser, "out of memory");
            return 0;
        }
        if (!ncc_parse_global_initializer_data(parser,
                                               type,
                                               initializer_data,
                                               initializer_size)) {
            free(initializer_data);
            return 0;
        }
        has_initializer = 1;
        if (type.array_length == 0u &&
            type.struct_type == NULL &&
            initializer_size <= sizeof(initial_value)) {
            memcpy(&initial_value, initializer_data, initializer_size);
        }
    } else if (unsized_array) {
        ncc_parser_error(parser, "unsized array requires an initializer");
        return 0;
    }
    if (!ncc_expect(parser, ";")) {
        return 0;
    }
    if (ncc_add_global(parser,
                       name,
                       type,
                       initial_value,
                       has_initializer,
                       initializer_data,
                       initializer_size) == NULL) {
        free(initializer_data);
        return 0;
    }
    return 1;
}

static int ncc_parse_struct_body(struct ncc_parser *parser,
                                 const char *name,
                                 int expect_semicolon) {
    struct ncc_struct_type *type;
    struct ncc_struct_field **tail;
    uint32_t field_count = 0u;

    type = ncc_add_struct_type(parser, name);
    if (type == NULL) {
        return 0;
    }
    tail = &type->fields;
    while (!ncc_token_is(&parser->lexer.token, "}") &&
           parser->lexer.token.kind != NCC_TOK_EOF) {
        struct ncc_type field_type;
        char field_name[NCC_NAME_MAX + 1];
        struct ncc_struct_field *field;
        uint32_t align;
        uint32_t size;

        if (field_count >= NCC_STRUCT_FIELD_MAX ||
            !ncc_parse_type(parser, &field_type) ||
            parser->lexer.token.kind != NCC_TOK_IDENT) {
            ncc_parser_error(parser, "invalid struct field");
            return 0;
        }
        ncc_copy_text(field_name, sizeof(field_name), parser->lexer.token.text);
        if (!ncc_next(parser)) {
            return 0;
        }
        if (ncc_consume(parser, "[")) {
            if (parser->lexer.token.kind != NCC_TOK_NUMBER ||
                parser->lexer.token.value == 0u ||
                parser->lexer.token.value > 65535u) {
                ncc_parser_error(parser, "array length must be a positive constant");
                return 0;
            }
            field_type.array_length = (uint32_t)parser->lexer.token.value;
            if (!ncc_next(parser) || !ncc_expect(parser, "]")) {
                return 0;
            }
        }
        if (!ncc_expect(parser, ";")) {
            return 0;
        }
        if (ncc_find_struct_field(type, field_name) != NULL) {
            ncc_parser_error(parser, "duplicate struct field");
            return 0;
        }
        field = calloc(1u, sizeof(*field));
        if (field == NULL) {
            ncc_parser_error(parser, "out of memory");
            return 0;
        }
        align = ncc_type_align(field_type);
        size = ncc_type_size(&field_type);
        type->size = (uint32_t)ncc_align_up(type->size, align);
        field->offset = type->size;
        field->type = field_type;
        ncc_copy_text(field->name, sizeof(field->name), field_name);
        type->size += size;
        if (align > type->align) {
            type->align = align;
        }
        *tail = field;
        tail = &field->next;
        field_count++;
    }
    if (!ncc_expect(parser, "}")) {
        return 0;
    }
    if (expect_semicolon && !ncc_expect(parser, ";")) {
        return 0;
    }
    type->size = (uint32_t)ncc_align_up(type->size, type->align);
    return 1;
}

static int ncc_parse_struct_definition(struct ncc_parser *parser) {
    char name[NCC_NAME_MAX + 1];

    if (!ncc_expect(parser, "struct") ||
        parser->lexer.token.kind != NCC_TOK_IDENT) {
        ncc_parser_error(parser, "expected struct name");
        return 0;
    }
    ncc_copy_text(name, sizeof(name), parser->lexer.token.text);
    if (!ncc_next(parser) || !ncc_expect(parser, "{")) {
        return 0;
    }
    return ncc_parse_struct_body(parser, name, 1);
}

static int ncc_type_same(struct ncc_type a, struct ncc_type b) {
    return a.base_size == b.base_size &&
           a.pointer_depth == b.pointer_depth &&
           a.array_length == b.array_length &&
           a.struct_type == b.struct_type;
}

static int ncc_add_typedef(struct ncc_parser *parser,
                           const char *name,
                           struct ncc_type type) {
    struct ncc_typedef *alias;

    if (name == NULL || name[0] == '\0' ||
        ncc_find_typedef(parser, name) != NULL) {
        ncc_parser_error(parser, "duplicate typedef");
        return 0;
    }
    alias = calloc(1u, sizeof(*alias));
    if (alias == NULL) {
        ncc_parser_error(parser, "out of memory");
        return 0;
    }
    ncc_copy_text(alias->name, sizeof(alias->name), name);
    alias->type = type;
    alias->next = parser->program->typedefs;
    parser->program->typedefs = alias;
    return 1;
}

static int ncc_add_enum_constant(struct ncc_parser *parser,
                                 const char *name,
                                 int64_t value) {
    struct ncc_enum_constant *constant;

    if (name == NULL || name[0] == '\0' ||
        ncc_find_enum_constant(parser, name) != NULL) {
        ncc_parser_error(parser, "duplicate enum constant");
        return 0;
    }
    constant = calloc(1u, sizeof(*constant));
    if (constant == NULL) {
        ncc_parser_error(parser, "out of memory");
        return 0;
    }
    ncc_copy_text(constant->name, sizeof(constant->name), name);
    constant->value = value;
    constant->next = parser->program->enum_constants;
    parser->program->enum_constants = constant;
    return 1;
}

static int ncc_parse_enum_value(struct ncc_parser *parser, int64_t *value_out) {
    int negative = 0;
    int64_t value;

    if (ncc_consume(parser, "-")) {
        negative = 1;
    } else {
        (void)ncc_consume(parser, "+");
    }
    if (parser->lexer.token.kind == NCC_TOK_NUMBER ||
        parser->lexer.token.kind == NCC_TOK_CHAR) {
        value = (int64_t)parser->lexer.token.value;
        if (!ncc_next(parser)) {
            return 0;
        }
    } else if (parser->lexer.token.kind == NCC_TOK_IDENT) {
        struct ncc_enum_constant *constant =
            ncc_find_enum_constant(parser, parser->lexer.token.text);

        if (constant == NULL) {
            ncc_parser_error(parser, "enum value must be an integer constant");
            return 0;
        }
        value = constant->value;
        if (!ncc_next(parser)) {
            return 0;
        }
    } else {
        ncc_parser_error(parser, "enum value must be an integer constant");
        return 0;
    }
    *value_out = negative ? -value : value;
    return 1;
}

static int ncc_parse_enum_body(struct ncc_parser *parser) {
    int64_t next_value = 0;

    while (!ncc_token_is(&parser->lexer.token, "}") &&
           parser->lexer.token.kind != NCC_TOK_EOF) {
        char name[NCC_NAME_MAX + 1];
        int64_t value = next_value;

        if (parser->lexer.token.kind != NCC_TOK_IDENT) {
            ncc_parser_error(parser, "expected enum constant");
            return 0;
        }
        ncc_copy_text(name, sizeof(name), parser->lexer.token.text);
        if (!ncc_next(parser)) {
            return 0;
        }
        if (ncc_consume(parser, "=") &&
            !ncc_parse_enum_value(parser, &value)) {
            return 0;
        }
        if (!ncc_add_enum_constant(parser, name, value)) {
            return 0;
        }
        next_value = value + 1;
        if (!ncc_consume(parser, ",")) {
            break;
        }
    }
    return ncc_expect(parser, "}");
}

static int ncc_parse_enum_definition(struct ncc_parser *parser) {
    if (!ncc_expect(parser, "enum")) {
        return 0;
    }
    if (parser->lexer.token.kind == NCC_TOK_IDENT &&
        !ncc_next(parser)) {
        return 0;
    }
    if (!ncc_expect(parser, "{") ||
        !ncc_parse_enum_body(parser) ||
        !ncc_expect(parser, ";")) {
        return 0;
    }
    return 1;
}

static int ncc_parse_typedef(struct ncc_parser *parser) {
    struct ncc_type type;
    char name[NCC_NAME_MAX + 1];

    if (!ncc_expect(parser, "typedef")) {
        return 0;
    }
    if (ncc_token_is(&parser->lexer.token, "struct")) {
        struct ncc_token saved = parser->lexer.token;
        uint32_t saved_offset = parser->lexer.offset;
        uint32_t saved_line = parser->lexer.line;
        char struct_name[NCC_NAME_MAX + 1];

        if (!ncc_next(parser)) {
            return 0;
        }
        if (parser->lexer.token.kind == NCC_TOK_IDENT) {
            ncc_copy_text(struct_name, sizeof(struct_name), parser->lexer.token.text);
            if (!ncc_next(parser)) {
                return 0;
            }
            if (ncc_consume(parser, "{")) {
                if (!ncc_parse_struct_body(parser, struct_name, 0)) {
                    return 0;
                }
                memset(&type, 0, sizeof(type));
                type.struct_type = ncc_find_struct(parser, struct_name);
                if (parser->lexer.token.kind != NCC_TOK_IDENT) {
                    ncc_parser_error(parser, "expected typedef name");
                    return 0;
                }
                ncc_copy_text(name, sizeof(name), parser->lexer.token.text);
                return ncc_next(parser) &&
                       ncc_expect(parser, ";") &&
                       ncc_add_typedef(parser, name, type);
            }
        }
        parser->lexer.token = saved;
        parser->lexer.offset = saved_offset;
        parser->lexer.line = saved_line;
    }
    if (ncc_token_is(&parser->lexer.token, "enum")) {
        struct ncc_token saved = parser->lexer.token;
        uint32_t saved_offset = parser->lexer.offset;
        uint32_t saved_line = parser->lexer.line;

        if (!ncc_next(parser)) {
            return 0;
        }
        if (parser->lexer.token.kind == NCC_TOK_IDENT &&
            !ncc_next(parser)) {
            return 0;
        }
        if (ncc_consume(parser, "{")) {
            if (!ncc_parse_enum_body(parser)) {
                return 0;
            }
            memset(&type, 0, sizeof(type));
            type.base_size = 8u;
            if (parser->lexer.token.kind != NCC_TOK_IDENT) {
                ncc_parser_error(parser, "expected typedef name");
                return 0;
            }
            ncc_copy_text(name, sizeof(name), parser->lexer.token.text);
            return ncc_next(parser) &&
                   ncc_expect(parser, ";") &&
                   ncc_add_typedef(parser, name, type);
        }
        parser->lexer.token = saved;
        parser->lexer.offset = saved_offset;
        parser->lexer.line = saved_line;
    }
    if (!ncc_parse_type(parser, &type) ||
        parser->lexer.token.kind != NCC_TOK_IDENT) {
        ncc_parser_error(parser, "invalid typedef");
        return 0;
    }
    ncc_copy_text(name, sizeof(name), parser->lexer.token.text);
    if (!ncc_next(parser)) {
        return 0;
    }
    if (ncc_consume(parser, "[")) {
        if (parser->lexer.token.kind != NCC_TOK_NUMBER ||
            parser->lexer.token.value == 0u) {
            ncc_parser_error(parser, "typedef array length must be positive");
            return 0;
        }
        type.array_length = (uint32_t)parser->lexer.token.value;
        if (!ncc_next(parser) || !ncc_expect(parser, "]")) {
            return 0;
        }
    }
    return ncc_expect(parser, ";") &&
           ncc_add_typedef(parser, name, type);
}

static struct ncc_function *ncc_find_function(struct ncc_program *program,
                                              const char *name) {
    struct ncc_function *function;

    if (program == NULL || name == NULL) {
        return NULL;
    }
    function = program->functions;
    while (function != NULL) {
        if (strcmp(function->name, name) == 0) {
            return function;
        }
        function = function->next;
    }
    return NULL;
}

static int ncc_function_signature_matches(struct ncc_function *function,
                                          struct ncc_type return_type,
                                          const struct ncc_type *param_types,
                                          uint32_t param_count) {
    if (function == NULL ||
        !ncc_type_same(function->return_type, return_type) ||
        function->param_count != param_count) {
        return 0;
    }
    for (uint32_t i = 0u; i < param_count; i++) {
        if (!ncc_type_same(function->param_types[i], param_types[i])) {
            return 0;
        }
    }
    return 1;
}

static struct ncc_function *ncc_add_function_header(
    struct ncc_parser *parser,
    struct ncc_program *program,
    const char *name,
    struct ncc_type return_type,
    const char param_names[NCC_PARAM_MAX][NCC_NAME_MAX + 1],
    const struct ncc_type *param_types,
    uint32_t param_count,
    int prototype_only) {
    struct ncc_function *function = ncc_find_function(program, name);

    if (function != NULL) {
        if (!ncc_function_signature_matches(function, return_type, param_types, param_count)) {
            ncc_parser_error(parser, "function prototype does not match definition");
            return NULL;
        }
        if (!prototype_only && !function->prototype_only) {
            ncc_parser_error(parser, "duplicate function definition");
            return NULL;
        }
        if (!prototype_only) {
            function->prototype_only = 0u;
            for (uint32_t i = 0u; i < param_count; i++) {
                ncc_copy_text(function->params[i],
                              sizeof(function->params[i]),
                              param_names[i]);
            }
        }
        return function;
    }
    function = calloc(1u, sizeof(*function));
    if (function == NULL) {
        ncc_parser_error(parser, "out of memory");
        return NULL;
    }
    ncc_copy_text(function->name, sizeof(function->name), name);
    function->return_type = return_type;
    function->param_count = param_count;
    function->prototype_only = prototype_only ? 1u : 0u;
    for (uint32_t i = 0u; i < param_count; i++) {
        ncc_copy_text(function->params[i], sizeof(function->params[i]), param_names[i]);
        function->param_types[i] = param_types[i];
    }
    function->next = program->functions;
    program->functions = function;
    return function;
}

static int ncc_parse_function_or_declaration(struct ncc_parser *parser,
                                             struct ncc_program *program) {
    char name[NCC_NAME_MAX + 1];
    struct ncc_type return_type;
    struct ncc_function *function;
    char param_names[NCC_PARAM_MAX][NCC_NAME_MAX + 1];
    struct ncc_type param_types[NCC_PARAM_MAX];
    uint32_t param_count = 0u;

    if (ncc_token_is(&parser->lexer.token, "typedef")) {
        return ncc_parse_typedef(parser);
    }
    if (ncc_token_is(&parser->lexer.token, "extern") ||
        ncc_token_is(&parser->lexer.token, "static")) {
        return ncc_skip_top_level_declaration(parser);
    }
    if (ncc_token_is(&parser->lexer.token, "struct")) {
        struct ncc_token saved = parser->lexer.token;
        uint32_t saved_offset = parser->lexer.offset;
        uint32_t saved_line = parser->lexer.line;

        if (!ncc_next(parser)) {
            return 0;
        }
        if (parser->lexer.token.kind == NCC_TOK_IDENT) {
            if (!ncc_next(parser)) {
                return 0;
            }
            if (ncc_token_is(&parser->lexer.token, "{")) {
                parser->lexer.token = saved;
                parser->lexer.offset = saved_offset;
                parser->lexer.line = saved_line;
                return ncc_parse_struct_definition(parser);
            }
        }
        parser->lexer.token = saved;
        parser->lexer.offset = saved_offset;
        parser->lexer.line = saved_line;
    }
    if (ncc_token_is(&parser->lexer.token, "enum")) {
        struct ncc_token saved = parser->lexer.token;
        uint32_t saved_offset = parser->lexer.offset;
        uint32_t saved_line = parser->lexer.line;

        if (!ncc_next(parser)) {
            return 0;
        }
        if (parser->lexer.token.kind == NCC_TOK_IDENT &&
            !ncc_next(parser)) {
            return 0;
        }
        if (ncc_token_is(&parser->lexer.token, "{")) {
            parser->lexer.token = saved;
            parser->lexer.offset = saved_offset;
            parser->lexer.line = saved_line;
            return ncc_parse_enum_definition(parser);
        }
        parser->lexer.token = saved;
        parser->lexer.offset = saved_offset;
        parser->lexer.line = saved_line;
    }
    if (!ncc_parse_type(parser, &return_type) || parser->lexer.token.kind != NCC_TOK_IDENT) {
        ncc_parser_error(parser, "expected function declaration");
        return 0;
    }
    ncc_copy_text(name, sizeof(name), parser->lexer.token.text);
    if (!ncc_next(parser)) {
        return 0;
    }
    if (!ncc_consume(parser, "(")) {
        return ncc_parse_global_variable(parser, name, return_type);
    }
    if (!ncc_token_is(&parser->lexer.token, ")")) {
        if (ncc_token_is(&parser->lexer.token, "void")) {
            if (!ncc_next(parser)) {
                return 0;
            }
        } else {
            for (;;) {
                if (param_count >= NCC_PARAM_MAX ||
                    !ncc_parse_type(parser, &param_types[param_count])) {
                    ncc_parser_error(parser, "invalid parameter list");
                    return 0;
                }
                if (parser->lexer.token.kind == NCC_TOK_IDENT) {
                    ncc_copy_text(param_names[param_count],
                                  sizeof(param_names[param_count]),
                                  parser->lexer.token.text);
                    if (!ncc_next(parser)) {
                        return 0;
                    }
                } else {
                    param_names[param_count][0] = '\0';
                }
                if (ncc_consume(parser, "[")) {
                    if (!ncc_expect(parser, "]")) {
                        return 0;
                    }
                    param_types[param_count].pointer_depth++;
                }
                param_count++;
                if (!ncc_consume(parser, ",")) {
                    break;
                }
            }
        }
    }
    if (!ncc_expect(parser, ")")) {
        return 0;
    }
    if (ncc_consume(parser, ";")) {
        return ncc_add_function_header(parser,
                                       program,
                                       name,
                                       return_type,
                                       param_names,
                                       param_types,
                                       param_count,
                                       1) != NULL;
    }
    if (!ncc_expect(parser, "{")) {
        return 0;
    }
    function = ncc_add_function_header(parser,
                                       program,
                                       name,
                                       return_type,
                                       param_names,
                                       param_types,
                                       param_count,
                                       0);
    if (function == NULL) {
        return 0;
    }
    parser->current_function = function;
    for (uint32_t i = 0u; i < param_count; i++) {
        struct ncc_local *local;

        if (param_names[i][0] == '\0') {
            snprintf(param_names[i], sizeof(param_names[i]), ".arg%u", i);
        }
        ncc_copy_text(function->params[i], sizeof(function->params[i]), param_names[i]);
        function->param_types[i] = param_types[i];
        local = ncc_add_local(parser, param_names[i], param_types[i]);
        if (local == NULL) {
            return 0;
        }
    }
    function->body = ncc_parse_block_after_open(parser);
    parser->current_function = NULL;
    if (function->body == NULL) {
        return 0;
    }
    return 1;
}

static void ncc_free_node(struct ncc_node *node) {
    while (node != NULL) {
        struct ncc_node *next = node->next;

        ncc_free_node(node->lhs);
        ncc_free_node(node->rhs);
        ncc_free_node(node->condition);
        ncc_free_node(node->then_node);
        ncc_free_node(node->else_node);
        for (uint32_t i = 0u; i < node->arg_count; i++) {
            ncc_free_node(node->args[i]);
        }
        free(node->string_value);
        free(node);
        node = next;
    }
}

void ncc_program_destroy(struct ncc_program *program) {
    struct ncc_function *function;
    struct ncc_global *global;
    struct ncc_struct_type *struct_type;
    struct ncc_typedef *alias;
    struct ncc_enum_constant *constant;

    if (program == NULL) {
        return;
    }
    function = program->functions;
    while (function != NULL) {
        struct ncc_function *next = function->next;
        struct ncc_local *local = function->locals;

        while (local != NULL) {
            struct ncc_local *local_next = local->next;
            free(local);
            local = local_next;
        }
        ncc_free_node(function->body);
        free(function);
        function = next;
    }
    global = program->globals;
    while (global != NULL) {
        struct ncc_global *next = global->next;

        free(global->initializer_data);
        free(global);
        global = next;
    }
    struct_type = program->structs;
    while (struct_type != NULL) {
        struct ncc_struct_type *next = struct_type->next;
        struct ncc_struct_field *field = struct_type->fields;

        while (field != NULL) {
            struct ncc_struct_field *field_next = field->next;

            free(field);
            field = field_next;
        }
        free(struct_type);
        struct_type = next;
    }
    alias = program->typedefs;
    while (alias != NULL) {
        struct ncc_typedef *next = alias->next;

        free(alias);
        alias = next;
    }
    constant = program->enum_constants;
    while (constant != NULL) {
        struct ncc_enum_constant *next = constant->next;

        free(constant);
        constant = next;
    }
    program->functions = NULL;
    program->globals = NULL;
    program->structs = NULL;
    program->typedefs = NULL;
    program->enum_constants = NULL;
}

int ncc_parse_file(const char *path, struct ncc_program *program, char *error, uint32_t error_size) {
    struct ncc_parser *parser;

    memset(program, 0, sizeof(*program));
    parser = calloc(1u, sizeof(*parser));
    if (parser == NULL) {
        ncc_copy_text(error, error_size, "out of memory");
        return 0;
    }
    if (!ncc_lexer_init(&parser->lexer, path)) {
        ncc_copy_text(error, error_size, parser->lexer.error);
        free(parser);
        return 0;
    }
    parser->program = program;
    while (parser->lexer.token.kind != NCC_TOK_EOF) {
        if (!ncc_parse_function_or_declaration(parser, program)) {
            ncc_copy_text(error,
                          error_size,
                          parser->error[0] != '\0' ? parser->error : parser->lexer.error);
            ncc_lexer_destroy(&parser->lexer);
            free(parser);
            ncc_program_destroy(program);
            return 0;
        }
    }
    ncc_lexer_destroy(&parser->lexer);
    free(parser);
    if (program->functions == NULL) {
        ncc_copy_text(error, error_size, "no function definitions");
        return 0;
    }
    return 1;
}
