# ChrysalisOS – Linux Compatibility Layer

**Author:** mihai209

---

## 🌍 Language / Limbă

<details>
<summary>🇬🇧 English</summary>

## 1. Overview

ChrysalisOS provides a **Linux compatibility layer** designed to execute Linux user-space applications.

This layer is:
- minimal
- non-invasive
- fully separated from the kernel core

Its purpose is **compatibility, not emulation of Linux itself**.

---

## 2. Design Philosophy

The compatibility layer follows these principles:

- no reuse of Linux kernel code
- no copying of internal Linux implementations
- clean-room implementation of required behavior
- strict boundary between ChrysalisOS kernel and compatibility layer

ChrysalisOS remains a **distinct operating system**, not a Linux derivative.

---

## 3. Technical Approach

The compatibility layer works by:

- translating Linux syscalls → ChrysalisOS syscalls
- providing ABI compatibility for ELF64 Linux binaries
- supporting dynamically linked Linux applications

Core mechanisms:

- syscall translation layer
- ELF loader with Linux ABI awareness
- user-space shim libraries

---

## 4. Use of Linux Libraries

ChrysalisOS may support **Linux user-space libraries**, such as:

- libc (e.g. glibc, musl)
- libm
- libpthread
- other standard shared libraries

These libraries are:

✔ **not modified internally**  
✔ **used as external components**  
✔ **dynamically linked where possible**

---

## 5. Licensing Compliance

All third-party libraries used are subject to their respective licenses.

ChrysalisOS ensures compliance by:

- including license texts in:

```
/Licenses/
```

- preserving original copyright notices
- respecting distribution requirements (e.g. LGPL, MIT, BSD)

---

## 6. No Code Reuse Policy

ChrysalisOS does **not include**:

- Linux kernel source code
- copied syscall implementations
- internal kernel logic from Linux

All compatibility behavior is:

> independently implemented based on documented interfaces

---

## 7. Legal Separation

The Linux compatibility layer:

- does not make ChrysalisOS a Linux-based system
- does not link against Linux kernel code
- does not inherit GPL obligations from the kernel

ChrysalisOS remains:

> an independent operating system with optional compatibility features

---

## 8. Limitations

Due to architectural differences:

- not all Linux applications will work
- kernel-dependent features may be unsupported
- performance may vary depending on syscall translation cost

---

## 9. Example: Running a Linux Binary

Suppose you have a Linux ELF64 binary called `hello-linux`. You can execute it in ChrysalisOS as follows:

```bash
# Place the binary in /system/linux_apps/
cp hello-linux /system/linux_apps/

# Run the binary via the compatibility layer
runmod /system/linux_apps/hello-linux
```

### Notes

- The binary runs **via the ChrysalisOS Linux compatibility layer**, not a Linux kernel.
- Some syscalls may be partially implemented or behave differently.
- Expect **bugs, crashes, or incomplete features**, especially for complex Linux applications.
- Dynamic linking with Linux libraries is supported, but library mismatches can occur.

This is an **experimental feature** designed for developer testing and compatibility experiments only.

---

## 10. Fork Responsibility

Forks that:

- modify or embed Linux code
- violate library licenses

are **fully responsible for their own compliance**.

They are not affiliated with ChrysalisOS.

---

## 11. Transparency

The compatibility layer is:

- auditable
- modular
- optional (can be disabled at build time)

---

## 12. Final Statement

> Compatibility is provided without compromising system integrity.

ChrysalisOS does not depend on Linux —  
it only provides a controlled environment to run Linux applications.

</details>

<details>
<summary>🇷🇴 Română</summary>

## 1. Prezentare

ChrysalisOS oferă un **layer de compatibilitate Linux** pentru rularea aplicațiilor user-space Linux.

Acest layer este:
- minimal
- separat de kernel
- neinvaziv

Scopul este **compatibilitate, nu replicarea Linux**.

---

## 2. Filosofie de design

Principii:

- nu reutilizează cod din kernel-ul Linux
- nu copiază implementări interne
- implementare clean-room
- separare strictă kernel ↔ compat layer

ChrysalisOS rămâne un sistem **independent**.

---

## 3. Abordare tehnică

Layer-ul funcționează prin:

- traducere syscall Linux → syscall ChrysalisOS
- compatibilitate ABI pentru ELF64
- suport pentru aplicații dinamice

---

## 4. Librării Linux

Se pot folosi:

- libc (glibc, musl)
- libpthread
- alte librării standard

Acestea sunt:

✔ externe  
✔ nemodificate  
✔ linkate dinamic  

---

## 5. Conformitate licențe

Licențele sunt incluse în:

```
/Licenses/
```

Se respectă:
- LGPL
- MIT
- BSD
- alte licențe aplicabile

---

## 6. Fără reutilizare cod Linux

Nu include:

- cod din kernel Linux
- implementări copiate
- logică internă Linux

Totul este implementat independent.

---

## 7. Separare legală

Layer-ul NU face OS-ul:

- bazat pe Linux
- dependent de GPL kernel

ChrysalisOS rămâne independent.

---

## 8. Limitări

- nu toate aplicațiile vor funcționa
- syscall-urile pot avea overhead
- unele features lipsesc

---

## 9. Exemplu: Rularea unui binar Linux

Presupunem că ai un binar ELF64 numit `hello-linux`. În ChrysalisOS se rulează astfel:

```bash
# Pune binarul în /system/linux_apps/
cp hello-linux /system/linux_apps/

# Rulează binarul prin layer-ul de compatibilitate
runmod /system/linux_apps/hello-linux
```

### Observații

- Binarul rulează **prin layer-ul de compatibilitate**, nu prin kernel Linux.
- Unele syscall-uri pot fi parțial implementate sau diferite.
- Se pot produce **erori, blocări sau comportament incomplet**, mai ales pentru aplicații complexe.
- Linkarea dinamică cu librării Linux este suportată, dar pot apărea incompatibilități.

Aceasta este o **funcționalitate experimentală**, destinată testării și compatibilității pentru dezvoltatori.

---

## 10. Responsabilitatea fork-urilor

Fork-urile care:

- încalcă licențe
- introduc cod Linux

→ sunt responsabile singure

---

## 11. Transparență

Layer-ul este:
- auditat
- modular
- opțional

---

## 12. Declarație finală

> Compatibilitate fără compromisuri de arhitectură.

ChrysalisOS nu depinde de Linux —  
doar permite rularea aplicațiilor Linux.

</details>
