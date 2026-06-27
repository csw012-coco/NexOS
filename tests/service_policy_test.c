#include <assert.h>
#include <stdint.h>
#include "user/apps/elf/nexbox/applets/system/service_policy.h"

int main(void) {
    enum service_restart_policy policy;

    assert(service_policy_parse_restart("never", &policy) && policy == SERVICE_RESTART_NEVER);
    assert(service_policy_parse_restart("on-failure", &policy) && policy == SERVICE_RESTART_ON_FAILURE);
    assert(service_policy_parse_restart("always", &policy) && policy == SERVICE_RESTART_ALWAYS);
    assert(!service_policy_parse_restart("sometimes", &policy));
    assert(!service_policy_should_restart(SERVICE_RESTART_NEVER, 1));
    assert(!service_policy_should_restart(SERVICE_RESTART_ON_FAILURE, 0));
    assert(service_policy_should_restart(SERVICE_RESTART_ON_FAILURE, 1));
    assert(service_policy_should_restart(SERVICE_RESTART_ALWAYS, 0));
    assert(service_policy_retry_allowed(99, 0));
    assert(service_policy_retry_allowed(2, 3));
    assert(!service_policy_retry_allowed(3, 3));
    assert(service_policy_backoff(100, 0) == 100);
    assert(service_policy_backoff(100, 3) == 800);
    assert(service_policy_backoff(100, 20) == 6400);
    return 0;
}
