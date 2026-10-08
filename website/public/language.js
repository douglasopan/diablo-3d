/* Honor an explicit language choice; otherwise use the browser's first language. */
(() => {
  'use strict';
  if (document.documentElement.lang === 'en') return;
  let preferred;
  try { preferred = localStorage.getItem('d3d-language'); } catch { /* Storage is optional. */ }
  if (!['pt-BR', 'en'].includes(preferred)) {
    preferred = /^en(?:-|$)/i.test(navigator.languages?.[0] || navigator.language || '') ? 'en' : 'pt-BR';
  }
  if (preferred !== 'en') return;
  const alternate = document.querySelector('link[rel="alternate"][hreflang="en"]');
  if (!alternate) return;
  const target = new URL(alternate.href);
  // The canonical host also serves local previews; keep navigation on this origin.
  window.location.replace(target.pathname + window.location.search + window.location.hash);
})();
