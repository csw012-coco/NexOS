#pragma once

#include <stdint.h>

enum service_restart_policy {
    SERVICE_RESTART_NEVER = 0,
    SERVICE_RESTART_ON_FAILURE = 1,
    SERVICE_RESTART_ALWAYS = 2
};

int service_policy_parse_restart(const char *text, enum service_restart_policy *out);
const char *service_policy_restart_name(enum service_restart_policy policy);
int service_policy_should_restart(enum service_restart_policy policy, int32_t exit_code);
int service_policy_retry_allowed(uint32_t restart_count, uint32_t max_retries);
uint32_t service_policy_backoff(uint32_t base_ms, uint32_t restart_count);
