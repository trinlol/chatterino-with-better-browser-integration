// Inject this into background.js to debug attachment flow
// Add after line 480 (in acknowledgeAttachedWindow function)

console.log('[DEBUG] Native acknowledgement received:', {
  winId: message.winId,
  browserWindowId: message.browserWindowId,
  attachRequestId: message.attachRequestId,
  requestId: message.requestId,
  sessionId: message.sessionId,
  generation: message.generation,
  pendingRequests: Array.from(pendingAttachRequests.keys())
});

const pending = pendingAttachRequests.get(String(message.winId ?? message.browserWindowId ?? ""));
if (!pending) {
  console.error('[DEBUG] No pending request found for this window!');
} else {
  console.log('[DEBUG] Pending request:', {
    requestId: pending.requestId,
    generation: pending.generation,
    sessionId: pending.sessionId
  });
  
  const responseRequestId = message.attachRequestId ?? message.requestId;
  if (pending.requestId !== responseRequestId) {
    console.error('[DEBUG] Request ID mismatch!', {
      expected: pending.requestId,
      received: responseRequestId
    });
  }
}
