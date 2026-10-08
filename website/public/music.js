/* A small integration around the self-hosted Plyr audio controls. */
(() => {
  'use strict';

  const audio = document.getElementById('background-music');
  const status = document.getElementById('music-status');
  if (!audio || !status) return;
  const english = document.documentElement.lang === 'en';
  const text = (pt, en) => english ? en : pt;
  const key = 'd3d-background-music-v1';
  const readState = () => {
    try {
      const state = JSON.parse(localStorage.getItem(key));
      if (!state || typeof state !== 'object' || Array.isArray(state)) return null;
      return {
        playing: typeof state.playing === 'boolean' ? state.playing : true,
        seconds: Number.isFinite(state.seconds) && state.seconds >= 0 ? state.seconds : 0,
        volume: Number.isFinite(state.volume) ? Math.max(0, Math.min(1, state.volume)) : 0.25,
        muted: state.muted === true,
      };
    } catch { return null; /* Playback works without storage. */ }
  };
  const saved = readState();
  const volume = saved?.volume ?? 0.25;
  let pendingPosition = saved?.seconds ?? null;
  audio.volume = volume;
  audio.muted = saved?.muted === true;

  // Native controls remain available if enhancement fails or JavaScript is disabled.
  if (window.Plyr) {
    try {
      new window.Plyr(audio, {
        controls: ['play', 'mute', 'volume'],
        iconUrl: audio.dataset.iconUrl,
        storage: { enabled: false },
        keyboard: { focused: true, global: false },
        autoplay: false,
        volume,
        muted: audio.muted,
        loop: { active: true },
        i18n: {
          play: text('Tocar', 'Play'),
          pause: text('Pausar', 'Pause'),
          mute: text('Silenciar', 'Mute'),
          unmute: text('Ativar som', 'Unmute'),
          volume: 'Volume',
          seek: text('Posição', 'Seek'),
        },
      });
    } catch { /* The browser's audio player is the fallback. */ }
  }

  let lastSave = 0;
  let leaving = false;
  const persist = () => {
    if (leaving) return;
    try {
      localStorage.setItem(key, JSON.stringify({
        playing: !audio.paused && !audio.ended,
        seconds: pendingPosition ?? audio.currentTime,
        volume: audio.volume,
        muted: audio.muted,
      }));
    } catch { /* Private browsing may disable storage. */ }
  };
  const updateStatus = () => {
    status.textContent = audio.paused
      ? text('Pausada', 'Paused')
      : text('Tocando', 'Playing');
  };
  const restorePosition = () => {
    if (pendingPosition !== null && !audio.seeking && audio.readyState > 0 && Number.isFinite(audio.duration) && audio.duration > 0) {
      try {
        if (pendingPosition === 0 && audio.currentTime < 0.5) {
          pendingPosition = null;
          return;
        }
        audio.currentTime = pendingPosition % audio.duration;
      } catch { /* Keep the saved position until metadata permits seeking. */ }
    }
  };
  const attemptPlay = () => {
    audio.play().catch(error => {
      if (error.name === 'AbortError') {
        updateStatus();
      } else {
        status.textContent = audio.error
          ? text('Áudio indisponível', 'Audio unavailable')
          : text('Toque para ouvir', 'Press play to listen');
      }
    });
  };
  audio.addEventListener('loadedmetadata', restorePosition);
  audio.addEventListener('canplay', restorePosition);
  audio.addEventListener('seeked', () => {
    if (pendingPosition !== null && Number.isFinite(audio.duration) && audio.duration > 0
      && Math.abs(audio.currentTime - pendingPosition % audio.duration) < 0.5) {
      pendingPosition = null;
    }
  });
  audio.addEventListener('play', () => { updateStatus(); persist(); });
  audio.addEventListener('pause', () => { updateStatus(); persist(); });
  audio.addEventListener('volumechange', persist);
  audio.addEventListener('timeupdate', () => {
    if (Date.now() - lastSave >= 3000) {
      persist();
      lastSave = Date.now();
    }
  });
  audio.addEventListener('error', () => {
    status.textContent = text('Áudio indisponível', 'Audio unavailable');
  });
  window.addEventListener('pagehide', () => { persist(); leaving = true; });
  window.addEventListener('pageshow', event => {
    leaving = false;
    if (!event.persisted) return;
    const latest = readState();
    if (!latest) return;
    pendingPosition = latest.seconds;
    audio.volume = latest.volume;
    audio.muted = latest.muted;
    restorePosition();
    if (latest.playing) {
      attemptPlay();
    } else {
      audio.pause();
      updateStatus();
    }
  });

  restorePosition();
  if (!saved || saved.playing === true) {
    attemptPlay();
  } else {
    updateStatus();
  }
})();
