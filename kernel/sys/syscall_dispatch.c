#include "abi/syscall_abi.h"
#include "kernel/public/sys/syscall_dispatch.h"

struct syscall_common_result syscall_dispatch_common(
    const struct syscall_register_request *request,
    const struct syscall_common_context *context) {
    struct syscall_common_result result = {
        .value = (uint32_t)-1,
        .action = SYSCALL_COMMON_RETURN
    };

    if (request == 0 || context == 0) {
        return result;
    }

    switch (request->number) {
        case SYS_EXIT:
            result.value = request->arg0;
            result.action = SYSCALL_COMMON_EXIT;
            break;
        case SYS_OPEN:
            if (context->open != 0) {
                result.value = context->open(context->opaque,
                                             request->arg0,
                                             request->arg1);
            }
            break;
        case SYS_READ:
            if (context->read != 0) {
                result.value = context->read(context->opaque,
                                             request->arg0,
                                             request->arg1,
                                             request->arg2,
                                             request->arg3);
            }
            break;
        case SYS_WRITE:
            if (context->write != 0) {
                result.value = context->write(context->opaque,
                                              request->arg0,
                                              request->arg1,
                                              request->arg2);
            }
            break;
        case SYS_CLOSE:
            if (context->close != 0) {
                result.value = context->close(context->opaque, request->arg0);
            }
            break;
        case SYS_DUP2:
            if (context->dup2 != 0) {
                result.value = context->dup2(context->opaque,
                                             request->arg0,
                                             request->arg1);
            }
            break;
        case SYS_PIPE:
            if (context->pipe != 0) {
                result.value = context->pipe(context->opaque,
                                             request->arg0);
            }
            break;
        case SYS_TICKS:
            result.value = context->ticks;
            break;
        case SYS_GETPID:
            result.value = context->pid;
            break;
        case SYS_YIELD:
            result.value = 0;
            result.action = SYSCALL_COMMON_YIELD;
            break;
        case SYS_PAGE_ALLOC:
            if (context->page_alloc != 0) {
                result.value = context->page_alloc(context->opaque);
            }
            break;
        case SYS_PAGE_FREE:
            if (context->page_free != 0) {
                result.value = context->page_free(context->opaque,
                                                  request->arg0);
            }
            break;
        case SYS_SPAWN:
            if (context->spawn != 0) {
                result.value = context->spawn(context->opaque,
                                              request->arg0,
                                              request->arg1,
                                              request->arg2);
            }
            break;
        case SYS_EXEC:
        case SYS_EXEC_REPLACE:
            if (context->exec != 0) {
                uint32_t spawned =
                    context->exec(context->opaque, request->arg0);

                if ((int32_t)spawned >= 0) {
                    result.value = 0u;
                    result.action = SYSCALL_COMMON_EXIT;
                }
            }
            break;
        case SYS_CHDIR:
            if (context->chdir != 0) {
                result.value = context->chdir(context->opaque,
                                              request->arg0);
            }
            break;
        case SYS_GETCWD:
            if (context->getcwd != 0) {
                result.value = context->getcwd(context->opaque,
                                               request->arg0,
                                               request->arg1);
            }
            break;
        case SYS_OPENDIR:
            if (context->opendir != 0) {
                result.value = context->opendir(context->opaque,
                                                request->arg0);
            }
            break;
        case SYS_READDIR:
            if (context->readdir != 0) {
                result.value = context->readdir(context->opaque,
                                                request->arg0,
                                                request->arg1);
            }
            break;
        default:
            break;
    }
    return result;
}
