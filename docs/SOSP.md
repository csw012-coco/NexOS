SOSP v1.4 — Sustainable Operating System Principles
Architecture Constitution
Core principle: Preserve structure at all costs.
================================================================================
Version History:

v1.0 Initial constitution
v1.1 Added SOSP-13, SOSP-14
v1.2 Added SOSP-15, refined several principles
v1.3 Added SOSP-16, SOSP-17, strengthened build & TD governance
v1.4 Adaptive Governance — Operating Modes (Solo/Small/Large), SOSP-00, SOSP-18~20 추가, Mode별 enforcement 조정

================================================================================
OVERVIEW
SOSP는 이 운영체제(또는 대형 시스템)의 아키텍처 헌법입니다.
규칙은 선택적 스타일 가이드가 아니라 강제적인 설계 제약입니다.
단, 팀 규모와 프로젝트 단계에 따라 적용 강도를 조정할 수 있는 Adaptive Governance를 도입합니다.
SOSP-00: Operating Modes (New v1.4)
모든 SOSP 원칙은 아래 3가지 모드 중 하나로 운영됩니다. 현재 모드는 /docs/project_status.md에 명시해야 합니다.

































Mode대상Enforcement 수준Review 주기TD 처리Solo / Bring-up1인 개발, 초기 프로토타입대부분 Warning + Self-review월 1회 self-review등록만 해도 OK (즉시 수정 강제 X)Small Team / Stabilization2~15명, 안정화 단계CI Warning + PR Review주 1회TD Owner (rotating) + EscalationLarge Team / Maintenance15명 이상, 여러 서브팀CI Gate + Architecture Review주 1~2회자동 Priority Up, P0/P1은 merge block
Mode 전환 시: ADR(/docs/adr/)에 기록하고, migration plan을 작성해야 합니다.
================================================================================
WHAT CHANGED IN v1.4

SOSP-00 Operating Modes 신규 추가 (Adaptive Governance 핵심)
SOSP-18 Policy-Mechanism Separation 신규
SOSP-19 Adaptive Ownership 신규
SOSP-20 Governance Scalability 신규 (메타 원칙)
기존 원칙에 Mode별 적용 기준 명시
Technical Debt Register에 “Applicable Mode” 필드 추가
Review Checklist를 Mode-aware로 개선
Quick Start 테이블 추가

================================================================================
QUICK START BY TEAM SIZE (v1.4 신규)
1인 개발자 (Solo Mode 추천)
반드시 지킬 것: SOSP-01, 02, 03, 06, 07, 11
완화 가능한 것: SOSP-04 (warning만), SOSP-10 (TD 등록으로 대체), SOSP-12 Level 2 (2주 이내)
소규모 팀 (Small Mode 추천)
대부분의 원칙을 PR 리뷰 + CI warning 수준으로 적용. TD는 팀 리드가 rotating으로 관리.
대규모 팀 (Large Mode 추천)
CI gate 강제, ADR 필수, layer별 소유팀 명확히 지정, TD burndown 목표 설정.
================================================================================
CORE PRINCIPLES
SOSP-01: No Shortcut Rule (최우선, 모든 모드 동일)
오늘의 편의가 내일의 기술 부채가 된다.
레이어 경계를 절대 우회하지 않는다.
SOSP-02 ~ SOSP-17 (v1.3 내용 유지, Mode별 enforcement만 조정)
SOSP-18: Policy-Mechanism Separation (New v1.4)
정책(Policy)과 메커니즘(Mechanism)을 명확히 분리하라.

Mechanism: “어떻게” (페이지 테이블 조작, 스케줄링 알고리즘 실행 등)
Policy: “무엇을” (어느 프로세스에 우선순위를 줄지, eviction 정책 선택 등)
허용: VMM은 eviction mechanism만 제공, 실제 정책은 별도 모듈로 분리.
모든 모드에서 권장. Large Mode에서는 강제.

SOSP-19: Adaptive Ownership (New v1.4)
소유권(Ownership)을 팀 규모에 따라 유연하게 정의한다.

Solo: 개발자 본인이 대부분 소유
Small: 모듈/레이어 단위 소유자 지정
Large: Layer Owner + Cross-team Reviewer 제도
자원 생성 시 항상 “누가 해제하는가”를 명시 (SOSP-14와 연계).

SOSP-20: Governance Scalability (New v1.4)
SOSP 자체가 팀 규모에 따라 enforcement를 조정할 수 있도록 설계되어야 한다.
이 원칙은 메타 규칙으로, SOSP-00과 함께 모든 다른 원칙에 우선 적용된다.
SOSP-04, SOSP-08, SOSP-12, SOSP-15 등 기존 원칙에는 각 항목 끝에 아래처럼 Mode별 가이드 추가:
Mode별 적용 예시 (SOSP-04)

Solo: make check-deps는 warning만
Small: PR에서 리뷰
Large: CI 실패 시 merge block

================================================================================
TECHNICAL DEBT REGISTER (SOSP-15, v1.4 강화)
각 TD entry에 아래 필드 추가:

Applicable Modes: Solo / Small / Large (복수 선택 가능)
Risk Score: 1~10
Impact Area: 영향을 받는 레이어/모듈

Solo Mode에서는 P3 TD를 “nice-to-have”로 자동 downgraded 가능.
================================================================================
OPERATIONAL PRIORITY (v1.4 업데이트)

SOSP-01 No Shortcut Rule
SOSP-04 Dependency Direction
SOSP-20 Governance Scalability (신규)
SOSP-00 Operating Modes (신규)
SOSP-12 Specification-Code Parity
... (이하 기존 순서, SOSP-18 Policy-Mechanism은 SOSP-02 Single Responsibility 바로 아래 배치 추천)

================================================================================
REVIEW CHECKLIST (v1.4 업데이트)
PR/merge 전에 반드시 확인:

현재 Operating Mode는 무엇인가? (project_status.md)
이 변경이 해당 Mode의 enforcement 규칙을 준수하는가?
Policy와 Mechanism이 분리되었는가? (SOSP-18)
소유권이 명확한가? (SOSP-19)
... (기존 체크리스트 + Mode 관련 항목 추가)

================================================================================
FORBIDDEN PATTERNS (업데이트)

God files → Large Mode에서는 절대 금지, Solo에서는 TD로 허용
Unvetted external includes (SOSP-17) → 모든 모드에서 wrapper 의무, Solo에서는 간단 wrapper 허용

================================================================================
APPENDIX: v1.3 → v1.4 Migration Plan

SOSP-00 Operating Modes 섹션 추가 및 project_status.md에 현재 Mode 기록 (Sprint 1)
TD Register 양식 업데이트 (Applicable Modes 필드 추가)
SOSP-18, 19, 20 원칙 문서화
Review Checklist 및 build system에 Mode 플래그 지원 (make check-deps --mode=solo)
기존 TD를 새 형식으로 마이그레이션 (30일 이내)

NOTE: Solo Mode에서 시작하더라도, 나중에 Large Mode로 전환할 때를 대비해 SOSP-01, SOSP-02, SOSP-07 등 핵심 구조 원칙은 처음부터 최대한 지키는 것을 강력 권장합니다.
================================================================================
SOSP is the architectural constitution for this codebase.
하지만 “팀 규모와 단계에 맞게 살아 움직이는 거버넌스”가 되어야 합니다.
