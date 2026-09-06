# Debug Instructions for Attachment Issue

## Phase 1: Add Logging to Background Script

1. Open `chatterino-extension/background.js`
2. Find line 582 (`case "chat-attached":`)
3. Add logging BEFORE line 583:

```javascript
case "chat-attached":
  console.log('[ATTACH-DEBUG] Native sent chat-attached:', {
    winId: msg.winId,
    browserWindowId: msg.browserWindowId,
    attachRequestId: msg.attachRequestId,
    requestId: msg.requestId,
    sessionId: msg.sessionId,
    generation: msg.generation,
    leaseExpiresAt: msg.leaseExpiresAt
  });
  acknowledgeAttachedWindow(msg);
  break;
```

4. Find the `acknowledgeAttachedWindow` function (line 480)
5. Add logging at the START of the function (after line 480):

```javascript
function acknowledgeAttachedWindow(message) {
  console.log('[ATTACH-DEBUG] acknowledgeAttachedWindow called');
  const key = String(message.winId ?? message.browserWindowId ?? "");
  console.log('[ATTACH-DEBUG] Window key:', key);
  const pending = pendingAttachRequests.get(key);
  console.log('[ATTACH-DEBUG] Pending request:', pending);
  console.log('[ATTACH-DEBUG] All pending keys:', Array.from(pendingAttachRequests.keys()));
  
  const responseRequestId = message.attachRequestId ?? message.requestId;
  console.log('[ATTACH-DEBUG] Response request ID:', responseRequestId);
  console.log('[ATTACH-DEBUG] Expected request ID:', pending?.requestId);
  
  if (!pending || pending.requestId !== responseRequestId) {
    console.error('[ATTACH-DEBUG] ACKNOWLEDGEMENT REJECTED!', {
      reason: !pending ? 'no pending request' : 'request ID mismatch',
      expected: pending?.requestId,
      received: responseRequestId
    });
    return;
  }
  
  console.log('[ATTACH-DEBUG] Acknowledgement accepted, proceeding...');
  // ... rest of function
```

6. Find line 506 (`await sendOverlayState(session, "attached");`)
7. Add logging BEFORE it:

```javascript
console.log('[ATTACH-DEBUG] About to send overlay state "attached" to tab:', session.tabId);
await sendOverlayState(session, "attached");
console.log('[ATTACH-DEBUG] Sent overlay state "attached"');
```

## Phase 2: Add Logging to Overlay Script

1. Open `chatterino-extension/overlay.js`
2. Find the message listener (line 454, `chrome.runtime.onMessage.addListener`)
3. Add logging at the START of the listener:

```javascript
chrome.runtime.onMessage.addListener((message) => {
  console.log('[OVERLAY-DEBUG] Message received:', message);
  
  if (message?.action === "requestChatRect") {
    console.log('[OVERLAY-DEBUG] Preparing attachment (requestChatRect)');
    prepareAttachment(message);
    // ... rest
  }
  
  if (message?.action === "nativeAttachState") {
    console.log('[OVERLAY-DEBUG] Native attach state:', message.state);
    console.log('[OVERLAY-DEBUG] Current attachment phase:', attachment.phase);
    console.log('[OVERLAY-DEBUG] Session IDs match?', message.sessionId, '===', attachment.sessionId);
    console.log('[OVERLAY-DEBUG] Generations match?', message.generation, '===', attachment.generation);
    
    if (message.state === "prepare") {
      console.log('[OVERLAY-DEBUG] Calling prepareAttachment');
      prepareAttachment(message);
    } else if (message.state === "attached") {
      console.log('[OVERLAY-DEBUG] Calling commitHide');
      const sameResult = sameAttachment(message);
      console.log('[OVERLAY-DEBUG] sameAttachment returned:', sameResult);
      if (!sameResult) {
        console.error('[OVERLAY-DEBUG] commitHide will be SKIPPED - sameAttachment check failed!');
      }
      commitHide(message);
    } else if (
      // ... rest
```

## Phase 3: Test and Collect Logs

1. Reload the extension in Chrome/Edge
2. Open DevTools on the background page:
   - Chrome: `chrome://extensions` → "Service Worker" link
   - Edge: `edge://extensions` → "Service Worker" link
3. Open a Twitch channel page
4. Open DevTools on the Twitch page (F12)
5. Try to attach by resizing the browser or navigating
6. Look for `[ATTACH-DEBUG]` and `[OVERLAY-DEBUG]` messages in BOTH consoles
7. Screenshot or copy the log output

## What to Look For

### If you see in background console:
- `Native sent chat-attached` → Native host IS responding
- `ACKNOWLEDGEMENT REJECTED` → This is the problem! Check the reason
- `no pending request` → The windowId doesn't match
- `request ID mismatch` → The requestId doesn't match

### If you see in content script console:
- `Message received: {action: "nativeAttachState", state: "attached"}` → Background IS sending the message
- `commitHide will be SKIPPED` → The overlay rejected it due to phase/sessionId/generation mismatch

### If you DON'T see:
- No `Native sent chat-attached` → Native host isn't sending acknowledgements
- No `Message received` in content console → Messages aren't reaching the page
