/* Private shell adapter: all filesystem mutations belong to d3dBrowserStorage. */
(function () {
  'use strict';
  function attachFileManager(options) {
    var doc = options.document, win = options.window;
    var ids = ['fileManagerModal', 'fileManagerBtn', 'closeModal', 'dropZone', 'fileInput', 'browseBtn', 'resetSettingsBtn', 'mpqFilesList'];
    var nodes = Object.create(null);
    ids.forEach(function (id) { nodes[id] = doc.getElementById(id); });
    if (ids.some(function (id) { return !nodes[id]; })) return { attached: false };
    var statuses = [doc.getElementById('fileManagerStatus'), doc.getElementById('fileManagerModalStatus')].filter(function (node) { return !!node; });
    var start = doc.getElementById('startGameBtn');
    var busy = false, started = false, storageReady = false, hasBase = false, persistenceFailed = false;
    function message(text) { if (statuses.length) statuses.forEach(function (node) { node.textContent = text; }); else if (options.showMessage) options.showMessage(text); }
    function api() {
      var storage = options.getModule() && options.getModule()['d3dBrowserStorage'];
      if (!storage) throw new Error('O jogo ainda está preparando o armazenamento local.');
      return storage;
    }
    function setBusy(value) {
      busy = value;
      nodes.fileInput.disabled = value || started || !storageReady;
      nodes.browseBtn.disabled = value || started || !storageReady;
      nodes.resetSettingsBtn.disabled = value || started || !storageReady;
      if (start) start.disabled = value || started || !storageReady || !hasBase || persistenceFailed;
    }
    function observeState(state) {
      if (!state) return;
      storageReady = !!state.ready;
      started = started || !!state.started;
      hasBase = !!state.hasBase;
      persistenceFailed = !!state.persistenceFailed;
      setBusy(busy);
    }
    function observeError(error) {
      if (error.code === 'PERSIST_FAILED' || error.code === 'PERSISTENCE_NOT_CONFIRMED') persistenceFailed = true;
      if (error.code === 'RESTORE_FAILED' || error.code === 'STORAGE_INIT_FAILED' || error.code === 'DIRECTORY_CONFLICT') storageReady = false;
      setBusy(busy);
    }
    function label(text) { var element = doc.createElement('p'); element.className = 'info-text'; element.textContent = text; nodes.mpqFilesList.appendChild(element); }
    async function refresh() {
      nodes.mpqFilesList.textContent = '';
      try {
        var files = await api().listFiles();
        hasBase = files.some(function (file) { return (file.name.toLowerCase() === 'diabdat.mpq' || file.name === 'spawn.mpq') && file.size > 0; });
        setBusy(busy);
        if (!files.length) label('Selecione DIABDAT.MPQ ou SPAWN.MPQ para iniciar.');
        files.forEach(function (file) {
          var row = doc.createElement('div'); row.className = 'file-item';
          var name = doc.createElement('span'); name.className = 'file-item-name'; name.textContent = file.name;
          var size = doc.createElement('span'); size.className = 'file-item-size'; size.textContent = (file.size / 1048576).toFixed(1) + ' MiB';
          var button = doc.createElement('button'); button.className = 'btn btn-delete'; button.textContent = 'Remover'; button.disabled = busy || started || !storageReady;
          button.addEventListener('click', function () {
            if (!options.confirm('Remover ' + file.name + ' deste navegador?')) return;
            perform(function (storage) { return storage.removeGameFile(file.name); }, 'Arquivo removido do navegador.');
          });
          row.appendChild(name); row.appendChild(size); row.appendChild(button); nodes.mpqFilesList.appendChild(row);
        });
      } catch (_) { hasBase = false; setBusy(busy); label('O armazenamento ainda não está disponível.'); }
    }
    async function perform(operation, success) {
      if (busy) return;
      setBusy(true);
      try { await operation(api()); message(success); }
      catch (error) {
        observeError(error);
        message(error.message + (error.imported && error.imported.length
          ? (error.persisted ? ' Importados e confirmados: ' : ' Ainda sem confirmação de salvamento: ') + error.imported.join(', ') + '.' : ''));
      } finally { nodes.fileInput.value = ''; setBusy(false); await refresh(); }
    }
    nodes.fileManagerBtn.addEventListener('click', function () { nodes.fileManagerModal.classList.add('show'); refresh(); });
    nodes.closeModal.addEventListener('click', function () { nodes.fileManagerModal.classList.remove('show'); });
    nodes.fileManagerModal.addEventListener('click', function (event) { if (event.target === nodes.fileManagerModal) nodes.fileManagerModal.classList.remove('show'); });
    nodes.browseBtn.addEventListener('click', function (event) { event.stopPropagation(); if (!busy && !started) nodes.fileInput.click(); });
    nodes.dropZone.addEventListener('click', function (event) { if (event.target !== nodes.fileInput && !busy && !started) nodes.fileInput.click(); });
    nodes.dropZone.addEventListener('dragover', function (event) { event.preventDefault(); nodes.dropZone.classList.add('dragover'); });
    nodes.dropZone.addEventListener('dragleave', function () { nodes.dropZone.classList.remove('dragover'); });
    nodes.dropZone.addEventListener('drop', function (event) {
      event.preventDefault(); nodes.dropZone.classList.remove('dragover');
      if (!started) perform(function (storage) { return storage.importGameFiles(event.dataTransfer.files); }, 'Arquivos salvos neste navegador. Clique em Iniciar jogo.');
    });
    nodes.fileInput.addEventListener('change', function (event) {
      perform(function (storage) { return storage.importGameFiles(event.target.files); }, 'Arquivos salvos neste navegador. Clique em Iniciar jogo.');
    });
    nodes.resetSettingsBtn.addEventListener('click', function () {
      if (!options.confirm('Redefinir diablo.ini? Seus personagens e saves serão mantidos.')) return;
      perform(function (storage) { return storage.resetSettings(); }, 'Configurações redefinidas. Seus saves foram preservados.');
    });
    if (start) start.addEventListener('click', function () { perform(function (storage) { return storage.startGame(); }, 'Diablo 3D iniciado.'); });
    win.addEventListener('d3d-storage-ready', function (event) { observeState(event.detail); message('Armazenamento local pronto.'); refresh(); });
    win.addEventListener('d3d-storage-status', function (event) { if (event.detail) { observeState(event.detail.state); if (event.detail.phase === 'running') { started = true; setBusy(busy); } } });
    win.addEventListener('d3d-storage-error', function (event) { if (event.detail) { observeError(event.detail); message(event.detail.message); } });
    setBusy(false);
    // The shell may attach after ready has already resolved. Restore its state
    // without forcing start enabled and then obtain a fresh file list.
    try { api().ready.then(function (state) { observeState(state); refresh(); }, function (error) { observeError(error); message(error.message); }); } catch (_) { /* preRun not reached yet; events will provide ready */ }
    return { attached: true, refresh: refresh };
  }
  if (typeof module === 'object' && module.exports) { module.exports = { attachFileManager: attachFileManager }; return; }
  attachFileManager({ document: document, window: window, getModule: function () { return typeof Module === 'undefined' ? null : Module; }, confirm: function (text) { return window.confirm(text); }, showMessage: function (text) { window.alert(text); } });
})();
