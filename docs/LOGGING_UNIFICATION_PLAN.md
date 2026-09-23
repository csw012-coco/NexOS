# NexOS i386/x86_64 로그 통일 계획

## 📋 목표
- i386과 x86_64의 로그 형식 통일
- 아키텍처 차이 때문에 반드시 달라야 하는 정보만 예외로 남김
- 로그 메시지 문구, prefix, 필드 순서, 대소문자, 숫자 형식 일관성 확보
- 기존 동작에 영향 없음

---

## 1️⃣ 현재 로그 차이점 분석 (요약)

### A. **구조적 차이** (변경 불가)

| 항목 | i386 | x86_64 | 상태 |
|------|------|--------|------|
| services/ 디렉토리 | ✅ 9개 파일 | ❌ 없음 | 구조적 차이 |
| platform_boot.c | ✅ 있음 | ❌ 없음 | 구조적 차이 |
| 부팅 로그 | shared.c (30+) | kernel_boot.c | 다른 구현 |
| 스모크 테스트 | smoke.c (28+) | 없음 | i386만 있음 |

**결론**: i386이 더 많은 기능을 가짐. x86_64는 동등한 로깅 기능 없음.

### B. **형식 차이** (통일 가능)

#### 1. ELF 로더 메시지

**i386** (`arch/x86/i386/driver/elf_loader.c`):
```c
kprint("driver: bad i386 reloc sec=%u index=%u type=%u sym=%u\n", ...);      // "i386" 포함
kprint("driver: unsupported i386 reloc type=%u\n", type);                    // "i386" 포함  
kprint("driver: ELF32 layout failed %s\n", file->path);                      // "ELF32"
kprint("driver: i386 load memory failed %s size=%u\n", ...);                 // "i386" 포함
```

**x86_64** (`arch/x86/x86_64/driver/elf_loader.c`):
```c
kprint("driver: bad reloc sec=%u index=%u type=%u sym=%u\n", ...);           // "i386" 없음
kprint("driver: unsupported reloc type=%u\n", type);                         // 일반 메시지
kprint("driver: ELF layout failed %s\n", file->path);                        // "ELF" (ELF64 아님)
kprint("driver: load memory failed %s size=%u\n", ...);                      // 일반 메시지
kprint("driver: reloc pc32 out of range type=%u place=%lx target=%lx\n", ...); // x86_64만
kprint("driver: reloc 32 out of range target=%lx\n", ...);                   // x86_64만
kprint("driver: reloc 32s out of range target=%lx\n", ...);                  // x86_64만
```

**차이 분류**:
- ✅ **통일 가능**: "i386 reloc" → "reloc", "i386 load memory" → "load memory", "ELF32" → "ELF"
- ✅ **x86_64 추가 메시지**: reloc range validation은 **x86_64만** 필요 (32비트 주소 공간 vs 64비트)
- ✅ **형식 지정자**: `%x` vs `%lx` 차이는 정당함 (32-bit vs 64-bit)

#### 2. 스케줄러 로그

**i386** (`arch/x86/i386/scheduler/run_ops.c`):
```c
kprint("scheduler: run_loaded failed name=%s entry=%x stack=%x root=%x\n",
       image->name, (uint32_t)image->entry, (uint32_t)image->stack, (uint32_t)image->root);
       //                  %x = 32비트
```

**x86_64 (없음)**: 다른 구현 사용

**결론**: 레지스터 크기가 다르므로 `%x` vs `%lx`는 유지해야 함. 메시지 자체는 동일.

#### 3. 테스트 프리픽스

**i386**:
- `"test32:"` prefix
- `"nexbox32:"` prefix

**x86_64**:
- 없음

**결론**: 이건 아키텍처별 테스트 환경이므로 유지해야 함. 필요하면 x86_64에도 동등한 이름 추가.

---

## 2️⃣ 통일 가능 부분 vs 불가능 부분

### ✅ 통일 가능 (형식 표준화)

| 항목 | 변경 전 (i386) | 변경 후 (통일) | 변경 전 (x86_64) | 비고 |
|------|---|---|---|---|
| **Reloc Error** | "bad i386 reloc" | "bad reloc" | "bad reloc" | i386에서 "i386" 제거 |
| **Reloc Unsupported** | "unsupported i386 reloc" | "unsupported reloc" | "unsupported reloc" | i386에서 "i386" 제거 |
| **ELF Format Name** | "ELF32 layout failed" | "ELF layout failed" | "ELF layout failed" | i386에서 "32" 제거 |
| **Memory Load** | "i386 load memory failed" | "load memory failed" | "load memory failed" | i386에서 "i386" 제거 |
| **Bootloader ID** | "janus: " | "janus: " (동일) | - | 이미 일관 |
| **부팅 정보** | "boot: " | "boot: " (동일) | - | 이미 일관 |
| **PCI 정보** | "pci: " | "pci: " (동일) | - | 이미 일관 |

### ❌ 통일하면 안 되는 부분 (아키텍처 고유)

| 항목 | i386 | x86_64 | 사유 |
|------|------|--------|------|
| **레지스터 크기** | `%x` (32비트) | `%lx` (64비트) | 실제 레지스터 크기 다름 |
| **ELF 클래스** | ELF32 | ELF64 | 파일 포맷 다름 |
| **주소 표현** | `0x%x` | `0x%lx` | 주소 공간 크기 다름 |
| **테스트 프리픽스** | "test32:", "nexbox32:" | (해당 없음) | 아키텍처별 테스트 |
| **Reloc 범위 검사** | (없음) | pc32, 32, 32s (있음) | x86_64는 더 엄격한 검증 필요 |
| **초기화 로그** | shared.c (30+) | kernel_boot.c | 구조적으로 다른 구현 |
| **스모크 테스트** | smoke.c | (없음) | i386만 수행 |

---

## 3️⃣ 추천 로그 포맷 표준

### **드라이버 로더 (arch/x86/*/driver/elf_loader.c)**

#### 현재 vs 추천

```c
// ===== i386 변경 =====

// BEFORE
kprint("driver: bad i386 reloc sec=%u index=%u type=%u sym=%u\n", ...);
kprint("driver: unsupported i386 reloc type=%u\n", type);
kprint("driver: ELF32 layout failed %s\n", file->path);
kprint("driver: i386 load memory failed %s size=%u\n", ...);

// AFTER (표준화)
kprint("driver: bad reloc sec=%u index=%u type=%u sym=%u\n", ...);
kprint("driver: unsupported reloc type=%u\n", type);
kprint("driver: ELF layout failed %s\n", file->path);
kprint("driver: load memory failed %s size=%u\n", ...);

// ===== x86_64 (그대로 유지) =====
kprint("driver: bad reloc sec=%u index=%u type=%u sym=%u\n", ...);           // ✓ 동일
kprint("driver: unsupported reloc type=%u\n", type);                         // ✓ 동일
kprint("driver: ELF layout failed %s\n", file->path);                        // ✓ 동일
kprint("driver: load memory failed %s size=%u\n", ...);                      // ✓ 동일
kprint("driver: reloc pc32 out of range type=%u place=%lx target=%lx\n", ...); // ✓ x86_64 고유
```

### **레지스터/주소 표시** (유지 필수)

```c
// i386 (32비트)
scheduler: run_loaded failed name=%s entry=%x stack=%x root=%x
driver: bad reloc... place=%x value=%x

// x86_64 (64비트)  
scheduler: run_loaded failed name=%s entry=%lx stack=%lx root=%lx
driver: bad reloc... place=%lx value=%lx
```

### **테스트 프리픽스** (아키텍처 고유)

```c
// i386
test32: RUN /cmd/test32 strict-mm
nexbox32: RUN /cmd/nexbox

// x86_64 (새로 추가 시 일관성 유지)
test64: RUN /cmd/test64 strict-mm
nexbox64: RUN /cmd/nexbox  // 이미 사용 중
```

### **부팅 로그** (이미 일관)

```c
// 모두 동일
janus: magic=%x version=%u size=%u
boot: cmdline=%x
boot: kernel_phys=%lx kernel_size=%lx entry=%lx
boot: console=framebuffer %ux%u pitch=%u bpp=%u text=%ux%u
```

---

## 4️⃣ 코드 수정 계획

### **Phase 1: 드라이버 로더 통일** (우선순위 높음)

**파일**: 
- `arch/x86/i386/driver/elf_loader.c`

**수정 사항**:

1. Line 336: "bad i386 reloc" → "bad reloc"
   ```c
   // BEFORE
   kprint("driver: bad i386 reloc sec=%u index=%u type=%u sym=%u\n", ...);
   // AFTER
   kprint("driver: bad reloc sec=%u index=%u type=%u sym=%u\n", ...);
   ```

2. Line 362: "unsupported i386 reloc" → "unsupported reloc"
   ```c
   // BEFORE
   kprint("driver: unsupported i386 reloc type=%u\n", type);
   // AFTER
   kprint("driver: unsupported reloc type=%u\n", type);
   ```

3. Line 475: "ELF32 layout" → "ELF layout"
   ```c
   // BEFORE
   kprint("driver: ELF32 layout failed %s\n", file->path);
   // AFTER
   kprint("driver: ELF layout failed %s\n", file->path);
   ```

4. Line 483: "i386 load memory" → "load memory"
   ```c
   // BEFORE
   kprint("driver: i386 load memory failed %s size=%u\n", file->path, load_size);
   // AFTER
   kprint("driver: load memory failed %s size=%u\n", file->path, load_size);
   ```

**x86_64**: 이미 표준화됨 (변경 불필요)

---

### **Phase 2: 스케줄러 메시지 확인** (선택사항)

**파일**: 
- `arch/x86/i386/scheduler/run_ops.c`

**상태**: 이미 일관성 있음. x86_64에도 동등한 로그가 있으면 확인 필요.

---

### **Phase 3: 부팅 로그** (구조적 차이로 변경 어려움)

**현황**:
- i386: `arch/x86/i386/services/shared.c` (30+ 메시지)
- x86_64: `kernel/core/kernel_boot.c` (동적 `hal_arch_name()` 사용)

**권고**:
1. i386-only 로그는 이미 구조적으로 다르므로 강제 변경 불필요
2. 공통 로그는 이미 일관성 있음 (`janus:`, `boot:`, `pci:` 등)
3. x86_64에 boot info 로깅 추가 필요시 i386과 동일한 포맷 사용

---

### **Phase 4: 테스트/스모크 로그** (i386만 있음)

**현황**:
- i386: `arch/x86/i386/services/smoke.c` ("test32:", "nexbox32:")
- x86_64: 없음

**권고**:
1. i386 "test32:" prefix는 유지 (32비트 테스트 명시)
2. x86_64 smoke 테스트 추가 시 "test64:" prefix 사용
3. 기타 기기 테스트 로그는 공통 형식 준수

---

## 5️⃣ 필요한 코드 수정 (최소변경)

### **수정 대상 파일**

#### 1. `arch/x86/i386/driver/elf_loader.c`

**4개 라인 변경**:

```diff
--- a/arch/x86/i386/driver/elf_loader.c
+++ b/arch/x86/i386/driver/elf_loader.c
@@ -333,7 +333,7 @@ static int elf_loader_build_reloc_table(...)
                     */
                     (void)relocate_entry(reloc, symbol, out);
                 } else {
-                    kprint("driver: bad i386 reloc sec=%u index=%u type=%u sym=%u\n",
+                    kprint("driver: bad reloc sec=%u index=%u type=%u sym=%u\n",
                            i, r, type, symbol_index);
                     return 0;
                 }
@@ -359,7 +359,7 @@ static int elf_loader_build_reloc_table(...)
             } else if (type == R_386_NONE) {
                 /* No-op relocation. */
             } else {
-                kprint("driver: unsupported i386 reloc type=%u\n", type);
+                kprint("driver: unsupported reloc type=%u\n", type);
                 return 0;
             }
         }
@@ -472,7 +472,7 @@ static int elf_loader_layout_image(...)
         segment->file_offset = sh_offset;
     }
 
-    kprint("driver: ELF32 layout failed %s\n", file->path);
+    kprint("driver: ELF layout failed %s\n", file->path);
     return 0;
 
 out_cleanup:
@@ -480,7 +480,7 @@ static int elf_loader_layout_image(...)
         kfree(segment);
     }
 
-    kprint("driver: i386 load memory failed %s size=%u\n", file->path, load_size);
+    kprint("driver: load memory failed %s size=%u\n", file->path, load_size);
     return 0;
 }
```

---

## 6️⃣ 검증 항목

### **수정 후 테스트 항목**

1. ✅ i386 드라이버 로더 로그 메시지 검증
   - "bad reloc" 메시지 정상 출력
   - "unsupported reloc" 메시지 정상 출력
   - "ELF layout failed" 메시지 정상 출력
   - "load memory failed" 메시지 정상 출력

2. ✅ x86_64와 i386 로그 일관성 비교
   - 동일한 에러 상황에서 메시지 포맷 동일성 확인

3. ✅ 기존 기능 무손상 확인
   - 드라이버 로딩 정상 작동
   - 부팅 프로세스 정상

---

## 7️⃣ 향후 권고사항

### **구조적 개선** (장기)

1. **공통 로그 helper 정의** (필요시)
   ```c
   // kernel/public/core/klog_driver.h 예시
   void klog_driver_bad_reloc(uint32_t sec, uint32_t idx, uint32_t type, uint32_t sym);
   void klog_driver_unsupported_reloc(uint32_t type);
   ```

2. **부팅 로그 통합** (선택)
   - x86_64에 i386과 유사한 boot info 로깅 추가 시
   - 동일한 포맷 준수

3. **테스트 프리픽스 확장** (선택)
   - x86_64 smoke 테스트 추가 시
   - "test64:", "nexbox64:" 등 일관성 있는 이름 사용

### **문서화**

- 이 파일을 `docs/` 에 로그 표준으로 추가
- 향후 새로운 로그 추가 시 아키텍처별 형식 가이드라인으로 사용

---

## 📊 요약표

| 단계 | 항목 | 현재 상태 | 액션 | 우선순위 |
|-----|------|---------|------|---------|
| 1 | 드라이버 reloc 메시지 | i386만 "i386" prefix | i386 4줄 수정 | 🔴 높음 |
| 2 | 스케줄러 로그 | 일관 | 검토 후 유지 | 🟡 중간 |
| 3 | 부팅 로그 | 구조 다름 | 강제 변경 불필요 | 🟢 낮음 |
| 4 | 테스트 로그 | i386만 있음 | 유지, x86_64 추가시 일관성 | 🟡 중간 |

---

## ✅ 체크리스트

- [ ] Phase 1: i386 드라이버 로더 4줄 수정
- [ ] Phase 1: x86_64 드라이버 로더 변경 없음 (이미 표준)
- [ ] 컴파일 테스트
- [ ] i386 부팅 테스트
- [ ] x86_64 부팅 테스트
- [ ] 드라이버 로드 에러 시나리오 테스트
- [ ] 로그 포맷 최종 검증
- [ ] 문서 업데이트 (이 파일 docs/ 이동)

