// Devtools snippet: attachment-flow diagnostics. Nothing in the extension
// loads this file and it is not listed in manifest.json.
//
// Usage: open the service worker's devtools (chrome://extensions -> "Service
// worker" -> Console), paste this file, then call it with a native
// acknowledgement message and the background's pending-request map, e.g. from
// a breakpoint in acknowledgeAttachedWindow():
//
//   chatterinoDebugAttachment(message, pendingAttachRequests);
//
// It only logs. Loading it defines one function and never throws, even when
// the arguments are missing.

// eslint-disable-next-line no-unused-vars
function chatterinoDebugAttachment(message, pendingRequests) {
  const msg = message && typeof message === "object" ? message : {};
  const pendingMap =
    pendingRequests && typeof pendingRequests.get === "function"
      ? pendingRequests
      : null;

  console.log("[DEBUG] Native acknowledgement received:", {
    winId: msg.winId,
    browserWindowId: msg.browserWindowId,
    attachRequestId: msg.attachRequestId,
    requestId: msg.requestId,
    sessionId: msg.sessionId,
    generation: msg.generation,
    pendingRequests: pendingMap ? Array.from(pendingMap.keys()) : [],
  });

  if (!pendingMap) {
    console.warn("[DEBUG] Pass the background's pendingAttachRequests map.");
    return;
  }

  const pending = pendingMap.get(
    String(msg.winId ?? msg.browserWindowId ?? "")
  );
  if (!pending) {
    console.error("[DEBUG] No pending request found for this window!");
    return;
  }
  console.log("[DEBUG] Pending request:", {
    requestId: pending.requestId,
    generation: pending.generation,
    sessionId: pending.sessionId,
  });

  const responseRequestId = msg.attachRequestId ?? msg.requestId;
  if (pending.requestId !== responseRequestId) {
    console.error("[DEBUG] Request ID mismatch!", {
      expected: pending.requestId,
      received: responseRequestId,
    });
  }
}
