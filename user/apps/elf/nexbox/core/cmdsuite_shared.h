#pragma once

#include "user/libc/include/fcntl.h"
#include "user/libc/include/file.h"
#include "user/libc/include/stdio.h"
#include "user/libc/include/stdlib.h"
#include "user/libc/include/string.h"
#include "user/libc/include/strings.h"
#include "user/libc/include/nexos/audio.h"
#include "user/libc/include/nexos/file.h"
#include "user/libc/include/nexos/fs.h"
#include "user/libc/include/nexos/net.h"
#include "user/libc/include/nexos/process.h"
#include "user/libc/include/nexos/string.h"
#include "user/libc/include/nexos/system.h"

#define CMD_PATH_MAX 64u
#define CMD_PAGER_LINES 20u

uint32_t str_len_local(const char *text);
void copy_line_local(char *dst, const char *src, uint32_t max_len);
int streq_local(const char *a, const char *b);
int streq_ignore_case_local(const char *a, const char *b);
int parse_u32_local(const char *text, uint32_t *out);
void write_dec(uint32_t value);
void write_sdec(int32_t value);
void write_hex_u32(uint32_t value);
void write_hex_u64(uint64_t value);
const char *process_exit_reason_local(int32_t exit_code);
void write_process_exit_status(int32_t exit_code);
void write_text_padded(const char *text, uint32_t width);
void write_human_size(uint64_t bytes);
void write_err_text(const char *text);
void write_err_usage(const char *verb, const char *suffix);
int cmd_open_resolved_path(const char *arg, uint32_t flags);
int cmd_build_program_command(int argc,
                              char **argv,
                              int start,
                              const char *verb,
                              int resolve_dot_name,
                              char *out,
                              uint32_t out_size);

int cmd_help(void);
int cmd_echo(int argc, char **argv);
int cmd_clear(void);
int cmd_pwd(void);
int cmd_env(int argc, char **argv);
int cmd_which_like(int argc, char **argv, const char *mode_name);
int cmd_ls(int argc, char **argv);
int cmd_cat(int argc, char **argv);
int cmd_less(int argc, char **argv);
int cmd_hexdump(int argc, char **argv);
int cmd_grep(int argc, char **argv);
int cmd_date(int argc, char **argv);
int cmd_hwclock(int argc, char **argv);
int cmd_sleep(int argc, char **argv);
int cmd_watch(int argc, char **argv);
int cmd_on(int argc, char **argv);
int cmd_events(int argc, char **argv);
int cmd_wc(int argc, char **argv);
int cmd_head(int argc, char **argv);
int cmd_tail(int argc, char **argv);
int cmd_find(int argc, char **argv);
int cmd_as(int argc, char **argv);
int cmd_pick(int argc, char **argv);
int cmd_select(int argc, char **argv);
int cmd_sort_by(int argc, char **argv);
int cmd_count_by(int argc, char **argv);
int cmd_to(int argc, char **argv);
int cmd_view(int argc, char **argv);
int cmd_ed(int argc, char **argv);
int cmd_vi(int argc, char **argv);
int cmd_touch(int argc, char **argv);
int cmd_mv(int argc, char **argv);
int cmd_cp(int argc, char **argv);
int cmd_mkdir(int argc, char **argv);
int cmd_rmdir(int argc, char **argv);
int cmd_rm(int argc, char **argv);
int cmd_run_like(int argc, char **argv, const char *verb, uint32_t mode, uint32_t flags, int use_exec);
int cmd_fasm(int argc, char **argv);
int cmd_stat(int argc, char **argv);
int cmd_du(int argc, char **argv);
int cmd_tree(int argc, char **argv);
int cmd_file(int argc, char **argv);
int cmd_parts(int argc, char **argv);
int cmd_fdisk(int argc, char **argv);
int cmd_blk(void);
int cmd_df(int argc, char **argv);
int cmd_mounts(void);
int cmd_progs(void);
int cmd_fatls(void);
int cmd_fatfind(int argc, char **argv);
int cmd_fatread(int argc, char **argv);
int cmd_cpio(int argc, char **argv);
int cmd_dmesg(void);
int cmd_lspci(void);
int cmd_ac97(void);
int cmd_rtl8139(void);
int cmd_rtl8139tx(int argc, char **argv);
int cmd_rtl8139rx(int argc, char **argv);
int cmd_arp(int argc, char **argv);
int cmd_route(int argc, char **argv);
int cmd_netstat(int argc, char **argv);
int cmd_ping(int argc, char **argv);
int cmd_dns(int argc, char **argv);
int cmd_dhcp(int argc, char **argv);
int cmd_ifconfig(int argc, char **argv);
int cmd_http(int argc, char **argv);
int cmd_wget(int argc, char **argv);
int cmd_nc(int argc, char **argv);
int cmd_audio(int argc, char **argv);
int cmd_tone(int argc, char **argv);
int cmd_wav(int argc, char **argv);
int cmd_mplay(int argc, char **argv);
int cmd_doctor(int argc, char **argv);
int cmd_nexctl(int argc, char **argv);
int cmd_sysinfo(int argc, char **argv);
int cmd_meminfo(void);
int cmd_minfo(void);
int cmd_uname(int argc, char **argv);
int cmd_cpuinfo(void);
int cmd_session(int argc, char **argv);
int cmd_service(int argc, char **argv);
int cmd_ps(void);
int cmd_jobs(void);
int cmd_wait(int argc, char **argv);
int cmd_alarm(int argc, char **argv);
int cmd_timeout(int argc, char **argv);
int cmd_kill_like(int argc, char **argv, const char *name);
int cmd_mount(int argc, char **argv);
int cmd_umount(int argc, char **argv);
int cmd_hotplug(int argc, char **argv);
int cmd_switch_root(int argc, char **argv);
int cmd_dbg(int argc, char **argv);
int cmdsuite_dispatch_main(int argc, char **argv);
