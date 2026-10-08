/* The local poster and ordinary links work without contacting YouTube. */
(() => {
  'use strict';

  const english = (document.documentElement.lang || '').startsWith('en');
  const videoIdPattern = /^[A-Za-z0-9_-]{11}$/;

  const cards = Array.from(document.querySelectorAll('[data-youtube-card]')).map(card => ({
    card,
    slot: card.querySelector('[data-youtube-slot]'),
    buttons: Array.from(card.querySelectorAll('button[data-enhancement="youtube"][data-youtube-id]')),
    status: card.querySelector('[data-youtube-status]'),
  })).filter(item => item.slot && item.buttons.length && item.buttons.every(button => videoIdPattern.test(button.dataset.youtubeId || '')));

  for (const item of cards) {
    const { slot, buttons, status } = item;
    const poster = Array.from(slot.childNodes);
    item.reset = () => {
      if (!slot.querySelector('iframe')) return;
      slot.replaceChildren(...poster);
      for (const button of buttons) {
        button.hidden = false;
        button.setAttribute('aria-expanded', 'false');
      }
      if (status) status.textContent = '';
    };
  }

  for (const item of cards) {
    const { slot, buttons, status } = item;
    const play = button => {
      if (slot.querySelector('iframe')) return;
      // A single explicit click starts the requested video and stops other audio.
      for (const other of cards) if (other !== item) other.reset();
      document.querySelectorAll('audio').forEach(audio => audio.pause());
      const player = document.createElement('iframe');
      player.src = `https://www.youtube-nocookie.com/embed/${button.dataset.youtubeId}?autoplay=1&playsinline=1`;
      player.title = button.dataset.youtubeTitle || (english ? 'YouTube player' : 'Player do YouTube');
      player.allow = 'autoplay; encrypted-media; picture-in-picture; fullscreen';
      player.allowFullscreen = true;
      player.referrerPolicy = 'strict-origin-when-cross-origin';
      player.tabIndex = 0;
      slot.replaceChildren(player);
      for (const trigger of buttons) {
        trigger.setAttribute('aria-expanded', 'true');
        trigger.hidden = true;
      }
      if (status) {
        status.textContent = button.dataset.youtubeStatusMessage || (english
          ? 'Player opened. Use the video controls for playback and volume.'
          : 'Player aberto. Use os controles do vídeo para reprodução e volume.');
      }
      player.focus();
    };
    for (const button of buttons) {
      button.hidden = false;
      button.addEventListener('click', () => play(button));
    }
  }
  document.querySelectorAll('audio').forEach(audio => {
    audio.addEventListener('play', () => cards.forEach(item => item.reset()));
  });
})();
