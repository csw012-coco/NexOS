# NexOS native C toolchain

The native compiler is a NexOS-specific compiler, not a port of GCC, Clang, or
TinyCC. TinyCC is useful as a reference for a small single-process compiler and
linker, but the NexOS compiler owns its parser, x86-64 code generator, ELF
writer, and static linker.

## Command line

The intended interface is:

```sh
ncc hello.c -o hello
./hello
```

Later, direct compile-and-run can be exposed as:

```sh
ncc -run hello.c
```

## Installed SDK

Native development files live outside the boot RAM disk:

```text
/system/devel/
  include/
    stdio.h
    stdlib.h
    stdint.h
    ...
    nexos/
    sys/
    user/public/sysapi.h
    abi/syscall_abi.h
  lib/
    crt0.o
    libc_start.o
    libnlibc.a
    nexos.ld
  README.txt
```

The compiler uses `/system/devel/include` and `/system/devel/lib` by default.

## Target ABI

- Architecture: x86-64
- Calling convention: System V AMD64 for C functions
- Executable format: static ELF64 `ET_EXEC`
- Default image base: `0x0000008000000000`
- Entry symbol: `_start`
- Runtime: `crt0.o`, `libc_start.o`, and `libnlibc.a`
- Dynamic linking: unsupported

NexOS system calls use interrupt `0x40`. The syscall number is passed in
`rax`; arguments 0 through 3 are passed in `rbx`, `rcx`, `rdx`, and `rsi`.

## Compiler stages

The initial compiler stays deliberately small:

1. Tokenizer and object-like/function-like macro preprocessor.
2. C parser with declarations, expressions, statements, functions, structs,
   pointers, arrays, and integer types.
3. Direct x86-64 code generation into in-memory sections.
4. ELF64 relocatable-object reader and static archive reader.
5. Static linker for the relocation types emitted by the compiler and nlibc.
6. ELF64 executable writer with separate RX and RW `PT_LOAD` segments.

The first useful milestone is compiling and linking:

```c
#include <stdio.h>

int main(void) {
    printf("Hello from native NexOS C!\n");
    return 0;
}
```

Supporting existing `libnlibc.a` requires at least the x86-64 relocations used
by the current cross-compiler output, including `R_X86_64_64`, `R_X86_64_PC32`,
and `R_X86_64_PLT32`.

## Initial implementation

The first `ncc` implementation supports:

- recursive `#include "..."` expansion relative to the including file
- `#include <...>` lookup in `/system/devel/include` (SDK macros are imported)
- integer and character constants
- string literals
- functions, prototypes, and up to six integer/pointer arguments
- local/global variables and assignment
- `char`, pointer declarations, address-of, and dereference
- named `struct` types with `.` and `->` member access
- `typedef` aliases for scalar, pointer, array, enum, and named struct types
- named and anonymous `enum` definitions with integer constants
- fixed-size local/global arrays and array subscripting
- local/global array and struct initializer lists with zero-filled remainder
- `char[]` size inference from string literal initializers
- pointer arithmetic scaled by the pointed-to element size
- prefix and postfix `++`/`--`, plus `+=` and `-=`
- `sizeof` for expressions and simple type names
- object-like `#define` macros in the current source file
- arithmetic, `%`, integer comparisons, and short-circuit `&&`/`||`
- bitwise `&`, `|`, `^`, `~`, `<<`, `>>` and their compound assignments
- right-associative conditional expressions with `?:`
- `return`, `if`/`else`, `while`, and `for`
- `switch`, `case`, `default`, fallthrough, and switch-local `break`
- loop-local `break` and `continue`, including nested loops
- calls to nlibc and user-defined functions
- static linking of ELF64 relocatable objects and GNU `.a` archives
- `R_X86_64_64`, `R_X86_64_PC32`, and `R_X86_64_PLT32`
- two-segment static NexOS ELF64 output

The initial linker includes every object from `libnlibc.a`. Symbol-driven
archive extraction and dead-code elimination are later optimizations.

`char` currently uses unsigned one-byte load semantics. Arrays currently
support positive constant lengths. `struct` support currently covers named
definitions, variables, arrays, `sizeof`, `.`, and `->`; struct initializers,
anonymous structs, and bitfields remain future work. `#define` supports
object-like replacement across the source and included headers. Function-like
macros remain future work. Simple SDK type headers such as `stdint.h`,
`stddef.h`, and `sys/types.h` contribute declarations; larger nlibc headers
still contribute macros only. Aggregate initializers remain future work.
