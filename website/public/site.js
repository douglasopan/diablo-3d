/* Static content stays usable without these optional interactions. */
(() => {
  'use strict';

  const language = document.documentElement.lang || 'pt-BR';
  const text = (portuguese, english) => language === 'en' ? english : portuguese;

  function initLanguage() {
    const links = [...document.querySelectorAll('[data-language-link]')];
    // Keep the equivalent section even after the reader follows the article's contents.
    const updateTargets = () => links.forEach((link) => {
      const target = new URL(link.href);
      target.hash = window.location.hash;
      link.href = target.pathname + target.hash;
    });
    updateTargets();
    window.addEventListener('hashchange', updateTargets);
    links.forEach((link) => {
      link.addEventListener('click', () => {
        updateTargets();
        try { localStorage.setItem('d3d-language', link.dataset.languageLink); } catch { /* Links still work. */ }
      });
    });
  }

  function initLogoMotion() {
    const image = document.querySelector('[data-logo]');
    const button = document.querySelector('[data-logo-toggle]');
    if (!image || !button) return;
    const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)');
    let playing = !reducedMotion.matches;
    const render = () => {
      image.src = playing ? image.dataset.animatedSrc : image.dataset.staticSrc;
      button.textContent = playing
        ? text('Pausar animação', 'Pause animation')
        : text('Animar logomarca', 'Animate logo');
      button.setAttribute('aria-pressed', String(playing));
    };
    button.addEventListener('click', () => { playing = !playing; render(); });
    reducedMotion.addEventListener('change', () => { playing = !reducedMotion.matches; render(); });
    button.hidden = false;
    render();
  }

  const normalize = (value) => String(value || '')
    .normalize('NFD')
    .replace(/\p{M}/gu, '')
    .toLocaleLowerCase(language)
    .replace(/\s+/g, ' ')
    .trim();

  function initAtmosphere() {
    const root = document.documentElement;
    const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)');
    const compact = window.matchMedia('(max-width: 680px)');
    const scenes = [...document.querySelectorAll('[data-parallax]')].map((image) => ({
      image,
      section: image.closest('.atmospheric') || image.parentElement,
    }));
    const active = new Set(scenes);
    let frame = 0;
    let measureWidth = true;

    const render = () => {
      frame = 0;
      if (measureWidth) {
        root.style.setProperty('--viewport-width', `${root.clientWidth}px`);
        measureWidth = false;
      }
      if (reducedMotion.matches) {
        scenes.forEach(({ image }) => {
          image.style.removeProperty('--parallax-y');
          image.style.removeProperty('will-change');
        });
        return;
      }

      const viewport = window.innerHeight;
      const amplitude = compact.matches ? 14 : 46;
      active.forEach(({ image, section }) => {
        const bounds = section.getBoundingClientRect();
        const progress = (bounds.top + bounds.height / 2 - viewport / 2)
          / (viewport / 2 + bounds.height / 2);
        const offset = -Math.max(-1, Math.min(1, progress)) * amplitude;
        image.style.setProperty('--parallax-y', `${Math.round(offset * 10) / 10}px`);
        image.style.willChange = 'transform';
      });
    };
    const schedule = () => {
      if (!frame && !document.hidden) frame = window.requestAnimationFrame(render);
    };

    if ('IntersectionObserver' in window && scenes.length) {
      const observer = new IntersectionObserver((entries) => {
        entries.forEach((entry) => {
          scenes.filter((scene) => scene.section === entry.target).forEach((scene) => {
            if (entry.isIntersecting) active.add(scene);
            else {
              active.delete(scene);
              scene.image.style.removeProperty('will-change');
            }
          });
        });
        schedule();
      }, { rootMargin: '180px 0px' });
      scenes.forEach(({ section }) => observer.observe(section));
    }

    window.addEventListener('scroll', schedule, { passive: true });
    window.addEventListener('resize', () => {
      measureWidth = true;
      schedule();
    }, { passive: true });
    window.addEventListener('pageshow', schedule);
    reducedMotion.addEventListener('change', schedule);
    document.addEventListener('visibilitychange', () => {
      if (document.hidden && frame) {
        window.cancelAnimationFrame(frame);
        frame = 0;
      } else schedule();
    });
    render();
  }

  function initMenu() {
    const button = document.querySelector('.menu-toggle');
    const nav = document.getElementById('navigation');
    const header = document.querySelector('.site-header');
    if (!button || !nav || !header) return;

    const desktop = window.matchMedia('(min-width: 1100px)');
    const setOpen = (open, restoreFocus = false) => {
      button.setAttribute('aria-expanded', String(open));
      button.setAttribute('aria-label', open ? text('Fechar menu', 'Close menu') : text('Abrir menu', 'Open menu'));
      nav.classList.toggle('is-open', open);
      header.classList.toggle('menu-open', open);
      if (restoreFocus) button.focus({ preventScroll: true });
    };

    button.addEventListener('click', () => {
      setOpen(button.getAttribute('aria-expanded') !== 'true');
    });
    nav.addEventListener('click', (event) => {
      if (event.target.closest('a')) setOpen(false);
    });
    document.addEventListener('keydown', (event) => {
      if (event.key === 'Escape' && button.getAttribute('aria-expanded') === 'true') {
        setOpen(false, true);
      }
    });
    document.addEventListener('click', (event) => {
      if (!header.contains(event.target)) setOpen(false);
    });
    header.addEventListener('focusout', (event) => {
      if (event.relatedTarget && !header.contains(event.relatedTarget)) setOpen(false);
    });
    desktop.addEventListener('change', () => setOpen(false));
    setOpen(false);
    button.hidden = false;
  }

  function initFilters() {
    const items = [...document.querySelectorAll('[data-filter-item]')];
    const bar = document.querySelector('.filter-bar');
    if (!bar || !items.length) return;

    const search = document.getElementById('search');
    const buttons = [...bar.querySelectorAll('button[data-filter]')];
    const count = document.getElementById('result-count');
    const empty = document.getElementById('no-results');
    const reset = document.getElementById('reset-filters');
    const allValues = new Set(['', '*', 'all', 'todos', 'todas', 'tudo']);
    let active = buttons.find((button) => button.getAttribute('aria-pressed') === 'true')?.dataset.filter || 'all';

    const index = items.map((element) => ({
      element,
      text: normalize(element.dataset.search || element.textContent),
      categories: normalize(element.dataset.category).split(/[,|;]/).map((value) => value.trim()),
    }));

    const update = () => {
      const words = normalize(search?.value).split(' ').filter(Boolean);
      const category = normalize(active);
      const showAll = allValues.has(category);
      let visible = 0;

      index.forEach((item) => {
        const categoryMatch = showAll || item.categories.some((value) => (
          value === category || value.split(' ').includes(category)
        ));
        const matches = categoryMatch && words.every((word) => item.text.includes(word));
        item.element.hidden = !matches;
        if (matches) visible += 1;
      });

      buttons.forEach((button) => {
        const pressed = normalize(button.dataset.filter) === category
          || (showAll && allValues.has(normalize(button.dataset.filter)));
        button.setAttribute('aria-pressed', String(pressed));
        button.classList.toggle('active', pressed);
      });
      if (count) count.textContent = `${visible} ${visible === 1 ? text('resultado', 'result') : text('resultados', 'results')}`;
      if (empty) empty.hidden = visible > 0;
      document.dispatchEvent(new CustomEvent('d3d:filter', { detail: { visible } }));
    };

    buttons.forEach((button) => button.addEventListener('click', () => {
      active = button.dataset.filter || 'all';
      update();
    }));
    search?.addEventListener('input', update);
    search?.addEventListener('keydown', (event) => {
      if (event.key === 'Escape' && search.value) {
        search.value = '';
        update();
      }
    });
    reset?.addEventListener('click', () => {
      if (search) search.value = '';
      active = buttons.find((button) => allValues.has(normalize(button.dataset.filter)))?.dataset.filter || 'all';
      update();
      search?.focus({ preventScroll: true });
    });
    window.addEventListener('pageshow', update);
    update();
    bar.hidden = false;
  }

  function initLightbox() {
    const dialog = document.getElementById('image-dialog');
    const image = document.getElementById('dialog-image');
    const caption = document.getElementById('dialog-caption');
    const counter = document.getElementById('dialog-counter');
    const close = dialog?.querySelector('.dialog-close');
    const previous = document.getElementById('dialog-prev');
    const next = document.getElementById('dialog-next');
    if (!dialog || typeof dialog.showModal !== 'function' || !image || !caption || !close) return;

    let triggers = [];
    let position = 0;
    let opener = null;
    const visibleTriggers = () => [...document.querySelectorAll('[data-lightbox]')]
      .filter((trigger) => trigger.isConnected && !trigger.closest('[hidden]'));

    const render = () => {
      const trigger = triggers[position];
      if (!trigger) return;
      const thumbnail = trigger.querySelector('img');
      const suppliedSource = trigger.dataset.lightbox;
      const source = (suppliedSource && suppliedSource !== 'true' ? suppliedSource : '')
        || trigger.getAttribute('href') || thumbnail?.currentSrc || thumbnail?.src;
      if (!source) return;
      image.src = source;
      image.alt = thumbnail?.alt || trigger.dataset.caption || text('Captura do desenvolvimento do D3D', 'D3D development screenshot');
      caption.textContent = trigger.dataset.caption
        || trigger.closest('figure')?.querySelector('figcaption')?.textContent.trim()
        || image.alt;
      if (counter) counter.textContent = `${position + 1} / ${triggers.length}`;
      [previous, next].forEach((button) => {
        if (button) {
          button.disabled = triggers.length < 2;
          button.hidden = triggers.length < 2;
        }
      });
    };

    const move = (amount) => {
      const current = triggers[position];
      triggers = visibleTriggers();
      if (!triggers.length) {
        dialog.close();
        return;
      }
      const currentPosition = triggers.indexOf(current);
      position = ((currentPosition < 0 ? 0 : currentPosition) + amount + triggers.length) % triggers.length;
      render();
    };

    document.querySelectorAll('[data-lightbox]').forEach((trigger) => {
      trigger.addEventListener('click', (event) => {
        if (event.ctrlKey || event.metaKey || event.shiftKey || event.altKey || event.button > 0) return;
        triggers = visibleTriggers();
        position = triggers.indexOf(trigger);
        if (position < 0) return;
        event.preventDefault();
        opener = trigger;
        render();
        if (!dialog.open) dialog.showModal();
        document.documentElement.classList.add('lightbox-open');
        close.focus({ preventScroll: true });
      });
    });

    close.addEventListener('click', () => dialog.close());
    previous?.addEventListener('click', () => move(-1));
    next?.addEventListener('click', () => move(1));
    dialog.addEventListener('keydown', (event) => {
      if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') {
        event.preventDefault();
        move(event.key === 'ArrowLeft' ? -1 : 1);
      }
    });
    dialog.addEventListener('click', (event) => {
      if (event.target !== dialog) return;
      const bounds = dialog.getBoundingClientRect();
      if (event.clientX < bounds.left || event.clientX > bounds.right
          || event.clientY < bounds.top || event.clientY > bounds.bottom) dialog.close();
    });
    dialog.addEventListener('close', () => {
      document.documentElement.classList.remove('lightbox-open');
      if (opener?.isConnected && !opener.closest('[hidden]')) opener.focus({ preventScroll: true });
    });
    document.addEventListener('d3d:filter', () => {
      if (!dialog.open) return;
      const current = triggers[position];
      triggers = visibleTriggers();
      position = triggers.indexOf(current);
      if (position < 0) dialog.close();
      else render();
    });
  }

  function initCopyButtons() {
    document.querySelectorAll('button[data-copy]').forEach((button) => {
      button.addEventListener('click', async () => {
        const value = button.dataset.copy || '';
        if (!value) return;
        const status = document.getElementById('copy-status');
        const identity = button.closest('.support-card')?.querySelector('.payment-identity strong')
          || document.querySelector('.payment-identity strong');
        button.disabled = true;
        button.setAttribute('aria-busy', 'true');
        button.classList.remove('is-copied');
        if (status) {
          status.textContent = text('Copiando e-mail…', 'Copying email…');
          status.dataset.state = 'pending';
        }

        try {
          if (!navigator.clipboard?.writeText) throw new Error('Clipboard unavailable');
          await navigator.clipboard.writeText(value);
          button.classList.add('is-copied');
          if (status) {
            status.textContent = text('E-mail copiado.', 'Email copied.');
            status.dataset.state = 'copied';
          }
        } catch {
          if (identity) {
            if (!identity.hasAttribute('tabindex')) identity.setAttribute('tabindex', '-1');
            identity.focus({ preventScroll: true });
            const selection = window.getSelection();
            const range = document.createRange();
            range.selectNodeContents(identity);
            selection?.removeAllRanges();
            selection?.addRange(range);
          }
          if (status) {
            status.textContent = identity
              ? text('E-mail selecionado. Use Ctrl+C, ⌘C ou o menu de copiar do navegador.', 'Email selected. Use Ctrl+C, ⌘C or your browser’s copy menu.')
              : text('Selecione o e-mail acima e copie pelo menu do navegador.', 'Select the email above and copy it using your browser’s menu.');
            status.dataset.state = 'manual';
          }
        } finally {
          button.disabled = false;
          button.removeAttribute('aria-busy');
        }
      });
      button.hidden = false;
    });
  }

  const init = () => {
    initLanguage();
    initLogoMotion();
    initAtmosphere();
    initMenu();
    initFilters();
    initLightbox();
    initCopyButtons();
    document.documentElement.classList.add('js');
  };
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init, { once: true });
  else init();
})();
