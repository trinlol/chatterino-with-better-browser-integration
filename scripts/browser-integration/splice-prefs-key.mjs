// Byte-surgical removal of a top-level key inside extensions.settings
// from a Chrome JSON prefs file, preserving every other byte exactly.
// Usage: node splice-prefs-key.mjs <prefs-file> <extensions-settings-child-key> [...more keys]
import { readFileSync, writeFileSync, copyFileSync } from "node:fs";

const file = process.argv[2];
const keys = process.argv.slice(3);
if (!file || keys.length === 0) {
  console.error("usage: node splice-prefs-key.mjs <file> <key> [key...]");
  process.exit(2);
}

let text = readFileSync(file, "utf8");
copyFileSync(file, file + ".splice.bak");

for (const key of keys) {
  // Find "\"<key>\":" inside the extensions.settings object. We locate ALL
  // occurrences of the quoted key followed by ':' and pick the one whose
  // surrounding context is the extensions.settings dict (heuristic: it is the
  // LAST occurrence at depth where parent path includes "settings" — in
  // practice the extension id appears as a key in exactly one place).
  const needle = '"' + key + '":';
  let idx = -1;
  let count = 0;
  let pos = 0;
  const positions = [];
  while ((pos = text.indexOf(needle, pos)) !== -1) {
    positions.push(pos);
    pos += needle.length;
  }
  if (positions.length === 0) {
    console.error('KEY_NOT_FOUND: ' + key);
    process.exit(3);
  }
  if (positions.length > 1) {
    // Disambiguate: keep occurrences whose balanced value contains "location"
    // (i.e. the value is an extension settings entry object).
    const candidates = positions.filter((p) => {
      const probe = text.slice(p, p + 4000);
      return probe.includes('"location"');
    });
    if (candidates.length !== 1) {
      console.error('KEY_AMBIGUOUS: ' + key + ' occurs ' + positions.length + ' times, ' + candidates.length + ' with location');
      process.exit(4);
    }
    idx = candidates[0];
    console.log('DISAMBIGUATED to occurrence with "location" field');
  } else {
    idx = positions[0];
  }

  // Walk the value: skip whitespace, then balanced braces/brackets respecting strings.
  let i = idx + needle.length;
  while (i < text.length && /\s/.test(text[i])) i++;
  const open = text[i];
  const close = open === "{" ? "}" : open === "[" ? "]" : null;
  let end;
  if (close) {
    let depth = 0;
    let inStr = false;
    let esc = false;
    for (; i < text.length; i++) {
      const c = text[i];
      if (inStr) {
        if (esc) esc = false;
        else if (c === "\\") esc = true;
        else if (c === '"') inStr = false;
      } else {
        if (c === '"') inStr = true;
        else if (c === open) depth++;
        else if (c === close) {
          depth--;
          if (depth === 0) {
            end = i + 1;
            break;
          }
        }
      }
    }
    if (end === undefined) {
      console.error('UNBALANCED: ' + key);
      process.exit(5);
    }
  } else {
    // scalar value: up to comma or end
    end = i;
    while (end < text.length && text[end] !== "," && text[end] !== "}") end++;
  }

  // Also swallow a trailing comma (and preceding whitespace) OR a leading comma.
  let spliceStart = idx;
  let spliceEnd = end;
  // after value: skip ws; if comma -> include it
  let j = spliceEnd;
  while (j < text.length && /\s/.test(text[j])) j++;
  if (text[j] === ",") {
    spliceEnd = j + 1;
  } else {
    // no trailing comma: we must remove a LEADING comma instead
    let k = spliceStart - 1;
    while (k >= 0 && /\s/.test(text[k])) k--;
    if (text[k] === ",") {
      spliceStart = k;
    } else {
      console.error('NO_SEPARATOR: ' + key);
      process.exit(6);
    }
  }
  text = text.slice(0, spliceStart) + text.slice(spliceEnd);
  console.log('SPLICED: ' + key + ' (' + (spliceEnd - spliceStart) + ' bytes)');
}

writeFileSync(file, text, "utf8");
console.log("WROTE: " + file + " (new size " + Buffer.byteLength(text, "utf8") + ")");