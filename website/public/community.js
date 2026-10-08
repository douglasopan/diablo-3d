(() => {
  'use strict';

  const SEEN_KEY = 'd3d.community.seen.v1';
  const UNTIL_KEY = 'd3d.community.dismissed-until.v1';
  const DELAY = 15000;
  const SNOOZE = 30 * 24 * 60 * 60 * 1000;

  function availableStorage(name) {
    try {
      const storage = window[name];
      const probe = 'd3d.community.storage-probe';
      storage.setItem(probe, '1');
      storage.removeItem(probe);
      return storage;
    } catch (_) {
      return null;
    }
  }

  function init() {
    const dialog = document.getElementById('community-invite');
    if (!dialog || typeof dialog.showModal !== 'function') return;

    const session = availableStorage('sessionStorage');
    const local = availableStorage('localStorage');
    const autofocus = dialog.querySelector('[autofocus]');
    let previousFocus = null;
    let mode = null;
    let switchingToModal = false;
    let returnFocusOnClose = false;
    let stopped = false;
    let pending = false;
    let elapsed = 0;
    let visibleSince = null;
    let timer = null;

    function canInviteAutomatically() {
      // A writable session flag is needed to avoid repeating on another page.
      if (!session) return false;
      try {
        if (session.getItem(SEEN_KEY) === '1') return false;
        for (const storage of [local, session]) {
          if (!storage) continue;
          const until = Number(storage.getItem(UNTIL_KEY) || 0);
          if (!Number.isFinite(until) || until > Date.now()) return false;
        }
        return true;
      } catch (_) {
        return false;
      }
    }

    function stopAutomaticInvitation() {
      stopped = true;
      window.clearTimeout(timer);
      window.removeEventListener('scroll', onScroll);
      document.removeEventListener('visibilitychange', onVisibility);
      document.removeEventListener('focusout', onFocusOut);
    }

    function editingOrAnotherDialog() {
      const active = document.activeElement;
      const editing = active && typeof active.closest === 'function'
        && active.closest('input, textarea, select, [contenteditable="true"], [role="textbox"]');
      return editing || document.querySelector('dialog[open]');
    }

    function openInvitation(manual = false) {
      const opener = document.activeElement;
      if (dialog.open) {
        if (!manual || mode !== 'automatic') return;
        // Native showModal cannot promote an already open nonmodal dialog.
        switchingToModal = true;
        dialog.close();
        switchingToModal = false;
      }
      if (!manual) {
        if (stopped) return;
        if (!canInviteAutomatically()) {
          stopAutomaticInvitation();
          return;
        }
        if (document.visibilityState !== 'visible' || editingOrAnotherDialog()) {
          pending = true;
          return;
        }
        try {
          session.setItem(SEEN_KEY, '1');
        } catch (_) {
          stopAutomaticInvitation();
          return;
        }
      } else if (session) {
        try { session.setItem(SEEN_KEY, '1'); } catch (_) { /* Manual still works. */ }
      }

      previousFocus = opener;
      returnFocusOnClose = manual;
      mode = manual ? 'manual' : 'automatic';
      dialog.dataset.communityMode = mode;
      dialog.setAttribute('aria-modal', manual ? 'true' : 'false');
      try {
        if (manual) {
          if (autofocus) autofocus.setAttribute('autofocus', '');
          dialog.showModal();
        } else {
          // show() also performs dialog focusing steps. Inertness during that
          // synchronous call keeps the reader's current focus untouched.
          if (autofocus) autofocus.removeAttribute('autofocus');
          const wasInert = dialog.inert;
          try {
            dialog.inert = true;
            dialog.show();
          } finally {
            dialog.inert = wasInert;
          }
          if (document.activeElement !== opener && opener && opener.isConnected && typeof opener.focus === 'function') {
            opener.focus({ preventScroll: true });
          }
        }
        stopAutomaticInvitation();
      } catch (_) {
        previousFocus = null;
        stopAutomaticInvitation();
      }
    }

    function closeInvitation() {
      if (dialog.open) {
        returnFocusOnClose = mode === 'manual' || dialog.contains(document.activeElement);
        dialog.close();
      }
    }

    dialog.addEventListener('cancel', (event) => {
      event.preventDefault();
      closeInvitation();
    });
    dialog.addEventListener('close', () => {
      // Ignore the close event from promotion if a manual modal is now open.
      if (switchingToModal || dialog.open) return;
      const until = String(Date.now() + SNOOZE);
      // Keep a session fallback if persistent storage is blocked or fills up.
      for (const storage of [local, session]) {
        if (storage) {
          try { storage.setItem(UNTIL_KEY, until); } catch (_) { /* Optional preference. */ }
        }
      }
      if (returnFocusOnClose && previousFocus && previousFocus.isConnected && typeof previousFocus.focus === 'function') {
        previousFocus.focus({ preventScroll: true });
      }
      previousFocus = null;
      if (autofocus) autofocus.setAttribute('autofocus', '');
    });
    document.addEventListener('keydown', (event) => {
      if (event.key !== 'Escape' || !dialog.open || mode !== 'automatic') return;
      if (document.querySelector('dialog[open]:not(#community-invite)')) return;
      if (dialog.contains(document.activeElement)) event.preventDefault();
      closeInvitation();
    });
    dialog.querySelectorAll('[data-community-close], [data-community-visit]').forEach((control) => {
      control.addEventListener('click', closeInvitation);
    });
    document.querySelectorAll('[data-community-open]').forEach((launcher) => {
      launcher.hidden = false;
      launcher.addEventListener('click', (event) => {
        event.preventDefault();
        openInvitation(true);
      });
    });

    function onVisibility() {
      window.clearTimeout(timer);
      if (document.visibilityState !== 'visible') {
        if (visibleSince !== null) elapsed += Date.now() - visibleSince;
        visibleSince = null;
        return;
      }
      visibleSince = Date.now();
      if (pending) openInvitation();
      if (stopped) return;
      timer = window.setTimeout(() => {
        elapsed = DELAY;
        pending = true;
        openInvitation();
      }, Math.max(0, DELAY - elapsed));
    }

    function onScroll() {
      const page = document.documentElement;
      const distance = page.scrollHeight - page.clientHeight;
      if (distance > 0 && window.scrollY / distance >= 0.45) {
        pending = true;
        openInvitation();
      }
    }

    function onFocusOut() {
      if (pending && !stopped) window.setTimeout(() => openInvitation(), 0);
    }

    if (typeof dialog.show === 'function' && canInviteAutomatically()) {
      window.addEventListener('scroll', onScroll, { passive: true });
      document.addEventListener('visibilitychange', onVisibility);
      document.addEventListener('focusout', onFocusOut);
      onVisibility();
    }
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init, { once: true });
  else init();
})();
