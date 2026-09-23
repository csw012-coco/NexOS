# NexOS Logging/Boot Message Differences: i386 vs x86_64

## Executive Summary

The i386 and x86_64 architectures have **significant structural and logging differences**:

1. **i386 has a `services/` directory with 9 service files** — x86_64 does NOT
2. **ELF loader differences**: i386 (ELF32, 9 messages) vs x86_64 (ELF64, 14 messages)
3. **Boot/test prefixes are i386-specific**: "test32:", "nexbox32:", etc.
4. **x86_64 is more stripped down** — minimal services, relies on common kernel code
5. **Architecture-aware boot logging** uses `hal_arch_name()` for dynamic prefixes

---

## 1. MISSING FILES (i386-only with Logging)

### **arch/x86/i386/platform_boot.c** 
- **Does NOT exist in x86_64**
- 2 kprint statements for services/init lifecycle

```c
kprint("kernel: services online\n");
kprint("kernel: system/init\n");
```

### **arch/x86/i386/services/ directory**
**Completely absent in x86_64!**

Files in i386/services/ with logging:
- `shared.c` (30 kprint statements)
- `smoke.c` (28 kprint statements)  
- `boot_user.c` (test/shell log prefixes)
- `driver.c` (1 kprint statement)
- `boot_flags.c` (no kprint)
- `input.c` (no kprint)
- `command.c` (no kprint)
- `console_loop.c` (no kprint)
- `tty_selftest.c` (no kprint)

---

## 2. ELF LOADER LOGGING DIFFERENCES

### **arch/x86/i386/driver/elf_loader.c** (ELF32 - 9 kprint statements)

#### i386-Specific Error Messages

**Line 128:**
```c
kprint("driver: file image allocation failed %s size=%u\n", file->path, file->size);
```
- Uses 32-bit format `%u` for size

**Line 271:**
```c
kprint("driver: unresolved symbol %s\n", name);
```

**Line 336 - i386-SPECIFIC PREFIX:**
```c
kprint("driver: bad i386 reloc sec=%u index=%u type=%u sym=%u\n",
       i, r, type, symbol_index);
```
✅ **DIFFERENCE**: Contains "i386" in message prefix

**Line 362 - i386-SPECIFIC PREFIX:**
```c
kprint("driver: unsupported i386 reloc type=%u\n", type);
```
✅ **DIFFERENCE**: Contains "i386" in message prefix

**Line 475:**
```c
kprint("driver: ELF32 layout failed %s\n", file->path);
```
✅ **DIFFERENCE**: Says "ELF32" not "ELF64"

**Line 483 - i386-SPECIFIC PREFIX:**
```c
kprint("driver: i386 load memory failed %s size=%u\n", file->path, load_size);
```
✅ **DIFFERENCE**: Contains "i386" in message prefix

**Line 502:**
```c
kprint("driver: register failed %s\n", file->path);
```

**Line 512:**
```c
kprint("driver: loaded %s as %s\n", file->path, driver->name);
```

---

### **arch/x86/x86_64/driver/elf_loader.c** (ELF64 - 14 kprint statements)

#### x86_64 Has MORE Detailed Relocation Error Checking

**Line 256:**
```c
kprint("driver: file image allocation failed %s size=%u\n", path, size);
```
- Same as i386 but variable named `path` not `file->path`

**Line 409:**
```c
kprint("driver: unresolved symbol %s\n", name);
```

**Lines 439-456 - EXTENDED RANGE VALIDATION (NOT IN i386):**
```c
// PC32 range check
kprint("driver: reloc pc32 out of range type=%u place=%lx target=%lx\n",
       type, (uint64_t)(uintptr_t)place, (uint64_t)value);

// 32-bit range check
kprint("driver: reloc 32 out of range target=%lx\n", (uint64_t)value);

// 32-bit signed range check  
kprint("driver: reloc 32s out of range target=%lx\n", (uint64_t)value);
```
✅ **DIFFERENCE**: x86_64 has 3 ADDITIONAL range validation messages that i386 lacks!

**Line 462:**
```c
kprint("driver: unsupported reloc type=%u\n", type);
```
✅ **DIFFERENCE**: Does NOT say "i386" — generic message

**Lines 534, 551, 559:**
```c
kprint("driver: bad reloc sec=%u index=%u type=%u sym=%u\n", ...);
kprint("driver: reloc symbol failed sec=%u index=%u sym=%u\n", ...);
kprint("driver: reloc write failed sec=%u index=%u type=%u\n", ...);
```
✅ **DIFFERENCE**: x86_64 has more detailed relocation tracking

**Line 672:**
```c
kprint("driver: ELF layout failed %s\n", file->path);
```
✅ **DIFFERENCE**: Says "ELF" not "ELF32"

**Line 679:**
```c
kprint("driver: load memory failed %s size=%u\n", file->path, load_size);
```
✅ **DIFFERENCE**: Does NOT say "i386" — generic message

**Line 706:**
```c
kprint("driver: register failed %s\n", file->path);
```

**Line 716:**
```c
kprint("driver: loaded %s as %s\n", file->path, driver->name);
```

---

## 3. i386-SPECIFIC BOOT INITIALIZATION LOGGING

### **arch/x86/i386/services/shared.c** (30 kprint statements)

This file has **EXTENSIVE boot logging** that does NOT exist for x86_64.

#### System Identification
```c
kernel_boot_log_system("i386");  // Hard-coded "i386" prefix
```

#### Bootloader Info
```c
kprint("janus: magic=%x version=%u size=%u\n",
       shared_services_boot_info->hdr.magic,
       (uint32_t)shared_services_boot_info->hdr.version,
       (uint32_t)shared_services_boot_info->hdr.size);
```
✅ **DIFFERENCE**: Uses 32-bit format specifiers

#### Boot Parameters
```c
kprint("boot: cmdline=%x\n", shared_services_boot_info->cmdline);
kprint("boot: kernel_phys=%lx kernel_size=%lx entry=%lx\n",
       shared_services_boot_info->kernel_phys_addr,
       shared_services_boot_info->kernel_phys_size,
       shared_services_boot_info->kernel_entry);
kprint("boot: kernel_phys(proto)=%lx kernel_phys(detect)=%lx kernel_phys(map)=%lx mapped=%u\n",
       ...);
```

#### Console Info
```c
kprint("boot: console=framebuffer %ux%u pitch=%u bpp=%u text=%ux%u\n",
       shared_services_fb_info.width,
       shared_services_fb_info.height,
       shared_services_fb_info.pitch,
       shared_services_fb_info.bpp,
       shared_services_fb_info.text_columns,
       shared_services_fb_info.text_rows);
```

#### Hardware Detection Logs
```c
kprint("pci: ide bdf=%u:%u.%u vendor=%x device=%x prog_if=%x\n", ...);
kprint("pci: ide bar0=%x bar1=%x bar2=%x bar3=%x bar4=%x\n", ...);
kprint("pci: ide controller not found\n");

kprint("ata: primary master sectors=%u model=%s\n", ...);
kprint("ata: primary master not found\n");

kprint("ac97: controller not found\n");
kprint("ac97: bdf=%u:%u.%u vendor=%x device=%x irq=%u pin=%u prog_if=%x\n", ...);
kprint("ac97: nambar=%x nabmbar=%x mixer_reset=%x power=%x ext_id=%x ext_ctrl=%x\n", ...);
kprint("ac97: codec_id=%x global_sta=%x global_cnt=%x init=%u\n", ...);

kprint("hda: controller not found\n");
kprint("hda: bdf=%u:%u.%u vendor=%x device=%x irq=%u pin=%u prog_if=%x\n", ...);
kprint("hda: mmio=%x:%x pci_cmd=%x gcap=%x version=%u.%u\n", ...);
kprint("hda: inpay=%x outpay=%x gctl=%x statests=%x wakeen=%x codec_mask=%x init=%u\n", ...);

kprint("rtl8139: controller not found\n");
kprint("rtl8139: bdf=%u:%u.%u vendor=%x device=%x irq=%u pin=%u init=%u\n", ...);
kprint("rtl8139: io=%x pci_cmd=%x cmd=%x imr=%x isr=%x media=%x speed=%u link=%u\n", ...);
kprint("rtl8139: mac=%x:%x:%x:%x:%x:%x tx=%x rx=%x\n", ...);

kprint("block: devices=%u\n", count);
kprint("block[%u]: name=%s block_size=%u block_count=%lx writable=%u\n", ...);
```

---

## 4. i386 TEST/SMOKE LOGGING

### **arch/x86/i386/services/smoke.c** (28 kprint statements)

All test messages use **"test32:" prefix** (NOT in x86_64):

```c
kprint("test32: RUN /cmd/test32 strict-mm\n");
kprint("test32: FAIL /cmd/test32 strict-mm status=%d\n", process.exit_code);
kprint("test32: RUN /cmd/test32 fork\n");
kprint("test32: RUN /cmd/test32 fork-shared\n");
kprint("test32: RUN /cmd/test32 fork-wait-exec\n");
```

**Nexbox prefix (i386-specific):**
```c
kprint("nexbox32: RUN %s\n", commands[i]);
kprint("nexbox32: FAIL %s status=%d\n", ...);
```

**Device test logging:**
```c
kprint("ehci: hid keyboard smoke OK count=%u\n", ...);
kprint("xhci: hid keyboard smoke OK count=%u\n", ...);
kprint("usb: hid keyboard smoke OK ehci=%u xhci=%u\n", ...);
kprint("usb: hid keyboard smoke FAILED ehci=%u xhci=%u\n", ...);

kprint("rtl8139: tx/rx smoke unavailable present=%u init=%u\n", ...);
kprint("hda: backend smoke unavailable present=%u init=%u\n", ...);
kprint("hda: backend smoke OK bdf=%u:%u.%u codec=%x gcap=%x\n", ...);

kprint("ac97: backend smoke unavailable present=%u init=%u\n", ...);
kprint("ac97: backend smoke OK bdf=%u:%u.%u codec=%x io=%x:%x\n", ...);

kprint("gfx/editor: RUN %s\n", ...);
kprint("gfx/editor: FAIL %s status=%d\n", ...);
```

---

## 5. i386 BOOT USER SERVICES

### **arch/x86/i386/services/boot_user.c**

Hard-coded test/shell prefixes (**NO EQUIVALENT FOR x86_64**):

```c
config->test_name = "/cmd/test32";           // test32 = 32-bit test
config->test_log_prefix = "test32";
config->test_pass_log = "test32: PASS\n";
config->test_fail_log = "test32: FAILED\n";
config->test_boot_pass_log = "selftest: test32 PASS";

config->shell_log_prefix = "init";
config->shell_start_log = "kernel: init starting path=/system/init\n";
config->shell_fail_log = "kernel: init failed path=/system/init\n";
config->shell_exit_log = "kernel: init complete path=/system/init\n";
```

---

## 6. i386 SCHEDULER LOGGING

### **arch/x86/i386/scheduler/run_ops.c**

Uses 32-bit register specifiers:

```c
kprint("scheduler: run_loaded failed name=%s entry=%x stack=%x root=%x\n",
       image->name != 0 ? image->name : "(null)",
       (uint32_t)image->entry,      // 32-bit
       (uint32_t)image->stack,      // 32-bit
       (uint32_t)image->root);      // 32-bit
```

✅ **DIFFERENCE**: Uses `%x` (32-bit) format specifiers

---

## 7. i386 DRIVER SERVICE LOGGING

### **arch/x86/i386/services/driver.c**

```c
kprint("driver: builtin active=%u total=%u files=%u\n",
       active,
       driver_count(),
       driver_file_count());
```

✅ **DIFFERENCE**: i386-specific driver service initialization logging

---

## 8. COMMON DYNAMIC LOGGING

### **kernel/core/kernel_boot.c**

Uses dynamic architecture name from HAL:

```c
kernel_boot_log_system(hal_arch_name());  // Returns "i386" or "x86_64" dynamically
```

Then applies same boot info logging:

```c
kprint("janus: magic=%x version=%u size=%u\n", ...);
kprint("boot: cmdline=%x\n", ...);
kprint("boot: kernel_phys=%lx kernel_size=%lx entry=%lx\n", ...);
kprint("paging: %s virt=%lx phys=%lx mapped=%u user=%u write=%u nx=%u flags=%lx\n", ...);
```

✅ **DIFFERENCE**: kernel/core/kernel_boot.c is SHARED but uses `hal_arch_name()` for architecture-aware prefixes

### **kernel/core/early_boot.c**

Uses **early_kprint** (not regular kprint):

```c
early_kprint("PIC remap + IRQ0 OK, ticks=%u masks=%x\n", ...);
early_kprint("PS/2 keyboard IRQ1 OK, count=%u scancode=%x ascii=%x dropped=%u\n", ...);
early_kprint("PCI OK, devices=%u first=%x storage=%u\n", ...);
early_kprint("Block layer OK, devices=%u partitions=%u\n", ...);
early_kprint("ATA PIO OK, sectors=%u first_partition_lba=%u\n", ...);
early_kprint("FAT32 boot sector OK, signature=%x\n", ...);
early_kprint("FAT32/VFS OK, BOOT/NEX386.ELF size=%u read=%u\n", ...);
early_kprint("ELF32 file OK, class/machine=%x entry=%x\n", ...);
early_kprint("ELF32 user loader OK, entry=%x stack=%x\n", ...);
early_kprint("Ring3 + int 0x40 OK, number=%x argument=%x\n", ...);
early_kprint("Ring3 IRQ0 OK, user_ticks=%u total_ticks=%u\n", ...);
early_kprint("Preemptive RR scheduler OK, ticks=%u switches=%u completed=%u\n", ...);
early_kprint("Scheduler task ticks: task0=%u task1=%u\n", ...);
early_kprint("Per-task root OK, task0=%x task1=%x\n", ...);
early_kprint("Address-space isolation OK, task0=%x task1=%x\n", ...);
```

Note: References "ELF32" (i386-only) and "NEX386.ELF" (i386-specific boot file)

---

## SUMMARY TABLE

| Category | i386 | x86_64 |
|----------|------|--------|
| **Architecture-specific services directory** | ✅ YES (9 files) | ❌ NO |
| **platform_boot.c** | ✅ YES (2 messages) | ❌ NO |
| **ELF Loader kprint statements** | 9 (with "i386" prefix) | 14 (more validation) |
| **Boot Info Logging** | ✅ extensive (30 kprints) | Uses kernel_boot.c |
| **Smoke/Test Logging** | ✅ YES ("test32:" prefix) | ❌ NO |
| **Boot User Service** | ✅ YES (test32 hardcoded) | ❌ NO |
| **Register Size in logs** | 32-bit (%x, %u) | 64-bit (%lx) |
| **ELF Format in messages** | "ELF32" | "ELF" or "ELF64" |

---

## KEY LOGGING DIFFERENCES AT A GLANCE

### **i386 Specific Prefixes**
- "i386:" (in relocation messages)
- "test32:" (in smoke/test messages)  
- "nexbox32:" (in i386-specific app tests)
- "ELF32" (not ELF64)

### **x86_64 Specific**
- More relocation error messages (range validation)
- No "i386" prefix
- No services directory
- No boot user service

### **Architecture-Aware (Both)**
- Uses `hal_arch_name()` for dynamic prefixes
- Uses 32-bit vs 64-bit format specifiers (%x vs %lx)
- Common boot info structure but different paths to reach it
