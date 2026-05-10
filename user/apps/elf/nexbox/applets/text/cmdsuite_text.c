#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"
#include "user/apps/elf/nexbox/applets/fs/cmd_ls_shared.h"

#define CMD_CAT_BUFFER_SIZE 512u

static int starts_with_text_local(const char *text, const char *prefix) {
    uint32_t i = 0;

    if (text == NULL || prefix == NULL) {
        return 0;
    }
    while (prefix[i] != '\0') {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

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

static int text_contains_local(const char *text, const char *pattern) {
    uint32_t i = 0;

    if (text == NULL || pattern == NULL || pattern[0] == '\0') {
        return 0;
    }
    while (text[i] != '\0') {
        uint32_t j = 0;

        while (pattern[j] != '\0' && text[i + j] != '\0' && text[i + j] == pattern[j]) {
            j++;
        }
        if (pattern[j] == '\0') {
            return 1;
        }
        i++;
    }
    return 0;
}

int cmd_help(void) {
    write_str("cmd commands: help actions action mapper echo clear pwd env which type ls cat less hexdump grep date hwclock sleep watch on events wc head tail find as pick select sort-by count-by to view ed vi vim touch mv cp mkdir rmdir rm asm stat du tree file blk parts fdisk df mounts progs fatls fatfind fatread cpio mount umount hotplug run runelf runbg ps session service jobs wait alarm timeout kill fg bg reboot switch_root dmesg lspci ac97 rtl8139 rtl8139tx rtl8139rx arp route netstat ping dns dhcp ifconfig http wget nc audio tone wav mplay doctor nexctl sysinfo meminfo minfo uname cpuinfo dbg\n");
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

static int parse_sleep_ticks_local(const char *text, uint32_t *ticks_out) {
    char *endptr = 0;
    unsigned long value;

    if (text == 0 || text[0] == '\0' || ticks_out == 0) {
        return 0;
    }
    value = strtoul(text, &endptr, 10);
    if (endptr == text || value > 0xfffffffful) {
        return 0;
    }
    if (*endptr == '\0' || streq_local(endptr, "s")) {
        if (value > 4294967ul) {
            return 0;
        }
        *ticks_out = (uint32_t)(value * 1000ul);
        return 1;
    }
    if (streq_local(endptr, "ms")) {
        *ticks_out = (uint32_t)value;
        return 1;
    }
    if (streq_local(endptr, "tick") || streq_local(endptr, "ticks")) {
        *ticks_out = (uint32_t)(value * 10ul);
        return 1;
    }
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

static int on_token_has_dot_local(const char *text) {
    uint32_t i = 0;

    while (text != NULL && text[i] != '\0') {
        if (text[i] == '/') {
            return 0;
        }
        if (text[i] == '.') {
            return i != 0u;
        }
        i++;
    }
    return 0;
}

static int parse_on_interval_local(const char *text, uint32_t *ticks_out) {
    if (text == NULL || ticks_out == NULL) {
        return 0;
    }
    if (starts_with_text_local(text, "interval=")) {
        char *endptr = 0;
        unsigned long value = strtoul(text + 9, &endptr, 10);

        if (endptr == text + 9 || *endptr != '\0' || value > 0xfffffffful) {
            return 0;
        }
        *ticks_out = (uint32_t)value;
        return 1;
    }
    return parse_sleep_ticks_local(text, ticks_out);
}

static void on_print_event_file_local(const char *path) {
    char buffer[128];
    int fd = open(path, 0);

    if (fd < 0) {
        return;
    }
    for (;;) {
        uint32_t got = (uint32_t)read((uint32_t)fd, buffer, sizeof(buffer));

        if (got == 0u) {
            break;
        }
        write_stdout(buffer, got);
        if (got < sizeof(buffer)) {
            break;
        }
    }
    close((uint32_t)fd);
}

static uint32_t on_read_event_fd_local(int fd, char *buffer, uint32_t size) {
    uint32_t total = 0;

    if (fd < 0 || buffer == NULL || size == 0u) {
        return 0;
    }
    while (total + 1u < size) {
        uint32_t got = (uint32_t)read((uint32_t)fd, buffer + total, size - total - 1u);

        if (got == 0u) {
            break;
        }
        total += got;
        if (got < size - total - 1u) {
            break;
        }
    }
    buffer[total < size ? total : size - 1u] = '\0';
    return total;
}

static const char *on_keyboard_ascii_name_local(const char *key) {
    if (key == NULL || key[0] == '\0') {
        return NULL;
    }
    if (streq_local(key, "space")) {
        return "space";
    }
    if (streq_local(key, "tab")) {
        return "\\t";
    }
    if (streq_local(key, "any")) {
        return "any";
    }
    if (key[1] == '\0') {
        return key;
    }
    return NULL;
}

static int on_keyboard_event_matches_local(const char *events, const char *key) {
    char pattern[32];
    const char *ascii_name;

    if (events == NULL || key == NULL || !text_contains_local(events, "event input.keyboard ")) {
        return 0;
    }
    if (!text_contains_local(events, " state=press ")) {
        return 0;
    }
    ascii_name = on_keyboard_ascii_name_local(key);
    if (ascii_name == NULL) {
        return 0;
    }
    if (streq_local(ascii_name, "any")) {
        return 1;
    }
    copy_line_local(pattern, " key=", sizeof(pattern));
    {
        uint32_t pos = str_len_local(pattern);
        uint32_t i = 0;

        while (ascii_name[i] != '\0' && pos + 2u < sizeof(pattern)) {
            pattern[pos++] = ascii_name[i++];
        }
        pattern[pos++] = ' ';
        pattern[pos] = '\0';
    }
    return text_contains_local(events, pattern);
}

static int on_file_event_matches_local(const char *events, const char *path) {
    char pattern[CMD_PATH_MAX + 8u];
    uint32_t pos = 0;
    uint32_t i = 0;

    if (events == NULL || path == NULL || !text_contains_local(events, "event file.change ")) {
        return 0;
    }
    copy_line_local(pattern, " path=", sizeof(pattern));
    pos = str_len_local(pattern);
    while (path[i] != '\0' && pos + 2u < sizeof(pattern)) {
        pattern[pos++] = path[i++];
    }
    pattern[pos++] = ' ';
    pattern[pos] = '\0';
    return text_contains_local(events, pattern);
}

static int on_net_status_event_matches_local(const char *events, const char *state) {
    char pattern[32];
    uint32_t pos;
    uint32_t i = 0;

    if (events == NULL || state == NULL || !text_contains_local(events, "event net.status ")) {
        return 0;
    }
    if (streq_local(state, "any")) {
        return 1;
    }
    if (!streq_local(state, "up") && !streq_local(state, "down")) {
        return 0;
    }
    copy_line_local(pattern, " state=", sizeof(pattern));
    pos = str_len_local(pattern);
    while (state[i] != '\0' && pos + 2u < sizeof(pattern)) {
        pattern[pos++] = state[i++];
    }
    pattern[pos++] = ' ';
    pattern[pos] = '\0';
    return text_contains_local(events, pattern);
}

static int on_mouse_button_mask_local(const char *button, uint32_t *mask_out) {
    if (button == NULL || mask_out == NULL) {
        return 0;
    }
    if (streq_local(button, "any")) {
        *mask_out = 0;
        return 1;
    }
    if (streq_local(button, "left")) {
        *mask_out = 1u;
        return 1;
    }
    if (streq_local(button, "right")) {
        *mask_out = 2u;
        return 1;
    }
    if (streq_local(button, "middle")) {
        *mask_out = 4u;
        return 1;
    }
    return 0;
}

static uint32_t on_parse_u32_field_local(const char *text) {
    uint32_t value = 0;

    while (text != NULL && *text >= '0' && *text <= '9') {
        value = value * 10u + (uint32_t)(*text - '0');
        text++;
    }
    return value;
}

static int on_mouse_event_matches_local(const char *events, const char *button) {
    uint32_t mask;
    uint32_t i = 0;

    if (events == NULL || button == NULL || !on_mouse_button_mask_local(button, &mask)) {
        return 0;
    }
    while (events[i] != '\0') {
        if (starts_with_text_local(events + i, "event input.mouse ")) {
            uint32_t j = i;

            if (mask == 0u) {
                return 1;
            }
            while (events[j] != '\0' && events[j] != '\n') {
                if (starts_with_text_local(events + j, " buttons=")) {
                    uint32_t buttons = on_parse_u32_field_local(events + j + 9);

                    if ((buttons & mask) != 0u) {
                        return 1;
                    }
                    break;
                }
                j++;
            }
        }
        while (events[i] != '\0' && events[i] != '\n') {
            i++;
        }
        if (events[i] == '\n') {
            i++;
        }
    }
    return 0;
}

static int on_block_event_matches_local(const char *events, const char *op) {
    char pattern[32];
    uint32_t pos;
    uint32_t i = 0;

    if (events == NULL || op == NULL || !text_contains_local(events, "event block.change ")) {
        return 0;
    }
    if (streq_local(op, "any")) {
        return 1;
    }
    if (!streq_local(op, "add") && !streq_local(op, "partition")) {
        return 0;
    }
    copy_line_local(pattern, " op=", sizeof(pattern));
    pos = str_len_local(pattern);
    while (op[i] != '\0' && pos + 2u < sizeof(pattern)) {
        pattern[pos++] = op[i++];
    }
    pattern[pos++] = ' ';
    pattern[pos] = '\0';
    return text_contains_local(events, pattern);
}

enum {
    EVENT_JOB_COMMAND_MAX = 240u,
    EVENT_JOB_ID_MAX = 12u,
    EVENT_JOB_META_MAX = 512u
};

static const char *g_event_job_dir = "/HOME/EVENTS";

static int event_job_is_log_name_local(const char *name) {
    return text_contains_local(name, ".LOG") || text_contains_local(name, ".log");
}

static void event_job_make_id_local(char *out, uint32_t out_size) {
    uint32_t value = ticks() % 1000000u;

    if (out == NULL || out_size == 0u) {
        return;
    }
    (void)snprintf(out, out_size, "J%06u", value);
}

static int event_job_make_path_local(const char *id, const char *suffix, char *out, uint32_t out_size) {
    if (id == NULL || id[0] == '\0' || out == NULL || out_size == 0u) {
        return 0;
    }
    return snprintf(out, out_size, "%s/%s%s", g_event_job_dir, id, suffix != NULL ? suffix : "") >= 0;
}

static int event_job_append_arg_local(char *out, uint32_t out_size, const char *arg) {
    uint32_t len;
    uint32_t arg_len;

    if (out == NULL || out_size == 0u || arg == NULL) {
        return 0;
    }
    len = str_len_local(out);
    arg_len = str_len_local(arg);
    if (len + arg_len + 2u >= out_size) {
        return 0;
    }
    if (len != 0u) {
        out[len++] = ' ';
        out[len] = '\0';
    }
    copy_line_local(out + len, arg, out_size - len);
    return 1;
}

static int event_job_build_worker_command_local(int argc, char **argv, const char *id, char *out, uint32_t out_size) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (!event_job_append_arg_local(out, out_size, "/CMD/ON") ||
        !event_job_append_arg_local(out, out_size, "--event-job") ||
        !event_job_append_arg_local(out, out_size, id)) {
        return 0;
    }
    for (int i = 1, after_run = 0; i < argc; i++) {
        if (streq_local(argv[i], "run")) {
            after_run = 1;
        }
        if (!after_run && streq_local(argv[i], "--daemon")) {
            continue;
        }
        if (!after_run && streq_local(argv[i], "--event-job")) {
            i++;
            continue;
        }
        if (!event_job_append_arg_local(out, out_size, argv[i])) {
            return 0;
        }
    }
    return 1;
}

static void event_job_capture_pids_local(uint32_t *out) {
    struct syscall_process_info info;

    if (out == NULL) {
        return;
    }
    for (uint32_t i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        out[i] = 0u;
        if (proc_query(NEX_PROC_QUERY_ALL, i, &info) > 0) {
            out[i] = info.pid;
        }
    }
}

static int event_job_pid_seen_local(const uint32_t *snapshot, uint32_t pid) {
    if (snapshot == NULL) {
        return 0;
    }
    for (uint32_t i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        if (snapshot[i] == pid) {
            return 1;
        }
    }
    return 0;
}

static uint32_t event_job_find_new_pid_local(const uint32_t *snapshot) {
    struct syscall_process_info info;

    for (uint32_t i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        if (proc_query(NEX_PROC_QUERY_ALL, i, &info) <= 0) {
            continue;
        }
        if (info.pid != 0u && !event_job_pid_seen_local(snapshot, info.pid)) {
            return info.pid;
        }
    }
    return 0u;
}

static int event_job_find_process_local(uint32_t pid, struct syscall_process_info *out) {
    struct syscall_process_info info;

    for (uint32_t i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        if (proc_query(NEX_PROC_QUERY_ALL, i, &info) <= 0) {
            continue;
        }
        if (info.pid == pid) {
            if (out != NULL) {
                *out = info;
            }
            return 1;
        }
    }
    return 0;
}

static int event_job_read_file_local(const char *path, char *out, uint32_t out_size) {
    int fd;
    uint32_t total = 0;

    if (path == NULL || out == NULL || out_size == 0u) {
        return 0;
    }
    fd = open(path, 0);
    if (fd < 0) {
        out[0] = '\0';
        return 0;
    }
    while (total + 1u < out_size) {
        uint32_t got = (uint32_t)read((uint32_t)fd, out + total, out_size - total - 1u);

        if (got == 0u) {
            break;
        }
        total += got;
        if (got < out_size - total - 1u) {
            break;
        }
    }
    out[total] = '\0';
    close((uint32_t)fd);
    return 1;
}

static int event_job_meta_value_local(const char *meta, const char *key, char *out, uint32_t out_size) {
    uint32_t i = 0;
    uint32_t key_len = str_len_local(key);

    if (meta == NULL || key == NULL || out == NULL || out_size == 0u) {
        return 0;
    }
    while (meta[i] != '\0') {
        if ((i == 0u || meta[i - 1u] == '\n') && starts_with_text_local(meta + i, key) &&
            meta[i + key_len] == ' ') {
            uint32_t pos = 0;
            i += key_len + 1u;
            while (meta[i] != '\0' && meta[i] != '\n' && pos + 1u < out_size) {
                out[pos++] = meta[i++];
            }
            out[pos] = '\0';
            return 1;
        }
        while (meta[i] != '\0' && meta[i] != '\n') {
            i++;
        }
        if (meta[i] == '\n') {
            i++;
        }
    }
    return 0;
}

static int event_job_parse_pid_local(const char *meta, uint32_t *pid_out) {
    char value[16];

    if (!event_job_meta_value_local(meta, "pid", value, sizeof(value))) {
        return 0;
    }
    return parse_u32_local(value, pid_out);
}

static int event_job_write_meta_local(const char *id, uint32_t pid, const char *command) {
    char path[CMD_PATH_MAX];
    char log_path[CMD_PATH_MAX];
    int fd;

    if (!event_job_make_path_local(id, "", path, sizeof(path)) ||
        !event_job_make_path_local(id, ".LOG", log_path, sizeof(log_path))) {
        return 0;
    }
    fd = open(path, O_CREAT | O_TRUNC);
    if (fd < 0) {
        return 0;
    }
    fdprintf((uint32_t)fd, "id %s\npid %u\nlog %s\ncmd %s\n", id, pid, log_path, command != NULL ? command : "");
    close((uint32_t)fd);
    return 1;
}

static int event_job_attach_log_local(const char *id) {
    char log_path[CMD_PATH_MAX];
    int fd;

    (void)mkdir("/HOME");
    (void)mkdir(g_event_job_dir);
    if (!event_job_make_path_local(id, ".LOG", log_path, sizeof(log_path))) {
        return 0;
    }
    fd = open(log_path, O_CREAT | O_APPEND);
    if (fd < 0) {
        return 0;
    }
    (void)dup2(fd, 1);
    (void)dup2(fd, 2);
    if (fd > 2) {
        close((uint32_t)fd);
    }
    fdprintf(1, "event-job %s start tick=%u\n", id, ticks());
    return 1;
}

static int event_job_spawn_daemon_local(int argc, char **argv) {
    uint32_t before[NEX_PROC_SLOTS_MAX];
    uint32_t pid = 0u;
    char id[EVENT_JOB_ID_MAX];
    char command[EVENT_JOB_COMMAND_MAX];
    int rc;

    (void)mkdir("/HOME");
    (void)mkdir(g_event_job_dir);
    event_job_make_id_local(id, sizeof(id));
    if (!event_job_build_worker_command_local(argc, argv, id, command, sizeof(command))) {
        write_err_str("on: daemon command too long\n");
        return 1;
    }
    event_job_capture_pids_local(before);
    rc = spawn(command, SYS_SPAWN_ELF, SYS_SPAWN_BACKGROUND);
    if (rc != 0) {
        write_err_str("on: daemon spawn failed\n");
        return 1;
    }
    for (uint32_t i = 0; i < 100u; i++) {
        pid = event_job_find_new_pid_local(before);
        if (pid != 0u) {
            break;
        }
        yield();
    }
    (void)event_job_write_meta_local(id, pid, command);
    write_str("event job ");
    write_str(id);
    write_str(" started");
    if (pid != 0u) {
        write_str(" pid=");
        write_dec(pid);
    }
    write_str("\n");
    return 0;
}

static int on_run_command_local(int argc, char **argv) {
    char *action_argv[18];

    if (argc <= 0 || argv == NULL || argv[0] == NULL) {
        return 1;
    }
    if (on_token_has_dot_local(argv[0])) {
        if (argc + 2 > (int)(sizeof(action_argv) / sizeof(action_argv[0]))) {
            write_err_str("on: too many action arguments\n");
            return 1;
        }
        action_argv[0] = "action";
        action_argv[1] = "run";
        for (int i = 0; i < argc; i++) {
            action_argv[i + 2] = argv[i];
        }
        return cmdsuite_dispatch_main(argc + 2, action_argv);
    }
    return cmdsuite_dispatch_main(argc, argv);
}

int cmd_on(int argc, char **argv) {
    uint32_t wait_ticks = 1000u;
    int wait_ticks_set = 0;
    int verbose = 0;
    int single = 0;
    int argi = 1;
    int run_index;
    const char *event_job_id = NULL;

    for (int i = 1; i < argc; i++) {
        if (streq_local(argv[i], "run")) {
            break;
        }
        if (streq_local(argv[i], "--daemon")) {
            return event_job_spawn_daemon_local(argc, argv);
        }
        if (streq_local(argv[i], "--event-job")) {
            i++;
        }
    }

    while (argi < argc && argv[argi][0] == '-') {
        if (streq_local(argv[argi], "-n") && argi + 1 < argc) {
            if (!parse_on_interval_local(argv[argi + 1], &wait_ticks)) {
                write_err_usage("on", " [-n interval] [-1] <file.change|event.timer|event.input.keyboard|event.input.mouse|event.net.status|event.block.change> ... run <command> [args]\n");
                return 1;
            }
            wait_ticks_set = 1;
            argi += 2;
        } else if (streq_local(argv[argi], "-1")) {
            single = 1;
            argi++;
        } else if (streq_local(argv[argi], "-v")) {
            verbose = 1;
            argi++;
        } else if (streq_local(argv[argi], "--event-job") && argi + 1 < argc) {
            event_job_id = argv[argi + 1];
            argi += 2;
        } else {
            write_err_usage("on", " [-n interval] [-1] <file.change|event.timer|event.input.keyboard|event.input.mouse|event.net.status|event.block.change> ... run <command> [args]\n");
            return 1;
        }
    }
    if (event_job_id != NULL && !event_job_attach_log_local(event_job_id)) {
        write_err_str("on: cannot attach event job log\n");
        return 1;
    }
    if (argc - argi >= 4 && streq_local(argv[argi], "event.timer")) {
        if (starts_with_text_local(argv[argi + 1], "interval=")) {
            if (!parse_on_interval_local(argv[argi + 1], &wait_ticks)) {
                write_err_usage("on", " [-1] event.timer interval=<ms> run <command> [args]\n");
                return 1;
            }
            run_index = argi + 2;
        } else {
            run_index = argi + 1;
        }
        if (!streq_local(argv[run_index], "run") || run_index + 1 >= argc) {
            write_err_usage("on", " [-1] event.timer [interval=<ms>] run <command> [args]\n");
            return 1;
        }
        write_str("on: watching /event/timer every ");
        write_dec(wait_ticks);
        write_str("ms (q to quit)\n");
        for (;;) {
            uint32_t start = ticks();
            char ch = 0;

            while ((uint32_t)(ticks() - start) < wait_ticks) {
                if (read_char_nonblock(&ch) == 0) {
                    if (ch == 'q' || ch == 'Q' || ch == 0x03) {
                        return 0;
                    }
                }
                yield();
            }
            on_print_event_file_local("/event/timer");
            (void)on_run_command_local(argc - run_index - 1, argv + run_index + 1);
            if (single) {
                return 0;
            }
        }
    }
    if (argc - argi >= 3 && streq_local(argv[argi], "event.net.status")) {
        const char *state = "any";
        uint32_t net_wait_ticks = wait_ticks_set ? wait_ticks : 20u;

        run_index = argi + 1;
        while (run_index < argc && !streq_local(argv[run_index], "run")) {
            if (starts_with_text_local(argv[run_index], "state=")) {
                state = argv[run_index] + 6;
            } else if (starts_with_text_local(argv[run_index], "interval=")) {
                if (!parse_on_interval_local(argv[run_index], &net_wait_ticks)) {
                    write_err_usage("on", " [-1] event.net.status [state=<up|down|any>] [interval=<ms>] run <command> [args]\n");
                    return 1;
                }
            } else {
                write_err_usage("on", " [-1] event.net.status [state=<up|down|any>] [interval=<ms>] run <command> [args]\n");
                return 1;
            }
            run_index++;
        }
        if ((!streq_local(state, "up") && !streq_local(state, "down") && !streq_local(state, "any")) ||
            run_index >= argc || run_index + 1 >= argc) {
            write_err_usage("on", " [-1] event.net.status [state=<up|down|any>] [interval=<ms>] run <command> [args]\n");
            return 1;
        }
        write_str("on: watching /event/net/status state=");
        write_str(state);
        write_str(" (q to quit)\n");
        {
            int event_fd = open("/event/net/status", 0);

            if (event_fd < 0) {
                write_err_str("on: cannot open /event/net/status\n");
                return 1;
            }
            for (;;) {
                uint32_t start = ticks();
                char ch = 0;
                char events[512];

                while ((uint32_t)(ticks() - start) < net_wait_ticks) {
                    if (read_char_nonblock(&ch) == 0) {
                        if (ch == 'q' || ch == 'Q' || ch == 0x03) {
                            close((uint32_t)event_fd);
                            return 0;
                        }
                    }
                    yield();
                }
                if (on_read_event_fd_local(event_fd, events, sizeof(events)) == 0u) {
                    continue;
                }
                if (!on_net_status_event_matches_local(events, state)) {
                    continue;
                }
                if (verbose) {
                    write_str("event net.status state=");
                    write_str(state);
                    write_str("\n");
                }
                (void)on_run_command_local(argc - run_index - 1, argv + run_index + 1);
                if (single) {
                    close((uint32_t)event_fd);
                    return 0;
                }
            }
        }
    }
    if (argc - argi >= 3 && streq_local(argv[argi], "event.input.mouse")) {
        const char *button = "any";
        uint32_t mouse_wait_ticks = wait_ticks_set ? wait_ticks : 20u;
        uint32_t button_mask = 0;

        run_index = argi + 1;
        while (run_index < argc && !streq_local(argv[run_index], "run")) {
            if (starts_with_text_local(argv[run_index], "button=")) {
                button = argv[run_index] + 7;
            } else if (starts_with_text_local(argv[run_index], "interval=")) {
                if (!parse_on_interval_local(argv[run_index], &mouse_wait_ticks)) {
                    write_err_usage("on", " [-1] event.input.mouse [button=<left|right|middle|any>] [interval=<ms>] run <command> [args]\n");
                    return 1;
                }
            } else {
                write_err_usage("on", " [-1] event.input.mouse [button=<left|right|middle|any>] [interval=<ms>] run <command> [args]\n");
                return 1;
            }
            run_index++;
        }
        if (!on_mouse_button_mask_local(button, &button_mask) || run_index >= argc || run_index + 1 >= argc) {
            write_err_usage("on", " [-1] event.input.mouse [button=<left|right|middle|any>] [interval=<ms>] run <command> [args]\n");
            return 1;
        }
        (void)button_mask;
        write_str("on: watching /event/input/mouse button=");
        write_str(button);
        write_str(" (q to quit)\n");
        {
            int event_fd = open("/event/input/mouse", 0);

            if (event_fd < 0) {
                write_err_str("on: cannot open /event/input/mouse\n");
                return 1;
            }
            for (;;) {
                uint32_t start = ticks();
                char ch = 0;
                char events[512];

                while ((uint32_t)(ticks() - start) < mouse_wait_ticks) {
                    if (read_char_nonblock(&ch) == 0) {
                        if (ch == 'q' || ch == 'Q' || ch == 0x03) {
                            close((uint32_t)event_fd);
                            return 0;
                        }
                    }
                    yield();
                }
                if (on_read_event_fd_local(event_fd, events, sizeof(events)) == 0u) {
                    continue;
                }
                if (!on_mouse_event_matches_local(events, button)) {
                    continue;
                }
                if (verbose) {
                    write_str("event input.mouse button=");
                    write_str(button);
                    write_str("\n");
                }
                (void)on_run_command_local(argc - run_index - 1, argv + run_index + 1);
                if (single) {
                    close((uint32_t)event_fd);
                    return 0;
                }
            }
        }
    }
    if (argc - argi >= 3 && streq_local(argv[argi], "event.block.change")) {
        const char *op = "any";
        uint32_t block_wait_ticks = wait_ticks_set ? wait_ticks : 20u;

        run_index = argi + 1;
        while (run_index < argc && !streq_local(argv[run_index], "run")) {
            if (starts_with_text_local(argv[run_index], "op=")) {
                op = argv[run_index] + 3;
            } else if (starts_with_text_local(argv[run_index], "interval=")) {
                if (!parse_on_interval_local(argv[run_index], &block_wait_ticks)) {
                    write_err_usage("on", " [-1] event.block.change [op=<add|partition|any>] [interval=<ms>] run <command> [args]\n");
                    return 1;
                }
            } else {
                write_err_usage("on", " [-1] event.block.change [op=<add|partition|any>] [interval=<ms>] run <command> [args]\n");
                return 1;
            }
            run_index++;
        }
        if ((!streq_local(op, "add") && !streq_local(op, "partition") && !streq_local(op, "any")) ||
            run_index >= argc || run_index + 1 >= argc) {
            write_err_usage("on", " [-1] event.block.change [op=<add|partition|any>] [interval=<ms>] run <command> [args]\n");
            return 1;
        }
        write_str("on: watching /event/block/change op=");
        write_str(op);
        write_str(" (q to quit)\n");
        {
            int event_fd = open("/event/block/change", 0);

            if (event_fd < 0) {
                write_err_str("on: cannot open /event/block/change\n");
                return 1;
            }
            for (;;) {
                uint32_t start = ticks();
                char ch = 0;
                char events[512];

                while ((uint32_t)(ticks() - start) < block_wait_ticks) {
                    if (read_char_nonblock(&ch) == 0) {
                        if (ch == 'q' || ch == 'Q' || ch == 0x03) {
                            close((uint32_t)event_fd);
                            return 0;
                        }
                    }
                    yield();
                }
                if (on_read_event_fd_local(event_fd, events, sizeof(events)) == 0u) {
                    continue;
                }
                if (!on_block_event_matches_local(events, op)) {
                    continue;
                }
                if (verbose) {
                    write_str("event block.change op=");
                    write_str(op);
                    write_str("\n");
                }
                (void)on_run_command_local(argc - run_index - 1, argv + run_index + 1);
                if (single) {
                    close((uint32_t)event_fd);
                    return 0;
                }
            }
        }
    }
    if (argc - argi >= 4 && streq_local(argv[argi], "event.input.keyboard")) {
        const char *key = NULL;
        uint32_t keyboard_wait_ticks = wait_ticks_set ? wait_ticks : 20u;

        run_index = argi + 1;
        while (run_index < argc && !streq_local(argv[run_index], "run")) {
            if (starts_with_text_local(argv[run_index], "key=")) {
                key = argv[run_index] + 4;
            } else if (starts_with_text_local(argv[run_index], "interval=")) {
                if (!parse_on_interval_local(argv[run_index], &keyboard_wait_ticks)) {
                    write_err_usage("on", " [-1] event.input.keyboard key=<char|space|tab|any> [interval=<ms>] run <command> [args]\n");
                    return 1;
                }
            } else {
                write_err_usage("on", " [-1] event.input.keyboard key=<char|space|tab|any> [interval=<ms>] run <command> [args]\n");
                return 1;
            }
            run_index++;
        }
        if (key == NULL || on_keyboard_ascii_name_local(key) == NULL ||
            run_index >= argc || run_index + 1 >= argc) {
            write_err_usage("on", " [-1] event.input.keyboard key=<char|space|tab|any> [interval=<ms>] run <command> [args]\n");
            return 1;
        }
        write_str("on: watching /event/input/keyboard key=");
        write_str(key);
        write_str(" (q to quit)\n");
        {
            int event_fd = open("/event/input/keyboard", 0);

            if (event_fd < 0) {
                write_err_str("on: cannot open /event/input/keyboard\n");
                return 1;
            }
            for (;;) {
            uint32_t start = ticks();
            char ch = 0;
            char events[512];

            while ((uint32_t)(ticks() - start) < keyboard_wait_ticks) {
                if (read_char_nonblock(&ch) == 0) {
                    if (ch == 'q' || ch == 'Q' || ch == 0x03) {
                        close((uint32_t)event_fd);
                        return 0;
                    }
                }
                yield();
            }
            if (on_read_event_fd_local(event_fd, events, sizeof(events)) == 0u) {
                continue;
            }
            if (!on_keyboard_event_matches_local(events, key)) {
                continue;
            }
            if (verbose) {
                write_str("event input.keyboard key=");
                write_str(key);
                write_str("\n");
            }
            (void)on_run_command_local(argc - run_index - 1, argv + run_index + 1);
            if (single) {
                close((uint32_t)event_fd);
                return 0;
            }
        }
        }
    }
    if (argc - argi < 5 || !streq_local(argv[argi], "file.change")) {
        write_err_usage("on", " [-n interval] [-1] file.change <path> run <command> [args]\n");
        return 1;
    }
    run_index = argi + 3;
    if (!streq_local(argv[run_index], "run") || run_index + 1 >= argc) {
        write_err_usage("on", " [-n interval] [-1] file.change <path> run <command> [args]\n");
        return 1;
    }
    write_str("on: watching file.change ");
    write_str(argv[argi + 1]);
    write_str(" (q to quit)\n");
    {
        int event_fd = open("/event/file/change", 0);

        if (event_fd < 0) {
            write_err_str("on: cannot open /event/file/change\n");
            return 1;
        }
        for (;;) {
        uint32_t start = ticks();
        char ch = 0;
        char events[512];

        while ((uint32_t)(ticks() - start) < wait_ticks) {
            if (read_char_nonblock(&ch) == 0) {
                if (ch == 'q' || ch == 'Q' || ch == 0x03) {
                    close((uint32_t)event_fd);
                    return 0;
                }
            }
            yield();
        }
        if (on_read_event_fd_local(event_fd, events, sizeof(events)) == 0u) {
            continue;
        }
        if (!on_file_event_matches_local(events, argv[argi + 1])) {
            continue;
        }
        if (verbose) {
            write_str("event file.change path=");
            write_str(argv[argi + 1]);
            write_str("\n");
        }
        (void)on_run_command_local(argc - run_index - 1, argv + run_index + 1);
        if (single) {
            close((uint32_t)event_fd);
            return 0;
        }
    }
    }
}

static void events_write_state_local(uint32_t state) {
    switch (state) {
        case NEX_PROC_STATE_READY:
            write_str("ready");
            break;
        case NEX_PROC_STATE_RUNNING:
            write_str("running");
            break;
        case NEX_PROC_STATE_SLEEPING:
            write_str("sleeping");
            break;
        case NEX_PROC_STATE_STOPPED:
            write_str("stopped");
            break;
        case NEX_PROC_STATE_EXITED:
            write_str("exited");
            break;
        case NEX_PROC_STATE_WAITING:
            write_str("waiting");
            break;
        default:
            write_str("gone");
            break;
    }
}

static int events_open_meta_by_id_local(const char *id_arg, char *path_out, uint32_t path_size) {
    char id[EVENT_JOB_ID_MAX];
    uint32_t numeric_id;

    if (id_arg == NULL || path_out == NULL || path_size == 0u) {
        return 0;
    }
    if (id_arg[0] == 'J' || id_arg[0] == 'j') {
        copy_line_local(id, id_arg, sizeof(id));
    } else if (parse_u32_local(id_arg, &numeric_id)) {
        (void)snprintf(id, sizeof(id), "J%06u", numeric_id % 1000000u);
    } else {
        return 0;
    }
    return event_job_make_path_local(id, "", path_out, path_size);
}

static void events_print_log_file_local(const char *path) {
    int fd;
    char buffer[128];

    fd = open(path, 0);
    if (fd < 0) {
        write_err_str("events: log not found\n");
        return;
    }
    for (;;) {
        uint32_t got = (uint32_t)read((uint32_t)fd, buffer, sizeof(buffer));

        if (got == 0u) {
            break;
        }
        write_stdout(buffer, got);
        if (got < sizeof(buffer)) {
            break;
        }
    }
    close((uint32_t)fd);
}

static int cmd_events_jobs_local(void) {
    struct syscall_dirent entry;
    int fd;
    uint32_t count = 0;

    fd = opendir(g_event_job_dir);
    if (fd < 0) {
        write_str("event jobs\nID      PID   STATE      LOG                  COMMAND\n");
        write_str("jobs=0\n");
        return 0;
    }
    write_str("event jobs\nID      PID   STATE      LOG                  COMMAND\n");
    while (readdir((uint32_t)fd, &entry) > 0) {
        char path[CMD_PATH_MAX];
        char meta[EVENT_JOB_META_MAX];
        char value[EVENT_JOB_COMMAND_MAX];
        char log_path[CMD_PATH_MAX];
        uint32_t pid = 0;
        struct syscall_process_info info;
        int alive;

        if (entry.name[0] == '\0' || event_job_is_log_name_local(entry.name)) {
            continue;
        }
        if (!event_job_make_path_local(entry.name, "", path, sizeof(path)) ||
            !event_job_read_file_local(path, meta, sizeof(meta)) ||
            !event_job_parse_pid_local(meta, &pid)) {
            continue;
        }
        alive = event_job_find_process_local(pid, &info);
        count++;
        write_text_padded(entry.name, 8u);
        write_dec(pid);
        if (pid < 10u) {
            write_str("     ");
        } else if (pid < 100u) {
            write_str("    ");
        } else if (pid < 1000u) {
            write_str("   ");
        } else {
            write_str("  ");
        }
        events_write_state_local(alive ? info.state : 0u);
        write_str(alive && info.state == NEX_PROC_STATE_SLEEPING ? "     " : "      ");
        if (!event_job_meta_value_local(meta, "log", log_path, sizeof(log_path))) {
            copy_line_local(log_path, "-", sizeof(log_path));
        }
        write_text_padded(log_path, 21u);
        if (!event_job_meta_value_local(meta, "cmd", value, sizeof(value))) {
            copy_line_local(value, "-", sizeof(value));
        }
        write_str(value);
        write_str("\n");
    }
    close((uint32_t)fd);
    write_str("jobs=");
    write_dec(count);
    write_str("\n");
    return 0;
}

int cmd_events(int argc, char **argv) {
    const char *op = argc >= 2 ? argv[1] : "jobs";

    if (streq_local(op, "as-table")) {
        char *as_argv[] = {"as", "event"};

        if (argc != 2) {
            write_err_usage("events", " as-table\n");
            return 1;
        }
        return cmd_as(2, as_argv);
    }
    if (streq_local(op, "jobs") || streq_local(op, "list")) {
        return cmd_events_jobs_local();
    }
    if (streq_local(op, "log")) {
        char meta_path[CMD_PATH_MAX];
        char meta[EVENT_JOB_META_MAX];
        char log_path[CMD_PATH_MAX];

        if (argc < 3 || !events_open_meta_by_id_local(argv[2], meta_path, sizeof(meta_path))) {
            write_err_usage("events", " log <id>\n");
            return 1;
        }
        if (!event_job_read_file_local(meta_path, meta, sizeof(meta)) ||
            !event_job_meta_value_local(meta, "log", log_path, sizeof(log_path))) {
            write_err_str("events: job not found\n");
            return 1;
        }
        events_print_log_file_local(log_path);
        return 0;
    }
    if (streq_local(op, "stop")) {
        char meta_path[CMD_PATH_MAX];
        char meta[EVENT_JOB_META_MAX];
        uint32_t pid = 0;

        if (argc < 3) {
            write_err_usage("events", " stop <id|pid>\n");
            return 1;
        }
        if (events_open_meta_by_id_local(argv[2], meta_path, sizeof(meta_path)) &&
            event_job_read_file_local(meta_path, meta, sizeof(meta)) &&
            event_job_parse_pid_local(meta, &pid)) {
            /* got pid from metadata */
        } else if (!parse_u32_local(argv[2], &pid)) {
            write_err_usage("events", " stop <id|pid>\n");
            return 1;
        }
        if (pid == 0u || kill(pid) <= 0) {
            write_err_str("events: stop failed\n");
            return 1;
        }
        write_str("stopped event job pid=");
        write_dec(pid);
        write_str("\n");
        return 0;
    }
    write_err_usage("events", " [jobs|as-table|log <id>|stop <id|pid>]\n");
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

static int table_split_assignment_local(const char *text,
                                        char *name,
                                        uint32_t name_size,
                                        char *value,
                                        uint32_t value_size) {
    uint32_t pos = 0;
    uint32_t value_pos = 0;

    if (text == NULL || name == NULL || value == NULL || name_size == 0 || value_size == 0) {
        return 0;
    }
    while (text[pos] != '\0' && text[pos] != '=') {
        if (pos + 1u >= name_size) {
            return 0;
        }
        name[pos] = text[pos];
        pos++;
    }
    if (pos == 0 || text[pos] != '=') {
        return 0;
    }
    name[pos] = '\0';
    pos++;
    while (text[pos] != '\0') {
        if (value_pos + 1u >= value_size) {
            return 0;
        }
        value[value_pos++] = text[pos++];
    }
    value[value_pos] = '\0';
    return value_pos != 0;
}

static uint32_t table_token_at_local(const char *line,
                                     uint32_t wanted,
                                     char *out,
                                     uint32_t out_size) {
    uint32_t pos = 0;
    uint32_t index = 0;

    if (line == NULL || out == NULL || out_size == 0) {
        return 0;
    }
    out[0] = '\0';
    while (line[pos] != '\0') {
        uint32_t out_pos = 0;

        while (line[pos] == ' ' || line[pos] == '\t') {
            pos++;
        }
        if (line[pos] == '\0') {
            break;
        }
        if (line[pos] == '"') {
            pos++;
            while (line[pos] != '\0') {
                char ch = line[pos++];

                if (ch == '\\' && line[pos] != '\0') {
                    ch = line[pos++];
                } else if (ch == '"') {
                    break;
                }
                if (index == wanted && out_pos + 1u < out_size) {
                    out[out_pos++] = ch;
                }
            }
            while (line[pos] != '\0' && line[pos] != ' ' && line[pos] != '\t') {
                pos++;
            }
        } else {
            while (line[pos] != '\0' && line[pos] != ' ' && line[pos] != '\t') {
                if (index == wanted && out_pos + 1u < out_size) {
                    out[out_pos++] = line[pos];
                }
                pos++;
            }
        }
        if (index == wanted) {
            out[out_pos] = '\0';
            return out_pos;
        }
        index++;
    }
    return 0;
}

static int table_column_index_local(const char *header, const char *name, uint32_t *index_out) {
    char token[64];
    uint32_t index = 0;

    if (header == NULL || name == NULL || index_out == NULL) {
        return 0;
    }
    while (table_token_at_local(header, index, token, sizeof(token)) != 0) {
        if (streq_local(token, name)) {
            *index_out = index;
            return 1;
        }
        index++;
    }
    return 0;
}

static void event_table_write_token_local(const char *text) {
    uint32_t i = 0;
    int quote = 0;

    if (text == NULL || text[0] == '\0') {
        write_str("-");
        return;
    }
    while (text[i] != '\0') {
        if (text[i] == ' ' || text[i] == '\t' || text[i] == '"' || text[i] == '\\') {
            quote = 1;
            break;
        }
        i++;
    }
    if (quote) {
        write_str("\"");
    }
    i = 0;
    while (text[i] != '\0') {
        if (text[i] == '"' || text[i] == '\\') {
            write_str("\\");
        }
        write_stdout(&text[i], 1u);
        i++;
    }
    if (quote) {
        write_str("\"");
    }
}

static int event_table_field_value_local(const char *line, const char *key, char *out, uint32_t out_size) {
    uint32_t pos = 0;
    uint32_t key_len = str_len_local(key);

    if (line == NULL || key == NULL || out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    while (line[pos] != '\0') {
        while (line[pos] == ' ' || line[pos] == '\t') {
            pos++;
        }
        if (line[pos] == '\0') {
            break;
        }
        if ((pos == 0u || line[pos - 1u] == ' ' || line[pos - 1u] == '\t') &&
            strncmp(line + pos, key, key_len) == 0 && line[pos + key_len] == '=') {
            uint32_t out_pos = 0;

            pos += key_len + 1u;
            while (line[pos] != '\0' && line[pos] != ' ' && line[pos] != '\t' && out_pos + 1u < out_size) {
                out[out_pos++] = line[pos++];
            }
            out[out_pos] = '\0';
            return out_pos != 0u;
        }
        while (line[pos] != '\0' && line[pos] != ' ' && line[pos] != '\t') {
            pos++;
        }
    }
    return 0;
}

static void event_table_write_field_local(const char *line, const char *key) {
    char value[64];

    if (event_table_field_value_local(line, key, value, sizeof(value))) {
        event_table_write_token_local(value);
    } else {
        write_str("-");
    }
}

static int event_table_type_local(const char *line, char *out, uint32_t out_size) {
    const char *cursor = line;

    if (line == NULL || out == NULL || out_size == 0u || !starts_with(line, "event ")) {
        return 0;
    }
    cursor += 6;
    return table_token_at_local(cursor, 0, out, out_size) != 0u;
}

static int cmd_as_event_table_local(void) {
    char line[256];
    char type[40];

    write_str("# nex/type: table\n");
    write_str("# nex/columns: type seq tick state key code shift ctrl caps op path bytes id dx dy buttons disk part dev speed source\n");
    write_str("type seq tick state key code shift ctrl caps op path bytes id dx dy buttons disk part dev speed source\n");
    while (read_line(STDIN_FILENO, line, sizeof(line)) != 0u) {
        if (!event_table_type_local(line, type, sizeof(type))) {
            continue;
        }
        event_table_write_token_local(type);
        write_str(" ");
        event_table_write_field_local(line, "seq");
        write_str(" ");
        event_table_write_field_local(line, "tick");
        write_str(" ");
        event_table_write_field_local(line, "state");
        write_str(" ");
        event_table_write_field_local(line, "key");
        write_str(" ");
        event_table_write_field_local(line, "code");
        write_str(" ");
        event_table_write_field_local(line, "shift");
        write_str(" ");
        event_table_write_field_local(line, "ctrl");
        write_str(" ");
        event_table_write_field_local(line, "caps");
        write_str(" ");
        event_table_write_field_local(line, "op");
        write_str(" ");
        event_table_write_field_local(line, "path");
        write_str(" ");
        event_table_write_field_local(line, "bytes");
        write_str(" ");
        event_table_write_field_local(line, "id");
        write_str(" ");
        event_table_write_field_local(line, "dx");
        write_str(" ");
        event_table_write_field_local(line, "dy");
        write_str(" ");
        event_table_write_field_local(line, "buttons");
        write_str(" ");
        event_table_write_field_local(line, "disk");
        write_str(" ");
        event_table_write_field_local(line, "part");
        write_str(" ");
        event_table_write_field_local(line, "dev");
        write_str(" ");
        event_table_write_field_local(line, "speed");
        write_str(" ");
        event_table_write_field_local(line, "source");
        write_str("\n");
    }
    return 0;
}

int cmd_as(int argc, char **argv) {
    char line[256];
    uint32_t got;

    if (argc != 2 || (!streq_local(argv[1], "table") && !streq_local(argv[1], "event"))) {
        write_err_usage("as", " <table|event>\n");
        return 1;
    }
    if (streq_local(argv[1], "event")) {
        return cmd_as_event_table_local();
    }
    got = read_line(STDIN_FILENO, line, sizeof(line));
    if (got == 0u) {
        return 0;
    }
    write_str("# nex/type: table\n");
    write_str("# nex/columns: ");
    write_str(line);
    write_str("\n");
    write_str(line);
    write_str("\n");
    while (read_line(STDIN_FILENO, line, sizeof(line)) != 0u) {
        write_str(line);
        write_str("\n");
    }
    return 0;
}

int cmd_pick(int argc, char **argv) {
    char name[32];
    char value[64];
    char header[256];
    char line[256];
    char token[64];
    uint32_t column = 0;
    int have_header = 0;

    if (argc != 2 || !table_split_assignment_local(argv[1], name, sizeof(name), value, sizeof(value))) {
        write_err_usage("pick", " <column=value>\n");
        return 1;
    }
    while (read_line(STDIN_FILENO, line, sizeof(line)) != 0u) {
        if (starts_with(line, "# nex/type:") || starts_with(line, "# nex/columns:")) {
            write_str(line);
            write_str("\n");
            continue;
        }
        if (!have_header) {
            copy_line_local(header, line, sizeof(header));
            if (!table_column_index_local(header, name, &column)) {
                write_err_str("pick: column not found: ");
                write_err_str(name);
                write_err_str("\n");
                return 1;
            }
            write_str(header);
            write_str("\n");
            have_header = 1;
            continue;
        }
        if (table_token_at_local(line, column, token, sizeof(token)) != 0 && streq_local(token, value)) {
            write_str(line);
            write_str("\n");
        }
    }
    return have_header ? 0 : 1;
}

enum {
    TABLE_PIPE_ROW_MAX = 48u,
    TABLE_PIPE_LINE_MAX = 256u,
    TABLE_PIPE_COL_MAX = 12u
};

static char g_table_pipe_header[TABLE_PIPE_LINE_MAX];
static char g_table_pipe_rows[TABLE_PIPE_ROW_MAX][TABLE_PIPE_LINE_MAX];
static uint32_t g_table_pipe_row_count;

static int table_read_input_local(void) {
    char line[TABLE_PIPE_LINE_MAX];
    int have_header = 0;

    g_table_pipe_header[0] = '\0';
    g_table_pipe_row_count = 0;
    while (read_line(STDIN_FILENO, line, sizeof(line)) != 0u) {
        if (starts_with(line, "# nex/type:") || starts_with(line, "# nex/columns:")) {
            continue;
        }
        if (!have_header) {
            copy_line_local(g_table_pipe_header, line, sizeof(g_table_pipe_header));
            have_header = 1;
            continue;
        }
        if (g_table_pipe_row_count >= TABLE_PIPE_ROW_MAX) {
            write_err_str("table: too many rows\n");
            return 0;
        }
        copy_line_local(g_table_pipe_rows[g_table_pipe_row_count],
                        line,
                        sizeof(g_table_pipe_rows[g_table_pipe_row_count]));
        g_table_pipe_row_count++;
    }
    return have_header;
}

static void table_write_meta_local(const char *header) {
    write_str("# nex/type: table\n");
    write_str("# nex/columns: ");
    write_str(header);
    write_str("\n");
}

static void table_write_headered_local(const char *header) {
    table_write_meta_local(header);
    write_str(header);
    write_str("\n");
}

static int table_compare_text_local(const char *a, const char *b) {
    uint32_t i = 0;

    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] < b[i]) {
            return -1;
        }
        if (a[i] > b[i]) {
            return 1;
        }
        i++;
    }
    if (a[i] == b[i]) {
        return 0;
    }
    return a[i] == '\0' ? -1 : 1;
}

static void table_append_selected_token_local(char *line_out,
                                              uint32_t line_size,
                                              uint32_t *pos_io,
                                              const char *token) {
    uint32_t i = 0;
    int quote = 0;

    if (line_out == NULL || pos_io == NULL || token == NULL || line_size == 0) {
        return;
    }
    if (*pos_io != 0 && *pos_io + 1u < line_size) {
        line_out[(*pos_io)++] = ' ';
    }
    while (token[i] != '\0') {
        if (token[i] == ' ' || token[i] == '\t' || token[i] == '"' || token[i] == '\\') {
            quote = 1;
            break;
        }
        i++;
    }
    i = 0;
    if (quote && *pos_io + 1u < line_size) {
        line_out[(*pos_io)++] = '"';
    }
    while (token[i] != '\0' && *pos_io + 1u < line_size) {
        if ((token[i] == '"' || token[i] == '\\') && *pos_io + 2u < line_size) {
            line_out[(*pos_io)++] = '\\';
        }
        line_out[(*pos_io)++] = token[i++];
    }
    if (quote && *pos_io + 1u < line_size) {
        line_out[(*pos_io)++] = '"';
    }
    line_out[*pos_io] = '\0';
}

int cmd_select(int argc, char **argv) {
    uint32_t columns[TABLE_PIPE_COL_MAX];
    uint32_t selected = 0;
    char out_line[TABLE_PIPE_LINE_MAX];
    char token[64];
    uint32_t pos = 0;

    if (argc < 2 || argc > (int)(TABLE_PIPE_COL_MAX + 1u)) {
        write_err_usage("select", " <column> [column...]\n");
        return 1;
    }
    if (!table_read_input_local()) {
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        if (!table_column_index_local(g_table_pipe_header, argv[i], &columns[selected])) {
            write_err_str("select: column not found: ");
            write_err_str(argv[i]);
            write_err_str("\n");
            return 1;
        }
        selected++;
    }
    out_line[0] = '\0';
    for (uint32_t i = 0; i < selected; i++) {
        (void)table_token_at_local(g_table_pipe_header, columns[i], token, sizeof(token));
        table_append_selected_token_local(out_line, sizeof(out_line), &pos, token);
    }
    table_write_headered_local(out_line);
    for (uint32_t r = 0; r < g_table_pipe_row_count; r++) {
        pos = 0;
        out_line[0] = '\0';
        for (uint32_t i = 0; i < selected; i++) {
            (void)table_token_at_local(g_table_pipe_rows[r], columns[i], token, sizeof(token));
            table_append_selected_token_local(out_line, sizeof(out_line), &pos, token);
        }
        write_str(out_line);
        write_str("\n");
    }
    return 0;
}

int cmd_sort_by(int argc, char **argv) {
    uint32_t column = 0;
    char left[64];
    char right[64];

    if (argc != 2) {
        write_err_usage("sort-by", " <column>\n");
        return 1;
    }
    if (!table_read_input_local()) {
        return 1;
    }
    if (!table_column_index_local(g_table_pipe_header, argv[1], &column)) {
        write_err_str("sort-by: column not found: ");
        write_err_str(argv[1]);
        write_err_str("\n");
        return 1;
    }
    for (uint32_t i = 0; i < g_table_pipe_row_count; i++) {
        for (uint32_t j = i + 1u; j < g_table_pipe_row_count; j++) {
            char tmp[TABLE_PIPE_LINE_MAX];

            (void)table_token_at_local(g_table_pipe_rows[i], column, left, sizeof(left));
            (void)table_token_at_local(g_table_pipe_rows[j], column, right, sizeof(right));
            if (table_compare_text_local(left, right) > 0) {
                copy_line_local(tmp, g_table_pipe_rows[i], sizeof(tmp));
                copy_line_local(g_table_pipe_rows[i], g_table_pipe_rows[j], sizeof(g_table_pipe_rows[i]));
                copy_line_local(g_table_pipe_rows[j], tmp, sizeof(g_table_pipe_rows[j]));
            }
        }
    }
    table_write_headered_local(g_table_pipe_header);
    for (uint32_t i = 0; i < g_table_pipe_row_count; i++) {
        write_str(g_table_pipe_rows[i]);
        write_str("\n");
    }
    return 0;
}

int cmd_count_by(int argc, char **argv) {
    uint32_t column = 0;
    uint8_t counted[TABLE_PIPE_ROW_MAX];
    char value[64];
    char other[64];

    if (argc != 2) {
        write_err_usage("count-by", " <column>\n");
        return 1;
    }
    if (!table_read_input_local()) {
        return 1;
    }
    if (!table_column_index_local(g_table_pipe_header, argv[1], &column)) {
        write_err_str("count-by: column not found: ");
        write_err_str(argv[1]);
        write_err_str("\n");
        return 1;
    }
    for (uint32_t i = 0; i < TABLE_PIPE_ROW_MAX; i++) {
        counted[i] = 0;
    }
    table_write_headered_local("value count");
    for (uint32_t i = 0; i < g_table_pipe_row_count; i++) {
        uint32_t count = 0;
        char out_line[TABLE_PIPE_LINE_MAX];
        uint32_t pos = 0;

        if (counted[i]) {
            continue;
        }
        (void)table_token_at_local(g_table_pipe_rows[i], column, value, sizeof(value));
        for (uint32_t j = i; j < g_table_pipe_row_count; j++) {
            (void)table_token_at_local(g_table_pipe_rows[j], column, other, sizeof(other));
            if (streq_local(value, other)) {
                counted[j] = 1;
                count++;
            }
        }
        out_line[0] = '\0';
        table_append_selected_token_local(out_line, sizeof(out_line), &pos, value);
        write_str(out_line);
        write_str(" ");
        write_dec(count);
        write_str("\n");
    }
    return 0;
}

static void table_write_json_string_local(const char *text) {
    uint32_t i = 0;

    write_str("\"");
    while (text != NULL && text[i] != '\0') {
        if (text[i] == '"' || text[i] == '\\') {
            write_stdout("\\", 1);
        }
        write_stdout(&text[i], 1);
        i++;
    }
    write_str("\"");
}

int cmd_to(int argc, char **argv) {
    char headers[TABLE_PIPE_COL_MAX][64];
    uint32_t header_count = 0;
    char token[64];

    if (argc != 2 || !streq_local(argv[1], "json")) {
        write_err_usage("to", " json\n");
        return 1;
    }
    if (!table_read_input_local()) {
        return 1;
    }
    while (header_count < TABLE_PIPE_COL_MAX &&
           table_token_at_local(g_table_pipe_header, header_count, headers[header_count], sizeof(headers[header_count])) != 0) {
        header_count++;
    }
    write_str("[\n");
    for (uint32_t r = 0; r < g_table_pipe_row_count; r++) {
        write_str("  {");
        for (uint32_t c = 0; c < header_count; c++) {
            if (c != 0) {
                write_str(", ");
            }
            table_write_json_string_local(headers[c]);
            write_str(": ");
            (void)table_token_at_local(g_table_pipe_rows[r], c, token, sizeof(token));
            table_write_json_string_local(token);
        }
        write_str(r + 1u == g_table_pipe_row_count ? "}\n" : "},\n");
    }
    write_str("]\n");
    return 0;
}

int cmd_view(int argc, char **argv) {
    if (argc != 2 || !streq_local(argv[1], "table")) {
        write_err_usage("view", " table\n");
        return 1;
    }
    if (!table_read_input_local()) {
        return 1;
    }
    write_str(g_table_pipe_header);
    write_str("\n");
    for (uint32_t i = 0; i < g_table_pipe_row_count; i++) {
        write_str(g_table_pipe_rows[i]);
        write_str("\n");
    }
    return 0;
}
