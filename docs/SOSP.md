# SOSP v1.4 — Sustainable Operating System Principles

**Architecture Constitution**  
**Core principle:** Preserve structure at all costs.

---

## Version History

| Version | Changes |
|---|---|
| v1.0 | Initial constitution |
| v1.1 | Added SOSP-13, SOSP-14 |
| v1.2 | Added SOSP-15, refined several principles |
| v1.3 | Added SOSP-16, SOSP-17, strengthened build & TD governance |
| v1.4 | Adaptive Governance — Operating Modes(Solo / Small / Large), SOSP-00, SOSP-18~20 추가, Mode별 enforcement 조정 |

---

## Overview

SOSP는 이 운영체제 또는 대형 시스템의 **아키텍처 헌법**입니다.

규칙은 선택적 스타일 가이드가 아니라 강제적인 설계 제약입니다. 단, 팀 규모와 프로젝트 단계에 따라 적용 강도를 조정할 수 있는 **Adaptive Governance**를 도입합니다.

---

## SOSP-00: Operating Modes

모든 SOSP 원칙은 아래 3가지 모드 중 하나로 운영됩니다. 현재 모드는 `/docs/project_status.md`에 명시해야 합니다.

| Mode | 대상 | Enforcement 수준 | Review 주기 | TD 처리 |
|---|---|---|---|---|
| Solo / Bring-up | 1인 개발, 초기 프로토타입 | 대부분 Warning + Self-review | 월 1회 self-review | 등록만 해도 OK. 즉시 수정 강제 없음 |
| Small Team / Stabilization | 2~15명, 안정화 단계 | CI Warning + PR Review | 주 1회 | TD Owner rotating + Escalation |
| Large Team / Maintenance | 15명 이상, 여러 서브팀 | CI Gate + Architecture Review | 주 1~2회 | 자동 Priority Up. P0/P1은 merge block |

Mode 전환 시에는 ADR(`/docs/adr/`)에 기록하고, migration plan을 작성해야 합니다.

---

## What Changed in v1.4

- SOSP-00 Operating Modes 신규 추가(Adaptive Governance 핵심)
- SOSP-18 Policy-Mechanism Separation 신규 추가
- SOSP-19 Adaptive Ownership 신규 추가
- SOSP-20 Governance Scalability 신규 추가(메타 원칙)
- 기존 원칙에 Mode별 적용 기준 명시
- Technical Debt Register에 `Applicable Mode` 필드 추가
- Review Checklist를 Mode-aware로 개선
- Quick Start 테이블 추가

---

## Quick Start by Team Size

| 팀 규모 | 추천 Mode | 운영 기준 |
|---|---|---|
| 1인 개발자 | Solo / Bring-up | 반드시 지킬 것: SOSP-01, SOSP-02, SOSP-03, SOSP-06, SOSP-07, SOSP-11. 완화 가능한 것: SOSP-04는 warning만, SOSP-10은 TD 등록으로 대체, SOSP-12 Level 2는 2주 이내 처리. |
| 소규모 팀 | Small Team / Stabilization | 대부분의 원칙을 PR 리뷰 + CI warning 수준으로 적용. TD는 팀 리드가 rotating으로 관리. |
| 대규모 팀 | Large Team / Maintenance | CI gate 강제, ADR 필수, layer별 소유팀 명확히 지정, TD burndown 목표 설정. |

---

## Core Principles

### SOSP-01: No Shortcut Rule

**최우선 원칙이며, 모든 모드에서 동일하게 적용합니다.**

오늘의 편의가 내일의 기술 부채가 됩니다. 레이어 경계를 절대 우회하지 않습니다.

### SOSP-02: Single Responsibility

**하나의 모듈은 하나의 목적만 가져야 합니다.**

규칙:

- 각 모듈과 파일은 변경되어야 하는 이유가 하나로 명확해야 합니다.
- 여러 책임이 뒤섞인 God file은 금지합니다.
- 서로 다른 영역의 책임이 한 파일 안에 쌓이면 반드시 분리해야 합니다.

예시:

```text
process_* : 프로세스 생명주기와 실행
fs_*      : 파일시스템 동작
vga.c     : 디스플레이 장치 구현 세부사항
```

### SOSP-03: Interface First

**구현보다 먼저 경계를 정의합니다.**

규칙:

- 안정적인 인터페이스는 구현을 시작하기 전에 먼저 설계해야 합니다.
- 내부 구현 변경이 넓은 범위의 아키텍처 파손으로 이어지면 안 됩니다.
- 호출자는 내부 저장 방식이 아니라 의도와 계약에 의존해야 합니다.

예시:

```c
int fs_read(const char *path, void *buf);
```

### SOSP-04: Dependency Direction [Refined v1.3]

**의존성은 아래 방향으로만 흘러야 합니다. 빌드 시스템은 이를 강제해야 합니다.**

기본 방향:

```text
user -> syscall -> core -> hal
```

금지되는 의존성:

- `hal/ -> core/` 또는 `hal/ -> syscall/` — HAL은 가장 낮은 레이어입니다.
- `vmm/ -> process/` — VMM은 프로세스 내부 구조를 알아서는 안 됩니다.
- `fs/ -> scheduler/` — 파일시스템은 정책으로부터 독립적이어야 합니다.
- `drivers/ -> any kernel policy` — 드라이버는 HAL 내부 또는 HAL 아래에 속해야 합니다.

빌드 시스템 명세(v1.3):

필수 파일:

- `.deps_allow` — 허용 목록 형식

  ```text
  FORMAT: SOURCE_LAYER -> TARGET_LAYER [# reason]
  ```

  예시:

  ```text
  core -> vmm # VMM is below core
  core -> fs/vfs # VFS is a core service
  ```

- `.deps_exception` — 예외 등록부

  ```text
  FORMAT: SOURCE TARGET SOSP_EXCEPTION_ID OWNER EXPIRY
  ```

  예시:

  ```text
  hal/ps2.c core/log.c DEP-EX-001 johndoe 2024-02-01
  ```

`make check-deps`는 반드시 다음을 수행해야 합니다.

- `.deps_exception`에 없는 위반이 발견되면 0이 아닌 종료 코드를 반환합니다.
- `[DEP VIOLATION] src -> dst (rule: SOSP-04)` 형식으로 출력합니다.
- CI는 이 종료 코드를 기준으로 merge를 차단해야 합니다.

허용되는 예외 패턴:

- 제한된 역방향 통신을 위한 callback 등록
- 이벤트 알림 시스템. 단, 아래 방향의 소유권 구조를 보존해야 합니다.

### SOSP-05: Minimize Global State

**전역 상태는 숨겨진 결합입니다.**

규칙:

- 전역 변수를 최소화합니다.
- 명시적인 context 구조체와 소유권이 있는 상태 객체를 우선 사용합니다.
- 전역 상태는 명확한 소유권을 가진 진짜 singleton을 표현할 때만 허용합니다.

### SOSP-06: Naming Defines Architecture

**이름이 모호하면 설계도 모호합니다.**

규칙:

- 이름은 소유권, 동작, 책임을 드러내야 합니다.
- 의미를 숨기는 범용 helper 이름은 피해야 합니다.

좋은 예:

```text
process_reap
fs_read_cluster
job_restore_foreground
```

금지 예:

```text
do_all
helper1
misc_work
```

### SOSP-07: Hardware Isolation (HAL)

**하드웨어는 반드시 격리되어야 합니다.**

규칙:

- 모든 하드웨어 제어는 HAL 또는 HAL이 소유한 장치 경계를 통해서만 수행해야 합니다.
- core kernel 코드는 하드웨어를 직접 조작하면 안 됩니다.
- 아키텍처별 세부사항은 HAL 경계 아래에 있어야 합니다.

HAL 소유 예시:

```text
ATA
VGA
keyboard
PIC/PIT
page-table register operations
```

### SOSP-08: Ensure Testability

**독립적으로 테스트할 수 없는 코드는 이미 위험한 상태입니다.**

테스트 가능성 단계:

| Tier | 이름 | 요구사항 | 목표 | 예시 |
|---|---|---|---|---|
| Tier 1 | Unit-testable | 부팅 없이 host 환경에서 실행 가능하고, 의존성을 mock할 수 있어야 함 | 모듈의 80% 이상 | `parser.c`, `algorithm.c`, `policy_decision.c` |
| Tier 2 | Emulation-testable | QEMU 또는 장치 모델을 가진 emulator에서 테스트 가능해야 함 | 대부분의 driver | `ata_driver.c`, `vga_text.c` |
| Tier 3 | Integration-only | 실제 하드웨어 또는 전체 시스템이 필요함 | 최소화하고 이유를 문서화 | `timer_interrupt.c` 같은 timing-dependent code |

규칙:

- Tier 1 모듈은 host 기반 unit test를 반드시 가져야 합니다.
- Tier 2 모듈은 emulation test suite를 반드시 가져야 합니다.
- Tier 3 모듈은 module header에 서면 사유를 남겨야 합니다.
- 정당한 사유 없이 더 낮은 tier에서 더 높은 tier로 이동하는 것은 금지합니다.

### SOSP-09: Structure Before Features

**기능 작업보다 구조가 먼저입니다.**

필수 순서:

1. 구조를 정의합니다.
2. 인터페이스를 확정합니다.
3. 기능을 구현합니다.

금지:

- `먼저 구현하고 나중에 정리한다`
- 기능 압박을 이유로 나쁜 경계를 정상화하는 것

### SOSP-10: Immediate Refactoring

**위반은 즉시 수정하거나 Technical Debt로 추적해야 합니다. SOSP-15를 참고합니다.**

규칙:

- 위반이 확인되면 그 위에 새 코드를 쌓지 않습니다.
- 해당 영역에서 기능 확장을 계속하기 전에 refactor를 수행하거나 TD entry를 생성합니다.
- 추적 없는 `나중에 고치기`는 허용되는 해결 방식이 아닙니다.

### SOSP-11: Uniform Error Semantics

**에러는 자신이 속한 레이어의 언어로 표현되어야 합니다.**

모든 레이어 경계에는 명시적인 error translation이 있어야 합니다.

GOOD — 명시적 변환:

```c
static core_error_t translate_hal_error(hal_error_t he) {
    switch (he) {
    case HAL_ERR_TIMEOUT:
        return CORE_ERR_IO_TIMEOUT;
    case HAL_ERR_NODEV:
        return CORE_ERR_DEVICE_MISSING;
    case HAL_ERR_DATA_CORRUPT:
        return CORE_ERR_IO_CORRUPT;
    default:
        return CORE_ERR_IO_UNKNOWN;
    }
}
```

BAD — raw cast. 의미가 사라집니다.

```c
return (core_error_t)ata_read_sector(...);
```

BAD — 조용히 에러를 삼킴:

```c
ata_read_sector(...);
return CORE_SUCCESS; // error disappeared
```

BAD — 맥락 없는 반환:

```c
return -1; // what does -1 mean?
```

필수 동작:

- syscall 경계는 syscall 관점에서 의미 있는 결과를 반환해야 합니다.
- core layer는 core domain의 에러 의미를 사용해야 합니다.
- low-level hardware의 세부사항은 변환 없이 위로 새어 나가면 안 됩니다.

### SOSP-12: Specification-Code Parity [Refined v1.3]

**문서는 청사진이고, 코드는 건물입니다.**

단계별 요구사항:

| Level | 범위 | Spec timing | Enforcement |
|---|---|---|---|
| Level 1 | 새 레이어, dependency direction 변경, public API signature | 구현 전 | 필수. 예외 없음 |
| Level 2 | 내부 refactoring, 의미가 유지되는 변경 | stable phase에서는 48시간 이내 / bring-up phase에서는 1주 이내 | CI warning |
| Level 3 | 의도된 동작을 복구하는 bug fix, 임시 debug code | spec update 불필요 | 예외 기록 |

v1.3 조정 — Solo / Bring-up Phase 예외:

1인 개발 프로젝트 또는 active bring-up phase에서는 Level 2 spec update 기한을 1주로 연장합니다. Level 1은 여전히 타협 불가입니다. 현재 phase는 project status file에 기록해야 합니다.

금지:

- spec update 없는 Level 1 변경
- stable phase에서 5일, bring-up phase에서 2주를 넘도록 spec drift를 방치하는 것

### SOSP-13: Structured Optimization

**성능은 아키텍처 안에 있어야지, 아키텍처 밖에 있으면 안 됩니다.**

규칙:

- 성능 작업이 레이어 경계를 무너뜨리면 안 됩니다.
- 속도를 이유로 인터페이스를 우회하는 것은 금지합니다.
- 최적화가 필요하다면 아키텍처 내부에 공식 fast-path interface를 정의해야 합니다.

허용:

- 명시적으로 문서화된 fast-path API
- 안정적인 경계 뒤에 숨겨진 국소적 최적화

금지:

- 레이어를 가로지르는 ad hoc 직접 호출
- 소유권 규칙을 우회하는 숨겨진 special case

### SOSP-14: Explicit Resource Ownership

**소유권은 명확하고 추적 가능해야 합니다.**

규칙:

- resource를 생성한 쪽은 누가 그것을 해제하는지 정의해야 합니다.
- ownership transfer는 명시적이어야 합니다.
- 공유 mutable resource는 모호한 소유권에 의존하면 안 됩니다.

GOOD — 명확한 create/destroy pair:

```c
process_t *process_create(void);
void process_destroy(process_t *proc);
```

GOOD — 명시적 ownership transfer:

```c
void list_add(list_t *, void *item);      // list owns item now
void *list_remove(list_t *, size_t idx);  // caller owns item now
```

BAD — 모호한 소유권:

```c
void *get_buffer(void); // who frees this?
```

### SOSP-15: Technical Debt Register [Refined v1.3]

**즉시 refactoring이 불가능하다면 debt는 반드시 추적해야 합니다.**

Governance(v1.3 Addition):

- TD Owner role을 지정해야 합니다. 이 사람은 주간 review를 진행합니다.
- Escalation path: P0/P1 overdue -> TD Owner -> Project Lead -> Architectural Review.
- owner가 30일 넘게 없는 TD entry는 자동으로 P0으로 승격되며, 해당 모듈의 모든 새 작업을 차단합니다.

각 TD entry에는 반드시 다음을 포함해야 합니다.

- Unique ID, 예: `TD-2024-001`
- 위반 설명
- Owner, 즉 책임자
- 예상 수정 effort(hours/days)
- Priority: P0=blocker, P1=high, P2=normal, P3=low
- 생성일
- 목표 해결일
- Escalated: Yes/No

TD Entry Format:

```markdown
## TD-2024-001: HAL dependency inversion

- Violation: hal/ps2_keyboard.c calls core/log.c directly
- Owner: johndoe
- Effort: 4 hours
- Priority: P1
- Created: 2024-01-15
- Target: 2024-01-22
- Status: Pending
- Workaround: None
- Escalated: No
```

규칙:

- TD entry는 sprint planning에서 매주 review해야 합니다.
- unresolved TD가 있는 영역에서 새 기능을 추가하려면 먼저 TD를 해결해야 합니다.

금지:

- TD entry 없이 `나중에 고치겠다`고 말하는 것
- owner가 30일 넘게 없는 TD entry
- P0/P1 debt가 target date를 넘었는데도 escalation하지 않는 것

### SOSP-16: Principle Conflict Resolution [New v1.3]

**둘 이상의 SOSP 원칙이 서로 모순되는 요구사항을 만들 경우, 즉흥적 판단이 아니라 구조화된 결정 절차를 따라야 합니다.**

결정 절차:

1. 어떤 SOSP 원칙들이 충돌하는지 식별합니다. 관련 원칙을 모두 명시적으로 이름 붙입니다.
2. Operational Priority 순서를 적용합니다: `SOSP-01 > SOSP-04 > ... > SOSP-13`.
   - 우선순위가 더 높은 원칙이 이깁니다.
3. 우선순위만으로 해결이 명확하지 않으면 Architecture Decision Record(ADR)로 에스컬레이션합니다.
4. 충돌, 선택한 해결책, 근거를 `/docs/adr/`에 기록합니다.
   - 낮은 우선순위 원칙을 위반했다면 TD entry를 생성합니다.

자주 발생하는 충돌 시나리오:

| 충돌 | 해결 |
|---|---|
| SOSP-13 optimize vs SOSP-01 no shortcut | SOSP-01이 우선합니다. 대신 fast-path API를 정의합니다. 속도를 위해 레이어를 우회하지 않습니다. |
| SOSP-10 refactor now vs SOSP-09 structure first | 위반이 구조를 막고 있다면 SOSP-10이 우선합니다. 명확하지 않으면 ADR을 사용합니다. |
| SOSP-15 track debt vs SOSP-10 refactor now | SOSP-10이 우선합니다. SOSP-15는 즉시 수정이 불가능할 때의 fallback입니다. 쉬운 수정을 미루기 위해 TD를 사용하지 않습니다. |

### SOSP-17: External Code Boundary Policy [New v1.3]

**Third-party library와 external header는 통제되지 않은 의존성을 들여와 레이어링을 조용히 위반할 수 있습니다. 모든 external code는 명시적인 경계 안에 가둬야 합니다.**

규칙:

- external code는 `/extern/` 또는 `/hal/extern/`에만 배치해야 합니다.
- external code는 `core/`, `vmm/`, `fs/`, `syscall/`에서 직접 include하면 절대 안 됩니다. wrapper module이 필요합니다.
- wrapper module은 external API를 internal interface로 변환하는 책임을 가집니다.
- external code는 wrapper boundary에서 SOSP-11(error translation)의 적용을 받습니다.
- vendored external code는 기본적으로 Tier 2 또는 Tier 3 testability로 취급합니다. SOSP-08에 따라 근거를 문서화해야 합니다.

허용되는 패턴:

```c
// extern/libfoo/foo.h <- external code lives here
// hal/extern_wrapper/foo_hal.c <- wrapper translates
#include "extern/libfoo/foo.h" // OK inside wrapper only

hal_error_t foo_hal_init(void) {
    int r = foo_init();
    return (r == FOO_OK) ? HAL_OK : HAL_ERR_INIT; // translate
}
```

금지되는 패턴:

```c
// core/scheduler.c
#include "extern/libfoo/foo.h" // VIOLATION: SOSP-17 + SOSP-01
```

### SOSP-18: Policy-Mechanism Separation

정책(Policy)과 메커니즘(Mechanism)을 명확히 분리합니다.

| 구분 | 의미 | 예시 |
|---|---|---|
| Mechanism | 어떻게 수행하는가 | 페이지 테이블 조작, 스케줄링 알고리즘 실행 |
| Policy | 무엇을 선택하는가 | 어느 프로세스에 우선순위를 줄지, eviction 정책을 어떻게 선택할지 |

허용 예시:

- VMM은 eviction mechanism만 제공합니다.
- 실제 eviction policy는 별도 모듈로 분리합니다.

모든 모드에서 권장하며, Large Mode에서는 강제합니다.

### SOSP-19: Adaptive Ownership

소유권(Ownership)을 팀 규모에 따라 유연하게 정의합니다.

| Mode | Ownership 기준 |
|---|---|
| Solo | 개발자 본인이 대부분 소유 |
| Small | 모듈 또는 레이어 단위 소유자 지정 |
| Large | Layer Owner + Cross-team Reviewer 제도 |

자원 생성 시 항상 **누가 해제하는가**를 명시합니다. 이 원칙은 SOSP-14와 연계됩니다.

### SOSP-20: Governance Scalability

SOSP 자체가 팀 규모에 따라 enforcement를 조정할 수 있도록 설계되어야 합니다.

이 원칙은 메타 규칙으로, SOSP-00과 함께 모든 다른 원칙에 우선 적용됩니다.

SOSP-04, SOSP-08, SOSP-12, SOSP-15 등 기존 원칙에는 각 항목 끝에 Mode별 가이드를 추가합니다.

#### Mode별 적용 예시: SOSP-04

| Mode | 적용 방식 |
|---|---|
| Solo | `make check-deps`는 warning만 발생 |
| Small | PR에서 리뷰 |
| Large | CI 실패 시 merge block |

---

## Technical Debt Register

SOSP-15를 v1.4에서 강화합니다.

각 TD entry에는 아래 필드를 추가합니다.

| Field | 설명 |
|---|---|
| Applicable Modes | Solo / Small / Large 중 적용되는 모드. 복수 선택 가능 |
| Risk Score | 1~10 범위의 위험 점수 |
| Impact Area | 영향을 받는 레이어 또는 모듈 |

Solo Mode에서는 P3 TD를 `nice-to-have`로 자동 downgrade할 수 있습니다.

---

## Operational Priority

1. SOSP-01 No Shortcut Rule
2. SOSP-04 Dependency Direction
3. SOSP-20 Governance Scalability
4. SOSP-00 Operating Modes
5. SOSP-12 Specification-Code Parity
6. SOSP-18 Policy-Mechanism Separation
7. 기타 기존 원칙

권장 배치: SOSP-18 Policy-Mechanism은 SOSP-02 Single Responsibility 바로 아래에 둡니다.

---

## Review Checklist

PR 또는 merge 전에 반드시 확인합니다.

- 현재 Operating Mode는 무엇인가? (`/docs/project_status.md`)
- 이 변경이 해당 Mode의 enforcement 규칙을 준수하는가?
- Policy와 Mechanism이 분리되었는가? (SOSP-18)
- 소유권이 명확한가? (SOSP-19)
- 기존 체크리스트와 Mode 관련 항목을 함께 만족하는가?

---

## Forbidden Patterns

| Pattern | Solo | Small | Large |
|---|---|---|---|
| God files | TD 등록 시 임시 허용 가능 | 리뷰에서 제한 | 절대 금지 |
| Unvetted external includes | 간단 wrapper 의무 | wrapper 의무 | wrapper + 리뷰 필수 |

---

## Appendix: v1.3 → v1.4 Migration Plan

1. SOSP-00 Operating Modes 섹션 추가
2. `/docs/project_status.md`에 현재 Mode 기록
3. TD Register 양식 업데이트(`Applicable Modes` 필드 추가)
4. SOSP-18, SOSP-19, SOSP-20 원칙 문서화
5. Review Checklist 및 build system에 Mode 플래그 지원

   ```bash
   make check-deps --mode=solo
   ```

6. 기존 TD를 새 형식으로 마이그레이션(30일 이내)

---

## Note

Solo Mode에서 시작하더라도, 나중에 Large Mode로 전환할 때를 대비해 SOSP-01, SOSP-02, SOSP-07 등 핵심 구조 원칙은 처음부터 최대한 지키는 것을 강력 권장합니다.

---

SOSP is the architectural constitution for this codebase.

하지만 SOSP는 팀 규모와 단계에 맞게 살아 움직이는 거버넌스가 되어야 합니다.
