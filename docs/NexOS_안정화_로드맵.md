# NexOS 안정화 로드맵

NexOS를 Linux/NT/XNU 같은 현대 OS처럼 안정적으로 만들려면, **기능을 많이 넣는 것보다 오류가 전체 시스템으로 전파되지 않도록 구조화하는 것**이 먼저다.

현대 OS가 안정적인 이유도 코드가 완벽해서가 아니라, 오류를 발견하고 격리하고 복구하는 장치가 여러 겹으로 구성되어 있기 때문이다.

---

# 2026-09 현재 진행 상태

이 문서는 목표 방향을 설명하는 로드맵이다. 아래 표는 현재 코드 기준의 구현 상태를 요약한다.

| 영역 | 상태 | 현재 구현/근거 | 남은 일 |
| --- | --- | --- | --- |
| Kernel API 경계 | 부분 완료 | `check-deps`가 syscall 계층의 HAL 직접 호출 금지와 ABI 위치를 검사한다. VFS/MM/proc/driver public API도 일부 정리되어 있다. | 전역 상태와 cross-layer shortcut을 계속 줄이고, 경계 위반 검사를 더 넓힌다. |
| IRQ 최소화 | 부분 완료 | display service pending, block async queue 등 deferred 형태가 일부 있다. | IRQ handler별 책임을 문서화하고, IRQ 안에서 무거운 작업을 하지 않는 규칙을 점검한다. |
| 메모리 관리자 방어성 | 부분 완료 | PMM refcount, invalid/free/reuse stress, paging root validation, canonical/alignment/overflow 검사, COW cleanup 검증이 있다. | kernel stack guard page, heap guard, slab poisoning, stack canary, debug-only expensive validation은 아직 필요하다. |
| User pointer 검증 | 거의 완료 | `vmm_copy_from_user()`, `vmm_copy_to_user()`, `vmm_copy_user_cstr()`가 user range, mapping, 권한, overflow를 검사한다. syscall arch copy ops도 이 계층을 사용한다. | 모든 syscall 신규 경로가 이 계층을 우회하지 않는지 계속 검사한다. |
| User fault 격리 | 거의 완료 | x86_64 user `#PF`, `#UD`, divide error, GPF는 현재 프로세스 종료와 wait status로 격리된다. `mmstress`와 i386 TEST32가 fault cleanup을 검증한다. | signal-like fault reason 정리와 i386/x86_64 parity를 계속 맞춘다. |
| Panic/process fault 분리 | 거의 완료 | user fault는 `proc: fatal user exception` 후 프로세스 종료, kernel fault는 panic 진단으로 분리되어 있다. | kernel/user boundary에서 애매한 nested context 케이스를 계속 stress한다. |
| Lock 정책/SMP 준비 | 미완에 가까움 | 일부 spinlock과 request lock은 존재한다. | lock ordering, owner, IRQ 접근 규칙, lock checking 문서가 필요하다. SMP 전 필수 작업이다. |
| 전역 상태 축소 | 부분 완료 | process/session/address-space context 전달이 늘어났다. | TTY, driver, block, USB, framebuffer의 남은 전역 상태와 소유권을 재점검한다. |
| 드라이버 timeout/recovery | 부분 완료 | xHCI reset/run timeout, blockdev timeout/failure 기록, 일부 audio wait timeout이 있다. | 모든 드라이버에 retry limit, malformed descriptor 검증, unplug, reset, recovery 정책을 일관 적용한다. |
| 로그 시스템 | 부분 완료 | `kprint`, boot trace, security/capability 로그, panic trace가 있다. | severity/subsystem 기반 structured logging은 아직 없다. |
| Panic 진단 | 부분 완료 | CPU state, fault address, process, syscall trace, scheduler trace, paging detail 출력이 있다. | symbolized backtrace/stack trace가 아직 핵심 미완이다. |
| Debug/Release 분리 | 미완 | 일반 빌드와 smoke gate는 있다. | debug assertions, poisoning, lock checking, bounds checking을 debug build에 묶는 체계가 필요하다. |
| Regression test | 많이 진행 | `check-stabilization-*`, x86_64/i386 smoke, PMM/VMM stress, boot loop target이 있다. | long soak, driver unplug/recovery, filesystem stress, release-candidate loop를 더 자동화한다. |

현재 우선순위는 새 대형 기능보다 **guard/poisoning**, **lock ownership 문서화**, **driver recovery**, **structured logging/backtrace**, **long soak 자동화**다.

## 다음 안정화 실행 순서

아래 항목은 현재 코드에서 확인한 위험도를 기준으로 한 다음 작업 순서다. 새 기능보다 이 순서로 안정화 debt를 줄인다.

| 우선순위 | 남은 작업 | 코드에서 확인한 근거 | 완료 조건 |
| --- | --- | --- | --- |
| 1 | 타이머 IRQ의 USB 작업 분리 | `kernel/core/kernel.c`의 `irq_dispatch()`가 타이머 tick에서 USB mouse/keyboard poll을 호출하고, `kernel/core/device_poll.c`의 `device_poll_poll_usb_mouse_events()`는 주기적으로 EHCI/XHCI hotplug poll까지 호출한다. 장치 응답 지연이 IRQ 지연으로 이어질 수 있다. | IRQ에서는 pending flag/tick만 갱신하고, USB HID/hotplug scan은 process context 또는 명시적 deferred worker에서 실행한다. |
| 2 | 커널 스택 guard page와 double-fault 전용 스택 | `arch/x86/x86_64/gdt.c`의 RSP0는 정적 배열이고 TSS `ist1..ist7`은 0이다. `kernel/internal/proc/process_internal_base.h`의 nested kernel stack도 연속 배열이다. | RSP0/nested kernel stack 아래 guard page를 두고, double fault는 IST 전용 스택에서 처리한다. |
| 3 | blockdev 비동기 큐의 실행/락 구조 수정 | `block/blockdev.c`는 큐가 차면 submit 경로에서 큐 drain을 실행하고, queue lock을 쥔 흐름에서 실제 I/O와 completion callback까지 이어질 수 있다. `blockdev_async_flush_pending()` 호출 정책도 명확히 해야 한다. | queue lock 밖에서 I/O와 callback을 실행하고, completion 재진입 규칙과 flush 호출 위치를 문서화/테스트한다. |
| 4 | panic backtrace | `kernel/core/kernel_panic.c`는 RBP와 여러 trace를 출력하지만 stack frame을 걷는 backtrace는 아직 없다. `Makefile`의 커널 CFLAGS에도 frame pointer 유지 옵션이 없다. | debug/stabilization 빌드에서 frame pointer를 유지하고, panic 화면/serial에 symbolizable return-address chain을 출력한다. |
| 5 | 공통 락/IRQ-safe 규칙 | `block/blockdev.c`, `drivers/storage/ahci.c` 등이 각각 자체 spinlock을 구현한다. 공통 owner 검사, IRQ 상태 보존, lock ordering 검증은 없다. | 공통 spinlock API와 lock-order 문서를 만들고, IRQ context에서 잡아도 되는 lock과 안 되는 lock을 분리한다. |
| 6 | 메모리 오염 탐지용 빌드 | `Makefile`은 커널에 `-fno-stack-protector`를 사용하고 기본 최적화는 `-O2`다. guard/poisoning/redzone/lock checking을 켜는 별도 debug build가 없다. | `MODE=debug` 또는 유사 빌드에서 stack protector 대체 검사, heap/page poisoning, redzone, expensive validation, lock checking을 켠다. |

---

## 1. 커널 내부 경계를 엄격하게 만들기

드라이버나 서브시스템이 임의의 전역 변수나 물리 주소를 막 건드리지 못하게 한다.

`VFS`, `MM`, `scheduler`, `driver`, `TTY`, `display` 각각의 API 경계를 명확하게 두고, 서로의 내부 구현에는 직접 접근하지 않도록 한다.

예:

```text
hal_*
vfs_*
proc_*
gfx_*
```

처럼 계층별 public API를 작게 유지한다.

NexOS의 철학인 **"모든 것은 파일, 모든 접근은 Capability"**와도 잘 맞는다.

커널 내부에서도 다음을 설계 기준으로 삼는다.

> 이 코드가 이 자원에 접근할 이유가 있는가?

---

## 2. IRQ에서는 큰 일을 하지 않기

IRQ handler는 가능한 한 짧게 유지한다.

```c
acknowledge_irq();
update_minimal_state();
set_pending_flag();
wake_worker();
```

정도만 수행한다.

IRQ 안에서 다음과 같은 큰 작업은 가급적 피한다.

```c
framebuffer_present();
disk_flush();
filesystem_work();
malloc();
printf();
```

NexOS framebuffer의 경우 다음 구조가 적절하다.

```text
1. 앱/그래픽 API
   -> gfx_present() 명시 호출

2. TTY/Console
   -> 출력 API 종료 시 필요하면 present

3. Timer IRQ
   -> hal_display_service_pending() 같은 보조 flush
```

Timer가 framebuffer의 소유자가 되면 서브시스템 간 결합도가 너무 높아진다.

---

## 3. 메모리 관리자를 방어적으로 만들기

커널 안정성에서 가장 중요한 부분 중 하나가 메모리 관리다.

Debug build에서는 적극적으로 검증한다.

```c
assert(page_aligned(addr));
assert(refcount > 0);
assert(!page_is_free(page));
```

추가하면 좋은 기능:

- double free 검출
- invalid free 검출
- heap guard
- stack canary
- unmapped guard page
- kernel/user 주소 검증
- page refcount
- slab poisoning

특히 커널 스택 아래에 **guard page**를 하나 두면 stack overflow를 훨씬 빨리 발견할 수 있다.

---

## 4. User Pointer를 절대 신뢰하지 않기

System call에서 user pointer를 그대로 역참조하지 않는다.

예:

```c
sys_write(fd, user_buf, len);
```

내부에서는 다음과 같은 표준 API를 거쳐야 한다.

```c
copy_from_user(kernel_buf, user_buf, len);
copy_to_user(user_buf, kernel_buf, len);
```

검증 항목:

- user address range인가?
- mapped 상태인가?
- 읽기/쓰기 권한이 있는가?
- 길이 계산에서 integer overflow가 없는가?

Linux의 `copy_from_user()` / `copy_to_user()` 같은 역할을 NexOS에서도 하나의 표준 계층으로 만든다.

---

## 5. 프로세스 오류를 커널 오류와 분리하기

사용자 프로그램에서 다음과 같은 오류가 발생해도 OS 전체가 죽어서는 안 된다.

```text
#UD
#PF
divide error
bad syscall
invalid pointer
```

기본 정책:

```text
해당 프로세스 종료
       ↓
자원 회수
       ↓
부모 프로세스에게 종료 사유 전달
       ↓
커널은 계속 실행
```

Page Fault가 발생했다고 무조건 kernel panic을 발생시키지 않는다.

CPL0에서 발생한 fault인지 사용자 모드에서 발생한 fault인지 구분한다.

---

## 6. Panic과 Process Fault를 완전히 분리하기

예:

```c
if (from_user_mode(frame)) {
    process_kill(current, SIGSEGV);
    schedule();
} else {
    kernel_panic(...);
}
```

사용자 프로그램의 잘못은 해당 프로세스만 종료하고, 커널 자체의 불변식이 깨졌을 때만 panic한다.

---

## 7. Lock 정책을 먼저 만들고 SMP는 나중에

SMP를 넣기 전에 single-core 상태에서 데이터 소유권과 lock 규칙을 먼저 정한다.

각 데이터 구조에 대해 다음을 정의한다.

```text
누가 이 데이터를 소유하는가?
IRQ에서도 접근하는가?
process context에서도 접근하는가?
어떤 lock으로 보호하는가?
lock 획득 순서는 무엇인가?
```

예:

```text
process lock
    ↓
fd table lock
    ↓
vfs node lock
    ↓
block device lock
```

Lock ordering을 미리 문서화하면 SMP 추가 후 deadlock을 크게 줄일 수 있다.

---

## 8. 전역 상태 줄이기

취미 OS가 커지면서 불안정해지는 대표적인 원인은 전역 상태다.

예:

```c
current_file;
active_tty;
current_process;
current_disk;
framebuffer_state;
usb_state;
```

가능하면 context를 명시적으로 전달한다.

```c
struct process *p;
struct file *f;
struct tty *tty;
```

핵심은 모든 것을 객체화하는 것이 아니다.

다음처럼 과도한 객체 계층은 오히려 복잡성을 키울 수 있다.

```text
HardwareObject
 └ DeviceObject
   └ IODevice
     └ StorageDevice
       └ BlockDevice
         └ SATADevice
```

중요한 것은 **객체화가 아니라 소유권과 경계가 명확한가**이다.

---

## 9. 드라이버 실패를 예상하고 작성하기

실제 하드웨어는 언제든 이상하게 동작할 수 있다고 가정해야 한다.

이런 코드는 위험하다.

```c
while (!(port->is & DONE))
    ;
```

반드시 timeout을 둔다.

```c
timeout = TIMEOUT_VALUE;

while (...) {
    if (--timeout == 0)
        return -ETIMEDOUT;
}
```

USB, AHCI, NVMe 등 모든 하드웨어 드라이버에 다음 경로가 필요하다.

- timeout
- retry limit
- malformed descriptor 검증
- unplug 처리
- device reset
- controller reset
- 실패 후 복구 경로

---

## 10. 로그 시스템을 printk 이상으로 발전시키기

예:

```text
[  12.031] usb: device 3 reset
[  12.034] ehci: qTD timeout
[  12.035] usb: retry 1/3
```

Severity를 구분한다.

```c
LOG_DEBUG
LOG_INFO
LOG_WARN
LOG_ERROR
LOG_FATAL
```

Subsystem도 구분한다.

```text
mm
sched
vfs
usb
ahci
gfx
tty
```

이렇게 하면 단순히 "멈췄다"가 아니라 멈추기 직전까지 어떤 사건이 발생했는지 추적할 수 있다.

---

## 11. Kernel Panic 화면과 진단 기능 만들기

안정적인 OS는 단순히 안 죽는 OS가 아니다.

**죽었을 때 왜 죽었는지 정확하게 설명할 수 있어야 한다.**

예:

```text
NEXOS KERNEL PANIC

Exception: Page Fault (#14)
CPU: 0
RIP: ffffffff80123abc
CR2: deadbeefdeadbeef
Error: supervisor write, not-present

Process: ush (pid 12)

RAX ...
RBX ...
...

Backtrace:
 kernel_memcpy+0x42
 vfs_read+0x91
 sys_read+0x37
```

특히 **stack trace/backtrace**가 중요하다.

---

## 12. Debug Build와 Release Build 분리하기

Debug build:

```text
assertions
heap poisoning
verbose logging
expensive validation
lock checking
bounds checking
```

Release build:

```text
필수 검사 유지
로그 축소
최적화 활성화
```

개발 중에는 성능보다 오류를 빨리 발견하는 것이 중요하다.

---

## 13. 자동화된 Regression Test 만들기

QEMU에서 자동으로 부팅한 뒤 여러 기능을 테스트한다.

예:

```text
boot
mkdir
mount
create file
write
read
pipe
fork/exec
invalid pointer syscall
kill process
USB enumerate
filesystem stress
reboot
```

각 테스트가 성공하면 serial console에 다음처럼 출력한다.

```text
TEST PASS
```

예를 들어 빌드마다:

```bash
make
qemu-system-x86_64 ... -serial stdio
```

를 실행하고 로그를 자동 검사한다.

새 기능을 추가했을 때 기존 기능이 깨지는 regression을 빠르게 잡을 수 있다.

---

# NexOS 안정화 우선순위

```text
              NexOS 안정화 로드맵

                    ┌─────────────┐
                    │ Kernel API  │
                    │ 경계 정리   │
                    └──────┬──────┘
                           │
                    ┌──────▼──────┐
                    │ 메모리 검증 │
                    │ Guard/Assert│
                    └──────┬──────┘
                           │
                    ┌──────▼──────┐
                    │ User/Kernel │
                    │ fault 격리  │
                    └──────┬──────┘
                           │
                    ┌──────▼──────┐
                    │ IRQ 최소화  │
                    │ Deferred I/O│
                    └──────┬──────┘
                           │
                    ┌──────▼──────┐
                    │ Lock/소유권 │
                    │ 규칙 확립   │
                    └──────┬──────┘
                           │
                    ┌──────▼──────┐
                    │ Driver      │
                    │ timeout     │
                    │ recovery    │
                    └──────┬──────┘
                           │
                    ┌──────▼──────┐
                    │ Trace/Panic │
                    │ 진단 체계   │
                    └──────┬──────┘
                           │
                    ┌──────▼──────┐
                    │ 자동 테스트 │
                    └──────┬──────┘
                           │
                       이후 SMP
```

---

# 현재 NexOS에서 추천하는 방향

현재 NexOS는 이미 다음 수준까지 기능 범위가 넓어졌다.

```text
VFS
ELF
프로세스
스케줄러
System Call
Pipe
TTY
AHCI
USB
Framebuffer
GUI
```

따라서 NVMe, SMP 등 새로운 대형 기능을 계속 추가하기 전에 기존 기능을 안정화하는 단계가 필요하다.

특히 우선순위는 다음과 같다.

1. Kernel API 경계 정리
2. Memory validation / guard
3. User fault와 kernel fault 분리
4. IRQ 최소화
5. Lock 및 resource ownership 규칙 정립
6. Driver timeout / recovery
7. Panic / trace / logging 개선
8. 자동 regression test
9. 그 이후 SMP

---

# 핵심 원칙

> **NexOS는 오류가 발생하지 않는 OS가 아니라, 한 부분의 오류가 다른 부분으로 전파되지 않는 OS로 만든다.**

Linux, Windows NT, XNU 같은 현대 OS 수준의 안정성으로 발전시키려면 이 원칙을 중심으로 설계하는 것이 좋다.
