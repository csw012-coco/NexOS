#include "kernel/internal/sys/syscall_internal.h"

enum {
    IPC_NAME_MAX = 31,
    IPC_MQ_MAX = 16,
    IPC_MQ_DEPTH = 16,
    IPC_SEM_MAX = 32,
    IPC_SEM_VALUE_MAX = 0x7fffffffu
};

struct ipc_message {
    uint16_t size;
    uint8_t data[SYS_MQ_MESSAGE_MAX];
};

struct ipc_message_queue {
    uint8_t used;
    uint8_t linked;
    uint8_t head;
    uint8_t count;
    char name[IPC_NAME_MAX + 1];
    struct ipc_message messages[IPC_MQ_DEPTH];
};

struct ipc_semaphore {
    uint8_t used;
    uint8_t linked;
    char name[IPC_NAME_MAX + 1];
    uint32_t value;
};

static struct ipc_message_queue g_message_queues[IPC_MQ_MAX];
static struct ipc_semaphore g_semaphores[IPC_SEM_MAX];

static int ipc_copy_name(uint64_t user_name_addr, char *name) {
    if (!syscall_copy_user_cstr(name, user_name_addr, IPC_NAME_MAX + 1u)) {
        return 0;
    }
    return name[0] != '\0';
}

static void ipc_copy_name_local(char *dst, const char *src) {
    uint32_t i = 0;

    while (src[i] != '\0' && i < IPC_NAME_MAX) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int ipc_name_equal(const char *lhs, const char *rhs) {
    uint32_t i = 0;

    while (lhs[i] != '\0' && rhs[i] != '\0' && lhs[i] == rhs[i]) {
        i++;
    }
    return lhs[i] == rhs[i];
}

static struct ipc_message_queue *ipc_mq_from_handle(uint32_t handle) {
    if (handle == 0 || handle > IPC_MQ_MAX || !g_message_queues[handle - 1u].used) {
        return 0;
    }
    return &g_message_queues[handle - 1u];
}

uint64_t syscall_handle_mq_open(uint64_t user_name_addr, uint32_t flags) {
    char name[IPC_NAME_MAX + 1];
    uint32_t free_slot = IPC_MQ_MAX;

    if (!ipc_copy_name(user_name_addr, name)) {
        return (uint64_t)-1;
    }
    for (uint32_t i = 0; i < IPC_MQ_MAX; i++) {
        if (g_message_queues[i].used && g_message_queues[i].linked &&
            ipc_name_equal(g_message_queues[i].name, name)) {
            if ((flags & SYS_IPC_CREATE) && (flags & SYS_IPC_EXCL)) {
                return (uint64_t)-1;
            }
            return i + 1u;
        }
        if (!g_message_queues[i].used && free_slot == IPC_MQ_MAX) {
            free_slot = i;
        }
    }
    if ((flags & SYS_IPC_CREATE) == 0 || free_slot == IPC_MQ_MAX) {
        return (uint64_t)-1;
    }
    memset(&g_message_queues[free_slot], 0, sizeof(g_message_queues[free_slot]));
    g_message_queues[free_slot].used = 1;
    g_message_queues[free_slot].linked = 1;
    ipc_copy_name_local(g_message_queues[free_slot].name, name);
    return free_slot + 1u;
}

uint64_t syscall_handle_mq_unlink(uint64_t user_name_addr) {
    char name[IPC_NAME_MAX + 1];

    if (!ipc_copy_name(user_name_addr, name)) {
        return 0;
    }
    for (uint32_t i = 0; i < IPC_MQ_MAX; i++) {
        if (g_message_queues[i].used && g_message_queues[i].linked &&
            ipc_name_equal(g_message_queues[i].name, name)) {
            memset(&g_message_queues[i], 0, sizeof(g_message_queues[i]));
            return 1;
        }
    }
    return 0;
}

uint64_t syscall_handle_mq_send(uint32_t handle, uint64_t user_buffer_addr) {
    struct syscall_mq_buffer buffer;
    struct ipc_message_queue *queue = ipc_mq_from_handle(handle);
    struct ipc_message *message;
    uint32_t tail;

    if (queue == 0 || !syscall_copy_from_user(&buffer, user_buffer_addr, sizeof(buffer)) ||
        buffer.size == 0 || buffer.size > SYS_MQ_MESSAGE_MAX) {
        return (uint64_t)-1;
    }
    if (queue->count == IPC_MQ_DEPTH) {
        return 0;
    }
    tail = (queue->head + queue->count) % IPC_MQ_DEPTH;
    message = &queue->messages[tail];
    if (!syscall_copy_from_user(message->data, buffer.data_addr, buffer.size)) {
        return (uint64_t)-1;
    }
    message->size = (uint16_t)buffer.size;
    queue->count++;
    return buffer.size;
}

uint64_t syscall_handle_mq_receive(uint32_t handle, uint64_t user_buffer_addr) {
    struct syscall_mq_buffer buffer;
    struct ipc_message_queue *queue = ipc_mq_from_handle(handle);
    struct ipc_message *message;

    if (queue == 0 || !syscall_copy_from_user(&buffer, user_buffer_addr, sizeof(buffer)) ||
        buffer.size == 0) {
        return (uint64_t)-1;
    }
    if (queue->count == 0) {
        return 0;
    }
    message = &queue->messages[queue->head];
    if (buffer.size < message->size ||
        !syscall_copy_to_user(buffer.data_addr, message->data, message->size)) {
        return (uint64_t)-1;
    }
    buffer.size = message->size;
    if (!syscall_copy_to_user(user_buffer_addr, &buffer, sizeof(buffer))) {
        return (uint64_t)-1;
    }
    queue->head = (queue->head + 1u) % IPC_MQ_DEPTH;
    queue->count--;
    return message->size;
}

static struct ipc_semaphore *ipc_sem_from_handle(uint32_t handle) {
    if (handle == 0 || handle > IPC_SEM_MAX || !g_semaphores[handle - 1u].used) {
        return 0;
    }
    return &g_semaphores[handle - 1u];
}

uint64_t syscall_handle_sem_open(uint64_t user_name_addr, uint32_t initial_value, uint32_t flags) {
    char name[IPC_NAME_MAX + 1];
    uint32_t free_slot = IPC_SEM_MAX;

    if (!ipc_copy_name(user_name_addr, name) || initial_value > IPC_SEM_VALUE_MAX) {
        return (uint64_t)-1;
    }
    for (uint32_t i = 0; i < IPC_SEM_MAX; i++) {
        if (g_semaphores[i].used && g_semaphores[i].linked &&
            ipc_name_equal(g_semaphores[i].name, name)) {
            if ((flags & SYS_IPC_CREATE) && (flags & SYS_IPC_EXCL)) {
                return (uint64_t)-1;
            }
            return i + 1u;
        }
        if (!g_semaphores[i].used && free_slot == IPC_SEM_MAX) {
            free_slot = i;
        }
    }
    if ((flags & SYS_IPC_CREATE) == 0 || free_slot == IPC_SEM_MAX) {
        return (uint64_t)-1;
    }
    memset(&g_semaphores[free_slot], 0, sizeof(g_semaphores[free_slot]));
    g_semaphores[free_slot].used = 1;
    g_semaphores[free_slot].linked = 1;
    g_semaphores[free_slot].value = initial_value;
    ipc_copy_name_local(g_semaphores[free_slot].name, name);
    return free_slot + 1u;
}

uint64_t syscall_handle_sem_unlink(uint64_t user_name_addr) {
    char name[IPC_NAME_MAX + 1];

    if (!ipc_copy_name(user_name_addr, name)) {
        return 0;
    }
    for (uint32_t i = 0; i < IPC_SEM_MAX; i++) {
        if (g_semaphores[i].used && g_semaphores[i].linked &&
            ipc_name_equal(g_semaphores[i].name, name)) {
            memset(&g_semaphores[i], 0, sizeof(g_semaphores[i]));
            return 1;
        }
    }
    return 0;
}

uint64_t syscall_handle_sem_trywait(uint32_t handle) {
    struct ipc_semaphore *sem = ipc_sem_from_handle(handle);

    if (sem == 0) {
        return (uint64_t)-1;
    }
    if (sem->value == 0) {
        return 0;
    }
    sem->value--;
    return 1;
}

uint64_t syscall_handle_sem_post(uint32_t handle) {
    struct ipc_semaphore *sem = ipc_sem_from_handle(handle);

    if (sem == 0 || sem->value == IPC_SEM_VALUE_MAX) {
        return 0;
    }
    sem->value++;
    return 1;
}
