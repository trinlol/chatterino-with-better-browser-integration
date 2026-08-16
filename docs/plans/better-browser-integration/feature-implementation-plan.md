# Feature Implementation Plan: Better Browser Integration Evolution

This plan evolves the existing Manifest V3 extension and native Chatterino overlay into a restart-safe, per-window integration. It keeps today's architecture and compatibility surface. It then strengthens attachment safety, recovery, routing, and verification before adding richer Twitch features. The first promise is simple: a failed integration must leave Twitch chat usable without requiring an extension reload.

## Reliability first, richer Twitch features second

Users get a browser-integrated Chatterino that survives browser-first and desktop-first startup, service-worker suspension, native-host restarts, navigation, and multiple browser windows. Twitch chat remains available whenever the native overlay is not proven healthy, and commands return to the tab and session that originated them.

Once that reliability baseline is demonstrated on Windows and Edge, the same session model supports bounded Twitch activity and moderation views. It also supports local continuity, a small read-only Lua event surface, and useful redacted diagnostics. The integration stays compatible with current extension and native behavior while the migration is in progress.

## User Stories

- **US-1:** As a viewer, I want Twitch chat to remain available when Chatterino cannot attach, so that an integration fault never leaves the page without chat.
- **US-2:** As a viewer with several Twitch windows, I want each Chatterino overlay and action tied to the correct tab, so that duplicate channels and navigation cannot cross wires.
- **US-3:** As a viewer, I want the integration to recover automatically from browser, worker, host, and desktop restarts, so that I do not need to reload the extension.
- **US-4:** As a moderator, I want trustworthy activity and authorized moderation capabilities in the integrated experience, so that I can act without losing session context.
- **US-5:** As a plugin author, I want stable read-only integration events, so that Lua extensions can react without controlling or destabilizing the browser transport.
- **US-6:** As an operator, I want reproducible cross-process tests and redacted diagnostics, so that release failures can be found and explained before users encounter them.

## Constraints and Boundaries

- **Driving constraint:** The current working tree contains valuable local changes. The fork is 32 upstream commits behind the inspected official revision. The highest-churn Twitch and startup paths already have known lifecycle gaps. Baseline and sync work therefore precedes redesign work. ([D-2](artifacts/implementation-decision-log.md#d-2-baseline-and-controlled-upstream-sync))
- **Recorded boundary:** The operator requested the complete improvement set and confirmed that it should evolve the existing architecture. The authoritative boundary is [artifacts/scope-boundary.md](artifacts/scope-boundary.md). ([D-1](artifacts/implementation-decision-log.md#d-1-evolve-the-existing-integration))
- **Out of scope:** The plan does not replace Manifest V3, `AttachedWindow`, or the Qt/Win32 boundary, and it does not introduce cloud continuity, remote telemetry, or a general browser-plugin framework. These are deferred because current evidence does not require them.
- **Compatibility boundary:** Protocol v2 is additive; v0/v1 parsing and behavior remain until the next-phase end-to-end gate passes. ([D-4](artifacts/implementation-decision-log.md#d-4-additive-protocol-v2))
- **Watch after ship:** Twitch markup, private GraphQL operations, OAuth scope availability, Edge lifecycle behavior, and geometry at Windows display transitions remain external change surfaces.

## Compatible migration before product UI

The implementation proceeds as an expand-and-contract migration. Each phase leaves a releasable integration and preserves the prior protocol while new capabilities negotiate in. Product UI waits until the new lifecycle has passed real browser and native verification. ([D-2](artifacts/implementation-decision-log.md#d-2-baseline-and-controlled-upstream-sync))

### Compatibility and migration

First, capture the current extension, native, protocol, and package behavior against the inspected upstream SHA. Next, bring upstream changes across in small tested slices. Recognize already-ported fixes by behavior rather than commit identity. Then isolate the existing connection, session storage, and message-routing responsibilities behind dependency-light seams without changing visible behavior. ([D-3](artifacts/implementation-decision-log.md#d-3-small-testable-extension-boundaries))

The extension background worker remains the composition root. The native host keeps its HWND adapter, and `AttachedWindow` remains the Win32 overlay implementation. New lifecycle components coordinate these existing boundaries rather than replacing them.

### Session lifecycle and protocol

The extension owns desired attachment sessions. The native side owns acknowledged, currently valid overlays. A stable session identifier plus a monotonic generation distinguishes each window/tab lifecycle. Browser window, tab, channel, HWND, and native overlay are attributes of that session. ([D-7](artifacts/implementation-decision-log.md#d-7-per-window-session-ownership))

Protocol v2 adds capability negotiation, session identity, generation, acknowledgements, leases, and result messages without breaking v0/v1 peers. All attachment and command responses echo enough identity to reject stale or cross-window messages. ([D-4](artifacts/implementation-decision-log.md#d-4-additive-protocol-v2))

Attachment becomes a prepare-ack-commit transaction. Measurement and preparation leave Twitch's chat subtree untouched. Only a matching, current acknowledgement of a visible and valid native overlay permits reversible hiding. Rejection, disconnect, detach, overlay loss, or lease expiry reveals Twitch chat within two seconds. ([D-5](artifacts/implementation-decision-log.md#d-5-transactional-fail-open-attachment))

Recovery is a state machine rather than a permanent block. Desired session identity and retry eligibility survive worker suspension in browser-session storage. Alarms provide restart-safe wakeups. Startup reconciles persisted desire with live tabs and native state. Bilateral leases make expiry authoritative, while a bounded circuit breaker distinguishes configuration failures from transient faults. ([D-6](artifacts/implementation-decision-log.md#d-6-restart-safe-recovery-and-leases))

Browser-bound commands route by exact session and generation. A legacy channel-only command that matches more than one live session fails safely; user input remains intact until a definitive result arrives, and uncertain Twitch acceptance is never resent automatically. ([D-8](artifacts/implementation-decision-log.md#d-8-session-exact-command-delivery))

### Twitch integration and product surface

Twitch-derived data enters through bounded adapters. They publish normalized events with source, freshness, supported state, and failure status. Authorized official APIs are preferred where the capability and scope inventory supports them. Timed, circuit-broken DOM or private-GraphQL adapters remain explicit fallbacks. ([D-10](artifacts/implementation-decision-log.md#d-10-bounded-twitch-capability-adapters))

Performance work begins with measurements of observer callbacks, DOM query time, geometry traffic, and retry traffic. Only then are observers scoped, updates coalesced, inactive adapters disconnected, and polling reduced. A low-frequency safety check remains. ([D-11](artifacts/implementation-decision-log.md#d-11-measure-before-performance-tuning))

The first product slices are a read-only activity view and clearly authorized moderator actions, each scoped to the current session and able to degrade without disturbing chat. Their behavior examples are settled before layout work because no visual design was supplied. ([D-12](artifacts/implementation-decision-log.md#d-12-thin-session-scoped-product-slices))

After normalized event schemas are stable, Lua receives a small additive read-only projection with versioned event names and exception isolation. Browser transport access and control authority are not exposed by default. ([D-13](artifacts/implementation-decision-log.md#d-13-read-only-lua-event-projection))

Diagnostics use a bounded local transition ring with reason codes, monotonic timing, shortened session identity, and revision. They exclude chat text, rewards, and credentials. They explain recovery without becoming part of recovery control. ([D-14](artifacts/implementation-decision-log.md#d-14-bounded-redacted-local-diagnostics))

## Work Units and Sequencing

| # | Work Unit | Story | Delivers | Justification | Depends On | Verification |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Checkpoint and upstream baseline | US-6 | A named local checkpoint, exact upstream SHA, behavior baselines, and a controlled upstream sync in reviewable slices. ([D-2](artifacts/implementation-decision-log.md#d-2-baseline-and-controlled-upstream-sync)) | Upstream stability and preservation of current user work were explicitly requested necessities. | — | Extension tests, native tests when buildable, protocol fixtures, packaging checks, and no unexplained golden or skip change after each slice. |
| 2 | Extract lifecycle seams without behavior change | US-3, US-6 | Testable connection supervision, attachment-session storage, and browser-message routing composed by the existing worker. ([D-3](artifacts/implementation-decision-log.md#d-3-small-testable-extension-boundaries)) | Reliable restart and session work requires deterministic state and injected clocks, ports, storage, and scheduling. | 1 | Characterization tests remain green; reducer tables cover current ready, attach, retry, disconnect, and detach behavior. |
| 3 | Add protocol v2 and native session registry | US-2, US-3 | Capability-negotiated v2 messages and explicit desired/actual per-window session records on both sides. ([D-4](artifacts/implementation-decision-log.md#d-4-additive-protocol-v2)) ([D-7](artifacts/implementation-decision-log.md#d-7-per-window-session-ownership)) | Multi-window safety and restart reconciliation are requested features that cannot rely on one global attachment Boolean. | 2 | v0/v1 compatibility fixtures pass; v2 validates identities and ranges; duplicate and stale generations are idempotent or rejected as specified. |
| 4 | Make attachment transactionally fail-open | US-1, US-3 | Twitch DOM preservation, prepare-ack-commit hiding, and automatic reveal on every loss path. ([D-5](artifacts/implementation-decision-log.md#d-5-transactional-fail-open-attachment)) | The request prioritizes stability and specifically identified destructive pre-attachment hiding. | 3 | A synthetic Twitch shell proves original nodes and listeners survive; stale acknowledgements cannot hide chat; all loss paths reveal within two seconds. |
| 5 | Persist and reconcile recovery state | US-1, US-3 | Restart-safe desired sessions, bounded backoff and circuit breaking, bilateral leases, idempotent teardown, and reconciliation after worker or native restart. ([D-6](artifacts/implementation-decision-log.md#d-6-restart-safe-recovery-and-leases)) | Automatic recovery without extension reload is a requested core outcome. | 3, 4 | Destroy-and-recreate worker tests resume exactly one attempt; host and desktop death, overdue alarms, queue loss, sleep, and stale state converge without permanent blocking. |
| 6 | Guarantee session-exact delivery and continuity | US-2, US-3 | Exact reward/reply routing, explicit delivery results, preserved drafts, and local rebinding after navigation or restart. ([D-8](artifacts/implementation-decision-log.md#d-8-session-exact-command-delivery)) | Per-window continuity and correct same-channel behavior were explicitly requested. | 3, 5 | Two windows on the same channel remain independent; ambiguous legacy routing fails without losing the draft; uncertain acceptance is not resent automatically. |
| 7 | Add bounded local diagnostics | US-6 | A redacted transition ring and popup-ready health snapshot across connection, protocol, session, adapter, and overlay boundaries. ([D-14](artifacts/implementation-decision-log.md#d-14-bounded-redacted-local-diagnostics)) | Automatic diagnostics were requested and are needed to make cross-process failures actionable. | 5, 6 | Bounds, redaction, reason codes, copy output, and exception paths are tested; diagnostics never changes lifecycle decisions. |
| 8 | Build executable browser/native fault gates | US-1, US-2, US-3, US-6 | Pure state and protocol tests, a real native-host/IPC process harness, a narrow Win32 seam, and an Edge extension E2E path with controllable faults. ([D-9](artifacts/implementation-decision-log.md#d-9-layered-executable-release-gates)) | Stability claims require executable cross-process evidence rather than source-text inspection. | 3–7 | Automated delay, reorder, disconnect, restart, HWND invalidation, duplicate-window, navigation, and worker-suspension scenarios pass in CI or the documented Windows gate. |
| 9 | Prove the real Windows and Edge release path | US-1, US-2, US-3, US-6 | Scheduled or pre-release validation with the packaged extension, native host, and Chatterino desktop application. ([D-9](artifacts/implementation-decision-log.md#d-9-layered-executable-release-gates)) | The requested acceptance targets include real startup, multi-window, and geometry behavior. | 8 | One hundred mixed startup/restart cycles need no extension reload; duplicate-channel windows stay independent; fallback is under two seconds; geometry is within two physical pixels across the defined DPI/zoom/window-state matrix. |
| 10 | Normalize and harden Twitch capabilities | US-4, US-6 | A capability and OAuth inventory, normalized activity/moderation events and commands, bounded adapters, freshness, cooldown, and safe fallback. ([D-10](artifacts/implementation-decision-log.md#d-10-bounded-twitch-capability-adapters)) | Better-than-extension Twitch integration was requested, but private and DOM sources require containment. | 8; OAuth inventory before authorized commands | Sanitized fixtures cover missing, renamed, null, malformed, rate-limited, hidden, stale, and recovered sources; schema drift cannot escape into Twitch. |
| 11 | Measure and tune integration overhead | US-1, US-6 | Scenario baselines, agreed budgets, scoped observers, coalesced geometry/activity, and inactive-adapter suspension. ([D-11](artifacts/implementation-decision-log.md#d-11-measure-before-performance-tuning)) | Performance improvement was requested, and current evidence contains no measured baseline. | 8, 10 | Recorded scenarios show no more than 10% p95 regression before tuning; final budgets are set from the baseline and enforced without weakening the two-second fallback. |
| 12 | Ship thin activity and moderation slices | US-2, US-4 | Read-only activity first, then minimum-scope authorized moderation actions with session-local continuity and fail-safe degradation. ([D-12](artifacts/implementation-decision-log.md#d-12-thin-session-scoped-product-slices)) | Activity rail and moderator cockpit were part of the requested feature set. | 9–11; behavior/design checkpoint before UI implementation | Adapter-to-component tests cover deduplication, authorization denial, channel switches, stale data, two sessions, and native disconnect; focused browser tests validate the agreed behavior examples. |
| 13 | Publish the stable Lua event projection and diagnostics UX | US-5, US-6 | Versioned read-only Lua events over stable normalized models plus a bounded operator-facing diagnostic view. ([D-13](artifacts/implementation-decision-log.md#d-13-read-only-lua-event-projection)) | Plugin extensibility and automatic diagnostics were requested after the reliability foundation. | 7, 10, 12 | Plugin exceptions are isolated, events remain session-scoped and backward compatible, and diagnostic export remains bounded and redacted. |

## Definition of Done

- [ ] Current user work is checkpointed, the upstream baseline is recorded, and every sync slice passes extension, native, protocol, and packaging gates. ([D-2](artifacts/implementation-decision-log.md#d-2-baseline-and-controlled-upstream-sync))
- [ ] No supported failure path destroys Twitch's chat subtree, and Twitch chat becomes usable within two seconds of reject, disconnect, overlay loss, detach, or lease expiry. ([D-5](artifacts/implementation-decision-log.md#d-5-transactional-fail-open-attachment))
- [ ] One hundred mixed browser-first, desktop-first, worker-suspension, native-host-restart, and desktop-restart cycles complete without an extension reload or stale overlay. ([D-6](artifacts/implementation-decision-log.md#d-6-restart-safe-recovery-and-leases))
- [ ] Two windows, including two tabs on the same channel, attach, navigate, detach, recover, and deliver commands independently. ([D-7](artifacts/implementation-decision-log.md#d-7-per-window-session-ownership))
- [ ] Drafts remain available until a definitive delivery result; ambiguous legacy routing and uncertain acceptance fail without an automatic duplicate send. ([D-8](artifacts/implementation-decision-log.md#d-8-session-exact-command-delivery))
- [ ] Overlay geometry remains within two physical pixels for Edge stable on Windows 11 at DPI 100/125/150/200%, zoom 80/100/125%, normal/maximized/fullscreen/minimize-restore, and a secondary display; unsupported combinations are documented rather than silently accepted. ([D-9](artifacts/implementation-decision-log.md#d-9-layered-executable-release-gates))
- [ ] Native/browser E2E runs as a release gate, with the real packaged Chatterino path scheduled or pre-release if a headed PR runner is unavailable. ([D-9](artifacts/implementation-decision-log.md#d-9-layered-executable-release-gates))
- [ ] Twitch adapters publish source and freshness, enforce timeouts and cooldowns, expire stale data, and degrade without throwing into the host page. ([D-10](artifacts/implementation-decision-log.md#d-10-bounded-twitch-capability-adapters))
- [ ] Performance budgets are based on recorded scenarios and guard observer, DOM-query, geometry, and retry overhead. ([D-11](artifacts/implementation-decision-log.md#d-11-measure-before-performance-tuning))
- [ ] Activity, moderation, Lua, and diagnostic surfaces are session-scoped, capability-aware, exception-isolated, and covered by behavior tests. ([D-12](artifacts/implementation-decision-log.md#d-12-thin-session-scoped-product-slices))
- [ ] Local diagnostics contain no chat text, reward payload, OAuth material, or full session identifiers and remain within their fixed storage bound. ([D-14](artifacts/implementation-decision-log.md#d-14-bounded-redacted-local-diagnostics))

## Testing Strategy

- **Observable behaviors to test:** Fail-open visibility, lifecycle convergence, per-window isolation, exact command results, geometry, adapter freshness and fallback, feature authorization, plugin isolation, and diagnostic redaction.
- **Edge cases requiring coverage:** Duplicate and out-of-order acknowledgements, stale generation, worker destruction, overdue alarm, host and desktop death, dropped IPC, invalid or reused HWND, zero geometry, same-channel windows, SPA navigation, sleep/resume, hidden tabs, malformed Twitch data, scope denial, and uncertain command acceptance.
- **Test doubles posture and levels:** Use pure reducers with injected time/storage/ports for lifecycle logic, sanitized fixtures for Twitch queries, a controllable fake native peer for browser E2E, narrow Win32 functions for native negative cases, and the packaged Edge/native/Chatterino combination for release evidence. ([D-9](artifacts/implementation-decision-log.md#d-9-layered-executable-release-gates))
- **CI posture:** First-attempt failures remain failures; retries may collect diagnostics but cannot turn a failed first attempt green. The full DPI/zoom matrix is scheduled or pre-release rather than multiplied across every PR.

## Security Posture

- Validate protocol identity, generation, capability, geometry ranges, payload limits, and command type on both sides before state changes. ([D-4](artifacts/implementation-decision-log.md#d-4-additive-protocol-v2))
- Inventory token ownership and request only the minimum Twitch OAuth scopes before enabling official moderation commands; authorization denial degrades to read-only behavior. ([D-10](artifacts/implementation-decision-log.md#d-10-bounded-twitch-capability-adapters))
- Keep private GraphQL and DOM fallbacks timed, circuit-broken, and data-minimized; never expose credentials or raw browser transport to Lua.
- Redact chat content, rewards, OAuth material, full session identifiers, and raw private responses from diagnostics. ([D-14](artifacts/implementation-decision-log.md#d-14-bounded-redacted-local-diagnostics))

## Operational Readiness

- Roll out by negotiated capability: legacy peers remain on v0/v1 while mutually capable peers enter v2, making capability rollback possible without reverting the whole release. ([D-4](artifacts/implementation-decision-log.md#d-4-additive-protocol-v2))
- Gate widening in order: deterministic lifecycle tests, fake-peer Edge E2E, real packaged Windows validation, then later Twitch and product slices.
- Treat the 100-cycle real-app result, two-second fallback, two-window isolation, and geometry matrix as release evidence rather than local build evidence.
- Preserve a documented path back to the legacy attachment behavior until the next-phase E2E gate passes; fail-open Twitch visibility remains mandatory during rollback.

## On-Call Resilience Posture

- Retry only while desired sessions exist, cap exponential backoff, coalesce replaceable traffic, and reset the breaker on successful handshake or evidence-bearing lifecycle probes. ([D-6](artifacts/implementation-decision-log.md#d-6-restart-safe-recovery-and-leases))
- Make attach, detach, lease renewal, and reconciliation idempotent; stale generations cannot mutate current state.
- Expiry is authoritative on both sides: the browser reveals Twitch and the desktop removes or hides the invalid overlay even if the explicit loss message was dropped.
- Preserve drafts until definitive results and never auto-resend after uncertain external acceptance. ([D-8](artifacts/implementation-decision-log.md#d-8-session-exact-command-delivery))
- Use cooperative receiver teardown and session-lost reasons so native shutdown does not strand hidden chat or block a replacement host.

## Risks and Assumptions

### Risks

| ID | Risk | Impact | Mitigation | Owner |
| --- | --- | --- | --- | --- |
| R1 | Upstream sync overlaps current local changes. | Valuable fork work or compatibility behavior could be lost. | Checkpoint first, sync in tested slices, and recognize already-ported fixes by behavior. | Implementation lead |
| R2 | Twitch changes markup or private GraphQL without notice. | Activity or moderation data becomes stale or unavailable. | Normalize behind bounded adapters, expire stale data, and keep explicit fallback and unsupported states. | Twitch API/security specialist |
| R3 | Acknowledgement is emitted before the native overlay is truly usable. | Twitch chat could be hidden behind an invalid overlay. | Require valid process/HWND ownership, nonzero geometry, expected channel, and visible overlay before readiness. | Windows native/IPC specialist |
| R4 | Cross-process races duplicate or misroute user actions. | A message could reach the wrong tab or be sent twice. | Route by session and generation, preserve drafts, and model uncertain acceptance without automatic resend. | Integration lead |
| R5 | Later product UI expands before behavior is settled. | Reliability work is delayed and untested layouts become accidental requirements. | Require behavior examples and a focused design checkpoint before each thin UI slice. | Product/UI owner |

### Assumptions

| ID | Assumption | What Changes If Wrong | Status |
| --- | --- | --- | --- |
| A1 | Current runtime behavior and passing tests are the migration compatibility baseline. | Work Unit 1 must add operator-approved characterization before sync or extraction. | Verified |
| A2 | Browser session storage and extension alarms can carry desired intent and retry eligibility across MV3 suspension. | Recovery persistence needs a different browser-local mechanism, without adding a database. | Runtime-only |
| A3 | A headed Edge job and a dedicated real-app Windows runner can be made available. | Fake-peer E2E remains CI-gated, while real-app evidence moves to a documented manual pre-release gate. | Open |
| A4 | Official Twitch API or EventSub coverage and the necessary OAuth scopes exist for some requested activity/moderation capabilities. | Unsupported capabilities stay read-only or use the bounded existing fallback; no privilege is inferred. | Open |
| A5 | The recorded real-browser scenarios can establish a stable performance baseline. | Final budgets wait while instrumentation or scenario control is improved. | Runtime-only |

## Deferred (YAGNI)

This is work no evidence supports yet, not work the work item excludes. Every entry carries the trigger that would justify revisiting it.

### Persisted HWND reuse across browser or operating-system lifetimes

- **Why deferred:** Window handles can be stale or reused, and durable desired-session reconciliation is sufficient for the requested recovery behavior.
- **Reopen when:** Measured startup traces show rediscovery is too slow or unreliable after session reconciliation.
- **Source:** R1, software-architect and junior-developer.

### General durable message broker or unbounded command log

- **Why deferred:** Current reliability needs durable desired state, idempotent reconciliation, and explicit results, not an event-sourcing system.
- **Reopen when:** A required command must survive browser-session or desktop lifetime boundaries and cannot be reconstructed safely.
- **Source:** R1, software-architect and on-call-engineer.

### Cross-device or cloud continuity

- **Why deferred:** Requested continuity is local rebinding across navigation and restarts; no account, privacy, or synchronization requirement exists.
- **Reopen when:** The operator specifies cross-device behavior, identity ownership, and conflict resolution.
- **Source:** R1, software-architect and junior-developer.

### Remote telemetry, dashboards, fleet storage, or remote kill switch

- **Why deferred:** There is no privacy, retention, hosting, incident-response, or fleet requirement; bounded local diagnostics cover the evidenced need.
- **Reopen when:** Opt-in telemetry requirements, retention policy, operator ownership, and a concrete fleet diagnosis gap are approved.
- **Source:** R1, software-architect, on-call-engineer, test-engineer, and junior-developer.

### Broad plugin SDK or default plugin control authority

- **Why deferred:** The requested plugin benefit can start with stable read-only Lua events; arbitrary browser transport would enlarge the compatibility and security surface.
- **Reopen when:** At least two concrete plugins require the same missing command capability and its authorization model is defined.
- **Source:** R1, software-architect and junior-developer.

### User-facing retry, lease, circuit-breaker, or polling controls

- **Why deferred:** These are internal safety mechanics with no evidenced user decision that improves recovery.
- **Reopen when:** Measured environments require distinct policies that cannot be selected automatically.
- **Source:** R1, software-architect, on-call-engineer, and junior-developer.

### New frontend framework, `AttachedWindow` replacement, or general Qt/Win32 abstraction

- **Why deferred:** The existing composition root and native overlay boundary can support the requested changes through narrow seams.
- **Reopen when:** The narrow interfaces cannot express a verified second platform or repeated native defect class.
- **Source:** R1, software-architect and test-engineer.

### Full Cartesian browser, operating-system, DPI, zoom, and device farm

- **Why deferred:** The defined Windows 11 and Edge matrix covers the current supported release path without multiplying every combination on every PR.
- **Reopen when:** Support policy expands or field failures cluster outside the defined matrix.
- **Source:** R1, test-engineer and junior-developer.

## Open Items

- **OI-1:** What exact interaction and visual design should the activity view and moderator cockpit use?
  - **Resolves when:** The product/UI owner approves behavior examples and a focused design artifact before Work Unit 12 UI implementation.
  - **Blocks implementation:** No. It blocks only the dependent product UI slice, not reliability, protocol, adapters, or read-only models.
- **OI-2:** Which requested Twitch capabilities are available through official APIs or EventSub under the tokens and scopes this application can safely own?
  - **Resolves when:** The Twitch API/security specialist completes the capability, token-ownership, and minimum-scope inventory before Work Unit 10 authorized commands.
  - **Blocks implementation:** No. It blocks official-source and authorized moderation choices, while earlier phases and bounded fallback adapters can proceed.
- **OI-3:** Can project CI provide a headed Edge extension job and a dedicated Windows machine for packaged Chatterino validation?
  - **Resolves when:** The Windows native/IPC and test owners complete the runner spike in Work Unit 8.
  - **Blocks implementation:** No. It determines whether Work Unit 9 is automated or a documented pre-release gate. It does not block the foundation.
- **OI-4:** What final callback, query, geometry, and retry budgets should CI enforce?
  - **Resolves when:** Work Unit 11 records the current scenarios and the performance specialist reviews their distributions.
  - **Blocks implementation:** No. It blocks final performance thresholds, not instrumentation or the fixed fallback and geometry acceptance targets.

## Specialist Handoffs for Implementation

- **`Windows native and IPC specialist`** — dispatch before Work Units 3 and 8; needs the protocol draft, queue-instance model, HWND/session registry proposal, fault matrix, and available Windows runner inventory.
- **`Twitch API and security specialist`** — dispatch before Work Unit 10; needs the requested capability list, current token ownership, available OAuth scopes, sanitized adapter fixtures, redaction policy, and fallback rules.
- **`Performance specialist`** — dispatch after Work Units 8 and 10 have instrumentation and representative scenarios; needs callback, DOM-query, geometry, retry, and adapter timing distributions.

## Sources and Plan Records

- **Feature specification:** No source specification file was provided; inputs were the operator's requested improvement set, the repository/upstream comparison, and the confirmed direction to evolve the architecture.
- **Discovery evidence:** [artifacts/.discovery-notes.md](artifacts/.discovery-notes.md)
- **Recorded scope:** [artifacts/scope-boundary.md](artifacts/scope-boundary.md)
- **Decision rationale and rejected alternatives:** [artifacts/implementation-decision-log.md](artifacts/implementation-decision-log.md)
- **Team composition and round-by-round history:** [artifacts/implementation-iteration-history.md](artifacts/implementation-iteration-history.md)

## Start with Work Units 1–8

Ship the plan in sequence, beginning with the checkpoint and reliability foundation. Hold only the later authorized Twitch, product UI, and final performance gates for their named specialist inputs; no open item blocks Work Units 1–8.
