# Chrysalis OS – Porting Guide

This document explains how to port **Chrysalis OS** to new architectures,
platforms, or environments.

Chrysalis OS is built with **strict separation between generic kernel code
and architecture-specific implementations**, making porting feasible without
rewriting the entire system.

---

## Currently Supported Architecture

### x86 (i386)

**Status:** Supported  
**Mode:** 32-bit protected mode  
**Boot:** Multiboot / Multiboot2 (GRUB)

Implemented subsystems:
- GDT / IDT
- Paging & Virtual Memory
- Physical Memory Manager
- PIC, APIC, IOAPIC
- SMP (multi-core)
- ACPI
- HPET
- PCI
- Framebuffer graphics
- Interrupt-driven drivers

---

## Architecture Layout

All architecture-specific code lives under:

```

kernel/arch/<arch>/

```

Example:
```

kernel/arch/i386/
├── boot/
├── cpu/
├── gdt/
├── idt/
├── irq/
├── paging/
├── apic/
├── smp/
├── switch.S

```

Generic kernel code must **never** directly depend on architecture details.

---

## What Must Be Ported

To support a new architecture (e.g. x86_64, ARM, RISC-V), the following must be
implemented.

---

### 1. Boot and Entry

- CPU entry point
- Stack initialization
- Early identity mapping
- Transfer control to `kernel_main`

---

### 2. CPU & Context Switching

- Interrupt enable/disable
- Exception handling
- Context save/restore
- Safe task switching

---

### 3. Memory Management

- Physical memory detection
- Paging / MMU setup
- Virtual address space management

---

### 4. Interrupt System

- IRQ delivery mechanism
- Timer interrupts
- Device interrupt routing

---

### 5. Timer Source

One reliable monotonic timer:
- HPET-equivalent
- Platform timer
- CPU-provided counter

---

### 6. Essential Devices

Minimum required devices:
- Serial or framebuffer console
- Keyboard / input abstraction
- Block storage device

---

## Portable Kernel Subsystems

These subsystems are architecture-independent and should not require changes:

- Scheduler
- VFS and filesystems
- Process/task model
- Shell
- UI stack (Compositor, Window Manager, FlyUI)

---

## Suggested Porting Order

1. Serial output (early debugging)
2. Boot to `kernel_main`
3. Exceptions and interrupts
4. Memory management
5. Timer
6. Basic input
7. Storage
8. Graphics
9. SMP (optional)

---

## Experimental and Future Targets

Potential future ports:
- x86_64 (long mode)
- ARMv7 / ARMv8
- RISC-V (RV32)

These are not implemented upstream.

---

## Notes for Modders

Chrysalis OS is released under the **MIT License**.

You are free to:
- Create new architecture ports
- Maintain downstream forks
- Distribute custom builds

Please credit the original project when redistributing.

---

End of file
