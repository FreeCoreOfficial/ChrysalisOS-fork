# ChrysalisOS – Age Verification Policy

**Author:** mihai209

---

## 🌍 Language / Limbă

<details>
<summary>🇬🇧 English</summary>

## 1. Core Principle

ChrysalisOS is a **minimal, deterministic, identity-agnostic operating system**.

It does not:
- identify users
- classify users
- enforce behavioral policies

The OS exists strictly as a **neutral execution layer**.

---

## 2. Strict Age Verification Policy

ChrysalisOS **does not and will never implement**:

- age verification
- age estimation (AI, biometric, heuristic)
- age-based classification
- identity binding (ID, KYC, digital identity systems)
- OS-level exposure of user attributes

This applies to:
- kernel
- system services
- official builds

**No exceptions. No optional modules.**

---

## 3. Jurisdiction Restrictions

Any country or jurisdiction that enforces **mandatory age verification at system level** is:

> ❌ **NOT authorized to use ChrysalisOS official builds**

Such regions will be listed in:


/system/BLACKLISTED.md


ChrysalisOS does not guarantee functionality, support, or legality in those regions.

---

## 4. Data Collection Policy

ChrysalisOS collects **only minimal local data**, stored in:


/system/users/*.json


Fields:
- username (required)
- password (optional)
- device name (required)

ChrysalisOS:
- has **no telemetry**
- has **no analytics**
- has **no remote API**
- does **not transmit user data**

All data remains **local and user-controlled**.

---

## 5. No OS-Level Control Layer

ChrysalisOS does not enforce:
- content restrictions
- application restrictions
- parental controls
- network filtering based on identity

Any such mechanisms must exist **outside the OS**.

---

## 6. Fork Policy

The MIT License allows modification, however:

- forks that implement **age verification or identity systems**
  are **NOT affiliated** with ChrysalisOS

- the ChrysalisOS name **must not imply endorsement**

---

## 7. No Compliance Mode

ChrysalisOS will never provide:
- compliance builds
- regional variants
- identity integration
- legal adaptation layers

---

## 8. Transparency Feature

The system includes a UI element:

> 📄 **"View BLACKLISTED.md"**

This allows users to inspect restricted jurisdictions.

---

## 9. Final Statement

> The OS executes code. It does not evaluate the user.

ChrysalisOS will never become an identity-aware or control-enforcing system.

</details>

---

<details>
<summary>🇷🇴 Română</summary>

## 1. Principiu de bază

ChrysalisOS este un sistem de operare **minimal, determinist și fără identitate**.

Nu:
- identifică utilizatorii
- clasifică utilizatorii
- aplică politici comportamentale

Este strict un **layer neutru de execuție**.

---

## 2. Politica strictă privind verificarea vârstei

ChrysalisOS **nu implementează și nu va implementa niciodată**:

- verificare de vârstă
- estimare de vârstă (AI, biometric, euristic)
- clasificare pe bază de vârstă
- legare de identitate (ID, KYC, identitate digitală)
- expunerea atributelor utilizatorului la nivel de OS

Se aplică pentru:
- kernel
- servicii de sistem
- build-uri oficiale

**Fără excepții. Fără module opționale.**

---

## 3. Restricții jurisdicționale

Țările care impun **verificare de vârstă la nivel de sistem** sunt:

> ❌ **NEAUTORIZATE să utilizeze ChrysalisOS oficial**

Acestea sunt listate în:


/system/BLACKLISTED.md


Nu garantăm funcționare sau suport în acele regiuni.

---

## 4. Politica de colectare a datelor

ChrysalisOS colectează doar date minime locale:


/system/users/*.json


Câmpuri:
- username (obligatoriu)
- parolă (opțional)
- nume dispozitiv (obligatoriu)

ChrysalisOS:
- nu are telemetry
- nu are analytics
- nu are API remote
- nu transmite date

Datele sunt **locale și controlate de utilizator**.

---

## 5. Fără control la nivel de OS

Nu implementează:
- restricții de conținut
- restricții de aplicații
- control parental
- filtrare pe bază de identitate

---

## 6. Politica pentru fork-uri

Fork-urile care adaugă:
- verificare de vârstă
- sisteme de identitate

→ **NU sunt afiliate** cu ChrysalisOS

---

## 7. Fără mod de conformitate

Nu vor exista:
- build-uri conforme legal
- variante regionale
- integrare identitate

---

## 8. Funcție de transparență

OS-ul include:

> 📄 **"Vezi BLACKLISTED.md"**

---

## 9. Declarație finală

> OS-ul execută cod. Nu evaluează utilizatorul.

ChrysalisOS nu va deveni niciodată un sistem de control.

</details>