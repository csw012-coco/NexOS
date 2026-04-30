#pragma once

#include "kernel/internal/proc/process_reap_internal.h"

void session_finish(struct process_session *session, struct user_page_mapping *mappings);
int session_enter_ring3(struct process_session *session,
                        struct user_page_mapping *mappings,
                        uint64_t entry,
                        uint64_t stack_top);
int session_run_active_slice(struct process_session *session,
                             struct user_page_mapping *mappings,
                             uint64_t entry,
                             uint64_t stack_top,
                             int finish_on_exit);
