# Contributing to Chrysalis OS

First of all: **thank you for your interest in Chrysalis OS**.
This project is both a learning journey and a long-term operating system experiment, built step by step with clarity and correctness in mind.

Contributions are welcome — whether they are code, documentation, testing, or design discussions.

---

## Philosophy

Chrysalis OS follows a few core principles:

* **Correctness over speed**
* **Clarity over clever hacks**
* **Incremental evolution**
* **Educational value matters**

If a solution is harder but architecturally correct, it is preferred.

---

## What You Can Contribute

You are welcome to contribute in many ways:

* Kernel features (drivers, FlyUI components, syscalls)
* Standalone app development (.petal apps)
* Installer logic and deployment scripts
* Bug fixes and stability improvements
* Refactoring for clarity or maintainability
* Documentation (very important)
* Experiments (clearly marked as such)

Even small improvements are valuable.

---

## Code Style & Guidelines

* Language: **C / C++ (freestanding)** and **x86 assembly**
* Target: **i386 (32-bit, protected mode)**
* Avoid dynamic allocation unless explicitly intended
* Avoid undefined behavior
* Prefer explicit code over “magic”

### Important

* Do **NOT** rely on libc
* Do **NOT** remove `-ffreestanding`
* Do **NOT** assume interrupts are always enabled
* Always consider **reentrancy** and **IRQ safety**

---

## Branching & Commits

* Keep commits **small and focused**
* One logical change per commit
* Clear commit messages (what + why)

Example:

```
mem: fix buddy allocator merge logic
```

---

## Testing Expectations

Before submitting a PR:

* Boot the kernel successfully
* Test inside QEMU
* Prefer testing with:

  * serial output enabled
  * interrupts both enabled and disabled
* If touching scheduling or IRQs, **expect crashes** — that’s normal

---

## Notes for Modders (Concise Checklist)

These notes are especially important if you plan to modify core kernel behavior.

### Preemptive Scheduling (IMPORTANT)

To implement **real preemptive multitasking**, be aware:

* A robust `arch/i386/switch.S` is required
  Saving only ESP is **not sufficient** for complex kernels.

  A proper context switch should:

  * Save full CPU state (`pushad`, `pushf`)
  * Preserve segment registers if needed
  * Restore state symmetrically

* In the PIT IRQ handler:

  * Save CPU context
  * Call `schedule()`
  * Restore context before returning

* User-mode tasks require:

  * A safe trampoline
  * Proper `iret` frames
  * Clean separation between ring 0 and ring 3 stacks

Simple tricks may work briefly — but will fail under load.

---

### Avoiding Triple Faults

Triple faults are **not catchable** and will reset the machine.

To avoid them:

* Install a **double-fault handler** in the IDT
* The double-fault handler should:

  * Print minimal debug info (prefer serial)
  * Halt the system or enter a safe loop

If the double-fault handler itself is invalid, the CPU will triple-fault.

---

### Diagnostics & Debugging Tips

* Keep **serial output** active during early boot
  It is the most reliable debug channel.

* When enabling `TASKS_ENABLED`:

  * Use VM snapshots
  * Expect crashes
  * Iterate carefully

* Recommended approach:

  * Create a minimal `switch_test` routine
  * Test context switching:

    * No interrupts
    * Tiny stacks
    * Controlled environment
  * Expand gradually to IRQ-driven scheduling

---

## Documentation Contributions

Documentation is **first-class** in Chrysalis OS.

You may contribute:

* Explanations of subsystems
* Architecture notes
* Design decisions
* Pitfalls and lessons learned

Clear documentation is as valuable as working code.

---

## License & Attribution

Chrysalis OS is released under the **MIT License**.

You may:

* Use
* Modify
* Redistribute

**Conditions:**

* The original author must be credited
* The license notice must be preserved

See the LICENSE file for details.

---

## Final Words

Chrysalis OS is intentionally ambitious.
You are encouraged to experiment — but also to **understand why things work (or break)**.

If you are here to learn low-level systems programming:
you are in the right place.

---

*Happy modding.* 🐣🦋
