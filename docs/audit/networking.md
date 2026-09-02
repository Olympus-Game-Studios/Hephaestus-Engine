# Networking audit

**Maturity: Level 0 — Missing**

No sockets, replication, dedicated server target, or net-id component in `engine/`.

**Impact:** Multiplayer genres are a **poor fit**. Do not treat EnTT ids as net ids.

**Necessary for Hephaestus 1.x?** Only if the studio’s next title is online. Foundations missing: tick model, stable entity ids, input actions, profiler.

**Recommended:** Explicitly out of scope. Do not prototype Steam/EOS until single-player gameplay plumbing exists.
