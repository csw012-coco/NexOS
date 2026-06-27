#include "user/libc/include/stdio.h"
#include "user/libc/include/fcntl.h"
#include "user/libc/include/string.h"
#include "user/libc/include/unistd.h"
#include "user/libc/include/nexos/process.h"
#include "user/libc/include/nexos/system.h"
#include "user/libc/include/sys/ipc.h"
#include "user/libc/include/sys/mman.h"

enum {
    IPC_DEMO_MAGIC = 0x4e584950u,
    IPC_DEMO_WAIT_TICKS = 200u
};

struct ipc_demo_shared {
    uint32_t magic;
    uint32_t child_pid;
    uint32_t value;
    char text[64];
};

static int is_child_mode(int argc, char **argv) {
    return argc >= 2 && strcmp(argv[1], "--child") == 0;
}

static int cleanup(int shm_handle, void *shared, size_t shared_size) {
    int ok = 1;

    if (shared != MAP_FAILED && munmap(shared, shared_size) != 0) {
        ok = 0;
    }
    if (shm_handle >= 0 && shm_unlink("ipcdemo.mem") != 0) {
        ok = 0;
    }
    if (mq_unlink("ipcdemo.queue") != 0) {
        ok = 0;
    }
    if (sem_unlink("ipcdemo.ready") != 0) {
        ok = 0;
    }
    return ok;
}

static int find_child_info(uint32_t pid, struct syscall_process_info *out) {
    struct syscall_process_info info;

    if (out == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        if (proc_query(NEX_PROC_QUERY_JOBS, i, &info) > 0 && info.pid == pid) {
            *out = info;
            return 1;
        }
    }
    return 0;
}

static uint32_t capture_process_ids(uint32_t *snapshot, uint32_t capacity) {
    struct syscall_process_info info;
    uint32_t count = 0;

    for (uint32_t i = 0; i < capacity; i++) {
        snapshot[i] = 0;
    }
    for (uint32_t i = 0; i < NEX_PROC_SLOTS_MAX && count < capacity; i++) {
        if (proc_query(NEX_PROC_QUERY_ALL, i, &info) > 0 && info.pid != 0u) {
            snapshot[count++] = info.pid;
        }
    }
    return count;
}

static int pid_seen(const uint32_t *snapshot, uint32_t count, uint32_t pid) {
    for (uint32_t i = 0; i < count; i++) {
        if (snapshot[i] == pid) {
            return 1;
        }
    }
    return 0;
}

static uint32_t find_new_process_pid(const uint32_t *snapshot, uint32_t count) {
    struct syscall_process_info info;

    for (uint32_t i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        if (proc_query(NEX_PROC_QUERY_ALL, i, &info) > 0 &&
            info.pid != 0u &&
            !pid_seen(snapshot, count, info.pid)) {
            return info.pid;
        }
    }
    return 0u;
}

static int run_child(void) {
    static const char child_text[] = "hello from the child";
    static const char child_message[] = "shared memory is ready";
    struct ipc_demo_shared *shared;
    int shm_handle;
    mqd_t queue;
    sem_t ready;

    shm_handle = shm_open("ipcdemo.mem", 0, 0);
    queue = mq_open("ipcdemo.queue", 0);
    ready = sem_open("ipcdemo.ready", 0, 0);
    if (shm_handle < 0 || queue < 0 || ready < 0) {
        return 2;
    }
    shared = mmap(0,
                  sizeof(*shared),
                  PROT_READ | PROT_WRITE,
                  MAP_SHARED,
                  shm_handle,
                  0);
    if (shared == MAP_FAILED) {
        return 2;
    }

    shared->magic = IPC_DEMO_MAGIC;
    shared->child_pid = (uint32_t)getpid();
    shared->value = 21u * 2u;
    memcpy(shared->text, child_text, sizeof(child_text));
    if (sem_post(ready) != 0 ||
        mq_send(queue, child_message, sizeof(child_message), 0) != 0) {
        (void)munmap(shared, sizeof(*shared));
        return 2;
    }
    (void)munmap(shared, sizeof(*shared));
    return 0;
}

int main(int argc, char **argv) {
    static const char child_text[] = "hello from the child";
    static const char child_message[] = "shared memory is ready";
    struct ipc_demo_shared *shared = MAP_FAILED;
    char message[SYS_MQ_MESSAGE_MAX];
    int shm_handle = -1;
    mqd_t queue;
    sem_t ready;
    uint32_t child;
    int spawn_rc;
    int received;
    int passed;
    uint32_t deadline;

    if (is_child_mode(argc, argv)) {
        return run_child();
    }

    printf("ipcdemo: shared memory + semaphore + message queue\n");
    (void)shm_unlink("ipcdemo.mem");
    (void)mq_unlink("ipcdemo.queue");
    (void)sem_unlink("ipcdemo.ready");

    shm_handle = shm_open("ipcdemo.mem", sizeof(*shared), SHM_CREATE | SHM_EXCL);
    queue = mq_open("ipcdemo.queue", IPC_CREATE | IPC_EXCL);
    ready = sem_open("ipcdemo.ready", 0, IPC_CREATE | IPC_EXCL);
    if (shm_handle < 0 || queue < 0 || ready < 0) {
        eprintf("ipcdemo: object creation failed\n");
        (void)cleanup(shm_handle, shared, sizeof(*shared));
        return 1;
    }

    shared = mmap(0,
                  sizeof(*shared),
                  PROT_READ | PROT_WRITE,
                  MAP_SHARED,
                  shm_handle,
                  0);
    if (shared == MAP_FAILED) {
        eprintf("ipcdemo: mmap failed\n");
        (void)cleanup(shm_handle, shared, sizeof(*shared));
        return 1;
    }
    memset(shared, 0, sizeof(*shared));

    spawn_rc = spawn("/cmd/ipcdemo --child", NEX_SPAWN_AUTO, NEX_SPAWN_BACKGROUND);
    if (spawn_rc < 0) {
        eprintf("ipcdemo: child spawn failed rc=%d\n", spawn_rc);
        (void)cleanup(shm_handle, shared, sizeof(*shared));
        return 1;
    }
    child = (uint32_t)spawn_rc;

    deadline = ticks() + IPC_DEMO_WAIT_TICKS;
    for (;;) {
        struct syscall_process_info child_info;

        if (sem_trywait(ready) == 0) {
            break;
        }
        if (child != 0u && wait(child, &child_info) > 0) {
            eprintf("ipcdemo: child exited before signaling code=%d\n",
                    child_info.exit_code);
            (void)cleanup(shm_handle, shared, sizeof(*shared));
            return 1;
        }
        if (ticks() >= deadline) {
            if (find_child_info((uint32_t)child, &child_info)) {
                eprintf("ipcdemo: timed out waiting for child signal state=%u\n",
                        child_info.state);
            } else {
                eprintf("ipcdemo: timed out waiting for child signal\n");
            }
            if (child != 0u) {
                (void)kill(child);
            }
            (void)cleanup(shm_handle, shared, sizeof(*shared));
            return 1;
        }
        sleep(1u);
    }
    received = mq_receive(queue, message, sizeof(message), 0);
    if (received < 0) {
        eprintf("ipcdemo: message receive failed\n");
        (void)cleanup(shm_handle, shared, sizeof(*shared));
        return 1;
    }

    passed = shared->magic == IPC_DEMO_MAGIC &&
             (child == 0u || shared->child_pid == child) &&
             shared->value == 42u &&
             strcmp(shared->text, child_text) == 0 &&
             strcmp(message, child_message) == 0;

    printf("ipcdemo: child pid=%u value=%u\n", shared->child_pid, shared->value);
    printf("ipcdemo: shared=\"%s\"\n", shared->text);
    printf("ipcdemo: queue=\"%s\"\n", message);
    printf("ipcdemo: %s\n", passed ? "PASS" : "FAIL");
    {
        int serial = open("/dev/ttys0", O_WRONLY);
        const char *result = passed ? "ipcdemo: PASS\n" : "ipcdemo: FAIL\n";

        if (serial >= 0) {
            (void)write(serial, result, strlen(result));
            (void)close(serial);
        }
    }

    if (!cleanup(shm_handle, shared, sizeof(*shared))) {
        eprintf("ipcdemo: cleanup failed\n");
        passed = 0;
    }
    return passed ? 0 : 1;
}
