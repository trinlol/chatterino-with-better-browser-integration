# Debugging the Attachment Issue - Testing Guide

## What Was Done

I've added comprehensive debug logging to track the complete attachment flow from native host → background script → content script → overlay activation.

**Files Modified:**
- `chatterino-extension/background.js` - Added logging to track native acknowledgements and request matching
- `chatterino-extension/overlay.js` - Added logging to track overlay state transitions

## How to Test

### Step 1: Reload the Extension

1. Open Chrome/Edge and go to:
   - Chrome: `chrome://extensions`
   - Edge: `edge://extensions`
2. Find "Chatterino Better Browser" extension
3. Click the **Reload** button (circular arrow icon)

### Step 2: Open DevTools on Background Page

1. On the extensions page, find "Chatterino Better Browser"
2. Click the **Service Worker** link (or "background page" in older browsers)
3. A DevTools window will open for the background script
4. Click the **Console** tab
5. Clear the console (click the 🚫 icon or press Ctrl+L)

### Step 3: Open DevTools on Twitch Page

1. Navigate to any Twitch channel (e.g., https://www.twitch.tv/directory)
2. Press **F12** to open DevTools
3. Click the **Console** tab
4. Clear the console

### Step 4: Attempt Attachment

Try one of these methods to trigger attachment:
- Resize the browser window
- Navigate to a different Twitch channel
- Click in the chat area
- Refresh the page

### Step 5: Collect Debug Logs

Look for messages prefixed with `[ATTACH-DEBUG]` and `[OVERLAY-DEBUG]` in BOTH consoles.

---

## What to Look For

### ✅ SUCCESS PATH (Everything Working)

**Background Console:**
```
[ATTACH-DEBUG] Native sent chat-attached: {winId: "...", sessionId: "...", ...}
[ATTACH-DEBUG] acknowledgeAttachedWindow called
[ATTACH-DEBUG] Window key: 12345
[ATTACH-DEBUG] Pending request: {requestId: "abc123", ...}
[ATTACH-DEBUG] Response request ID: abc123
[ATTACH-DEBUG] Expected request ID: abc123
[ATTACH-DEBUG] Acknowledgement accepted, proceeding...
[ATTACH-DEBUG] About to send overlay state "attached" to tab: 98765
[ATTACH-DEBUG] Sent overlay state "attached"
```

**Content Console (Twitch page):**
```
[OVERLAY-DEBUG] prepareAttachment called with: {sessionId: "...", generation: 1}
[OVERLAY-DEBUG] Attachment state after prepare: {phase: "prepared", sessionId: "...", generation: 1}
[OVERLAY-DEBUG] Message received: {action: "nativeAttachState", state: "attached", ...}
[OVERLAY-DEBUG] Native attach state: attached
[OVERLAY-DEBUG] Current attachment phase: prepared
[OVERLAY-DEBUG] Calling commitHide
[OVERLAY-DEBUG] sameAttachment returned: true
[OVERLAY-DEBUG] commitHide called with message: {...}
[OVERLAY-DEBUG] sameAttachment check passed, proceeding with commit
[OVERLAY-DEBUG] Phase set to "attached", dispatching chatterino-companion-active event
[OVERLAY-DEBUG] Event dispatched successfully
```

---

### ❌ FAILURE SCENARIOS

#### Problem 1: No Native Acknowledgement Received

**Background Console:**
```
(nothing with [ATTACH-DEBUG])
```

**Diagnosis:** The native host isn't sending `"chat-attached"` messages back.

**Possible Causes:**
- Native host process isn't running
- Native host can't communicate with the extension (extension ID mismatch)
- Native host is rejecting the attachment request silently

**Next Steps:**
- Check if "Chatterino Better Browser.exe" is running in Task Manager
- Check the native host registration (see below)

---

#### Problem 2: Acknowledgement Rejected (No Pending Request)

**Background Console:**
```
[ATTACH-DEBUG] Native sent chat-attached: {winId: "67890", ...}
[ATTACH-DEBUG] acknowledgeAttachedWindow called
[ATTACH-DEBUG] Window key: 67890
[ATTACH-DEBUG] Pending request: undefined
[ATTACH-DEBUG] All pending keys: ["12345"]
[ATTACH-DEBUG] ACKNOWLEDGEMENT REJECTED! {reason: "no pending request", ...}
```

**Diagnosis:** The `winId` or `browserWindowId` from the native host doesn't match any pending request.

**Possible Causes:**
- Native host is sending back a different window ID than it received
- The pending request expired or was cleared before the acknowledgement arrived
- Multiple browser windows are conflicting

**Next Steps:** Compare the window IDs in the logs. The key should match one of the pending keys.

---

#### Problem 3: Acknowledgement Rejected (Request ID Mismatch)

**Background Console:**
```
[ATTACH-DEBUG] Native sent chat-attached: {attachRequestId: "xyz789", ...}
[ATTACH-DEBUG] acknowledgeAttachedWindow called
[ATTACH-DEBUG] Window key: 12345
[ATTACH-DEBUG] Pending request: {requestId: "abc123", ...}
[ATTACH-DEBUG] Response request ID: xyz789
[ATTACH-DEBUG] Expected request ID: abc123
[ATTACH-DEBUG] ACKNOWLEDGEMENT REJECTED! {reason: "request ID mismatch", expected: "abc123", received: "xyz789"}
```

**Diagnosis:** The `attachRequestId` from the native host doesn't match the expected request ID.

**Possible Causes:**
- Native host is not echoing back the correct `attachRequestId`
- Native host is responding to an old/stale request
- Race condition with multiple rapid requests

**Next Steps:** This is likely a bug in the C++ native messaging code. The native host must send back the exact `attachRequestId` it received.

---

#### Problem 4: Overlay Rejects Attachment (Phase/Session Mismatch)

**Background Console:**
```
[ATTACH-DEBUG] About to send overlay state "attached" to tab: 98765
[ATTACH-DEBUG] Sent overlay state "attached"
```

**Content Console:**
```
[OVERLAY-DEBUG] Message received: {action: "nativeAttachState", state: "attached", sessionId: "sess2", generation: 2}
[OVERLAY-DEBUG] Native attach state: attached
[OVERLAY-DEBUG] Current attachment phase: prepared
[OVERLAY-DEBUG] Message sessionId: sess2 Attachment sessionId: sess1
[OVERLAY-DEBUG] Message generation: 2 Attachment generation: 1
[OVERLAY-DEBUG] Calling commitHide
[OVERLAY-DEBUG] sameAttachment returned: false
[OVERLAY-DEBUG] commitHide will be SKIPPED - sameAttachment check failed!
[OVERLAY-DEBUG] Failure reason: {phase: "prepared", sessionIdMatch: false, generationMatch: false}
[OVERLAY-DEBUG] commitHide called with message: {...}
[OVERLAY-DEBUG] commitHide ABORTED - sameAttachment returned false
```

**Diagnosis:** The overlay received the "attached" message but rejected it because the session ID or generation doesn't match what it prepared.

**Possible Causes:**
- Multiple rapid attachment attempts created conflicting sessions
- The "prepare" and "attached" messages had different session metadata
- Race condition where a new prepare happened between the old prepare and its acknowledgement

**Next Steps:** This indicates the background script sent inconsistent session data. Check that the same session is used throughout the flow.

---

#### Problem 5: Message Never Reaches Content Script

**Background Console:**
```
[ATTACH-DEBUG] About to send overlay state "attached" to tab: 98765
[ATTACH-DEBUG] Sent overlay state "attached"
```

**Content Console:**
```
(no [OVERLAY-DEBUG] messages at all)
```

**Diagnosis:** The background script sent the message but the content script never received it.

**Possible Causes:**
- Tab was closed or navigated away
- Content script failed to load
- Chrome's message passing failed

**Next Steps:**
- Verify the content script is injected (check the "Sources" tab in DevTools, look for `overlay.js`)
- Try refreshing the Twitch page

---

## Checking Native Host Registration

If you see "No native acknowledgement received", check the native host setup:

### 1. Check Registry Keys

Open PowerShell and run:
```powershell
Get-ItemProperty -Path "HKCU:\Software\Google\Chrome\NativeMessagingHosts\com.chatterino.chatterino"
Get-ItemProperty -Path "HKCU:\Software\Microsoft\Edge\NativeMessagingHosts\com.chatterino.chatterino"
```

Should show the path to a manifest JSON file.

### 2. Check Manifest File

The manifest file (shown in step 1) should contain:
```json
{
  "allowed_origins": [
    "chrome-extension://glknmaideaikkmemifbfkhnomoknepka/",
    "chrome-extension://bogfpdfoagkaebimmlcbgmfmanhbhhlm/",
    ...
  ],
  "name": "com.chatterino.chatterino",
  "path": "C:/path/to/Chatterino Better Browser.exe",
  "type": "stdio"
}
```

### 3. Check Extension ID

1. Go to `chrome://extensions` or `edge://extensions`
2. Find "Chatterino Better Browser"
3. Copy the **ID** (long string like `abcdefghijklmnopqrstuvwxyz123456`)
4. Check if this ID is in the manifest's `allowed_origins` array

**If not:** You need to add your extension ID to Chatterino's settings:
- Open Chatterino Better Browser app
- Go to Settings → General
- Find "Extra extension IDs"
- Add your extension ID
- Restart Chatterino

---

## Next Steps After Collecting Logs

Once you have the logs, they will show EXACTLY where the flow breaks:

1. **Copy the logs** from both consoles
2. Share them with me or analyze using the scenarios above
3. I can then provide a targeted fix for the specific failure point

The logs will reveal:
- Whether the native host is responding at all
- Whether window IDs or request IDs are mismatched
- Whether session metadata is inconsistent
- Whether the overlay is receiving and processing messages

This systematic approach will pinpoint the root cause within minutes instead of guessing.
