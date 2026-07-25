# =========================
# User libc sources
# =========================

USER_NLIBC_C_SRCS := \
	user/libc/sys/syscall.c \
	user/libc/std/string.c \
	user/libc/std/io.c \
	user/libc/std/printf.c \
	user/libc/std/stdio_scan.c \
	user/libc/std/env.c \
	user/libc/std/malloc.c \
	user/libc/std/stdlib.c

USER_NLIBC_ASM_SRCS := \
	user/libc/sys/arch/x86/syscall.S

USER_CRT_C_SRCS := \
	user/libc/crt/libc_start.c

USER_CRT_ASM_SRCS := \
	user/libc/crt/crt0.S

USER_NLIBC_C_OBJS := $(addprefix $(BUILD)/,$(USER_NLIBC_C_SRCS:.c=.o))
USER_NLIBC_ASM_OBJS := $(addprefix $(BUILD)/,$(USER_NLIBC_ASM_SRCS:.S=.o))
USER_CRT_C_OBJS := $(addprefix $(BUILD)/,$(USER_CRT_C_SRCS:.c=.o))
USER_CRT_ASM_OBJS := $(addprefix $(BUILD)/,$(USER_CRT_ASM_SRCS:.S=.o))

USER_NLIBC_OBJS := $(USER_NLIBC_C_OBJS) $(USER_NLIBC_ASM_OBJS)
USER_NLIBC := $(BUILD)/libnlibc.a
USER_CRT0 := $(BUILD)/user/libc/crt/crt0.o
USER_CRT_START := $(BUILD)/user/libc/crt/libc_start.o
