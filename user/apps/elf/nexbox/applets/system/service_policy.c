#include "user/apps/elf/nexbox/applets/system/service_policy.h"

static int text_equal(const char *a, const char *b) {
    uint32_t i = 0;

    if (a == 0 || b == 0) {
        return 0;
    }
    while (a[i] != '\0' && b[i] != '\0' && a[i] == b[i]) {
        i++;
    }
    return a[i] == b[i];
}

int service_policy_parse_restart(const char *text, enum service_restart_policy *out) {
    if (out == 0) {
        return 0;
    }
    if (text_equal(text, "never")) {
        *out = SERVICE_RESTART_NEVER;
    } else if (text_equal(text, "on-failure")) {
        *out = SERVICE_RESTART_ON_FAILURE;
    } else if (text_equal(text, "always")) {
        *out = SERVICE_RESTART_ALWAYS;
    } else {
        return 0;
    }
    return 1;
}

const char *service_policy_restart_name(enum service_restart_policy policy) {
    if (policy == SERVICE_RESTART_ALWAYS) {
        return "always";
    }
    if (policy == SERVICE_RESTART_ON_FAILURE) {
        return "on-failure";
    }
    return "never";
}

int service_policy_should_restart(enum service_restart_policy policy, int32_t exit_code) {
    return policy == SERVICE_RESTART_ALWAYS ||
           (policy == SERVICE_RESTART_ON_FAILURE && exit_code != 0);
}

int service_policy_retry_allowed(uint32_t restart_count, uint32_t max_retries) {
    return max_retries == 0u || restart_count < max_retries;
}

uint32_t service_policy_backoff(uint32_t base_ms, uint32_t restart_count) {
    uint32_t shift = restart_count > 6u ? 6u : restart_count;

    if (base_ms == 0u) {
        return 0u;
    }
    if (base_ms > 0xffffffffu >> shift) {
        return 0xffffffffu;
    }
    return base_ms << shift;
}
