---
paths:
  - "**/migrations/**"
  - "**/schema/**"
  - "**/*.sql"
  - "**/prisma/**"
---

# Data and schema rules

<!-- Loads only when Claude reads a matching file. Adjust the paths list to the repo's ORM or migration tool. -->

- Migrations are additive and reversible. Never edit an applied migration; write a new one.
- Schema and API changes stay backward compatible for one release: expand, migrate, contract.
- No destructive data operations (drop, truncate, delete without a filter, history rewrite) without explicit approval in the thread.
- Concurrency: share nothing, or share immutable. Any shared mutable state has one documented owner.
