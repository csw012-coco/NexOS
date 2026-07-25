#include "kernel/internal/proc/process_internal_base.h"
#include "lib/string.h"

struct user_page_mapping g_user_page_mappings[USER_DYNAMIC_PAGE_LIMIT];
struct process_session g_user_session;
struct process_session *g_bound_session = &g_user_session;
struct user_page_mapping *g_bound_mappings = g_user_page_mappings;
uint64_t g_next_user_alloc = USER_ALLOC_BASE;

struct process_session *process32_bind_current_address_space(
    const struct process *process,
    const struct address_space *address_space,
    struct user_page_mapping *mappings) {
    if (process == 0 || address_space == 0 || mappings == 0) {
        process_bind_session(0, 0);
        return 0;
    }

    g_user_session.address_space = *address_space;
    g_user_session.process = *process;
    g_user_session.process.address_space = &g_user_session.address_space;
    process_bind_session(&g_user_session, mappings);
    return &g_user_session;
}

void process_bind_session(struct process_session *session,
                          struct user_page_mapping *mappings) {
    g_bound_session = session != 0 ? session : &g_user_session;
    g_bound_mappings = mappings != 0 ? mappings : g_user_page_mappings;
}

void addrspace_reset(struct address_space *address_space) {
    if (address_space == 0) {
        return;
    }
    memset(address_space, 0, sizeof(*address_space));
}
