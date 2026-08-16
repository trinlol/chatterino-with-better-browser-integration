# Implementation Decision Log: Better Browser Integration Evolution

This file records the implementation decisions behind [the primary plan](../feature-implementation-plan.md). Round evidence and team composition live in [implementation-iteration-history.md](implementation-iteration-history.md).

## Trivial decisions

- None. Each committed decision either has dependent decisions, rejected alternatives, or repository evidence and therefore uses the full form.

## Full decisions

### D-1: Evolve the existing integration

- **Question:** Should the requested improvements evolve the current browser/native integration or replace it wholesale?
- **Decision:** Retain and improve the Manifest V3 extension, native host, and Chatterino overlay architecture.
- **Rationale:** The operator explicitly chose evolution, and the existing boundaries already implement the product behavior that the reliability and feature work extends.
- **Evidence:** User input, "evolving it"; `artifacts/scope-boundary.md`; ARCH-00.
- **Rejected alternatives:**
  - Replace the extension/native architecture wholesale — rejected because it conflicts with the confirmed direction and discards the current compatibility surface without an evidenced need.
- **Specialist owner:** Implementation lead.
- **Revisit criterion:** Revisit only on new operator direction or evidence that a required behavior cannot be expressed through the existing boundaries.
- **Dissent (if any):** None.
- **Driven by rounds:** R1.
- **Dependent decisions:** D-2, D-3, D-4, D-5, D-6, D-7, D-8, D-9, D-10, D-11, D-12, D-13, D-14.
- **Referenced in plan:** Constraints and Boundaries.

### D-2: Baseline and controlled upstream sync

- **Question:** How should upstream Chatterino work and the dirty local tree be reconciled before integration redesign?
- **Decision:** Create a named checkpoint, record the exact upstream revision and current behavior, then sync in reviewable slices with extension, native, protocol, and package verification after each slice.
- **Rationale:** The fork is 32 upstream commits behind the inspected revision, 18 files have overlapping change, and current uncommitted work already carries several upstream fixes. Mixing that reconciliation with lifecycle redesign would make regressions difficult to attribute.
- **Evidence:** `artifacts/.discovery-notes.md`; ARCH-01 and ARCH-15; TE-01; JUN-01.
- **Rejected alternatives:**
  - Blindly merge or cherry-pick all upstream commits — rejected because already-ported behavior and overlapping files could be duplicated or lost.
  - Refactor attachment while syncing upstream — rejected because it removes the stable baseline needed to isolate compatibility failures.
- **Specialist owner:** Implementation lead.
- **Revisit criterion:** Revisit only if the local work is first cleanly separated and an upstream integration mechanism proves equivalent at each stated gate.
- **Dissent (if any):** None.
- **Driven by rounds:** R1.
- **Dependent decisions:** D-3, D-4, D-5, D-6, D-7, D-8, D-9, D-10, D-11, D-12, D-13, D-14.
- **Referenced in plan:** Constraints and Boundaries; Implementation Approach; Compatibility and migration; Work Units and Sequencing; Definition of Done.

### D-3: Small testable extension boundaries

- **Question:** How should lifecycle logic become testable without replacing the extension architecture?
- **Decision:** Keep `background.js` as the composition root and separate dependency-light connection supervision, attachment-session storage, and browser-message routing with injected browser, timer, storage, and port dependencies.
- **Rationale:** Manifest V3 restarts and cross-process races require deterministic state tests, but a general framework would add migration risk without supporting a requested behavior.
- **Evidence:** `chatterino-extension/background.js`; ARCH-02 and ARCH-07; TE-02; the high-churn finding in `artifacts/.discovery-notes.md`.
- **Rejected alternatives:**
  - Keep all lifecycle state in process-global maps and callbacks — rejected because worker recreation cannot be simulated or recovered deterministically.
  - Introduce a general extension framework — rejected because no second application or broad plugin transport requirement exists.
- **Specialist owner:** Browser integration lead.
- **Revisit criterion:** Revisit the boundary names after characterization if the responsibilities cannot be isolated without changing behavior.
- **Dissent (if any):** None.
- **Driven by rounds:** R1.
- **Dependent decisions:** D-4, D-5, D-6, D-7, D-8, D-10, D-11, D-14.
- **Referenced in plan:** Compatibility and migration; Work Units and Sequencing.

### D-4: Additive protocol v2

- **Question:** How should the protocol carry session identity and reliable results without breaking installed peers?
- **Decision:** Add capability-negotiated protocol v2 fields and typed messages for session, generation, tab/window identity, leases, acknowledgement, reconciliation, and command results while retaining v0/v1 parsing through the next-phase E2E gate.
- **Rationale:** Both extension and desktop can be upgraded in different orders. Additive negotiation permits independent rollout and gives stale and malformed messages explicit rejection semantics.
- **Evidence:** `chatterino-extension/protocol.js`; `src/singletons/NativeMessagingProtocol.cpp`; ARCH-03; RES-05 and RES-13; TE-05.
- **Rejected alternatives:**
  - Break directly to a v2-only protocol — rejected because browser and desktop update order is not guaranteed.
  - Continue inferring identity from channel name and global attachment state — rejected because duplicate channels and stale acknowledgements are ambiguous.
- **Specialist owner:** Protocol owner with Windows native/IPC specialist review.
- **Revisit criterion:** Remove legacy parsing only after the next release phase records compatible packaged E2E results and an explicit minimum-version policy.
- **Dissent (if any):** None.
- **Driven by rounds:** R1.
- **Dependent decisions:** D-5, D-6, D-7, D-8, D-9, D-10, D-12, D-13, D-14.
- **Referenced in plan:** Constraints and Boundaries; Session lifecycle and protocol; Work Units and Sequencing; Definition of Done; Security Posture; Operational Readiness.

### D-5: Transactional fail-open attachment

- **Question:** When may the extension replace or hide Twitch chat?
- **Decision:** Preserve Twitch's chat subtree during prepare, hide it reversibly only after a matching current acknowledgement proves a visible valid native overlay, and reveal it on reject, disconnect, detach, overlay loss, or lease expiry within two seconds.
- **Rationale:** The current destructive replacement can leave no usable chat when native attachment fails. Session- and generation-matched readiness prevents a stale overlay from authorizing a hide.
- **Evidence:** `chatterino-extension/overlay.js`; `chatterino-extension/anti-wipe.js`; ARCH-05; RES-01 and RES-09; TE-03; JUN-02 and JUN-03.
- **Rejected alternatives:**
  - Keep destructive `innerHTML` replacement and repair with `anti-wipe` — rejected because restoration races Twitch's React ownership and can outlive a failed overlay.
  - Treat host connection or screenshot presence as attachment readiness — rejected because neither proves the correct live HWND, channel, geometry, and session.
- **Specialist owner:** Browser integration lead with Windows native/IPC specialist validation.
- **Revisit criterion:** The exact readiness proof may be strengthened if real-app traces identify another necessary condition; the fail-open guarantee does not change.
- **Dissent (if any):** None.
- **Driven by rounds:** R1.
- **Dependent decisions:** D-6, D-7, D-9.
- **Referenced in plan:** Session lifecycle and protocol; Work Units and Sequencing; Definition of Done.

### D-6: Restart-safe recovery and leases

- **Question:** How should the integration recover from transient failures and MV3 worker recreation without reloads or permanent blocking?
- **Decision:** Use explicit lifecycle and breaker states, persist desired session identity and retry eligibility in browser-session storage, use alarms and lifecycle events as durable wakeups, reconcile on startup, and enforce bilateral fixed internal leases with authoritative expiry.
- **Rationale:** Ports, timers, and process-global maps do not survive worker suspension, and the current permanent `portConnectBlocked` state prevents evidence-triggered recovery. Desired state plus idempotent reconciliation is sufficient without a database or durable message log.
- **Evidence:** `chatterino-extension/background.js`; ARCH-06 and ARCH-07; RES-02, RES-03, RES-04, RES-06, RES-11, and RES-16; TE-02 and TE-04; JUN-04 and JUN-05.
- **Rejected alternatives:**
  - Persist timers, ports, or HWND values — rejected because they are not valid durable identities across lifecycle boundaries.
  - Stop forever after a fixed attempt count — rejected because native-host availability can change after startup.
  - Add user-facing recovery tuning — rejected because no user decision is needed for the evidenced automatic behavior.
- **Specialist owner:** Browser integration lead and on-call resilience owner.
- **Revisit criterion:** Revisit internal backoff and lease values after fault-harness and real-app measurements, without exposing knobs unless automatic selection fails in distinct environments.
- **Dissent (if any):** None.
- **Driven by rounds:** R1.
- **Dependent decisions:** D-7, D-8, D-9, D-12, D-14.
- **Referenced in plan:** Session lifecycle and protocol; Work Units and Sequencing; Definition of Done; On-Call Resilience Posture.

### D-7: Per-window session ownership

- **Question:** What state model prevents multiple windows and duplicate channels from sharing attachment state?
- **Decision:** Let the extension own desired sessions and a native `AttachmentSessionRegistry` own acknowledged actual overlays, keyed by opaque session ID and monotonic generation; channel, window, tab, HWND, and native overlay remain validated attributes.
- **Rationale:** The native global attached Boolean and channel-first browser routing cannot describe multiple overlays or distinguish a replaced tab lifecycle. A session registry also makes attach and detach idempotent.
- **Evidence:** `src/singletons/NativeMessaging.cpp`; `src/widgets/AttachedWindow.cpp`; ARCH-04 and ARCH-08; RES-05 and RES-10; TE-05 and TE-07; JUN-06.
- **Rejected alternatives:**
  - Key sessions by channel name — rejected because the same channel can be open in several tabs or windows.
  - Let `AttachedWindow` own protocol state — rejected because Win32 overlay mechanics should not become transport/session policy.
- **Specialist owner:** Desktop integration lead with Windows native/IPC specialist review.
- **Revisit criterion:** Revisit only if a verified multi-desktop-process requirement needs a wider process-qualified identity; that requirement is currently deferred.
- **Dissent (if any):** None.
- **Driven by rounds:** R1.
- **Dependent decisions:** D-8, D-9, D-12, D-13, D-14.
- **Referenced in plan:** Session lifecycle and protocol; Work Units and Sequencing; Definition of Done.

### D-8: Session-exact command delivery

- **Question:** How should rewards, replies, and later continuity actions reach the correct browser target without duplicate sends?
- **Decision:** Route by request ID, session ID, and generation with channel validation; preserve user input until a definitive result, retry only before delivery, represent uncertain Twitch acceptance as a retryable failure without automatic resend, and reject ambiguous legacy channel-only matches.
- **Rationale:** Selecting the first matching channel can target the wrong tab. An exactly-once promise cannot be supported across the external Twitch boundary, so the safe behavior is explicit uncertainty and user-preserved state.
- **Evidence:** `src/widgets/splits/SplitInput.cpp`; `chatterino-extension/background.js`; ARCH-10; RES-07 and RES-08; TE-06; JUN-07.
- **Rejected alternatives:**
  - Route to the first tab with the same channel — rejected because duplicate-channel windows are valid and indistinguishable by channel.
  - Automatically resend after uncertain external acceptance — rejected because that can duplicate the user's action.
  - Claim exactly-once delivery — rejected because Twitch acceptance is outside the integration's transaction boundary.
- **Specialist owner:** Cross-process integration lead.
- **Revisit criterion:** Revisit resend behavior only if Twitch exposes an idempotency key or authoritative query that closes the uncertainty window.
- **Dissent (if any):** None.
- **Driven by rounds:** R1.
- **Dependent decisions:** D-9, D-12.
- **Referenced in plan:** Session lifecycle and protocol; Work Units and Sequencing; Definition of Done; On-Call Resilience Posture.

### D-9: Layered executable release gates

- **Question:** What evidence is sufficient to call the browser/native integration stable?
- **Decision:** Combine pure JS/C++ state and protocol tests, native-host and IPC process fault tests, a controllable fake-peer Edge E2E path, and a scheduled or pre-release packaged Edge/native/Chatterino run with defined restart, multi-window, fallback, and geometry targets.
- **Rationale:** The existing HWND regression test inspects source text and cannot prove process, window, service-worker, or display behavior. Narrow deterministic levels provide diagnosis, while the real package path proves the integration users run.
- **Evidence:** `.github/workflows/extension-test.yml`; `chatterino-extension/tests/`; `tests/src/NativeMessaging*.cpp`; ARCH-14; RES-17; TE-02 through TE-11 and TE-14; JUN-08.
- **Rejected alternatives:**
  - Rely on source-text checks and unit tests — rejected because they cannot execute HWND ownership, native messaging, or MV3 suspension.
  - Put the full browser/OS/display Cartesian matrix on every PR — rejected because support policy and runner budget do not justify it.
  - Accept retry-green CI — rejected because retries would conceal lifecycle races rather than prove first-attempt convergence.
- **Specialist owner:** Test owner with Windows native/IPC specialist support.
- **Revisit criterion:** Expand the matrix when support policy or field failures identify a new risk cluster; reduce no stated acceptance target without operator approval.
- **Dissent (if any):** None.
- **Driven by rounds:** R1.
- **Dependent decisions:** D-10, D-11, D-12, D-13, D-14.
- **Referenced in plan:** Work Units and Sequencing; Definition of Done; Testing Strategy.

### D-10: Bounded Twitch capability adapters

- **Question:** How should official Twitch sources, private GraphQL, and DOM observation coexist safely?
- **Decision:** Normalize sources behind typed capability adapters that publish source, freshness, support, and failure status; prefer authorized official sources after a scope inventory, and keep private GraphQL or DOM fallbacks timed, circuit-broken, stale-expiring, and independently degradable.
- **Rationale:** Twitch-owned DOM and private operations are high-churn external surfaces. A normalized seam prevents schema drift from escaping into chat, while an official-only design would remove currently useful capabilities that may not be authorized or available.
- **Evidence:** `chatterino-extension/twitch-api.js`; `chatterino-extension/content.js`; ARCH-09; RES-14; TE-12; JUN-09.
- **Rejected alternatives:**
  - Depend solely on current DOM or private GraphQL shapes — rejected because they change without compatibility guarantees.
  - Build a complete EventSub/auth subsystem before inventory — rejected because event coverage, token ownership, and scopes are unverified.
  - Expose adapter tuning to users — rejected because bounded automatic defaults meet the current requirement.
- **Specialist owner:** Twitch API and security specialist.
- **Revisit criterion:** Revisit source priority whenever official coverage or OAuth ownership changes, or sanitized failure data shows the fallback order is wrong.
- **Dissent (if any):** None.
- **Driven by rounds:** R1.
- **Dependent decisions:** D-11, D-12, D-13.
- **Referenced in plan:** Twitch integration and product surface; Work Units and Sequencing; Definition of Done; Security Posture.

### D-11: Measure before performance tuning

- **Question:** How should observer, query, geometry, retry, and adapter overhead be improved without inventing budgets?
- **Decision:** Instrument representative scenarios first, establish reviewed budgets from the measured distributions, then scope observers, coalesce replaceable updates, disconnect inactive adapters, and retain a low-frequency safety poll.
- **Rationale:** No performance baseline or production corpus was available. Measurement prevents arbitrary cadence changes from harming recovery or missing Twitch updates.
- **Evidence:** `artifacts/.discovery-notes.md`; ARCH-12; TE-12; JUN-10.
- **Rejected alternatives:**
  - Set final budgets from intuition — rejected because there is no current distribution to justify them.
  - Remove polling entirely — rejected because a bounded safety path is still needed for missed DOM and lifecycle signals.
  - Add workers or a new frontend framework — rejected because no measured bottleneck justifies their complexity.
- **Specialist owner:** Performance specialist after instrumentation.
- **Revisit criterion:** Revisit the selected optimizations and budgets when representative measurements or Twitch architecture materially change.
- **Dissent (if any):** None.
- **Driven by rounds:** R1.
- **Dependent decisions:** D-12.
- **Referenced in plan:** Twitch integration and product surface; Work Units and Sequencing; Definition of Done.

### D-12: Thin session-scoped product slices

- **Question:** How should the requested activity rail and moderator cockpit proceed without supplied visual or interaction designs?
- **Decision:** Define behavior examples and a focused design checkpoint, then ship a read-only activity slice before minimum-scope authorized moderation actions; keep both session-scoped and independently degradable.
- **Rationale:** The capabilities are explicitly requested, but inventing a large layout or command set would turn missing product decisions into accidental requirements. Thin vertical slices validate the normalized model and session behavior first.
- **Evidence:** Operator request captured in `artifacts/scope-boundary.md`; JUN-11 reframing; TE-13; ARCH-09 and ARCH-15.
- **Rejected alternatives:**
  - Remove the product features because visuals are absent — rejected because they are within the operator's requested feature set.
  - Plan a complete moderator dashboard immediately — rejected because exact interactions, scopes, and failure behavior are not yet settled.
- **Specialist owner:** Product/UI owner with Twitch API and security specialist.
- **Revisit criterion:** Expand beyond thin slices only after behavior evidence, authorization coverage, and user validation identify the next capability.
- **Dissent (if any):** None.
- **Driven by rounds:** R1.
- **Dependent decisions:** D-13.
- **Referenced in plan:** Twitch integration and product surface; Work Units and Sequencing; Definition of Done.

### D-13: Read-only Lua event projection

- **Question:** What plugin extensibility should the evolved integration expose?
- **Decision:** After normalized models stabilize, publish a small additive versioned set of session-scoped read-only Lua events with exception isolation; do not expose raw browser transport or control authority by default.
- **Rationale:** Read-only events provide the requested extensibility while preserving transport, authorization, and recovery invariants. A broad SDK has no evidenced consumers or security model.
- **Evidence:** ARCH-11; JUN-12; TE-13.
- **Rejected alternatives:**
  - Expose native messaging directly to plugins — rejected because it bypasses session validation and enlarges the attack and compatibility surface.
  - Design a universal browser-plugin SDK — rejected because no concrete multi-plugin requirements exist.
- **Specialist owner:** Plugin API owner.
- **Revisit criterion:** Add a narrow command only when at least two concrete plugins need it and an authorization, result, and compatibility contract is defined.
- **Dissent (if any):** None.
- **Driven by rounds:** R1.
- **Dependent decisions:** None.
- **Referenced in plan:** Twitch integration and product surface; Work Units and Sequencing; Definition of Done.

### D-14: Bounded redacted local diagnostics

- **Question:** What diagnostic capability helps users and maintainers without creating a telemetry system or controlling recovery?
- **Decision:** Record a fixed-size local transition ring with monotonic time, component, shortened session identity, generation, event, and reason; expose current health and copyable redacted diagnostics, but keep diagnostics observational only.
- **Rationale:** Cross-process failures need a shared timeline, yet chat, rewards, credentials, and remote retention are unnecessary and privacy-sensitive. Keeping the log out of lifecycle control prevents diagnostic corruption from changing behavior.
- **Evidence:** `chatterino-extension/integration-health.js`; ARCH-13; RES-15; TE-13; JUN-12.
- **Rejected alternatives:**
  - Add remote telemetry or dashboards now — rejected because no consent, retention, hosting, or fleet requirement exists.
  - Use logs as the durable recovery source — rejected because diagnostics may be truncated and should not own state.
- **Specialist owner:** Browser integration lead with security review.
- **Revisit criterion:** Consider opt-in remote signals only after a documented fleet diagnosis gap, privacy policy, retention rule, and operator owner exist.
- **Dissent (if any):** None.
- **Driven by rounds:** R1.
- **Dependent decisions:** None.
- **Referenced in plan:** Twitch integration and product surface; Work Units and Sequencing; Definition of Done; Security Posture.
