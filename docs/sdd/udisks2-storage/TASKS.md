# UDisks2 Storage — I-001

| Task | Requirements | State | Evidence |
|---|---|---|---|
| Separate component exports and dependencies | R1 | Done | Storage-only and legacy Audio install consumers |
| Public types, stable models and injection seam | R2, R8 | Done | Model identity and raw topology tests |
| Async ObjectManager backend and lifetime handling | R3 | Done | Isolated fake D-Bus discovery, invalidation, races and restart tests |
| Safe correlated operations and sibling confirmation | R4–R7 | Done | Sequencing, partial failure, remount, cancellation and scope tests |
| Clean acceptance, review and local commit | R1–R8 | Done | Clean build; 4/4 suites; affected Storage rerun; formatting, diff and REUSE checks passed |

Publication is a separate handoff. USB, optical-media and real polkit interaction remain required manual ecosystem
checks after consumer implementation; no automated fake is claimed as hardware evidence.
