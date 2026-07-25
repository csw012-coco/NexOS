# =========================
# User application sources
# =========================

USER_ELF_C_SRCS := \
	user/apps/elf/hello.c \
	user/apps/elf/keydemo.c \
	user/apps/elf/yielddemo.c \
	user/apps/elf/badptr.c \
	user/apps/elf/pfdemo.c \
	user/apps/elf/gpfdemo.c \
	user/apps/elf/uddemo.c \
	user/apps/elf/dedemo.c \
	user/apps/elf/sleepdemo.c \
	user/apps/elf/cat.c \
	user/apps/elf/ls.c \
	user/apps/elf/nexbox/applets/fs/cmd_ls_shared.c \
	user/apps/elf/wdemo.c \
	user/apps/elf/guidemo.c \
	user/apps/elf/forth.c \
	user/apps/elf/ush.c \
	user/apps/elf/ush_editor.c \
	user/apps/elf/ush_vars.c \
	user/apps/elf/ush_exec.c \
	user/apps/elf/ush_exec_dispatch.c \
	user/apps/elf/ush_exec_external.c \
	user/apps/elf/ush_exec_pipeline.c \
	user/apps/elf/ush_exec_redir.c \
	user/apps/elf/ush_exec_stdio.c \
	user/apps/elf/ush_parse.c \
	user/apps/elf/nexbox/core/cmdsuite.c \
	user/apps/elf/nexbox/core/cmdsuite_dispatch.c \
	user/apps/elf/nexbox/core/cmdsuite_action.c \
	user/apps/elf/nexbox/applets/fs/cmdsuite_basic.c \
	user/apps/elf/nexbox/applets/text/cmdsuite_text.c \
	user/apps/elf/nexbox/applets/text/cmdsuite_text_events.c \
	user/apps/elf/nexbox/applets/text/cmdsuite_text_table.c \
	user/apps/elf/nexbox/applets/audio/cmdsuite_audio.c \
	user/apps/elf/nexbox/applets/net/cmdsuite_net.c \
	user/apps/elf/nexbox/applets/net/cmdsuite_net_arp.c \
	user/apps/elf/nexbox/applets/net/cmdsuite_net_dns.c \
	user/apps/elf/nexbox/applets/net/cmdsuite_net_dhcp.c \
	user/apps/elf/nexbox/applets/net/cmdsuite_net_tcp.c \
	user/apps/elf/nexbox/applets/net/cmdsuite_net_http.c \
	user/apps/elf/nexbox/applets/net/cmdsuite_net_rtl8139.c \
	user/apps/elf/nexbox/applets/editor/cmdsuite_editor.c \
	user/apps/elf/nexbox/applets/fs/cmdsuite_storage_fdisk.c \
	user/apps/elf/nexbox/applets/fs/cmdsuite_storage_tools.c \
	user/apps/elf/nexbox/applets/fs/cmdsuite_storage_block.c \
	user/apps/elf/nexbox/applets/fs/cmdsuite_storage_cpio.c \
	user/apps/elf/nexbox/applets/fs/cmdsuite_storage.c \
	user/apps/elf/nexbox/applets/system/cmdsuite_session.c \
	user/apps/elf/nexbox/applets/system/cmdsuite_service.c \
	user/apps/elf/nexbox/applets/system/service_policy.c \
	user/apps/elf/nexbox/applets/system/cmdsuite_nexctl.c \
	user/apps/elf/nexbox/applets/system/cmdsuite_sysinfo.c \
	user/apps/elf/nexbox/applets/proc/cmdsuite_proc.c \
	user/apps/elf/nexbox/applets/debug/cmdsuite_debug.c \
	user/apps/elf/nexbox/applets/debug/cmdsuite_debug_doctor.c \
	user/apps/elf/nexbox/applets/asm/cmdsuite_asm.c

USER_ELF_C_OBJS := $(addprefix $(BUILD)/,$(USER_ELF_C_SRCS:.c=.o))

USER_ELF_BINS := $(BUILD)/DOOM.ELF $(BUILD)/IPCDEMO.ELF $(BUILD)/IMGVIEW.ELF $(BUILD)/NCC.ELF $(BUILD)/HELLO.ELF $(BUILD)/KEYDEMO.ELF $(BUILD)/YIELDDEMO.ELF $(BUILD)/BADPTR.ELF $(BUILD)/PFDEMO.ELF $(BUILD)/GPFDEMO.ELF $(BUILD)/UDDEMO.ELF $(BUILD)/DEDEMO.ELF $(BUILD)/SLEEPDEMO.ELF $(BUILD)/CATDEMO.ELF $(BUILD)/LSDEMO.ELF $(BUILD)/WDEMO.ELF $(BUILD)/GUIDEMO.ELF $(BUILD)/FORTH.ELF $(BUILD)/USH.ELF $(BUILD)/NEXBOX.ELF

HELLO_ELF_OBJS := $(BUILD)/user/apps/elf/hello.o
KEYDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/keydemo.o
YIELDDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/yielddemo.o
BADPTR_ELF_OBJS := $(BUILD)/user/apps/elf/badptr.o
PFDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/pfdemo.o
GPFDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/gpfdemo.o
UDDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/uddemo.o
DEDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/dedemo.o
SLEEPDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/sleepdemo.o
CATDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/cat.o
LSDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/ls.o $(BUILD)/user/apps/elf/nexbox/applets/fs/cmd_ls_shared.o
WDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/wdemo.o
GUIDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/guidemo.o
FORTH_ELF_OBJS := $(BUILD)/user/apps/elf/forth.o
USH_ELF_OBJS := $(BUILD)/user/apps/elf/ush.o $(BUILD)/user/apps/elf/ush_editor.o $(BUILD)/user/apps/elf/ush_vars.o $(BUILD)/user/apps/elf/ush_exec.o $(BUILD)/user/apps/elf/ush_exec_dispatch.o $(BUILD)/user/apps/elf/ush_exec_external.o $(BUILD)/user/apps/elf/ush_exec_pipeline.o $(BUILD)/user/apps/elf/ush_exec_redir.o $(BUILD)/user/apps/elf/ush_exec_stdio.o $(BUILD)/user/apps/elf/ush_parse.o
NEXBOX_ELF_OBJS := $(BUILD)/user/apps/elf/nexbox/core/cmdsuite.o $(BUILD)/user/apps/elf/nexbox/core/cmdsuite_dispatch.o $(BUILD)/user/apps/elf/nexbox/core/cmdsuite_action.o $(BUILD)/user/apps/elf/nexbox/applets/fs/cmdsuite_basic.o $(BUILD)/user/apps/elf/nexbox/applets/text/cmdsuite_text.o $(BUILD)/user/apps/elf/nexbox/applets/text/cmdsuite_text_events.o $(BUILD)/user/apps/elf/nexbox/applets/text/cmdsuite_text_table.o $(BUILD)/user/apps/elf/nexbox/applets/audio/cmdsuite_audio.o $(BUILD)/user/apps/elf/nexbox/applets/net/cmdsuite_net.o $(BUILD)/user/apps/elf/nexbox/applets/net/cmdsuite_net_arp.o $(BUILD)/user/apps/elf/nexbox/applets/net/cmdsuite_net_dns.o $(BUILD)/user/apps/elf/nexbox/applets/net/cmdsuite_net_dhcp.o $(BUILD)/user/apps/elf/nexbox/applets/net/cmdsuite_net_tcp.o $(BUILD)/user/apps/elf/nexbox/applets/net/cmdsuite_net_http.o $(BUILD)/user/apps/elf/nexbox/applets/net/cmdsuite_net_rtl8139.o $(BUILD)/user/apps/elf/nexbox/applets/editor/cmdsuite_editor.o $(BUILD)/user/apps/elf/nexbox/applets/fs/cmdsuite_storage_fdisk.o $(BUILD)/user/apps/elf/nexbox/applets/fs/cmdsuite_storage_tools.o $(BUILD)/user/apps/elf/nexbox/applets/fs/cmdsuite_storage_block.o $(BUILD)/user/apps/elf/nexbox/applets/fs/cmdsuite_storage_cpio.o $(BUILD)/user/apps/elf/nexbox/applets/fs/cmdsuite_storage.o $(BUILD)/user/apps/elf/nexbox/applets/system/cmdsuite_session.o $(BUILD)/user/apps/elf/nexbox/applets/system/cmdsuite_service.o $(BUILD)/user/apps/elf/nexbox/applets/system/service_policy.o $(BUILD)/user/apps/elf/nexbox/applets/system/cmdsuite_nexctl.o $(BUILD)/user/apps/elf/nexbox/applets/system/cmdsuite_sysinfo.o $(BUILD)/user/apps/elf/nexbox/applets/proc/cmdsuite_proc.o $(BUILD)/user/apps/elf/nexbox/applets/debug/cmdsuite_debug.o $(BUILD)/user/apps/elf/nexbox/applets/debug/cmdsuite_debug_doctor.o $(BUILD)/user/apps/elf/nexbox/applets/asm/cmdsuite_asm.o $(BUILD)/user/apps/elf/nexbox/applets/fs/cmd_ls_shared.o
I386_NEXBOX_FULL_SRCS := $(patsubst $(BUILD)/%.o,%.c,$(NEXBOX_ELF_OBJS))
I386_NEXBOX_FULL_OBJS := $(patsubst %.c,$(I386_BUILD)/full/%.o,$(I386_NEXBOX_FULL_SRCS))

.PHONY: test-service-policy
test-service-policy:
	$(Q)cc -std=c11 -Wall -Wextra -I$(ROOT) tests/service_policy_test.c user/apps/elf/nexbox/applets/system/service_policy.c -o $(BUILD)/service_policy_test
	$(Q)$(BUILD)/service_policy_test

$(eval $(call define_user_elf,HELLO.ELF,$(HELLO_ELF_OBJS)))
$(eval $(call define_user_elf,KEYDEMO.ELF,$(KEYDEMO_ELF_OBJS)))
$(eval $(call define_user_elf,YIELDDEMO.ELF,$(YIELDDEMO_ELF_OBJS)))
$(eval $(call define_user_elf,BADPTR.ELF,$(BADPTR_ELF_OBJS)))
$(eval $(call define_user_elf,PFDEMO.ELF,$(PFDEMO_ELF_OBJS)))
$(eval $(call define_user_elf,GPFDEMO.ELF,$(GPFDEMO_ELF_OBJS)))
$(eval $(call define_user_elf,UDDEMO.ELF,$(UDDEMO_ELF_OBJS)))
$(eval $(call define_user_elf,DEDEMO.ELF,$(DEDEMO_ELF_OBJS)))
$(eval $(call define_user_elf,SLEEPDEMO.ELF,$(SLEEPDEMO_ELF_OBJS)))
$(eval $(call define_user_elf,CATDEMO.ELF,$(CATDEMO_ELF_OBJS)))
$(eval $(call define_user_elf,LSDEMO.ELF,$(LSDEMO_ELF_OBJS)))
$(eval $(call define_user_elf,WDEMO.ELF,$(WDEMO_ELF_OBJS)))
$(eval $(call define_user_elf,GUIDEMO.ELF,$(GUIDEMO_ELF_OBJS)))
$(eval $(call define_user_elf,FORTH.ELF,$(FORTH_ELF_OBJS)))
$(eval $(call define_user_elf,USH.ELF,$(USH_ELF_OBJS)))
$(eval $(call define_user_elf,NEXBOX.ELF,$(NEXBOX_ELF_OBJS)))



IMGVIEW_ELF_OBJS := $(BUILD)/user/apps/elf/imgview.o
IPCDEMO_ELF_OBJS := $(BUILD)/user/apps/elf/ipcdemo.o

$(eval $(call define_user_elf,IMGVIEW.ELF,$(IMGVIEW_ELF_OBJS)))
$(eval $(call define_user_elf,IPCDEMO.ELF,$(IPCDEMO_ELF_OBJS)))

DOOM_ELF_OBJS := \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric_main.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric_math.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric_platform.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/am_map.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/d_event.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/d_items.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/d_iwad.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/d_loop.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/d_main.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/d_mode.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/d_net.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/doomdef.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/doomgeneric.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/doomstat.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/dstrings.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/dummy.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/f_finale.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/f_wipe.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/g_game.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/gusconf.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/hu_lib.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/hu_stuff.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/i_cdmus.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/i_endoom.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/i_input.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/i_joystick.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/i_nexossound.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/i_scale.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/i_sound.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/i_system.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/i_timer.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/i_video.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/icon.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/info.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/m_argv.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/m_bbox.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/m_cheat.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/m_config.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/m_controls.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/m_fixed.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/m_menu.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/m_misc.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/m_random.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/memio.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/mus2mid.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_ceilng.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_doors.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_enemy.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_floor.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_inter.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_lights.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_map.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_maputl.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_mobj.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_plats.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_pspr.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_saveg.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_setup.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_sight.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_spec.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_switch.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_telept.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_tick.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/p_user.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/r_bsp.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/r_data.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/r_draw.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/r_main.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/r_plane.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/r_segs.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/r_sky.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/r_things.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/s_sound.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/sha1.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/sounds.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/st_lib.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/st_stuff.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/statdump.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/tables.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/v_video.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/w_checksum.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/w_file.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/w_file_stdc.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/w_main.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/w_wad.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/wi_stuff.o \
	$(BUILD)/user/apps/elf/doomgeneric/doomgeneric/z_zone.o \

I386_DOOM_SRCS := $(patsubst $(BUILD)/%.o,%.c,$(DOOM_ELF_OBJS))
I386_DOOM_OBJS := $(patsubst %.c,$(I386_BUILD)/full/%.o,$(I386_DOOM_SRCS))

$(eval $(call define_user_elf,DOOM.ELF,$(DOOM_ELF_OBJS)))

$(I386_DOOM_USER): $(I386_CRT0) $$(I386_DOOM_OBJS) \
		$(I386_NLIBC) $(ROOT)/user/i386/test32.ld
	$(call log_cmd,LD32,$@)
	$(Q)$(I386_LD) $(I386_LDFLAGS) -T $(ROOT)/user/i386/test32.ld \
		-o $@ $(I386_CRT0) $(I386_DOOM_OBJS) $(I386_NLIBC)

NCC_ELF_OBJS := \
	$(BUILD)/user/apps/elf/ncc/ncc_main.o \
	$(BUILD)/user/apps/elf/ncc/ncc_lexer.o \
	$(BUILD)/user/apps/elf/ncc/ncc_parser.o \
	$(BUILD)/user/apps/elf/ncc/ncc_codegen.o \
	$(BUILD)/user/apps/elf/ncc/ncc_link.o \
	$(BUILD)/user/apps/elf/ncc/ncc_util.o

$(eval $(call define_user_elf,NCC.ELF,$(NCC_ELF_OBJS)))

# Doomgeneric uses double math stubs; x86_64 ABI returns double in XMM regs.
$(BUILD)/user/apps/elf/doomgeneric/doomgeneric_math.o: USERCFLAGS := $(filter-out -mno-sse -mgeneral-regs-only,$(USERCFLAGS)) -msse -mfpmath=sse

# Doomgeneric NexOS port flags
$(BUILD)/user/apps/elf/doomgeneric/%.o: USERCFLAGS += -D__NEXOS__

# Doomgeneric needs float/double ABI. Keep this limited to Doom objects.
DOOM_USERCFLAGS := $(filter-out -mno-sse -mgeneral-regs-only,$(USERCFLAGS)) -msse -mfpmath=sse -D__NEXOS__

$(BUILD)/user/apps/elf/doomgeneric/%.o: USERCFLAGS := $(DOOM_USERCFLAGS)
