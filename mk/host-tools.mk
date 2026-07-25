# Host tool checks shared by build, image, and QEMU smoke targets.

define require_cmd
	@command -v $(1) >/dev/null 2>&1 || { \
		printf '%s\n' "missing host tool: $(1)"; \
		exit 1; \
	}
endef

check-host-tools:
	$(call require_cmd,cc)
	$(call require_cmd,nasm)
	$(call require_cmd,ld)
	$(call require_cmd,ar)
	$(call require_cmd,readelf)
	$(call require_cmd,dd)
	$(call require_cmd,timeout)

check-host-tools-image:
	$(call require_cmd,mcopy)
	$(call require_cmd,mdir)
	$(call require_cmd,truncate)
	$(call require_cmd,mkfs.fat)
	$(call require_cmd,parted)

check-host-tools-x86_64: check-host-tools
	$(call require_cmd,$(CROSS_PREFIX)gcc)
	$(call require_cmd,$(CROSS_PREFIX)ld)
	$(call require_cmd,$(CROSS_PREFIX)ar)

check-host-tools-i386: check-host-tools
	$(call require_cmd,$(firstword $(I386_CC)))
	$(call require_cmd,$(I386_LD))
	$(call require_cmd,$(I386_AR))

check-host-tools-qemu-i386:
	$(call require_cmd,$(I386_QEMU))

check-host-tools-qemu-x86_64:
	$(call require_cmd,$(QEMU_X86_64))
