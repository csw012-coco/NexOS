#pragma once

#include <stddef.h>
#include <stdint.h>

#include "abi/syscall_abi.h"

#define IPC_CREATE SYS_IPC_CREATE
#define IPC_EXCL SYS_IPC_EXCL
#define IPC_NONBLOCK SYS_IPC_NONBLOCK

typedef int mqd_t;
typedef int sem_t;

mqd_t mq_open(const char *name, int flags);
int mq_unlink(const char *name);
int mq_send(mqd_t queue, const void *data, size_t size, int flags);
int mq_receive(mqd_t queue, void *data, size_t capacity, int flags);

sem_t sem_open(const char *name, unsigned int initial_value, int flags);
int sem_unlink(const char *name);
int sem_trywait(sem_t sem);
int sem_wait(sem_t sem);
int sem_post(sem_t sem);
