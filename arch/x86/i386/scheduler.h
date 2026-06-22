#pragma once

typedef unsigned int uint32_t;
typedef signed int int32_t;

struct i386_irq_frame;
struct process_snapshot;
struct vfs;

int i386_scheduler_run(uint32_t entry0,
                       uint32_t stack0,
                       uint32_t root0,
                       uint32_t entry1,
                       uint32_t stack1,
                       uint32_t root1);
struct i386_irq_frame *i386_scheduler_tick(struct i386_irq_frame *frame);
uint32_t i386_scheduler_exit(struct i386_irq_frame *frame, int exit_code);
uint32_t i386_scheduler_yield(struct i386_irq_frame *frame);
uint32_t i386_scheduler_ticks(void);
uint32_t i386_scheduler_switches(void);
uint32_t i386_scheduler_completed(void);
uint32_t i386_scheduler_task_ticks(uint32_t task);
uint32_t i386_scheduler_task_root(uint32_t task);
uint32_t i386_scheduler_task_result(uint32_t task);
uint32_t i386_scheduler_current_pid(void);
int32_t i386_scheduler_open(struct vfs *vfs,
                            const char *path,
                            uint32_t flags);
int32_t i386_scheduler_read(struct vfs *vfs,
                            uint32_t fd,
                            void *buffer,
                            uint32_t size,
                            uint32_t flags);
int32_t i386_scheduler_close(uint32_t fd);
int i386_scheduler_process_snapshot(uint32_t task,
                                    struct process_snapshot *snapshot);
