# ChrysalisOS – Age Verification Rationale

**Author:** mihai209

---

## 🌍 Language / Limbă

<details>
<summary>🇬🇧 English</summary>

## 1. Architectural Position

Age verification introduces **non-deterministic identity dependencies** into a system that should remain:

- deterministic
- auditable
- minimal

This violates core OS design principles.

---

## 2. Security Impact

Age verification systems require:

- identity storage
- external verification endpoints
- trust chains
- user classification

This expands the attack surface with:
- data exfiltration vectors
- privilege escalation paths
- persistent identity mapping

---

## 3. Complexity Cost

Adding age verification implies:

- new system services
- policy engines
- API surfaces
- failure modes

This contradicts:

> minimal trusted computing base (TCB)

---

## 4. Control vs Execution

An operating system should:

✔ execute instructions  
✘ enforce societal policy  

Mixing these roles leads to:
- opaque behavior
- non-local side effects
- reduced developer control

---

## 5. Jurisdictional Fragmentation

Supporting legal compliance per region leads to:

- build fragmentation
- inconsistent behavior
- unverifiable binaries

ChrysalisOS rejects this model entirely.

---

## 6. Project Scope Boundary

ChrysalisOS defines a hard boundary:

> Identity and policy enforcement are external concerns.

---

## 7. Fork Freedom

Forks may implement such systems, but:

- they diverge architecturally
- they are not ChrysalisOS

---

## 8. Conclusion

Age verification is incompatible with:

- deterministic execution
- minimal design
- zero-trust internal model

Therefore, it is permanently excluded.

</details>

---

<details>
<summary>🇷🇴 Română</summary>

## 1. Poziție arhitecturală

Verificarea vârstei introduce dependențe de identitate într-un sistem care trebuie să fie:

- determinist
- auditat
- minimal

---

## 2. Impact de securitate

Necesită:
- stocare identitate
- endpoint-uri externe
- lanțuri de încredere

Crește suprafața de atac:
- exfiltrare date
- escaladare privilegii

---

## 3. Cost de complexitate

Implică:
- servicii noi
- API-uri
- politici runtime

Contrazice:
> TCB minim

---

## 4. Execuție vs control

OS-ul trebuie să:
✔ execute cod  
✘ controleze utilizatorul  

---

## 5. Fragmentare legală

Compliance per țară =:
- build-uri diferite
- comportament inconsistent

Respins complet.

---

## 6. Limită de proiect

Identitatea este externă OS-ului.

---

## 7. Fork-uri

Pot implementa, dar:
- nu mai sunt ChrysalisOS

---

## 8. Concluzie

Incompatibil cu:
- determinism
- minimalism
- zero trust

</details>