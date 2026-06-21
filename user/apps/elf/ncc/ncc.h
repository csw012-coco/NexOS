#pragma once

#include "user/libc/include/nlibc.h"

enum {
    NCC_SOURCE_MAX = 65535,
    NCC_TOKEN_TEXT_MAX = 63,
    NCC_NAME_MAX = 63,
    NCC_PATH_MAX = 255,
    NCC_MACRO_MAX = 1024,
    NCC_MACRO_BODY_MAX = 255,
    NCC_INCLUDE_MAX = 64,
    NCC_INCLUDE_DEPTH_MAX = 16,
    NCC_PARAM_MAX = 6,
    NCC_ARG_MAX = 6,
    NCC_SECTION_MAX = 32,
    NCC_SYMBOL_MAX = 768,
    NCC_RELOC_MAX = 2048,
    NCC_OBJECT_MAX = 16,
    NCC_GLOBAL_MAX = 1024,
    NCC_STRUCT_FIELD_MAX = 64,
    NCC_ELF_BASE = 0x0000008000000000ull
};

enum ncc_token_kind {
    NCC_TOK_EOF,
    NCC_TOK_IDENT,
    NCC_TOK_NUMBER,
    NCC_TOK_STRING,
    NCC_TOK_CHAR,
    NCC_TOK_PUNCT
};

struct ncc_token {
    enum ncc_token_kind kind;
    char text[NCC_TOKEN_TEXT_MAX + 1];
    uint64_t value;
    uint32_t line;
};

struct ncc_lexer {
    char *source;
    uint32_t length;
    uint32_t offset;
    uint32_t line;
    struct ncc_token token;
    char error[160];
};

enum ncc_node_kind {
    NCC_NODE_NUMBER,
    NCC_NODE_STRING,
    NCC_NODE_VARIABLE,
    NCC_NODE_CALL,
    NCC_NODE_ASSIGN,
    NCC_NODE_ADD_ASSIGN,
    NCC_NODE_SUB_ASSIGN,
    NCC_NODE_AND_ASSIGN,
    NCC_NODE_OR_ASSIGN,
    NCC_NODE_XOR_ASSIGN,
    NCC_NODE_SHL_ASSIGN,
    NCC_NODE_SHR_ASSIGN,
    NCC_NODE_PRE_INC,
    NCC_NODE_PRE_DEC,
    NCC_NODE_POST_INC,
    NCC_NODE_POST_DEC,
    NCC_NODE_ADD,
    NCC_NODE_SUB,
    NCC_NODE_MUL,
    NCC_NODE_DIV,
    NCC_NODE_MOD,
    NCC_NODE_BIT_AND,
    NCC_NODE_BIT_OR,
    NCC_NODE_BIT_XOR,
    NCC_NODE_SHL,
    NCC_NODE_SHR,
    NCC_NODE_LOGICAL_AND,
    NCC_NODE_LOGICAL_OR,
    NCC_NODE_CONDITIONAL,
    NCC_NODE_EQ,
    NCC_NODE_NE,
    NCC_NODE_LT,
    NCC_NODE_LE,
    NCC_NODE_NEG,
    NCC_NODE_NOT,
    NCC_NODE_BIT_NOT,
    NCC_NODE_ADDRESS,
    NCC_NODE_DEREFERENCE,
    NCC_NODE_INDEX,
    NCC_NODE_MEMBER,
    NCC_NODE_RETURN,
    NCC_NODE_EXPR_STMT,
    NCC_NODE_BLOCK,
    NCC_NODE_IF,
    NCC_NODE_WHILE,
    NCC_NODE_FOR,
    NCC_NODE_SWITCH,
    NCC_NODE_CASE,
    NCC_NODE_DEFAULT,
    NCC_NODE_BREAK,
    NCC_NODE_CONTINUE
};

struct ncc_type {
    uint8_t base_size;
    uint8_t pointer_depth;
    uint16_t reserved;
    uint32_t array_length;
    struct ncc_struct_type *struct_type;
};

struct ncc_struct_field {
    char name[NCC_NAME_MAX + 1];
    struct ncc_type type;
    uint32_t offset;
    struct ncc_struct_field *next;
};

struct ncc_struct_type {
    char name[NCC_NAME_MAX + 1];
    struct ncc_struct_field *fields;
    uint32_t size;
    uint32_t align;
    struct ncc_struct_type *next;
};

struct ncc_node {
    enum ncc_node_kind kind;
    struct ncc_node *lhs;
    struct ncc_node *rhs;
    struct ncc_node *condition;
    struct ncc_node *then_node;
    struct ncc_node *else_node;
    struct ncc_node *next;
    struct ncc_node *args[NCC_ARG_MAX];
    uint32_t arg_count;
    int64_t value;
    int32_t stack_offset;
    uint32_t field_offset;
    uint8_t is_global;
    char name[NCC_NAME_MAX + 1];
    char *string_value;
    uint32_t string_length;
    struct ncc_type type;
};

struct ncc_local {
    char name[NCC_NAME_MAX + 1];
    int32_t stack_offset;
    struct ncc_type type;
    struct ncc_local *next;
};

struct ncc_function {
    char name[NCC_NAME_MAX + 1];
    char params[NCC_PARAM_MAX][NCC_NAME_MAX + 1];
    struct ncc_type return_type;
    struct ncc_type param_types[NCC_PARAM_MAX];
    uint32_t param_count;
    uint8_t prototype_only;
    uint32_t stack_size;
    struct ncc_local *locals;
    struct ncc_node *body;
    struct ncc_function *next;
};

struct ncc_global {
    char name[NCC_NAME_MAX + 1];
    struct ncc_type type;
    int64_t initial_value;
    uint8_t *initializer_data;
    uint32_t initializer_size;
    uint8_t has_initializer;
    struct ncc_global *next;
};

struct ncc_typedef {
    char name[NCC_NAME_MAX + 1];
    struct ncc_type type;
    struct ncc_typedef *next;
};

struct ncc_enum_constant {
    char name[NCC_NAME_MAX + 1];
    int64_t value;
    struct ncc_enum_constant *next;
};

struct ncc_program {
    struct ncc_function *functions;
    struct ncc_global *globals;
    struct ncc_struct_type *structs;
    struct ncc_typedef *typedefs;
    struct ncc_enum_constant *enum_constants;
};

struct ncc_parser {
    struct ncc_lexer lexer;
    struct ncc_program *program;
    struct ncc_function *current_function;
    uint32_t loop_depth;
    uint32_t switch_depth;
    char error[160];
};

struct ncc_buffer {
    uint8_t *data;
    uint32_t size;
    uint32_t capacity;
};

enum ncc_section_kind {
    NCC_SEC_TEXT,
    NCC_SEC_RODATA,
    NCC_SEC_DATA,
    NCC_SEC_BSS
};

struct ncc_section {
    enum ncc_section_kind kind;
    struct ncc_buffer bytes;
    uint64_t size;
    uint64_t align;
    uint64_t output_offset;
};

struct ncc_symbol {
    char name[NCC_NAME_MAX + 1];
    int32_t section;
    uint64_t value;
    uint64_t size;
    uint8_t global;
    uint8_t weak;
};

struct ncc_reloc {
    int32_t section;
    uint64_t offset;
    uint32_t type;
    uint32_t symbol;
    int64_t addend;
};

struct ncc_object {
    char name[NCC_NAME_MAX + 1];
    struct ncc_section *sections;
    uint32_t section_count;
    struct ncc_symbol *symbols;
    uint32_t symbol_count;
    struct ncc_reloc *relocs;
    uint32_t reloc_count;
    uint8_t *owned_image;
};

struct ncc_codegen {
    struct ncc_object object;
    uint32_t string_index;
    char error[160];
};

int ncc_read_file(const char *path, uint8_t **data_out, uint32_t *size_out);
int ncc_write_all(int fd, const void *data, uint32_t size);
void ncc_copy_text(char *dst, uint32_t dst_size, const char *src);
uint64_t ncc_align_up(uint64_t value, uint64_t align);

int ncc_lexer_init(struct ncc_lexer *lexer, const char *path);
void ncc_lexer_destroy(struct ncc_lexer *lexer);
int ncc_lexer_next(struct ncc_lexer *lexer);
int ncc_token_is(const struct ncc_token *token, const char *text);

int ncc_parse_file(const char *path, struct ncc_program *program, char *error, uint32_t error_size);
void ncc_program_destroy(struct ncc_program *program);

int ncc_codegen_program(struct ncc_program *program,
                        struct ncc_object *object_out,
                        char *error,
                        uint32_t error_size);
void ncc_object_destroy(struct ncc_object *object);

int ncc_load_elf_object(const char *path,
                        struct ncc_object *object,
                        char *error,
                        uint32_t error_size);
int ncc_load_archive(const char *path,
                     struct ncc_object *objects,
                     uint32_t object_capacity,
                     uint32_t *object_count,
                     char *error,
                     uint32_t error_size);
int ncc_link_executable(const char *output_path,
                        struct ncc_object *objects,
                        uint32_t object_count,
                        char *error,
                        uint32_t error_size);
