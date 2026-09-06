# Investigation Results - Why Attachments Fail

## Executive Summary

Four parallel investigations have identified **multiple critical failure points** that can cause the chat overlay to stay stuck in "prepared" phase and never receive the "attached" acknowledgement:

1. **Silent message dropping** - IPC queue failures are ignored
2. **Silent rejection points** - Three places where attachments are rejected without sending error responses
3. **No delivery confirmation** - Only 1 out of 6+ IPC send operations checks if the message was delivered

## Critical Issue #1: Silent Message Dropping in IPC Queue

**Location:** `src/singletons/NativeMessaging.cpp:578-595`

### The Problem

The `sendToBrowserExtension()` function **completely ignores** the return value from `ipc::sendMessage()`:

```cpp
void sendToBrowserExtension(const QJsonObject &obj)
{
    auto *app = tryGetApp();
    if (!app)
    {
        return;  // Silent failure #1
    }
    
    const auto message = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    ipc::sendMessage(BROWSER_IPC_QUEUE_NAME, message);  // Return value IGNORED!
}
```

### When Messages Are Dropped

The IPC queue can silently drop messages in these scenarios:

| Scenario | Cause | Behavior |
|----------|-------|----------|
| **Queue Full** | 100 messages already in queue | `try_send()` returns false, message dropped |
| **Queue Unavailable** | Native host not running or queue doesn't exist | Open fails, message dropped |
| **Message Too Large** | Message exceeds 1024 bytes | Rejected immediately |

**All failures are only logged at debug level** - no warnings, no errors, no retries.

### Impact

**ALL desktop → browser messages use this function:**
- Desktop-ready announcements
- **Chat attachment confirmations** ← This is why attachments fail!
- Lease renewals
- Session status reports
- Reconciliation responses
- Native chat results

**Only 1 function in the entire codebase checks delivery status:** `sendNativeChat()` in SplitInput.cpp

## Critical Issue #2: Silent Rejection Points

**Location:** `src/singletons/NativeMessaging.cpp` (handleSelect function)

### Three Places That Silently Reject Attachments

#### Point A: Missing winId (lines 850-854)
```cpp
if (args.winId.isNull())
{
    qCDebug(chatterinoNativeMessage) << "winId in select is missing";
    return;  // ⚠️ NO RESPONSE SENT TO BROWSER
}
```

#### Point B: Startup Replay with Invalid browserHwnd (lines 888-892)
```cpp
if ((attach || attachFullscreen) && root["startupReplay"_L1].toBool() &&
    browserTarget == nullptr)
{
    return;  // ⚠️ NO RESPONSE SENT TO BROWSER
}
```

**This is particularly problematic** because:
- Happens during browser/extension restarts
- Occurs BEFORE the v2 protocol error reporting check
- Browser waits indefinitely for a response that never comes

#### Point C: Unknown Channel Type (lines 895-899)
```cpp
if (type != u"twitch"_s)
{
    qCDebug(chatterinoNativeMessage) << "NM unknown channel type";
    return;  // ⚠️ NO RESPONSE SENT TO BROWSER
}
```

### Proper Error Reporting (for comparison)

The v2 protocol DOES have proper error reporting in some cases:

```cpp
if (v2 && (browserTarget == nullptr ||
           !isSupportedOverlayTarget(HWND(browserTarget))))
{
    this->parent_.reportSession(session, u"attachment-rejected"_s,
                                u"invalid-browser-hwnd"_s, requestId);
    return;  // ✅ ERROR SENT BACK TO BROWSER
}
```

But this check only runs AFTER the three silent rejection points above.

## Critical Issue #3: browserHwnd Validation Can Fail Silently

**Location:** `src/singletons/NativeMessaging.cpp:125-161`

### Windows API Calls That Can Fail

The `isSupportedOverlayTarget()` function calls multiple Windows APIs that can fail silently:

```cpp
bool isSupportedOverlayTarget(HWND window)
{
    // Can fail if HWND is invalid
    if (window == nullptr || ::IsWindow(window) == 0)
        return false;
    
    // Can fail if process is inaccessible
    if (::GetWindowThreadProcessId(window, &processId) == 0)
        return false;
    
    // Can fail due to permissions
    const auto process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, ...);
    if (process == nullptr)
        return false;
    
    // Can fail if process handle is invalid
    const bool havePath = ::QueryFullProcessImageNameW(process, ...);
    if (!havePath)
        return false;
    
    // Check if executable is a supported browser
    return executable.compare(u"chrome.exe", Qt::CaseInsensitive) == 0 || ...;
}
```

Any of these failures will cause `browserTarget` to be `nullptr`, triggering the silent rejection points above.

## Diagnostic Logging Added

### BrowserExtension.cpp (Native Host Bridge)

✅ **Completed** - Logs added to track:
- Messages received from browser stdin
- Messages sent to IPC queue "chatterino_gui"
- Messages received from IPC queue "chatterino_browser"
- Messages sent back to browser stdout

### NativeMessaging.cpp (Desktop App)

✅ **Completed** - Logs added to track:
- Select message receipt with all key fields
- browserHwnd validation results
- Protocol version (v2 vs legacy)
- Session state changes
- reportSession() calls with complete payload
- IPC queue send operations

### IPC Queue Architecture

✅ **Investigated** - Key findings:
- Two queues: `chatterino_gui` (browser → desktop) and `chatterino_browser` (desktop → browser)
- Each queue: 100 message capacity, 1024 bytes per message
- Non-blocking `try_send()` - drops messages if queue is full
- No retry logic, no buffering, no error propagation

## Root Cause Analysis

### Why "chat-attached" Never Reaches the Browser

The acknowledgement flow has **multiple single points of failure**:

```
Desktop App
    ↓ reportSession(session, "chat-attached", ...)
    ↓
sendToBrowserExtension(json)
    ↓ Ignores return value!
    ↓
ipc::sendMessage("chatterino_browser", message)
    ↓ Returns DeliveryStatus (ignored)
    ↓ try_send() - fails if queue full
    ↓
IPC Queue "chatterino_browser"
    ↓ 100 message capacity
    ↓ Native host must read fast enough
    ↓
Native Host: runBrowserOutboundLoop()
    ↓ receiveFor(250ms timeout)
    ↓
sendToBrowser(stdout)
    ↓
Browser Extension: background.js
    ↓ chrome.runtime.onMessage
    ↓
Content Script: overlay.js
    ✗ Never receives state: "attached"
```

### Failure Modes

1. **Desktop never calls reportSession()** → Silent rejection due to validation failure
2. **IPC queue is full** → Message dropped, no retry
3. **Native host not reading fast enough** → Queue backs up, new messages dropped
4. **Native host crashed** → Queue unavailable, all messages dropped
5. **Message > 1024 bytes** → Rejected (unlikely for chat-attached but possible)

## Recommendations

### Immediate Fixes

1. **Check IPC delivery status** in `sendToBrowserExtension()`:
   ```cpp
   auto status = ipc::sendMessage(BROWSER_IPC_QUEUE_NAME, message);
   if (status != ipc::DeliveryStatus::Delivered) {
       qCWarning(chatterinoNativeMessage) 
           << "Failed to send to browser:" << static_cast<int>(status);
       // Consider retry logic or fallback
   }
   ```

2. **Fix silent rejection points** - Always send error response before returning:
   ```cpp
   if (args.winId.isNull()) {
       reportSession(session, u"attachment-rejected"_s, 
                     u"missing-winid"_s, requestId);
       return;
   }
   ```

3. **Add retry logic** for critical messages like attachment confirmations

4. **Surface errors** - Log failures at WARNING or ERROR level, not just DEBUG

### Long-term Improvements

1. **Queue monitoring** - Detect when queue is consistently full
2. **Message prioritization** - Critical messages should have priority
3. **Delivery confirmation** - Browser should acknowledge receipt
4. **Graceful degradation** - Handle queue unavailability without silent failure
5. **Timeout handling** - Browser should timeout and reveal chat if no acknowledgement after N seconds

## Testing Instructions

### Enable Debug Logging

The added logs use `qCDebug(chatterinoNativeMessage)`. To see them:

**Option 1: Environment Variable**
```bash
set QT_LOGGING_RULES="chatterino.nativemessage=true"
```

**Option 2: Logging Configuration File**
Create `qtlogging.ini` in Chatterino directory:
```ini
[Rules]
chatterino.nativemessage=true
```

### What to Look For

1. **Native Host Logs (stderr):**
   ```
   [BrowserExtension] runBrowserOutboundLoop: starting
   [BrowserExtension] runLoop: received message from browser stdin
   [BrowserExtension] runLoop: sending message to IPC queue 'chatterino_gui'
   [BrowserExtension] runBrowserOutboundLoop: received message from IPC queue 'chatterino_browser'
   [BrowserExtension] sendToBrowser: length=123 content={...}
   ```

2. **Desktop App Logs (Qt debug output):**
   ```
   handleSelect: received select message - v2Protocol=true winId=... sessionId=... requestId=...
   handleSelect: browserHwnd validation successful
   handleSelect: about to call reportSession with chat-attached
   reportSession: status=chat-attached sessionId=... generation=...
   sendToBrowserExtension: sending to IPC queue 'chatterino_browser' - message: {...}
   ```

3. **Browser Extension Logs:**
   ```
   [BACKGROUND-STARTUP] Background script loaded
   [ATTACH-DEBUG] Sending message to native host: {action: "select", ...}
   [ATTACH-DEBUG] Native sent chat-attached: {winId: ..., sessionId: ...}
   [OVERLAY-DEBUG] Message received: {action: "nativeAttachState", state: "attached"}
   ```

### Diagnosis Matrix

| Desktop Logs | Native Host Logs | Browser Logs | Diagnosis |
|--------------|------------------|--------------|-----------|
| ✅ handleSelect | ✅ runLoop received | ❌ No acknowledgement | IPC queue issue or native host send failure |
| ✅ reportSession | ❌ No outbound message | ❌ No acknowledgement | IPC queue full or unavailable |
| ❌ No handleSelect | ✅ runLoop sent to queue | ❌ No acknowledgement | Desktop not receiving from queue |
| ❌ No handleSelect | ❌ No runLoop | ❌ No acknowledgement | Native host not running or crashed |
| ✅ Validation failed | ❌ No response | ❌ Waiting forever | Silent rejection point triggered |

## Files Modified

- ✅ `src/BrowserExtension.cpp` - Added comprehensive logging
- ✅ `src/singletons/NativeMessaging.cpp` - Added comprehensive logging
- 📝 `src/singletons/NativeMessaging.cpp` - **Needs fix** for silent rejections
- 📝 `src/singletons/NativeMessaging.cpp` - **Needs fix** to check IPC delivery status
