"""Community invitation contracts; no browser, network or site build required."""

import json
from html.parser import HTMLParser
from pathlib import Path
import shutil
import subprocess
import unittest

from community import DISCORD_URL, YOUTUBE_URL, community_invite


class Elements(HTMLParser):
    def __init__(self):
        super().__init__()
        self.elements = []

    def handle_starttag(self, tag, attrs):
        self.elements.append((tag, dict(attrs)))


class CommunityMarkupTests(unittest.TestCase):
    def test_both_languages_have_accessible_optional_dialog_and_direct_links(self):
        for language, expected_lang, voluntary in [
            ("pt", "pt-BR", "A participação é voluntária."),
            ("en", "en", "Taking part is optional."),
        ]:
            with self.subTest(language=language):
                markup = community_invite(language)
                parser = Elements()
                parser.feed(markup)
                dialogs = [attrs for tag, attrs in parser.elements if tag == "dialog"]
                self.assertEqual(len(dialogs), 1)
                self.assertEqual(dialogs[0]["lang"], expected_lang)
                self.assertEqual(dialogs[0]["aria-labelledby"], "community-invite-title")
                self.assertEqual(dialogs[0]["aria-describedby"], "community-invite-text")
                self.assertNotIn("open", dialogs[0])
                close = [attrs for tag, attrs in parser.elements if tag == "button" and "autofocus" in attrs]
                self.assertEqual(len(close), 1)
                self.assertIn("data-community-close", close[0])
                self.assertTrue(close[0]["aria-label"])
                links = [attrs for tag, attrs in parser.elements if tag == "a"]
                self.assertEqual({link["href"] for link in links}, {DISCORD_URL, YOUTUBE_URL})
                self.assertTrue(all("noopener" in link["rel"] for link in links))
                self.assertFalse(any(tag in {"form", "input", "iframe", "audio", "video"} for tag, _ in parser.elements))
                self.assertIn(voluntary, markup)

    @unittest.skipUnless(shutil.which("node"), "Node.js is needed for the isolated interaction contracts")
    def test_storage_timing_focus_and_optional_actions(self):
        script = Path(__file__).with_name("public") / "community.js"
        result = subprocess.run(
            [shutil.which("node"), "-e", _INTERACTION_CONTRACTS, str(script)],
            capture_output=True, text=True, timeout=20, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout)["scenarios"], 12)


_INTERACTION_CONTRACTS = r'''
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync(process.argv[1], 'utf8');
const seenKey = 'd3d.community.seen.v1';
const untilKey = 'd3d.community.dismissed-until.v1';
const month = 30 * 24 * 60 * 60 * 1000;
const epoch = 1800000000000;

function storage(blocked = false) {
  const values = new Map();
  return {
    values,
    getItem(key) { if (blocked) throw Error('blocked'); return values.get(key) ?? null; },
    setItem(key, value) { if (blocked) throw Error('blocked'); values.set(key, value); },
    removeItem(key) { if (blocked) throw Error('blocked'); values.delete(key); }
  };
}
class Target {
  constructor() { this.listeners = new Map(); this.attributes = new Map(); }
  setAttribute(name, value) { this.attributes.set(name, value); }
  removeAttribute(name) { this.attributes.delete(name); }
  addEventListener(name, callback) {
    const list = this.listeners.get(name) || [];
    list.push(callback); this.listeners.set(name, list);
  }
  removeEventListener(name, callback) {
    this.listeners.set(name, (this.listeners.get(name) || []).filter(item => item !== callback));
  }
  emit(name, supplied = {}) {
    const event = { preventDefault() { this.defaultPrevented = true; }, ...supplied };
    for (const callback of [...(this.listeners.get(name) || [])]) callback(event);
    return event;
  }
}
function page({session = storage(), local = storage(), now = epoch, hidden = false} = {}) {
  let clock = now;
  let sequence = 0;
  const timers = new Map();
  const document = new Target();
  const window = new Target();
  const launcher = new Target();
  launcher.hidden = true;
  launcher.isConnected = true;
  launcher.closest = () => null;
  launcher.focus = () => { document.activeElement = launcher; launcher.focused = true; };
  const close = new Target();
  close.setAttribute('autofocus', '');
  const later = new Target();
  const visit = new Target();
  const dialog = new Target();
  dialog.open = false;
  dialog.dataset = {};
  dialog.inert = false;
  dialog.querySelector = () => close;
  dialog.querySelectorAll = () => [close, later, visit];
  dialog.contains = target => [dialog, close, later, visit].includes(target);
  dialog.show = () => {
    dialog.open = true; dialog.opens = (dialog.opens || 0) + 1;
    dialog.openMode = 'nonmodal';
    if (!dialog.inert) document.activeElement = close;
  };
  dialog.showModal = () => {
    dialog.open = true; dialog.opens = (dialog.opens || 0) + 1;
    dialog.openMode = 'modal'; document.activeElement = close;
  };
  dialog.close = () => { dialog.open = false; dialog.emit('close'); };
  document.activeElement = launcher;
  document.readyState = 'complete';
  document.visibilityState = hidden ? 'hidden' : 'visible';
  document.documentElement = {scrollHeight: 2000, clientHeight: 1000};
  document.getElementById = () => dialog;
  document.querySelectorAll = () => [launcher];
  document.querySelector = selector => selector.includes(':not(')
    ? document.otherDialog || null : dialog.open ? dialog : document.otherDialog || null;
  window.sessionStorage = session;
  window.localStorage = local;
  window.scrollY = 0;
  window.setTimeout = (callback, delay) => {
    const id = ++sequence; timers.set(id, {callback, time: clock + delay}); return id;
  };
  window.clearTimeout = id => timers.delete(id);
  const FakeDate = class extends Date { static now() { return clock; } };
  vm.runInNewContext(source, {window, document, Date: FakeDate});
  function advance(ms) {
    const end = clock + ms;
    for (;;) {
      const next = [...timers.entries()].filter(([, timer]) => timer.time <= end)
        .sort((left, right) => left[1].time - right[1].time)[0];
      if (!next) break;
      timers.delete(next[0]); clock = next[1].time; next[1].callback();
    }
    clock = end;
  }
  return {window, document, launcher, dialog, close, later, visit, advance, session, local, now: () => clock};
}

// 1. Automatic display stays nonmodal, keeps reading focus and stores dismissal.
const sharedLocal = storage();
const sharedSession = storage();
const first = page({session: sharedSession, local: sharedLocal});
assert.equal(first.launcher.hidden, false);
first.advance(14999); assert.equal(first.dialog.open, false);
first.advance(1); assert.equal(first.dialog.open, true);
assert.equal(first.dialog.openMode, 'nonmodal');
assert.equal(first.dialog.attributes.get('aria-modal'), 'false');
assert.equal(first.document.activeElement, first.launcher);
assert.equal(first.dialog.inert, false);
assert.equal(first.launcher.focused, undefined);
assert.equal(sharedSession.getItem(seenKey), '1');
first.close.emit('click'); assert.equal(first.dialog.open, false);
assert.equal(Number(sharedLocal.getItem(untilKey)), first.now() + month);
assert.equal(first.document.activeElement, first.launcher);
assert.equal(first.launcher.focused, undefined);

// 2. Another page/language in the same session must remain quiet.
const anotherPage = page({session: sharedSession, local: sharedLocal});
anotherPage.advance(60000); anotherPage.window.scrollY = 900; anotherPage.window.emit('scroll');
assert.equal(anotherPage.dialog.open, false);

// 3. Persistent choice survives a fresh session; manual invitation remains available.
const freshSession = page({local: sharedLocal});
freshSession.advance(60000); assert.equal(freshSession.dialog.open, false);
freshSession.launcher.emit('click'); assert.equal(freshSession.dialog.open, true);
assert.equal(freshSession.dialog.openMode, 'modal');
assert.equal(freshSession.dialog.attributes.get('aria-modal'), 'true');
freshSession.later.emit('click'); assert.equal(freshSession.dialog.open, false);
assert.equal(freshSession.document.activeElement, freshSession.launcher);
const expired = page({local: sharedLocal, now: Number(sharedLocal.getItem(untilKey)) + 1});
expired.advance(15000); assert.equal(expired.dialog.open, true);

// 4. Scroll threshold opens once; a later timer cannot open it a second time.
const scrolling = page();
scrolling.window.scrollY = 449; scrolling.window.emit('scroll'); assert.equal(scrolling.dialog.open, false);
scrolling.window.scrollY = 450; scrolling.window.emit('scroll'); assert.equal(scrolling.dialog.open, true);
scrolling.close.emit('click'); scrolling.advance(60000); assert.equal(scrolling.dialog.opens, 1);

// 5. Blocked storage disables automatic display, with a working manual path.
const blocked = page({session: storage(true), local: storage(true)});
blocked.advance(60000); blocked.window.scrollY = 900; blocked.window.emit('scroll');
assert.equal(blocked.dialog.open, false);
blocked.launcher.emit('click'); assert.equal(blocked.dialog.open, true);
const cancel = blocked.dialog.emit('cancel');
assert.equal(cancel.defaultPrevented, true); assert.equal(blocked.dialog.open, false);
assert.equal(blocked.launcher.focused, true);

// 6. A blocked persistent store uses a writable session preference instead.
const sessionFallback = storage();
const fallback = page({session: sessionFallback, local: storage(true)});
fallback.advance(15000); fallback.dialog.emit('cancel');
assert.equal(Number(sessionFallback.getItem(untilKey)), fallback.now() + month);
const fallbackNextPage = page({session: sessionFallback, local: storage(true)});
fallbackNextPage.advance(60000); assert.equal(fallbackNextPage.dialog.open, false);

// 7. Hidden time does not count as reading time.
const hidden = page({hidden: true});
hidden.advance(60000); assert.equal(hidden.dialog.open, false);
hidden.document.visibilityState = 'visible'; hidden.document.emit('visibilitychange');
hidden.advance(14999); assert.equal(hidden.dialog.open, false);
hidden.advance(1); assert.equal(hidden.dialog.open, true);

// 8. Writing in a field is not interrupted; a pending invite waits for focus to leave.
const writing = page();
writing.document.activeElement = {closest: () => true};
writing.advance(15000); assert.equal(writing.dialog.open, false);
writing.document.activeElement = writing.launcher; writing.document.emit('focusout');
writing.advance(0); assert.equal(writing.dialog.open, true);
writing.visit.emit('click'); assert.equal(writing.dialog.open, false);
assert.equal(Number(writing.local.getItem(untilKey)), writing.now() + month);

// 9. Without a session flag, persistent storage alone never causes repeated auto invites.
const noSession = page({session: storage(true)});
noSession.advance(60000); assert.equal(noSession.dialog.open, false);
noSession.launcher.emit('click'); assert.equal(noSession.dialog.open, true);
noSession.close.emit('click'); assert.equal(noSession.dialog.open, false);

// 10. A manual launcher promotes an existing panel without treating promotion as dismissal.
const promotion = page();
promotion.advance(15000); assert.equal(promotion.dialog.openMode, 'nonmodal');
promotion.document.activeElement = promotion.launcher;
promotion.launcher.emit('click'); assert.equal(promotion.dialog.openMode, 'modal');
assert.equal(promotion.dialog.open, true); assert.equal(promotion.local.getItem(untilKey), null);
assert.equal(promotion.document.activeElement, promotion.close);
promotion.close.emit('click'); assert.equal(promotion.document.activeElement, promotion.launcher);
assert.equal(Number(promotion.local.getItem(untilKey)), promotion.now() + month);

// 11. Escape outside the automatic panel keeps the current reader focus intact.
const passive = page(); passive.advance(15000);
const reader = {isConnected: true, closest: () => null, focus() { throw Error('Unexpected focus change'); }};
passive.document.activeElement = reader;
passive.document.otherDialog = {open: true};
passive.document.emit('keydown', {key: 'Escape'}); assert.equal(passive.dialog.open, true);
passive.document.otherDialog = null;
passive.document.emit('keydown', {key: 'Escape'}); assert.equal(passive.dialog.open, false);
assert.equal(passive.document.activeElement, reader);
assert.equal(passive.launcher.focused, undefined);
assert.equal(Number(passive.local.getItem(untilKey)), passive.now() + month);

// 12. Another open dialog delays the automatic panel until it has closed.
const otherModal = page(); otherModal.document.otherDialog = {open: true};
otherModal.advance(15000); assert.equal(otherModal.dialog.open, false);
otherModal.document.otherDialog = null; otherModal.document.emit('focusout');
otherModal.advance(0); assert.equal(otherModal.dialog.openMode, 'nonmodal');
assert.equal(otherModal.document.activeElement, otherModal.launcher);

process.stdout.write(JSON.stringify({scenarios: 12}));
'''


if __name__ == "__main__":
    unittest.main()
