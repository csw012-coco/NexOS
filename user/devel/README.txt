NexOS native development kit
============================

include/
  Public nlibc, NexOS and ABI headers.

lib/crt0.o
  ELF process entry point.

lib/libc_start.o
  Initializes nlibc and calls main(argc, argv, envp).

lib/libnlibc.a
  NexOS static C library.

lib/nexos.ld
  Default static ELF64 executable layout.

Native compiler defaults:

  include path: /system/devel/include
  library path: /system/devel/lib
  ELF base:     0x0000008000000000
  entry:        _start
  link order:   crt0.o libc_start.o program objects libnlibc.a

The first NexOS compiler will be static-only and will emit ELF64 ET_EXEC files
accepted directly by the NexOS process loader.
