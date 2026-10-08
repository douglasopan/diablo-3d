/* One native audio player for the public soundtrack playlist. */
(() => {
  'use strict';

  const audio = document.getElementById('soundtrack-player');
  if (!audio) return;
  const buttons = Array.from(document.querySelectorAll('[data-soundtrack-src][data-soundtrack-title]'));
  if (!buttons.length) return;
  const title = document.getElementById('soundtrack-title');
  const status = document.getElementById('soundtrack-status');
  const background = document.getElementById('background-music');
  const english = document.documentElement.lang === 'en';
  const text = (pt, en) => english ? en : pt;
  const storageKey = 'd3d-soundtrack-volume-v1';
  const sourceOf = button => new URL(button.dataset.soundtrackSrc, document.baseURI).href;
  let selected = buttons.find(button => sourceOf(button) === audio.src) || buttons[0];

  // Keep playback manual; only volume and mute are remembered.
  audio.controls = true;
  audio.autoplay = false;
  audio.removeAttribute('autoplay');
  try {
    const saved = JSON.parse(localStorage.getItem(storageKey));
    if (saved && typeof saved === 'object') {
      if (Number.isFinite(saved.volume)) audio.volume = Math.max(0, Math.min(1, saved.volume));
      audio.muted = saved.muted === true;
    }
  } catch { /* Native playback also works without storage. */ }

  const update = () => {
    const playing = !audio.paused && !audio.ended && !audio.error;
    const trackTitle = selected.dataset.soundtrackTitle;
    if (title) title.textContent = trackTitle;
    audio.setAttribute('aria-label', text('Ouvir ', 'Listen to ') + trackTitle);
    for (const button of buttons) {
      const current = button === selected;
      const label = current && playing ? text('Pausar', 'Pause') : text('Tocar', 'Play');
      button.setAttribute('aria-label', label + ' ' + button.dataset.soundtrackTitle);
      button.setAttribute('aria-pressed', String(current && playing));
      if (current) button.setAttribute('aria-current', 'true');
      else button.removeAttribute('aria-current');
      const visibleLabel = button.querySelector('[data-soundtrack-label]');
      if (visibleLabel) visibleLabel.textContent = label;
    }
    if (status) status.textContent = (playing ? text('Tocando', 'Playing') : text('Pausada', 'Paused')) + ' · ' + trackTitle;
  };

  const select = button => {
    selected = button;
    const source = sourceOf(button);
    if (audio.src !== source || audio.error) {
      audio.pause();
      audio.src = source;
      audio.load();
    }
    update();
  };

  const play = () => {
    if (background) background.pause();
    const source = audio.src;
    audio.play().catch(error => {
      if (error.name === 'AbortError' || audio.src !== source) return;
      update();
      if (status) status.textContent = audio.error
        ? text('Áudio indisponível', 'Audio unavailable')
        : text('Toque para ouvir', 'Press play to listen');
    });
  };

  for (const button of buttons) {
    button.addEventListener('click', () => {
      if (selected === button && !audio.paused && !audio.ended) {
        audio.pause();
      } else {
        select(button);
        play();
      }
    });
    button.hidden = false;
  }

  // The library page should omit the background widget. These guards also
  // prevent overlap if both players are present in a shared page shell.
  audio.addEventListener('play', () => {
    if (background) background.pause();
    update();
  });
  if (background) background.addEventListener('play', () => audio.pause());
  audio.addEventListener('pause', update);
  audio.addEventListener('ended', () => {
    const next = buttons[buttons.indexOf(selected) + 1];
    if (next) {
      select(next);
      play();
    } else update();
  });
  audio.addEventListener('error', () => {
    update();
    if (status) status.textContent = text('Áudio indisponível', 'Audio unavailable');
  });
  audio.addEventListener('volumechange', () => {
    try {
      localStorage.setItem(storageKey, JSON.stringify({ volume: audio.volume, muted: audio.muted }));
    } catch { /* Storage is optional. */ }
  });

  select(selected);
})();
