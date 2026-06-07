// Listen for messages from content.js and forward them to Chatterino via native messaging.
chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (message.action === 'prediction' || message.action === 'pin') {
    chrome.runtime.sendNativeMessage('com.chatterino.chatterino', message, (response) => {
      if (chrome.runtime.lastError) {
        console.warn('[Chatterino Companion] Native messaging error:', chrome.runtime.lastError.message);
      } else {
        console.log('[Chatterino Companion] Native message sent successfully:', response);
      }
    });
  }
});
