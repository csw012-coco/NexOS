#pragma once

#include <stdint.h>

struct process;

int process_command_resolve_exec_line(const struct process *process,
                                      char *line,
                                      uint32_t line_size);
int process_command_parse_argv(char *command,
                               const char *argv[],
                               uint32_t argv_max,
                               int *argc_out);
