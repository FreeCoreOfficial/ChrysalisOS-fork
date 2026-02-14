# Chrysalis OS – Distributions & Forks

Chrysalis OS explicitly supports the creation of custom distributions,
similar to how Linux enables Ubuntu, Arch, Gentoo, etc.

This document defines what a Chrysalis-based distribution is and how to build one.

---

## What Is a Chrysalis Distribution?

A Chrysalis OS distribution is any project that:

- Uses Chrysalis OS as its base
- Modifies or extends the kernel, UI, or tooling
- Ships under a distinct name or vision

Distributions may differ in:
- UI/UX
- Window manager
- Default services
- Driver sets
- Target hardware
- Philosophy

---

## Legal Requirements (GNU GPL v3)

Because Chrysalis OS uses the GNU GPL v3:

You **must**:
- Preserve the original copyright notice
- Acknowledge Chrysalis OS as the upstream base
- Preserve GPL license notices in source and binaries
- Provide corresponding source code when distributing binaries
- Keep derivative distributions under GPL-compatible terms

You **may**:
- Rebrand the OS
- Modify any part of the system
- Distribute binaries or source
- Use it commercially

Copyleft rules apply for distributed derivatives.

---

## Branding & Identity

You are encouraged to:

- Choose a unique name
- Create your own boot splash
- Modify FlyUI or replace it
- Ship custom defaults

Do **not** claim your distro *is* Chrysalis OS.
It should be **based on Chrysalis OS**.

---

## Technical Freedom

Distributions may:

- Replace the window manager
- Swap out FlyUI for another toolkit
- Implement custom schedulers
- Add or remove subsystems
- Freeze kernel versions or track upstream

There is no mandatory compatibility layer.

---

## Recommended Files for Distros

A clean distribution should include:

- `DISTRO.md` – explains what makes your distro unique
- `LICENSE` – includes GNU GPL v3 license text
- `CREDITS.md` – acknowledges Chrysalis OS
- Optional: `ROADMAP.md`

---

## Relationship With Upstream

Upstream Chrysalis OS:

- Does not guarantee API or ABI stability
- Does not enforce design decisions on distros
- Welcomes patches but does not require them

Distributions are sovereign.

---

## Final Words

Chrysalis OS is a **foundation**, not a cage.

Build minimal systems.  
Build experimental systems.  
Build weird systems.

If it boots and you own it — it’s valid.
