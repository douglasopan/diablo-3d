/* The local poster and ordinary links work without contacting YouTube. */
(() => {
  'use strict';

  const english = (document.documentElement.lang || '').startsWith('en');
  const videoIdPattern = /^[A-Za-z0-9_-]{11}$/;

  document.querySelectorAll('button[data-enhancement="youtube"][data-youtube-id]').forEach((button) => {
    const card = button.closest('[data-youtube-card]');
    const slot = card && card.querySelector('[data-youtube-slot]');
    const videoId = button.dataset.youtubeId || '';
    if (!slot || !videoIdPattern.test(videoId)) return;

    button.hidden = false;
    button.addEventListener('click', () => {
      if (slot.querySelector('iframe')) return;
      const player = document.createElement('iframe');
      player.src = `https://www.youtube-nocookie.com/embed/${videoId}?autoplay=0`;
      player.title = button.dataset.youtubeTitle || (english ? 'YouTube player' : 'Player do YouTube');
      player.allow = 'encrypted-media; picture-in-picture; fullscreen';
      player.allowFullscreen = true;
      player.referrerPolicy = 'strict-origin-when-cross-origin';
      player.tabIndex = 0;
      slot.replaceChildren(player);
      button.setAttribute('aria-expanded', 'true');
      button.hidden = true;
      const status = card.querySelector('[data-youtube-status]');
      if (status) {
        status.textContent = button.dataset.youtubeStatusMessage || (english
          ? 'Use the video controls to start listening.'
          : 'Use os controles do vídeo para começar a ouvir.');
      }
      player.focus();
    });
  });
})();
