#pragma once

#include "kernel/internal/proc/process_reap_internal.h"

void session_finish(struct process_session *session, struct user_page_mapping *mappings);
int session_bind_user_context(struct process_session *session,
                              struct user_page_mapping *mappings);
void session_prepare_user_return_context(struct process_session *session,
                                         struct user_page_mapping *mappings);
int session_prepare_user_frame_return(struct process_session *session,
                                      struct user_page_mapping *mappings,
                                      const struct syscall_frame *frame);
void session_abort_user_frame_return(struct process_session *session,
                                     struct user_page_mapping *mappings);
int session_enter_ring3(struct process_session *session,
                        struct user_page_mapping *mappings,
                        uint64_t entry,
                        uint64_t stack_top);
int session_run_active_slice(struct process_session *session,
                             struct user_page_mapping *mappings,
                             uint64_t entry,
                             uint64_t stack_top,
                             int finish_on_exit);
