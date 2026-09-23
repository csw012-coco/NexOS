# NexOS 현대식 I/O 설계안

## 1. 목적

NexOS의 현재 블록 I/O는 단순한 sector 단위 동기 처리와 작은 캐시 구조로 인해, 현대 OS에서 흔히 쓰는 성능 패턴이 부족합니다. 이 설계는 다음 목표를 만족하도록 작성했습니다.

- CPU가 데이터 이동을 과도하게 맡지 않도록 DMA 중심 구조로 전환
- 작은 I/O를 큰 배치로 묶어서 처리
- 읽기와 쓰기 모두 캐시와 read-ahead/write-back을 활용
- 대기 중에는 다른 작업을 수행하도록 비동기 경로를 확보
- 복사 횟수 감소와 메모리 매핑을 통한 zero-copy로 전환
- 점진적 도입이 가능하도록 단계별 마이그레이션 구조를 채택

---

## 2. 현재 병목점 요약

현재 NexOS 구조를 보면 주요 병목은 다음과 같습니다.

1. ATA 경로는 sector 단위 PIO 루프를 순차적으로 처리하는 구조입니다.
   - [drivers/storage/ata.c](../drivers/storage/ata.c)
2. AHCI는 DMA 기반 경로를 일부 이미 갖고 있지만, 전체 블록 스택이 이를 충분히 활용하지 못할 가능성이 큽니다.
   - [drivers/storage/ahci.c](../drivers/storage/ahci.c)
3. NXFS는 캐시가 8개 블록 규모로 작고, dirty writeback과 flush가 자주 발생할 수 있습니다.
   - [fs/nxfs.h](../fs/nxfs.h)
   - [fs/nxfs_io.c](../fs/nxfs_io.c)
4. blockdev 계층은 캐시 구조가 단순하고 read/write 요청의 batching이 거의 없습니다.
   - [block/blockdev.c](../block/blockdev.c)
   - [block/blockdev.h](../block/blockdev.h)

이 구조는 기능적으로는 동작하지만, 현대 OS의 핵심 성능 요소인 흡입/배치/비동기/병렬 처리로는 부족합니다.

---

## 3. 기본 원칙

### 3.1 DMA를 우선으로 하라

CPU는 데이터 복사와 동기화보다 제어와 스케줄링에 집중해야 합니다.

- 장치가 메모리로 직접 전송
- CPU는 결과 처리와 요청 큐 관리만 수행
- 버퍼 주소만 전달하고 대기

이 원칙은 AHCI, USB MSC, NVMe 같은 장치에서 가장 큰 효과를 냅니다.

### 3.2 작은 I/O는 배치로 합친다

작은 512-byte I/O를 매번 개별로 보내면 장치 보유시간이 길어지고, IRQ/상태 전환 비용이 커집니다.

- 4KB, 32KB, 128KB 단위 배치 요청으로 묶기
- 연속 LBA를 하나의 요청으로 조립
- 주기적으로 merge queue 적용

### 3.3 캐시를 “공존 메모리 버퍼”로 만든다

NexOS의 캐시는 단순한 read cache가 아니라, 다음의 역할을 가지게 해야 합니다.

- 최근 데이터 재사용 큐
- read-ahead 버퍼
- dirty page writeback 버퍼
- metadata hot cache

### 3.4 대기하는 동안 다른 일 수행

동기형 blockdev 읽기/쓰기 API를 그대로 유지하되, 내부적으로는 작업이 끝날 때까지 기다리기보다는 다음 구조를 마련합니다.

- request queue
- completion queue
- worker thread 또는 interrupt-driven completion
- deferred flush worker

### 3.5 복사를 줄여라

가능하면 다음 경로를 택합니다.

- 사용자 버퍼와 커널 버퍼를 직접 매핑
- fs cache와 block cache를 공유
- read-ahead는 물리적 버퍼를 재사용
- writeback은 하나의 큰 버퍼를 모아 전송

---

## 4. 설계 개요

### 4.1 계층 구조

NexOS의 I/O 스택은 다음 6개 계층으로 설계하는 것이 적절합니다.

1. 애플리케이션/파일시스템 계층
   - 파일, vnode, inode, 메타데이터 접근
2. VFS/FS 계층
   - 읽기/쓰기 요청을 page 또는 block 단위로 변환
3. block cache 계층
   - 최근 사용 블록 보관
   - read-ahead
   - dirty block 집계
4. block request queue 계층
   - LBA 범위 집계
   - 병렬 큐 관리
   - completion 관리
5. device driver 계층
   - AHCI / ATA / USB MSC
   - DMA 전송과 IRQ 완료 처리
6. 장치 하드웨어 계층
   - sector 기반 I/O
   - read/write/flush 명령

이 구조는 현재 [block/blockdev.c](../block/blockdev.c), [fs/nxfs_io.c](../fs/nxfs_io.c), [drivers/storage/ahci.c](../drivers/storage/ahci.c) 사이를 연결하는 가장 자연스러운 단계별 확장입니다.

---

## 5. 핵심 데이터 구조

### 5.1 block IO request

```c
struct block_io_request {
    struct block_device *dev;
    uint64_t start_lba;
    uint32_t sector_count;
    uint8_t *buffer;
    uint32_t flags;
    uint32_t req_id;
    enum block_io_dir {
        BLOCK_IO_READ = 1,
        BLOCK_IO_WRITE = 2,
        BLOCK_IO_FLUSH = 3
    } dir;
    void (*complete)(struct block_io_request *req, int status);
    void *context;
};
```

### 5.2 request queue

```c
struct block_io_queue {
    struct block_io_request *ring;
    uint32_t depth;
    uint32_t head;
    uint32_t tail;
    uint32_t in_flight;
    uint32_t max_segments;
    volatile uint32_t lock;
};
```

### 5.3 cache entry

```c
struct block_cache_entry {
    struct block_device *dev;
    uint64_t lba;
    uint32_t sector_count;
    uint8_t *data;
    uint32_t valid;
    uint32_t dirty;
    uint32_t refcount;
    uint32_t last_used;
};
```

---

## 6. 요청 경로 설계

### 6.1 읽기 경로

1. 파일/FS에서 LBA 범위를 계산
2. block cache에서 이미 있으면 즉시 반환
3. cache miss면 read-ahead 범위를 함께 계산
4. 연속 LBA를 하나의 회차로 합쳐서 요청 queue에 넣기
5. driver가 DMA로 전달
6. 완료 시 cache에 적재

### 6.2 쓰기 경로

1. dirty block이 쌓이면 즉시 디스크로 돌리지 않고 write-back queue로 보냄
2. 같은 범위/연속 LBA를 merge
3. 버퍼를 정리하고 flush 시점에 전송
4. 전송 완료 후 dirty flag 해제

### 6.3 flush 경로

flush는 즉시 강제 쓰기보다 정리된 배치 쓰기를 선호합니다.

- dirty block이 N개 이상이면 flush
- 일정 시간 동안 dirty 유지되면 flush
- writeback queue가 비면 flush 신호를 보냄

---

## 7. Read-ahead와 write-back 정책

### 7.1 read-ahead

순차 접근 패턴에서는 다음 block을 미리 가져오는 것이 중요합니다.

- 현재 블록 직후 범위를 예측해서 읽기
- 파일의 논리적 연속 구간이면 32KB~1MB 단위로 prefetch
- metadata와 디렉터리는 더 작게 prefetch

예시:

- 파일 데이터: 64KB 또는 256KB range prefetch
- 메타데이터: 8KB 또는 16KB range prefetch

### 7.2 write-back

쓰기 요청은 즉시 disk에 밀어 넣지 말고 일정 시점마다 정리합니다.

- dirty threshold: 예를 들어 64KB 또는 256KB
- flush timeout: 10ms~100ms
- 비동기 flush worker 관리

이렇게 해야 시스템이 작은 random write 폭주를 막고, 큰 연속 write로 바뀝니다.

---

## 8. 캐시 전략

### 8.1 캐시 메모리 크기

기본 목표는 다음 구조로 시작하는 것입니다.

- read cache: 64KB~4MB
- write-back buffer: 64KB~1MB
- metadata cache: 32KB~256KB

NexOS의 현재 [fs/nxfs.h](../fs/nxfs.h) 의 8개 블록 캐시는 너무 작아, 파일 전송과 메타데이터 순차 접근이 모두 제한됩니다.

### 8.2 교체 정책

초기에는 다음 전략으로 충분합니다.

- FIFO 또는 simple LRU
- 최근 사용 시간 기반 정렬
- dirty block 우선 처리

복잡한 알고리즘을 처음부터 넣기보다, 안정적인 LRU 기반부터 시작하는 것이 좋습니다.

---

## 9. 비동기 I/O 스택

### 9.1 설계 목표

- 완료는 IRQ 또는 worker thread에서 처리
- 애플리케이션은 기다리지 않고 다른 작업 수행
- 큐가 꽉 찼을 때는 backpressure를 적용

### 9.2 기본 흐름

```text
submit request
  -> validate
  -> merge into queue
  -> issue DMA command
  -> return immediately or wait on completion event

IRQ / completion worker:
  -> mark request done
  -> release buffer
  -> wake waiter
  -> update cache / stats
```

### 9.3 장점

- 디스크 대기 중 CPU를 놀게 하지 않음
- 여러 요청을 동시에 처리
- 파일 복사, 로딩, 스냅샷 작업에 유리

---

## 10. zero-copy와 메모리 매핑

### 10.1 목표

현대 OS는 복사를 줄이기 위해 다음을 사용합니다.

- mmap
- page cache 공유
- buffer ownership transfer
- GPU buffer와 공유 메모리 사용

NexOS는 초기 단계라서 완전한 zero-copy는 어렵지만, 최소 기능으로 먼저 적용할 수 있습니다.

예시:

- 파일 읽기 결과를 page cache 안에 유지
- 파일 쓰기는 page cache에서 dirty 상태만 표시
- 필요한 경우에만 최종 block device 전송

### 10.2 중요 포인트

복사는 항상 비용이 들고, CPU가 오래 붙잡는 구조는 느립니다.

- 캐시와 버퍼를 재사용
- 커널 영역 버퍼를 그대로 사용자 영역에 매핑 가능하게 설계
- 불필요한 memcpy 제거

---

## 11. 디바이스 드라이버 전환 전략

### 11.1 ATA 계열

현재 [drivers/storage/ata.c](../drivers/storage/ata.c) 는 PIO 루프 기반입니다.

다음 단계로 이동합니다.

1. request queue 추가
2. 1 sector 단위 I/O를 다중 sector 결합
3. DMA 전용 절차 추가
4. IRQ completion 통합

### 11.2 AHCI 계열

[drivers/storage/ahci.c](../drivers/storage/ahci.c) 는 이미 DMA 기반이므로, 여기서는 다음이 중요합니다.

- command queue를 여러 개 운영
- chunk 크기 증가
- 병렬 읽기/쓰기 동시 처리
- completion path의 비동기화

### 11.3 USB MSC

[drivers/usb/xhci_internal.h](../drivers/usb/xhci_internal.h) 에서 이미 read-ahead와 DMA 보유 구조가 일부 보입니다.

여기에 다음을 추가하면 좋습니다.

- request merging
- bulk transfer batching
- completion queue

---

## 12. NexOS에서의 우선순위

### Phase 1: 가장 빠른 효과

1. blockdev request queue 도입
2. 연속 LBA merge
3. read-ahead/ write-back buffer 추가
4. AHCI/ATA DMA chunk 크기 증가
5. dirty block flush batching

### Phase 2: 구조 정교화

1. 비동기 completion worker 추가
2. cache entry 레벨 LRU 도입
3. 메타데이터 hot cache 분리
4. flush timeout 정책 추가

### Phase 3: modern optimization

1. mmap 기반 사용자 버퍼 공유
2. zero-copy 경로 추가
3. 여러 디바이스 큐 병렬화
4. SMP 및 스레드 기반 I/O worker 분리

---

## 13. 구현 타겟 파일

권장 수정 위치는 다음입니다.

- [block/blockdev.h](../block/blockdev.h)
- [block/blockdev.c](../block/blockdev.c)
- [fs/nxfs.h](../fs/nxfs.h)
- [fs/nxfs_io.c](../fs/nxfs_io.c)
- [drivers/storage/ata.c](../drivers/storage/ata.c)
- [drivers/storage/ahci.c](../drivers/storage/ahci.c)
- [drivers/usb/xhci_internal.h](../drivers/usb/xhci_internal.h)

---

## 14. 기대 효과

이 설계를 도입하면 기대되는 개선은 다음과 같습니다.

- 작은 random I/O가 큰 배치 I/O로 바뀜
- CPU 대기 시간이 감소
- 디스크 패턴이 순차적이며 병렬적으로 보임
- 캐시 히트율 상승
- flush 비용 감소
- 복사 비용 감소
- 전체 파일 복사, 로딩, 쓰기 성능이 크게 개선

---

## 15. 결론

NexOS의 속도 개선은 단순히 “더 빠른 루프”를 쓰는 문제라기보다, 현대 OS가 채택하는 기본 아이디어를 따라가는 문제입니다.

핵심은 다음입니다.

- GPU/장치에게 실제 전송을 맡기기
- CPU가 대기하지 않게 만들기
- 작은 I/O를 묶어 큰 I/O로 만들기
- 호출할 때마다 디스크를 안 가게 캐시하기
- 짧은 쓰기를 모아서 나중에 처리하기
- 복사를 줄이고 메모리를 재사용하기

이 설계안은 NexOS가 현대식 I/O 성능에 도달하기 위한 가장 현실적인 첫 번째 큰 단계입니다.
