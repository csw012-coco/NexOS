# NexOS C Style

## Goal

This document defines the default C code style for NexOS.

The goal is simple:

`make new code look like it belongs in the tree already`

This is a conservative kernel-oriented C style:
- C89/C99-era discipline
- small helpers
- shallow nesting
- explicit ownership
- readable diffs

Naming-specific rules that cut across subsystems remain in
[naming_conventions.md](/home/csw012/nos/docs/naming_conventions.md).

## Core Rules

- Use `NULL` for pointers.
- Use `0` for integer zero values.
- Prefer fixed-width integer types such as `uint32_t`, `uint64_t`, `int32_t`.
- Use `snake_case` for functions and variables.
- Use `UPPER_SNAKE_CASE` for macros, enum constants, and compile-time constants.
- File-local helpers should use suffixes such as `*_local`.
- Internal-only cross-file helpers should use `*_internal` or live behind internal headers.
- Prefer early returns over deep nesting.
- Prefer small helpers over long multi-purpose functions.
- Use spaces for indentation, not tabs.
- Use K&R-style braces.

## Formatting

### Indentation

- Use 4 spaces per indentation level.
- Do not use tabs for alignment or indentation.
- Keep continuation lines readable rather than vertically over-aligned.

Good:

```c
if (proc == NULL) {
    return 0;
}
```

Bad:

```c
if (proc == NULL) {
\treturn 0;
}
```

### Braces

- Opening braces stay on the same line for functions and control flow.
- Always use braces for `if`, `else`, `for`, and `while` bodies.

Good:

```c
static int process_ready_local(struct process *proc) {
    if (proc == NULL) {
        return 0;
    }
    return proc->state == PROCESS_STATE_READY;
}
```

Bad:

```c
static int process_ready_local(struct process *proc)
{
    if (proc == NULL)
        return 0;
    return proc->state == PROCESS_STATE_READY;
}
```

### Line Width

- Prefer roughly 100 columns as a soft limit.
- If a line becomes hard to scan, wrap it.
- When wrapping function arguments, prefer one argument per line if needed.

Good:

```c
if (!process_prepare_arguments(command_line != NULL ? command_line : name83,
                               envp,
                               &stack_top)) {
    return 0;
}
```

## Types and Values

### Pointer and Integer Nullability

- Use `NULL` for pointers.
- Use `0` for integer and enum zero values.
- Do not use `NULL` for integers.

Good:

```c
struct process *proc = NULL;
uint32_t count = 0;
```

Bad:

```c
struct process *proc = 0;
uint32_t count = NULL;
```

### Integer Types

- Prefer `uint32_t` and `uint64_t` for kernel and ABI-facing code.
- Use signed fixed-width types when negative values are meaningful.
- Avoid plain `int`, `long`, and `unsigned long` unless the API truly wants them.

## Naming

### Functions and Variables

- Use `snake_case`.
- Names should reflect ownership and intent.

Good:

```c
static int ush_expand_command_text_local(const char *text, char *out, uint32_t out_size);
uint32_t process_program_count(void);
```

Bad:

```c
static int ExpandText(const char *text, char *out, uint32_t out_size);
uint32_t Count(void);
```

### Helper Suffixes

- File-local helpers: `*_local`
- Internal subsystem helpers: `*_internal`

Examples:

```c
static int text_contains_local(const char *text, const char *pattern);
int fs_service_open_internal(struct process *proc, const char *path);
```

### Constants and Macros

- Use `UPPER_SNAKE_CASE`.
- Macro names should describe policy or limits, not temporary usage.

Examples:

```c
#define USH_LINE_MAX 63u
#define CMD_PATH_MAX 64u
```

## Function Style

### Early Return

- Prefer early return to reduce indentation.
- Validate arguments at the top.

Good:

```c
if (path == NULL || path[0] == '\0') {
    return -1;
}
```

Bad:

```c
if (path != NULL && path[0] != '\0') {
    /* main logic */
}
return -1;
```

### Small Helpers

- Split parsing, formatting, lookup, and mutation into separate helpers.
- Do not keep unrelated logic in one function just to avoid creating helpers.

Prefer:
- `*_parse_*`
- `*_lookup_*`
- `*_copy_*`
- `*_apply_*`
- `*_reset_*`

### Variable Declarations

- Prefer one declaration per line when it improves readability.
- Keep declarations near the top of the function in conservative code paths.
- Avoid mixing unrelated declarations and executable logic in a confusing way.

Good:

```c
uint32_t i;
uint32_t out_len = 0;
int quoted = 0;
```

## Control Flow

- Prefer simple loops and explicit state transitions.
- Avoid clever one-liners.
- Avoid hidden fallthrough unless it is documented and obvious.

Prefer:

```c
if (fd < 0) {
    return 0;
}
```

Over:

```c
return fd >= 0;
```

When the explicit branch is easier to scan in context.

## Comments

- Write comments for constraints, invariants, ownership, or non-obvious intent.
- Do not comment what the code plainly says.
- Keep comments short and specific.

Good:

```c
/* Keep redirection targets out of glob expansion. */
```

Bad:

```c
/* Increment i by one. */
i++;
```

## File Layout

Within a `.c` file, prefer this order:

1. includes
2. file-local constants and structs
3. low-level helpers
4. parsing/validation helpers
5. lookup helpers
6. state mutation helpers
7. exported functions

This matches the current NexOS direction and keeps readers moving from simple to complex.

## Preferred Patterns

### Good

```c
static int cmd_open_path_local(const char *path, uint32_t flags) {
    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    return open(path, flags);
}
```

```c
if (!ush_expand_command_text_local(spec->command, expanded, sizeof(expanded))) {
    write_err_str("expand error\n");
    return 0;
}
```

### Avoid

```c
static int helper(const char *p, uint32_t f) {
    return p ? open(p, f) : -1;
}
```

```c
if (cond) {
    if (other_cond) {
        if (third_cond) {
            /* deep nesting */
        }
    }
}
```

## Review Checklist

When adding or editing C code, check:

1. Are pointers using `NULL` and integers using `0`?
2. Are names `snake_case` and constants `UPPER_SNAKE_CASE`?
3. Are file-local helpers marked with `*_local` when appropriate?
4. Are braces K&R-style and indentation spaces-only?
5. Can any deep nesting become early returns?
6. Can any long function be split into smaller helpers?
7. Are comments explaining constraints instead of restating code?

## Default Rule

If a new piece of code can be written in multiple acceptable styles, choose the one that looks most like nearby NexOS code.
