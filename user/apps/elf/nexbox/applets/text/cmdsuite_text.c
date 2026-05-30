#include "user/apps/elf/nexbox/applets/text/cmdsuite_text_common.h"


static int write_stdout_all_or_stop(const char *buf, uint32_t bytes) {
    uint32_t offset = 0;

    while (offset < bytes) {
        ssize_t written = write(STDOUT_FILENO, buf + offset, bytes - offset);

        if (written < 0) {
            return -1;
        }
        if (written == 0) {
            return 0;
        }
        offset += (uint32_t)written;
    }
    return 1;
}

static int pager_wait(uint32_t tty_fd) {
    char ch = 0;

    (void)write((int)tty_fd,
                "\r\x1b[7m-- more -- space:page enter:line q:quit\x1b[0m",
                str_len_local("\r\x1b[7m-- more -- space:page enter:line q:quit\x1b[0m"));
    for (;;) {
        if (nex_read((int)tty_fd, &ch, 2, NEX_READ_CHAR) == 0) {
            (void)write((int)tty_fd, "\r\x1b[2K", str_len_local("\r\x1b[2K"));
            return 0;
        }
        if (ch == 'q' || ch == 'Q' || ch == 0x03) {
            (void)write((int)tty_fd, "\r\x1b[2K", str_len_local("\r\x1b[2K"));
            return 0;
        }
        if (ch == '\r' || ch == '\n') {
            (void)write((int)tty_fd, "\r\x1b[2K", str_len_local("\r\x1b[2K"));
            return 1;
        }
        if (ch == ' ') {
            (void)write((int)tty_fd, "\r\x1b[2K", str_len_local("\r\x1b[2K"));
            return CMD_PAGER_LINES;
        }
    }
}

static void pager_run_fd(uint32_t content_fd, uint32_t tty_fd) {
    char ch = 0;
    uint32_t remaining_lines = CMD_PAGER_LINES;

    while (read((int)content_fd, &ch, 1) > 0) {
        write_stdout(&ch, 1);
        if (ch != '\n') {
            continue;
        }
        if (remaining_lines > 1u) {
            remaining_lines--;
            continue;
        }
        remaining_lines = (uint32_t)pager_wait(tty_fd);
        if (remaining_lines == 0) {
            break;
        }
    }
}


int cmd_help(void) {
    write_str("cmd commands: help actions action mapper echo clear pwd tty env font which type ls cat less hexdump grep date hwclock sleep watch on events clipboard wc head tail find as pick select sort-by count-by to view ed vi vim touch mv cp mkdir rmdir rm asm stat du tree file blk parts fdisk df mounts progs fatls fatfind fatread cpio mount umount hotplug run runelf runbg ps session service jobs wait alarm timeout kill fg bg reboot switch_root dmesg lspci ac97 hda rtl8139 rtl8139tx rtl8139rx arp route netstat ping dns dhcp ifconfig http wget nc audio tone wav mplay doctor nexctl sysinfo meminfo minfo uname cpuinfo config dbg\n");
    write_str("shell-only builtins: cd exit [code] exec set export alias functions history source .\n");
    write_str("multicall: nexbox <applet> [args]\n");
    write_str("set lists shell-local vars; env/export list exported environment\n");
    write_str("relative paths follow the process cwd\n");
    return 0;
}

int cmd_echo(int argc, char **argv) {
    int i;

    for (i = 1; i < argc; i++) {
        if (i > 1) {
            write_stdout(" ", 1);
        }
        write_str(argv[i]);
    }
    write_str("\n");
    return 0;
}

int cmd_clear(void) {
    clear();
    return 0;
}

int cmd_pwd(void) {
    char cwd[CMD_PATH_MAX];

    if (getcwd(cwd, sizeof(cwd)) < 0) {
        write_str("/\n");
        return 1;
    }
    write_str(cwd);
    write_str("\n");
    return 0;
}

int cmd_tty(int argc, char **argv) {
    struct syscall_tty_info info;

    if (argc > 1) {
        if (argc == 2 &&
            (streq_local(argv[1], "-h") || streq_local(argv[1], "--help"))) {
            write_str("usage: tty\n");
            write_str("print the terminal connected to stdin\n");
            return 0;
        }
        write_err_usage("tty", "\n");
        return 1;
    }
    if (tty_query(STDIN_FILENO, &info) <= 0 || info.path[0] == '\0') {
        write_err_str("not a tty\n");
        return 1;
    }
    write_str(info.path);
    write_str("\n");
    return 0;
}

int cmd_env(int argc, char **argv) {
    uint32_t i = 0;
    int listed = 0;

    (void)argv;

    if (argc > 1) {
        write_err_usage("env", "\n");
        return 1;
    }
    while (environ != NULL && environ[i] != NULL) {
        write_str(environ[i]);
        write_str("\n");
        listed = 1;
        i++;
    }
    if (!listed) {
        write_str("<empty>\n");
    }
    return 0;
}

static void font_write_help_local(void) {
    write_str("usage: font [info|sample|--table]\n");
    write_str("show the active text font cell, console grid, and sample glyphs\n");
}

static int font_hex_file_available_local(void) {
    int fd = open("/system/font/font.hex", 0);

    if (fd < 0) {
        fd = open("/SYSTEM/FONT/FONT.HEX", 0);
    }
    if (fd < 0) {
        return 0;
    }
    close((uint32_t)fd);
    return 1;
}

static int font_write_info_local(int table) {
    struct syscall_machine_info machine;
    uint32_t cell_width;
    uint32_t cell_height;
    int has_font_file;

    if (machine_info_query(&machine) <= 0) {
        write_err_str("font: machine info query failed\n");
        return 1;
    }

    cell_width = machine.text_cell_width != 0u ? machine.text_cell_width : 8u;
    cell_height = machine.text_cell_height != 0u ? machine.text_cell_height : 16u;
    has_font_file = font_hex_file_available_local();

    if (table) {
        write_str("#!type=table columns=property,value\n");
        write_str("property value\n");
        write_str("grid ");
        write_dec(machine.text_columns);
        write_str("x");
        write_dec(machine.text_rows);
        write_str("\ncell ");
        write_dec(cell_width);
        write_str("x");
        write_dec(cell_height);
        write_str("\nfont_file ");
        write_str(has_font_file ? "/system/font/font.hex" : "none");
        write_str("\n");
        return 0;
    }

    write_str("font:     framebuffer text font\n");
    write_str("grid:     ");
    write_dec(machine.text_columns);
    write_str("x");
    write_dec(machine.text_rows);
    write_str("\ncell:     ");
    write_dec(cell_width);
    write_str("x");
    write_dec(cell_height);
    write_str("\nfont hex: ");
    write_str(has_font_file ? "/system/font/font.hex" : "not found");
    write_str("\n");
    return 0;
}

static void font_write_sample_local(void) {
    write_str("ASCII:\n");
    write_str(" !\"#$%&'()*+,-./0123456789:;<=>?\n");
    write_str(" @ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_\n");
    write_str(" `abcdefghijklmnopqrstuvwxyz{|}~\n");
    write_str("Box:\n");
    write_str(" +--------+  The quick brown fox jumps over 13 lazy dogs.\n");
    write_str(" | NexOS  |  0123456789 ABC abc\n");
    write_str(" +--------+\n");
}

int cmd_font(int argc, char **argv) {
    if (argc > 2) {
        write_err_usage("font", " [info|sample|--table]\n");
        return 1;
    }
    if (argc == 2) {
        if (streq_local(argv[1], "-h") || streq_local(argv[1], "--help")) {
            font_write_help_local();
            return 0;
        }
        if (streq_local(argv[1], "info")) {
            return font_write_info_local(0);
        }
        if (streq_local(argv[1], "sample")) {
            font_write_sample_local();
            return 0;
        }
        if (streq_local(argv[1], "--table")) {
            return font_write_info_local(1);
        }
        write_err_usage("font", " [info|sample|--table]\n");
        return 1;
    }

    return font_write_info_local(0);
}

int cmd_which_like(int argc, char **argv, const char *mode_name) {
    static const char *const shell_only[] = {"cd", "exit", "exec", "set", "export", "alias", "functions", "history", "source", "."};
    uint32_t i;

    if (argc < 2) {
        write_err_usage(mode_name, " <name> [name...]\n");
        return 1;
    }
    for (i = 1; i < (uint32_t)argc; i++) {
        char upper_name[CMD_PATH_MAX];
        char path[CMD_PATH_MAX];
        uint32_t j = 0;
        int found = 0;

        if (argv[i] == NULL || argv[i][0] == '\0') {
            continue;
        }
        while (j < sizeof(shell_only) / sizeof(shell_only[0])) {
            if (streq_local(argv[i], shell_only[j])) {
                if (streq_local(mode_name, "type")) {
                    write_str(argv[i]);
                    write_str(" is a shell builtin\n");
                } else {
                    write_str(argv[i]);
                    write_str(": shell builtin\n");
                }
                found = 1;
                break;
            }
            j++;
        }
        if (found) {
            continue;
        }
        copy_line_local(upper_name, argv[i], sizeof(upper_name));
        for (j = 0; upper_name[j] != '\0'; j++) {
            if (upper_name[j] >= 'a' && upper_name[j] <= 'z') {
                upper_name[j] = (char)(upper_name[j] - ('a' - 'A'));
            }
        }
        if (snprintf(path, sizeof(path), "/CMD/%s", upper_name) < 0 || path[0] == '\0') {
            write_err_str(argv[i]);
            write_err_str(": not found\n");
            continue;
        }
        {
            int fd = open(path, 0);

            if (fd >= 0) {
                close((uint32_t)fd);
                if (streq_local(mode_name, "type")) {
                    write_str(argv[i]);
                    write_str(" is ");
                    write_str(path);
                    write_str("\n");
                } else {
                    write_str(path);
                    write_str("\n");
                }
                continue;
            }
        }
        write_err_str(argv[i]);
        write_err_str(": not found\n");
    }
    return 0;
}

int cmd_ls(int argc, char **argv) {
    const char *path = ".";
    int long_format = 0;
    int show_all = 0;
    int path_set = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (uint32_t j = 1; argv[i][j] != '\0'; j++) {
                if (argv[i][j] == 'l') {
                    long_format = 1;
                } else if (argv[i][j] == 'a') {
                    show_all = 1;
                } else {
                    write_err_usage("ls", " [-a] [-l] [path]\n");
                    return 1;
                }
            }
        } else if (!path_set) {
            path = argv[i];
            path_set = 1;
        } else {
            write_err_usage("ls", " [-a] [-l] [path]\n");
            return 1;
        }
    }
    return cmd_ls_path(path, long_format, show_all);
}

int cmd_cat(int argc, char **argv) {
    char buf[CMD_CAT_BUFFER_SIZE];
    char json_path[CMD_PATH_MAX + 8u];
    int fd = STDIN_FILENO;
    uint8_t last_was_newline = 1u;
    int output_closed = 0;
    int json = 0;
    int path_index = 1;

    if (argc > 1 && streq_local(argv[1], "--json")) {
        json = 1;
        path_index = 2;
    }
    if (json && argc <= path_index) {
        write_err_usage("cat", " --json /event/<source>\n");
        return 1;
    }
    if (argc > path_index && argv[path_index][0] != '\0') {
        const char *path = argv[path_index];

        if (json) {
            uint32_t i = 0;

            if (!starts_with_text_local(path, "/event/")) {
                write_err_str("cat: --json requires /event path\n");
                return 1;
            }
            while (path[i] != '\0' && i + 6u < sizeof(json_path)) {
                json_path[i] = path[i];
                i++;
            }
            if (i + 6u >= sizeof(json_path)) {
                write_err_str("cat: path too long\n");
                return 1;
            }
            json_path[i++] = '.';
            json_path[i++] = 'j';
            json_path[i++] = 's';
            json_path[i++] = 'o';
            json_path[i++] = 'n';
            json_path[i] = '\0';
            path = json_path;
        }
        fd = cmd_open_resolved_path(path, 0);
        if (fd < 0) {
            write_err_str("open failed\n");
            return 1;
        }
    }
    for (;;) {
        ssize_t read_rc = read(fd, buf, sizeof(buf));
        uint32_t bytes;

        if (read_rc < 0) {
            write_err_str("cat: read failed\n");
            if (fd != STDIN_FILENO) {
                close((uint32_t)fd);
            }
            return 1;
        }
        if (read_rc == 0) {
            break;
        }
        bytes = (uint32_t)read_rc;
        {
            int write_rc = write_stdout_all_or_stop(buf, bytes);

            if (write_rc < 0) {
                write_err_str("cat: write failed\n");
                if (fd != STDIN_FILENO) {
                    close((uint32_t)fd);
                }
                return 1;
            }
            if (write_rc == 0) {
                output_closed = 1;
                break;
            }
        }
        last_was_newline = buf[bytes - 1u] == '\n' ? 1u : 0u;
    }
    if (fd != STDIN_FILENO) {
        close((uint32_t)fd);
        if (!output_closed && !last_was_newline) {
            write_str("\n");
        }
    }
    return 0;
}

int cmd_less(int argc, char **argv) {
    int content_fd = STDIN_FILENO;
    int tty_fd;
    int close_content = 0;

    if (argc > 1 && argv[1][0] != '\0') {
        content_fd = cmd_open_resolved_path(argv[1], 0);
        if (content_fd < 0) {
            write_err_str("less: open failed\n");
            return 1;
        }
        close_content = 1;
    }
    tty_fd = open("/dev/tty", 0);
    if (tty_fd < 0) {
        tty_fd = STDIN_FILENO;
    }
    pager_run_fd((uint32_t)content_fd, (uint32_t)tty_fd);
    if (tty_fd >= 0 && tty_fd != STDIN_FILENO) {
        close((uint32_t)tty_fd);
    }
    if (close_content) {
        close((uint32_t)content_fd);
    }
    return 0;
}

int cmd_hexdump(int argc, char **argv) {
    static const char hex[] = "0123456789ABCDEF";
    uint8_t buf[16];
    uint32_t offset = 0;
    int fd = STDIN_FILENO;

    if (argc > 2) {
        write_err_usage("hexdump", " [path]\n");
        return 1;
    }
    if (argc == 2 && argv[1][0] != '\0') {
        fd = cmd_open_resolved_path(argv[1], 0);
        if (fd < 0) {
            write_err_str("hexdump: open failed\n");
            return 1;
        }
    }
    for (;;) {
        uint32_t got = (uint32_t)read(fd, (char *)buf, sizeof(buf));
        uint32_t i;

        if (got == 0) {
            break;
        }
        write_hex_u32(offset);
        write_str("  ");
        for (i = 0; i < sizeof(buf); i++) {
            if (i < got) {
                char out[3];

                out[0] = hex[(buf[i] >> 4) & 0x0fu];
                out[1] = hex[buf[i] & 0x0fu];
                out[2] = '\0';
                write_str(out);
            } else {
                write_str("  ");
            }
            if (i + 1u != sizeof(buf)) {
                write_stdout(" ", 1);
            }
        }
        write_str("  |");
        for (i = 0; i < got; i++) {
            char ch = (buf[i] >= 32u && buf[i] <= 126u) ? (char)buf[i] : '.';

            write_stdout(&ch, 1);
        }
        write_str("|\n");
        offset += got;
        if (got < sizeof(buf)) {
            break;
        }
    }
    if (fd != STDIN_FILENO) {
        close((uint32_t)fd);
    }
    return 0;
}

int cmd_grep(int argc, char **argv) {
    char line[128];
    char *pattern;
    int fd = STDIN_FILENO;
    int close_fd = 0;
    int found = 0;

    if (argc < 2 || argc > 3) {
        write_err_usage("grep", " <pattern> [path]\n");
        return 1;
    }
    pattern = argv[1];
    if (pattern[0] == '\0') {
        write_err_str("grep: empty pattern\n");
        return 1;
    }
    if (argc == 3) {
        fd = cmd_open_resolved_path(argv[2], 0);
        if (fd < 0) {
            write_err_str("grep: open failed\n");
            return 1;
        }
        close_fd = 1;
    }

    for (;;) {
        uint32_t got = read_line((uint32_t)fd, line, sizeof(line));

        if (got == 0) {
            break;
        }
        if (starts_with(line, pattern) || text_contains_local(line, pattern)) {
            write_str(line);
            write_str("\n");
            found = 1;
        }
    }

    if (close_fd) {
        close((uint32_t)fd);
    }
    return found ? 0 : 1;
}

static void text_write_padded2_local(uint32_t value) {
    if (value < 10u) {
        write_str("0");
    }
    write_dec(value);
}

static void text_write_rtc_iso_local(const struct syscall_rtc_info *rtc) {
    write_dec(rtc->year);
    write_str("-");
    text_write_padded2_local(rtc->month);
    write_str("-");
    text_write_padded2_local(rtc->day);
    write_str(" ");
    text_write_padded2_local(rtc->hour);
    write_str(":");
    text_write_padded2_local(rtc->minute);
    write_str(":");
    text_write_padded2_local(rtc->second);
}

int cmd_date(int argc, char **argv) {
    struct syscall_machine_info info;
    struct syscall_rtc_info rtc;
    uint32_t tick_count;
    uint32_t total_seconds;
    uint32_t hours;
    uint32_t minutes;
    uint32_t seconds;
    uint32_t millis;
    int iso_only = 0;
    int unix_only = 0;
    int raw = 0;

    if (argc > 2) {
        write_err_usage("date", " [--iso|+%s|--raw]\n");
        return 1;
    }
    if (argc == 2) {
        if (streq_local(argv[1], "--iso") || streq_local(argv[1], "-I")) {
            iso_only = 1;
        } else if (streq_local(argv[1], "+%s") || streq_local(argv[1], "--unix")) {
            unix_only = 1;
        } else if (streq_local(argv[1], "--raw")) {
            raw = 1;
        } else {
            write_err_usage("date", " [--iso|+%s|--raw]\n");
            return 1;
        }
    }
    if ((iso_only || unix_only) && (rtc_query(&rtc) <= 0 || !rtc.present || !rtc.valid)) {
        write_err_str("date: rtc unavailable or invalid\n");
        return 1;
    }
    if (iso_only) {
        text_write_rtc_iso_local(&rtc);
        write_str("\n");
        return 0;
    }
    if (unix_only) {
        write_dec(rtc.unix_time);
        write_str("\n");
        return 0;
    }
    if (machine_info_query(&info) <= 0) {
        write_err_str("date: machine info query failed\n");
        return 1;
    }
    if (rtc_query(&rtc) > 0 && rtc.present) {
        write_str("rtc: ");
        text_write_rtc_iso_local(&rtc);
        write_str(" valid=");
        write_dec(rtc.valid);
        write_str(" unix=");
        write_dec(rtc.unix_time);
        write_str("\n");
        if (raw) {
            write_str("rtc_raw: status_a=0x");
            write_hex_u32(rtc.status_a);
            write_str(" status_b=0x");
            write_hex_u32(rtc.status_b);
            write_str(" century=");
            write_dec(rtc.century);
            write_str(" raw_year=");
            write_dec(rtc.raw_year);
            write_str("\n");
        }
    } else {
        write_str("rtc: unavailable\n");
    }

    tick_count = ticks();
    total_seconds = tick_count / 1000u;
    millis = tick_count % 1000u;
    hours = total_seconds / 3600u;
    minutes = (total_seconds / 60u) % 60u;
    seconds = total_seconds % 60u;

    write_str("build: ");
    write_str(info.build_date);
    write_str("\n");
    write_str("uptime: ");
    if (hours < 10u) {
        write_str("0");
    }
    write_dec(hours);
    write_str(":");
    if (minutes < 10u) {
        write_str("0");
    }
    write_dec(minutes);
    write_str(":");
    if (seconds < 10u) {
        write_str("0");
    }
    write_dec(seconds);
    write_str(".");
    if (millis < 100u) {
        write_str("0");
    }
    if (millis < 10u) {
        write_str("0");
    }
    write_dec(millis);
    write_str("\n");
    return 0;
}

int cmd_hwclock(int argc, char **argv) {
    static const char *const weekday_names[] = {
        "unknown", "sun", "mon", "tue", "wed", "thu", "fri", "sat"
    };
    struct syscall_rtc_info rtc;
    const char *weekday = "unknown";

    (void)argv;
    if (argc > 1) {
        write_err_usage("hwclock", "\n");
        return 1;
    }
    if (rtc_query(&rtc) <= 0 || !rtc.present) {
        write_err_str("hwclock: rtc unavailable\n");
        return 1;
    }

    if (rtc.weekday < sizeof(weekday_names) / sizeof(weekday_names[0])) {
        weekday = weekday_names[rtc.weekday];
    }

    write_str("rtc: ");
    write_dec(rtc.year);
    write_str("-");
    if (rtc.month < 10u) {
        write_str("0");
    }
    write_dec(rtc.month);
    write_str("-");
    text_write_padded2_local(rtc.day);
    write_str(" ");
    text_write_padded2_local(rtc.hour);
    write_str(":");
    text_write_padded2_local(rtc.minute);
    write_str(":");
    text_write_padded2_local(rtc.second);
    write_str("\n");

    write_str("weekday: ");
    write_str(weekday);
    write_str(" (");
    write_dec(rtc.weekday);
    write_str(")\n");

    write_str("unix: ");
    write_dec(rtc.unix_time);
    write_str("\n");

    write_str("mode: ");
    write_str(rtc.binary_mode ? "binary" : "bcd");
    write_str(", ");
    write_str(rtc.hour_24 ? "24h" : "12h");
    write_str("\n");

    write_str("status: updating=");
    write_dec(rtc.updating);
    write_str(" present=");
    write_dec(rtc.present);
    write_str(" valid=");
    write_dec(rtc.valid);
    write_str(" status_a=0x");
    write_hex_u32(rtc.status_a);
    write_str(" status_b=0x");
    write_hex_u32(rtc.status_b);
    write_str(" century=");
    write_dec(rtc.century);
    write_str(" raw_year=");
    write_dec(rtc.raw_year);
    write_str("\n");
    return 0;
}


int cmd_sleep(int argc, char **argv) {
    uint32_t wait_ticks;

    if (argc != 2 || !parse_sleep_ticks_local(argv[1], &wait_ticks)) {
        write_err_usage("sleep", " <seconds|ms|ticks>\n");
        return 1;
    }
    sleep(wait_ticks);
    return 0;
}

int cmd_watch(int argc, char **argv) {
    uint32_t wait_ticks = 1000u;
    int cmd_index = 1;

    if (argc >= 3 && streq_local(argv[1], "-n")) {
        if (!parse_sleep_ticks_local(argv[2], &wait_ticks)) {
            write_err_usage("watch", " [-n interval] <command> [args]\n");
            return 1;
        }
        cmd_index = 3;
    }
    if (argc <= cmd_index) {
        write_err_usage("watch", " [-n interval] <command> [args]\n");
        return 1;
    }

    for (;;) {
        uint32_t start;
        char ch = 0;
        int i;

        clear();
        write_str("Every ");
        write_dec(wait_ticks / 1000u);
        write_str(".");
        write_dec((wait_ticks % 1000u) / 100u);
        write_dec((wait_ticks % 1000u) / 10u % 10u);
        write_dec(wait_ticks % 10u);
        write_str("s: ");
        write_str(argv[cmd_index]);
        for (i = cmd_index + 1; i < argc; i++) {
            write_str(" ");
            write_str(argv[i]);
        }
        write_str("    (q to quit)\n\n");
        (void)cmdsuite_dispatch_main(argc - cmd_index, argv + cmd_index);

        start = ticks();
        while ((uint32_t)(ticks() - start) < wait_ticks) {
            if (read_char_nonblock(&ch) == 0) {
                if (ch == 'q' || ch == 'Q' || ch == 0x03) {
                    return 0;
                }
            }
            yield();
        }
    }
}


int cmd_clipboard(int argc, char **argv) {
    char text[4097];
    uint32_t pos;

    if (argc == 1 ||
        (argc == 2 && (streq_local(argv[1], "get") || streq_local(argv[1], "read")))) {
        int copied = clipboard_get(text, sizeof(text));

        if (copied < 0) {
            write_err_str("clipboard: read failed\n");
            return 1;
        }
        if (copied > 0) {
            write_stdout(text, (uint32_t)copied);
        }
        write_str("\n");
        return 0;
    }
    if (argc == 2 && streq_local(argv[1], "size")) {
        int size = clipboard_size();

        if (size < 0) {
            write_err_str("clipboard: size failed\n");
            return 1;
        }
        write_dec((uint32_t)size);
        write_str("\n");
        return 0;
    }
    if (argc == 2 && streq_local(argv[1], "clear")) {
        if (clipboard_clear() < 0) {
            write_err_str("clipboard: clear failed\n");
            return 1;
        }
        return 0;
    }
    if (argc >= 3 && (streq_local(argv[1], "set") || streq_local(argv[1], "write"))) {
        pos = 0u;
        for (int i = 2; i < argc && pos + 1u < sizeof(text); i++) {
            uint32_t len = str_len_local(argv[i]);

            if (i > 2 && pos + 1u < sizeof(text)) {
                text[pos++] = ' ';
            }
            for (uint32_t j = 0; j < len && pos + 1u < sizeof(text); j++) {
                text[pos++] = argv[i][j];
            }
        }
        text[pos] = '\0';
        if (clipboard_set(text, pos) < 0) {
            write_err_str("clipboard: set failed\n");
            return 1;
        }
        return 0;
    }
    write_err_usage("clipboard", " [get|set <text>|clear|size]\n");
    return 1;
}


int cmd_wc(int argc, char **argv) {
    char line[128];
    uint32_t lines = 0;
    uint32_t words = 0;
    uint32_t bytes = 0;
    uint32_t i;
    int fd = STDIN_FILENO;
    int in_word = 0;

    if (argc > 2) {
        write_err_usage("wc", " [path]\n");
        return 1;
    }
    if (argc == 2) {
        fd = cmd_open_resolved_path(argv[1], 0);
        if (fd < 0) {
            write_err_str("wc: open failed\n");
            return 1;
        }
    }
    for (;;) {
        uint32_t got = (uint32_t)read(fd, line, sizeof(line));

        if (got == 0) {
            break;
        }
        bytes += got;
        for (i = 0; i < got; i++) {
            char ch = line[i];

            if (ch == '\n') {
                lines++;
            }
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                in_word = 0;
            } else if (!in_word) {
                words++;
                in_word = 1;
            }
        }
        if (got < sizeof(line)) {
            break;
        }
    }
    if (fd != STDIN_FILENO) {
        close((uint32_t)fd);
    }
    printf("%u %u %u", lines, words, bytes);
    if (argc == 2) {
        printf(" %s", argv[1]);
    }
    write_str("\n");
    return 0;
}

int cmd_head(int argc, char **argv) {
    char line[128];
    uint32_t count = 10u;
    int fd;

    if (argc < 2 || argc > 3) {
        write_err_usage("head", " <path> [count]\n");
        return 1;
    }
    if (argc == 3) {
        if (!parse_u32_local(argv[2], &count)) {
            write_err_str("head: invalid count\n");
            return 1;
        }
    }
    fd = cmd_open_resolved_path(argv[1], 0);
    if (fd < 0) {
        write_err_str("head: open failed\n");
        return 1;
    }
    while (count != 0u) {
        uint32_t got = read_line((uint32_t)fd, line, sizeof(line));

        if (got == 0) {
            break;
        }
        write_str(line);
        write_str("\n");
        count--;
    }
    close((uint32_t)fd);
    return 0;
}

int cmd_tail(int argc, char **argv) {
    char lines[16][128];
    uint32_t count = 10u;
    uint32_t total = 0;
    uint32_t start;
    uint32_t i;
    char line[128];
    int fd;

    if (argc < 2 || argc > 3) {
        write_err_usage("tail", " <path> [count]\n");
        return 1;
    }
    if (argc == 3) {
        if (!parse_u32_local(argv[2], &count) || count == 0u || count > 16u) {
            write_err_str("tail: count must be 1..16\n");
            return 1;
        }
    }
    fd = cmd_open_resolved_path(argv[1], 0);
    if (fd < 0) {
        write_err_str("tail: open failed\n");
        return 1;
    }
    while (read_line((uint32_t)fd, line, sizeof(line)) > 0) {
        copy_line_local(lines[total % 16u], line, sizeof(lines[0]));
        total++;
    }
    close((uint32_t)fd);

    start = total > count ? total - count : 0u;
    for (i = start; i < total; i++) {
        write_str(lines[i % 16u]);
        write_str("\n");
    }
    return 0;
}

static int cmd_find_walk_local(const char *path, const char *needle) {
    struct syscall_dirent entry;
    char child[CMD_PATH_MAX];
    int fd;
    int found = 0;

    fd = opendir(path);
    if (fd < 0) {
        return 0;
    }
    while (readdir((uint32_t)fd, &entry) > 0) {
        if (streq_local(entry.name, ".") || streq_local(entry.name, "..")) {
            continue;
        }
        if (snprintf(child,
                     sizeof(child),
                     streq_local(path, "/") ? "/%s" : "%s/%s",
                     entry.name) < 0) {
            continue;
        }
        if (needle == NULL || needle[0] == '\0' || streq_local(entry.name, needle) ||
            text_contains_local(entry.name, needle)) {
            write_str(child);
            write_str("\n");
            found = 1;
        }
        if ((entry.attributes & 0x10u) != 0u) {
            if (cmd_find_walk_local(child, needle)) {
                found = 1;
            }
        }
    }
    close((uint32_t)fd);
    return found;
}

int cmd_find(int argc, char **argv) {
    const char *path = ".";
    const char *needle = NULL;

    if (argc > 3) {
        write_err_usage("find", " [path] [pattern]\n");
        return 1;
    }
    if (argc >= 2) {
        path = argv[1];
    }
    if (argc == 3) {
        needle = argv[2];
    }
    return cmd_find_walk_local(path, needle) ? 0 : 1;
}
