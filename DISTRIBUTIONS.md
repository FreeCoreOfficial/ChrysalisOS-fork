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

## Legal Requirements (MIT License)

Because Chrysalis OS uses the MIT License:

You **must**:
- Preserve the original copyright notice
- Acknowledge Chrysalis OS as the upstream base

You **may**:
- Rebrand the OS
- Modify any part of the system
- Distribute binaries or source
- Use it commercially

There is no copyleft, no viral clause, no forced openness.

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
- `LICENSE` – includes MIT license text
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
