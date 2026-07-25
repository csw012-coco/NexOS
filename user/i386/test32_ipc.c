#include "test32_ipc.h"

int test32_ipc_mq_sem_case(void) {
    char mq_message[16];
    mqd_t mq;
    sem_t sem;

    mq = mq_open("test32.mq", IPC_CREATE | IPC_EXCL);
    if (mq <= 0 ||
        mq_send(mq, "mq32", 5u, IPC_NONBLOCK) != 0 ||
        mq_unlink("test32.mq") != 0 ||
        mq_open("test32.mq", 0) >= 0) {
        return 94;
    }
    memset(mq_message, 0, sizeof(mq_message));
    if (mq_receive(mq, mq_message, sizeof(mq_message), IPC_NONBLOCK) != 5 ||
        strcmp(mq_message, "mq32") != 0) {
        return 95;
    }
    sem = sem_open("test32.sem", 1u, IPC_CREATE | IPC_EXCL);
    if (sem <= 0 ||
        sem_unlink("test32.sem") != 0 ||
        sem_open("test32.sem", 0u, 0) >= 0 ||
        sem_trywait(sem) != 0 ||
        sem_trywait(sem) == 0 ||
        sem_post(sem) != 0 ||
        sem_wait(sem) != 0) {
        return 96;
    }
    if (puts("[test32] libc32 mq/sem syscalls OK") == EOF) {
        return 97;
    }
    return 0;
}
