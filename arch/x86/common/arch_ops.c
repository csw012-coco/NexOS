#include "kernel/public/arch/arch_ops.h"

#if defined(__i386__)
extern const struct arch_ops arch_x86_32_ops;
const struct arch_ops *arch = &arch_x86_32_ops;
#elif defined(__x86_64__)
extern const struct arch_ops arch_x86_64_ops;
const struct arch_ops *arch = &arch_x86_64_ops;
#else
const struct arch_ops *arch = 0;
#endif
