#pragma once

struct nex_action_entry;

int cmd_wrap_actions(int argc, char **argv);
int cmd_wrap_action(int argc, char **argv);
int cmd_wrap_mapper(int argc, char **argv);
const struct nex_action_entry *nex_action_find_for_invocation(const char *command, int argc, char **argv);
int nex_action_check_allowed(const struct nex_action_entry *action, const char *source_name);
