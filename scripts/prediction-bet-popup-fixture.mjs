// Offline, deterministic reproduction + verification harness for:
//
//   "On my plugin, when I click on the prediction / channel point menu, it
//    doesn't let me open it. It prints the prediction to the chatterino
//    instead of letting me put a prediction using my points."
//
// The "plugin" is the companion browser extension. When a prediction is live,
// the toolbar prediction replica forwards a click to the hidden native
// outcome button. Twitch opens its "How many points do you want to use?" bet
// popup anchored to that off-screen source control
// (`.chatterino-native-voting-source` sits at left: -10000px), so the popup
// renders off-screen/collapsed and the user only sees the engagement system
// message in Chatterino (the normal "prints the prediction" sync).
//
// This harness loads the REAL content scripts (protocol.js, activity-state.js,
// voting-ui.js, content.js) and the REAL stylesheet against a fixture Twitch
// DOM, then asserts the bet popup becomes visible, on-screen, and clickable
// after clicking the toolbar prediction replica.
//
// Run:  node scripts/prediction-bet-popup-fixture.mjs
// Exit: 0 = pass, 2 = bug still reproduces, 1 = harness error
import { chromium } from "playwright";
import path from "node:path";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const ext = path.join(root, "chatterino-extension");

const CHROME_STUB = `
window.__ccMessages = [];
const _chrome = {
  runtime: {
    id: "cc-fixture-extension",
    sendMessage: (msg) => { window.__ccMessages.push(msg); return Promise.resolve(); },
    onMessage: { addListener: () => {} },
  },
  storage: {
    local: { get: (defaults, cb) => cb(defaults), set: () => {} },
    onChanged: { addListener: () => {} },
  },
};
Object.defineProperty(window, "chrome", { value: _chrome, configurable: true });
`;

const FIXTURE_HTML = `<!doctype html>
<html lang="en" class="chatterino-companion-active">
<head>
  <meta charset="utf-8">
  <title>Twitch — fixture</title>
  <style>
    body { margin: 0; background: #0e0e10; color: #efeff1; font-family: sans-serif; }
    .chat-shell { position: absolute; right: 0; top: 0; width: 340px; height: 100%; background: #18181b; z-index: 1; }
    .chat-room { position: absolute; right: 0; top: 0; width: 340px; height: 100%; }
    .chat-room__content { padding: 12px; }
    .prediction-banner { background: #063f31; border: 1px solid #00f593; border-radius: 6px; padding: 10px; margin-bottom: 8px; }
    .prediction-banner h4 { margin: 0 0 6px; font-size: 13px; }
    .prediction-banner button { display: block; width: 100%; margin: 4px 0; padding: 6px; background: #1f1f23; border: 1px solid #4b367c; color: #efeff1; border-radius: 4px; cursor: pointer; }
    .channel-header { position: absolute; top: 60px; left: 50%; transform: translateX(-50%); display: flex; gap: 8px; align-items: center; padding: 8px 12px; background: #18181b; border: 1px solid #303036; border-radius: 6px; z-index: 2; }
    .ReactModal__Overlay { position: fixed; inset: 0; background: rgba(0,0,0,0.5); z-index: 5000; }
    .ReactModal__Content { position: fixed; background: #1f1f23; color: #efeff1; border-radius: 6px; padding: 12px; box-shadow: 0 4px 16px rgba(0,0,0,0.5); }
    .bet-chip { display: inline-block; margin: 4px; padding: 6px 10px; background: #303036; border: 1px solid #4b367c; color: #efeff1; border-radius: 4px; cursor: pointer; }
  </style>
</head>
<body>
  <div class="chat-shell"><div>Chatterino should show here. Make sure Chatterino is running and the browser extension is enabled.</div></div>
  <div class="stream-chat">
    <section class="chat-room">
      <div class="chat-room__content">
        <div data-test-selector="community-prediction-banner" class="prediction-banner">
          <h4>Will the streamer win?</h4>
          <button data-test-selector="prediction-outcome" aria-label="Team A">Team A <span>1,234 pts</span></button>
          <button data-test-selector="prediction-outcome" aria-label="Team B">Team B <span>567 pts</span></button>
          <span>2:00 left</span>
        </div>
      </div>
    </section>
  </div>
  <div class="channel-header">
    <button data-a-target="follow-button">Follow</button>
    <div data-test-selector="community-points-summary" class="community-points-summary">
      <button aria-label="Channel Points">10,000</button>
    </div>
  </div>
  <script>
    // Mock of Twitch's React behavior: clicking a NATIVE prediction outcome
    // (not the toolbar replica) opens the "How many points..." bet popup
    // anchored to that button's on-screen position. Because the extension
    // parks the native banner at left:-10000px, this lands the popup
    // off-screen — the exact failure the user described.
    window.__ccMock = { betOpened: 0, betClickCount: 0, lastBetPoints: null };
    function openBetMenu(sourceBtn) {
      const old = document.getElementById("mock-bet-menu");
      if (old) old.remove();
      const r = sourceBtn.getBoundingClientRect();
      const menu = document.createElement("div");
      menu.className = "ReactModal__Content";
      menu.id = "mock-bet-menu";
      menu.setAttribute("role", "dialog");
      menu.setAttribute("aria-label", "Prediction");
      menu.style.cssText =
        "position:fixed;left:" + r.left + "px;top:" + (r.top + r.height + 8) +
        "px;width:340px;z-index:9999;";
      menu.innerHTML =
        '<h3 style="margin:0 0 8px;font-size:13px;">Prediction — How many points do you want to use?</h3>' +
        "<div>" +
        '<button class="bet-chip" data-test-selector="prediction-bet-chip" data-points="1000">1,000</button>' +
        '<button class="bet-chip" data-test-selector="prediction-bet-chip" data-points="5000">5,000</button>' +
        '<button class="bet-chip" data-test-selector="prediction-bet-chip" data-points="10000">10,000</button>' +
        "</div>" +
        '<button class="bet-confirm" data-test-selector="prediction-bet-confirm" style="margin-top:8px;padding:6px 12px;background:#9146ff;border:none;border-radius:4px;color:#fff;cursor:pointer;">Place bet</button>';
      menu.addEventListener("click", (e) => {
        const chip = e.target.closest(".bet-chip");
        if (chip) {
          window.__ccMock.betClickCount += 1;
          window.__ccMock.lastBetPoints = chip.getAttribute("data-points");
        }
      });
      const overlay = document.createElement("div");
      overlay.className = "ReactModal__Overlay";
      overlay.appendChild(menu);
      document.body.appendChild(overlay);
      window.__ccMock.betOpened += 1;
    }
    document.addEventListener(
      "click",
      (e) => {
        if (e.target.closest("#chatterino-toolbar-portal")) return;
        const btn = e.target.closest('[data-test-selector="prediction-outcome"]');
        if (btn) { openBetMenu(btn); return; }
        // While a prediction accepts entries, Twitch answers the points button
        // with the same bet prompt instead of the reward centre.
        const points = e.target.closest('[aria-label="Channel Points"]');
        if (points) openBetMenu(points);
      },
      true
    );
  </script>
</body>
</html>`;

const [protocolJs, activityJs, votingJs, contentJs, stylesCss] =
  await Promise.all([
    readFile(path.join(ext, "protocol.js"), "utf8"),
    readFile(path.join(ext, "activity-state.js"), "utf8"),
    readFile(path.join(ext, "voting-ui.js"), "utf8"),
    readFile(path.join(ext, "content.js"), "utf8"),
    readFile(path.join(ext, "styles.css"), "utf8"),
  ]);

const browser = await chromium.launch({ headless: true });
const page = await browser.newPage({ viewport: { width: 1600, height: 900 } });
await page.addInitScript(CHROME_STUB);
await page.route("**/*", async (route) => {
  const req = route.request();
  if (req.resourceType() === "document") {
    await route.fulfill({
      status: 200,
      contentType: "text/html",
      body: FIXTURE_HTML,
    });
  } else {
    await route.abort();
  }
});

let exitCode = 1;
let failureDetail = "";
try {
  await page.goto("https://www.twitch.tv/moonmoon", {
    waitUntil: "domcontentloaded",
    timeout: 30000,
  });
  await page.addScriptTag({ content: protocolJs });
  await page.addScriptTag({ content: activityJs });
  await page.addScriptTag({ content: votingJs });
  await page.addStyleTag({ content: stylesCss });
  await page.addScriptTag({ content: contentJs });

  await page.waitForSelector("#chatterino-prediction-replica", {
    timeout: 15000,
  });
  console.log("[fixture] prediction replica mounted in toolbar");

  // Sanity: the companion sync must have emitted the prediction engagement
  // message ("prints the prediction to Chatterino") — this is expected.
  const messages = await page.evaluate(() => window.__ccMessages || []);
  const engagement = messages.filter(
    (m) =>
      m?.action === "engagement" &&
      m?.kind === "prediction" &&
      m?.lifecycle === "upsert"
  );
  if (engagement.length === 0) {
    throw new Error(
      "no prediction engagement sync message reached the native host stub"
    );
  }
  console.log("[fixture] prediction engagement sync message emitted (expected)");

  // Click Team A on the toolbar prediction replica — this forwards to the
  // hidden native outcome button and should surface the bet popup.
  await page.evaluate(() => {
    const replica = document.getElementById("chatterino-prediction-replica");
    const teamA = replica.querySelector(
      '[data-test-selector="prediction-outcome"]'
    );
    if (!teamA) throw new Error("replica has no outcome button");
    teamA.click();
  });

  // The fix must make the bet popup visible, fully on-screen, and owned by the
  // extension (class applied), then chips inside it must stay clickable.
  await page.waitForFunction(
    () => {
      const menu = document.getElementById("mock-bet-menu");
      if (!menu) return false;
      const r = menu.getBoundingClientRect();
      return (
        r.width > 0 &&
        r.height > 0 &&
        r.left >= 0 &&
        r.right <= window.innerWidth &&
        r.top >= 0 &&
        r.bottom <= window.innerHeight &&
        getComputedStyle(menu).visibility !== "hidden" &&
        menu.classList.contains("chatterino-native-prediction-dialog")
      );
    },
    null,
    { timeout: 8000 }
  );

  const chip = await page.evaluate(() => {
    const menu = document.getElementById("mock-bet-menu");
    const c = menu.querySelector(".bet-chip");
    c.click();
    return {
      count: window.__ccMock.betClickCount,
      last: window.__ccMock.lastBetPoints,
    };
  });
  if (chip.count !== 1) {
    throw new Error("bet chip did not register a click after repositioning");
  }

  const rect = await page.evaluate(() => {
    const r = document.getElementById("mock-bet-menu").getBoundingClientRect();
    return {
      left: Math.round(r.left),
      top: Math.round(r.top),
      width: Math.round(r.width),
      height: Math.round(r.height),
    };
  });
  console.log(
    "[fixture] bet popup visible & on-screen at",
    JSON.stringify(rect)
  );
  console.log("[fixture] bet chip clickable:", JSON.stringify(chip));

  // Second entry point the user named: the channel-points button. While a
  // prediction is accepting entries Twitch answers it with the same bet prompt
  // instead of the reward centre, so that path must reposition it too.
  await page.evaluate(() => {
    document.getElementById("mock-bet-menu")?.closest(".ReactModal__Overlay")
      ?.remove();
    window.__ccMock.betOpened = 0;
    document.getElementById("chatterino-points-replica")?.click();
  });

  await page.waitForFunction(
    () => {
      const menu = document.getElementById("mock-bet-menu");
      if (!menu) return false;
      const r = menu.getBoundingClientRect();
      return (
        r.width > 0 &&
        r.left >= 0 &&
        r.right <= window.innerWidth &&
        menu.classList.contains("chatterino-native-prediction-dialog")
      );
    },
    null,
    { timeout: 8000 }
  );
  console.log(
    "[fixture] channel-points button also surfaces the bet popup on-screen"
  );
  exitCode = 0;
} catch (error) {
  exitCode = 2;
  failureDetail = error.message;
  const state = await page
    .evaluate(() => {
      const menu = document.getElementById("mock-bet-menu");
      const replica = document.getElementById("chatterino-prediction-replica");
      return {
        hasReplica: !!replica,
        hasBetMenu: !!menu,
        betOpened: window.__ccMock?.betOpened || 0,
        menuRect: menu
          ? (() => {
              const r = menu.getBoundingClientRect();
              return {
                left: Math.round(r.left),
                top: Math.round(r.top),
                width: Math.round(r.width),
                height: Math.round(r.height),
              };
            })()
          : null,
        menuClass: menu?.className || null,
        portalLeft:
          document.getElementById("chatterino-toolbar-portal")?.style?.left ||
          null,
      };
    })
    .catch(() => null);
  console.error("[fixture] FAIL:", failureDetail);
  if (state) console.error("[fixture] state:", JSON.stringify(state, null, 2));
} finally {
  await browser.close();
}

if (exitCode === 0) {
  console.log(
    "\n[fixture] PASS — bet popup visible, on-screen, and clickable"
  );
} else {
  console.log("\n[fixture] BUG REPRODUCES — bet popup is not usable without fix");
}
process.exit(exitCode);
