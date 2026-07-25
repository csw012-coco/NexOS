# Architecture selection, compiler, and linker defaults.

ARCH ?= $(if $(arch),$(arch),x86_64)

ifeq ($(ARCH),i386)
ARCH_BUILD_TARGET := kernel-i386
ARCH_CHECK_TARGET := check-i386
ARCH_RUN_TARGET := run-i386
else ifeq ($(ARCH),x86_64)
ARCH_BUILD_TARGET := all-x86_64
ARCH_CHECK_TARGET := check-x86_64
ARCH_RUN_TARGET := run-x86_64
else ifeq ($(ARCH),x64)
ARCH_BUILD_TARGET := all-x86_64
ARCH_CHECK_TARGET := check-x86_64
ARCH_RUN_TARGET := run-x86_64
else
ARCH_BUILD_TARGET := arch-unsupported
ARCH_CHECK_TARGET := arch-unsupported
ARCH_RUN_TARGET := arch-unsupported
endif

CCACHE ?= $(shell command -v ccache 2>/dev/null)
DEFAULT_CROSS_BINDIR := $(HOME)/opt/cross/bin
DEFAULT_CROSS_PREFIX := $(if $(shell command -v x86_64-elf-gcc 2>/dev/null),x86_64-elf-,$(if $(wildcard $(DEFAULT_CROSS_BINDIR)/x86_64-elf-gcc),$(DEFAULT_CROSS_BINDIR)/x86_64-elf-,x86_64-elf-))
CROSS_PREFIX ?= $(DEFAULT_CROSS_PREFIX)
ifeq ($(origin CC),default)
CC := $(if $(CCACHE),$(CCACHE) ,)$(CROSS_PREFIX)gcc
endif
ifeq ($(origin LD),default)
LD := $(CROSS_PREFIX)ld
endif
ifeq ($(origin AR),default)
AR := $(CROSS_PREFIX)ar
endif
AS := nasm
HOSTCC := $(if $(CCACHE),$(CCACHE) ,)cc
QEMU_X86_64 ?= qemu-system-x86_64
Q ?= @

I386_CC ?= $(if $(CCACHE),$(CCACHE) ,)cc
I386_LD ?= ld
I386_AR ?= ar
