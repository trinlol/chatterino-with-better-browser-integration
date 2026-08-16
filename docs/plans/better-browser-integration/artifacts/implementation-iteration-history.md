# Implementation Iteration History: Better Browser Integration Evolution

This file records how the implementation plan evolved. Committed decisions live in
[implementation-decision-log.md](implementation-decision-log.md), and the primary plan lives in
[../feature-implementation-plan.md](../feature-implementation-plan.md).

## R1: Parallel reliability, architecture, test, and implementability review

- **Specialists engaged:** software-architect, on-call-engineer, test-engineer, junior-developer.
- **New input provided:** The operator's requested feature set, repository discovery notes, the upstream comparison, and
  the confirmed direction to evolve the existing Manifest V3 extension and native overlay architecture.
- **Claim ledger:**

  | Claim | State | Originating findings | Aggregated conclusion |
  | --- | --- | --- | --- |
  | CL-01 | Evidenced | ARCH-01, TE-01, JUN-01 | Checkpoint current work, record the upstream SHA, and sync in tested slices before redesign work. |
  | CL-02 | Evidenced | ARCH-05, TE-03, JUN-02/JUN-03, RES-01/RES-09 | Preserve Twitch's chat subtree and hide it only after a session-matched native readiness proof. |
  | CL-03 | Evidenced | ARCH-06/ARCH-07, TE-02/TE-04, JUN-04/JUN-05, RES-02/RES-03/RES-11 | Replace permanent blocking and process-local retry state with a restart-safe connection and reconciliation state machine. |
  | CL-04 | Evidenced | ARCH-03/ARCH-04/ARCH-08, TE-05/TE-07, JUN-06, RES-05/RES-10/RES-13 | Add a backward-compatible protocol and explicit per-window session registry on both sides. |
  | CL-05 | Evidenced | ARCH-10, TE-06, JUN-07, RES-07 | Route browser-bound actions to the originating session and preserve user input until a definitive result arrives. |
  | CL-06 | Evidenced | ARCH-14, TE-08/TE-09/TE-10/TE-11, JUN-08, RES-17 | Build deterministic state and fault tests, then one real Windows and Edge release path. |
  | CL-07 | Evidenced | ARCH-09/ARCH-12, TE-12, JUN-09/JUN-10, RES-14 | Put Twitch sources behind bounded, freshness-aware adapters and measure before tuning observers or polling. |
  | CL-08 | Unverified | TE-12, JUN-10 | No runtime performance baseline exists; establish one before setting final budgets or changing cadence. |
  | CL-09 | Unverified | JUN-11, TE-13 | Activity and moderation features are requested, but their final interaction design and authorization matrix are not supplied. |
  | CL-10 | Evidenced | ARCH-11/ARCH-13, TE-13, JUN-12, RES-15 | Add bounded redacted local diagnostics, then expose small read-only Lua event projections after schemas stabilize. |
  | CL-11 | Unverified | ARCH-09, TE-13 | Official Twitch API and EventSub coverage depends on OAuth scopes and event types that have not been inventoried. |
  | CL-12 | Unverified | TE-09/TE-10/TE-14 | A headed Edge job and dedicated real-app Windows runner are not yet proven available. |
  | CL-13 | Evidenced | RES-04/RES-08/RES-12/RES-16 | Use leases, delivery status, isolated native-host instances, and cooperative teardown to close cross-process failure gaps. |

- **Open Questions raised:**
  - **OQ-1:** How much activity-rail and moderator-cockpit UI should the plan prescribe without visual designs?
    Resolved by junior-developer reframing: commit behavior-level thin slices and a design checkpoint, not invented layouts
    ([D-12](implementation-decision-log.md#d-12-thin-session-scoped-product-slices)).
  - **OQ-2:** Which official Twitch sources and moderation commands can the integration use? Resolved as a staged capability
    and authorization inventory before those work units; unsupported events retain bounded DOM or private-GraphQL fallback
    ([D-10](implementation-decision-log.md#d-10-bounded-twitch-capability-adapters)).
  - **OQ-3:** Can current CI run a headed Manifest V3 extension and a real desktop app? Unverified. A Windows runner spike is
    a work-unit gate; it does not block the reliability and protocol phases
    ([D-9](implementation-decision-log.md#d-9-layered-executable-release-gates)).
  - **OQ-4:** What performance thresholds should apply? Resolved through baseline measurement first, followed by budgets
    based on the recorded scenarios. The requested two-second chat fallback and two-physical-pixel geometry targets remain fixed
    ([D-11](implementation-decision-log.md#d-11-measure-before-performance-tuning)).
  - **OQ-5:** What should happen when a legacy command matches multiple same-channel tabs? Resolved from fail-safe behavior:
    reject the ambiguous command, preserve the draft, and record a diagnostic instead of guessing
    ([D-8](implementation-decision-log.md#d-8-session-exact-command-delivery)).
- **Spec-maturity tags:** Ten plan-level claims (CL-01 through CL-07, CL-10, CL-12, CL-13) and three spec-level claims
  (CL-08, CL-09, CL-11). No contradiction tag was raised. The maturity gate did not trip because each spec-level unknown
  has a bounded discovery or design gate before its dependent phase and does not block the foundation work.
- **Resolution source:** OQ-1 used junior-developer reframing; OQ-2 used repository evidence plus a later specialist
  handoff; OQ-3 remains a non-blocking implementation spike; OQ-4 used evidence and acceptance targets from the requested
  comparison; OQ-5 used evidence and the fail-open requirement. The architecture direction came from user input.
- **Decisions produced:** D-1 through D-14.
- **Changed in plan:** Outcome; User Stories; Constraints and Boundaries; Implementation Approach; Work Units and Sequencing;
  Definition of Done; Testing Strategy; Security Posture; Operational Readiness; On-Call Resilience Posture; Risks and
  Assumptions; Deferred (YAGNI); Open Items; Specialist Handoffs for Implementation; Sources and Plan Records; Recommendation.
- **Next-step recommendation:** Go to synthesis. The four reviews agree on a reliability-first sequence and no disputed
  finding or user-blocking question remains.

## YAGNI sweep

No operator-requested feature was cut. Eight larger mechanisms failed the evidence gate and remain outside the work units;
the thin activity, moderation, Lua, continuity, and diagnostic outcomes remain in scope.

| Deferred candidate | Why it failed the gate | Concrete reopen trigger | Source |
| --- | --- | --- | --- |
| Persisted HWND reuse across browser or operating-system lifetimes | HWNDs can be stale or reused, while desired-session reconciliation already covers the requested recovery outcome. | Startup traces show rediscovery is too slow or unreliable after reconciliation. | ARCH-06, JUN-04 |
| General durable message broker or unbounded command log | Durable desired state, idempotent actions, and explicit results meet current failure cases without event sourcing. | A required command must survive browser-session or desktop lifetime boundaries and cannot be reconstructed safely. | ARCH-07, RES-08 |
| Cross-device or cloud continuity | The requested continuity is local rebinding; no identity, privacy, or conflict policy exists for cloud state. | The operator specifies cross-device behavior, identity ownership, and conflict resolution. | ARCH-10, JUN-07 |
| Remote telemetry, dashboards, fleet storage, or remote kill switch | No consent, privacy, retention, hosting, incident-response, or fleet requirement was supplied. | An approved opt-in telemetry requirement names retention, operator ownership, and a concrete fleet diagnosis gap. | ARCH-13, RES-15, JUN-10/JUN-12 |
| Broad browser-plugin SDK, arbitrary plugin transport, or default control authority | A small read-only Lua projection satisfies the requested extensibility without opening transport or authorization boundaries. | At least two concrete plugins require the same command capability and its authorization model is defined. | ARCH-11, JUN-12 |
| User-facing retry, lease, circuit-breaker, or polling controls | These are internal safety mechanics with no evidenced user decision that improves recovery. | Measurements show distinct environments need policies that cannot be selected automatically. | ARCH-06, RES-02, JUN-05 |
| New frontend framework, replacement for `AttachedWindow`, or general Qt/Win32 abstraction | Narrow seams around the existing boundaries can express every requested behavior. | A verified second platform or repeated native defect class cannot be handled through the narrow adapters. | ARCH-02/ARCH-08/ARCH-12, TE-07/TE-08 |
| Full Cartesian browser, OS, DPI, zoom, and device farm | The defined Windows 11 and Edge matrix covers the current release path without multiplying every combination on every PR. | Support policy expands or field failures cluster outside the defined matrix. | TE-11, JUN-08 |

## Specialist handoffs for implementation

- **Windows native and IPC specialist:** validate queue isolation, HWND ownership and death detection, cooperative receiver
  teardown, the narrow Win32 fault seam, and the headed Edge runner.
- **Twitch API and security specialist:** define OAuth token ownership, minimum scopes, redaction, official-source coverage,
  and fallback policy before authorized moderation or EventSub work.
- **Performance specialist:** establish budgets from instrumented Twitch scenarios after local measurements exist; this
  does not block the foundation phases.

## Unaudited evidence classes

- Live native-host restart traces, desktop captures, and real overlay HWND measurements were unavailable to all specialists.
- A current Twitch OAuth scope inventory and sanitized production GraphQL corpus were unavailable to all specialists.
- CI budget, branch-protection policy, and dedicated interactive Windows-runner availability were unavailable to all specialists.

## Escalation register

### E1: Should this plan evolve the existing architecture or replace it?

- **Round:** R1
- **Answer:** Evolve it.
- **Landed in:** [scope-boundary.md](scope-boundary.md) and the plan's architecture and sequencing decisions.
