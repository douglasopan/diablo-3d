/* Private Diablo 3D browser-storage candidate. No server archive requests. */
(function () {
  'use strict';
  var DIRECTORY = '/libsdl/diasurgical/devilution';
  var ARCHIVES = Object.freeze(['diabdat.mpq', 'spawn.mpq', 'hellfire.mpq', 'hfmonk.mpq', 'hfmusic.mpq', 'hfvoice.mpq', 'hfbard.mpq', 'hfbarb.mpq']);
  function storageError(code, message) {
    var error = new Error(message);
    error.code = code;
    Object.defineProperty(error, 'd3dStorageError', { value: true });
    return error;
  }
  function canonicalName(name) {
    if (typeof name !== 'string' || !/^[A-Za-z]+\.mpq$/i.test(name))
      throw storageError('INVALID_FILENAME', 'Selecione apenas arquivos MPQ do Diablo ou Hellfire com nomes originais.');
    var canonical = name.toLowerCase();
    if (ARCHIVES.indexOf(canonical) < 0)
      throw storageError('INVALID_FILENAME', 'Este arquivo não faz parte dos arquivos de jogo aceitos.');
    // The native loader tries original-CD DIABDAT.MPQ before lowercase.
    // Import at its highest-priority native spelling to avoid an old uppercase
    // archive silently shadowing a newly imported lowercase copy.
    return canonical === 'diabdat.mpq' ? 'DIABDAT.MPQ' : canonical;
  }
  function missing(error) { return error && (error.code === 'ENOENT' || error.errno === 44); }
  function createStorage(options) {
    var fs = options.FS;
    var started = false, hydrated = false, persistenceFailed = false, transaction = 0;
    var gateHeld = true;
    var dependency = 'd3d-local-game-start';
    function emit(type, detail) {
      // A UI listener must not corrupt storage or its queue.
      try { if (options.emit) options.emit(type, detail); } catch (_) { /* presentation only */ }
    }
    function report(error) { emit('d3d-storage-error', { code: error.code || 'STORAGE_ERROR', message: error.message }); }
    function mkdir(path) {
      try {
        if (!fs.isDir(fs.stat(path).mode))
          throw storageError('DIRECTORY_CONFLICT', 'Um caminho necessário ao armazenamento não é uma pasta.');
      } catch (error) {
        if (!missing(error)) throw error;
        fs.mkdir(path);
      }
    }
    function sync(populate) {
      return new Promise(function (resolve, reject) {
        try {
          fs.syncfs(populate, function (error) {
            if (error) reject(storageError(populate ? 'RESTORE_FAILED' : 'PERSIST_FAILED', populate
              ? 'Não foi possível recuperar o armazenamento local. Recarregue e verifique a permissão do navegador.'
              : 'Não foi possível salvar no navegador. Verifique espaço disponível e permissão de armazenamento.'));
            else resolve();
          });
        } catch (_) {
          reject(storageError(populate ? 'RESTORE_FAILED' : 'PERSIST_FAILED', 'O armazenamento local não está disponível.'));
        }
      });
    }
    function listNow() {
      return fs.readdir(DIRECTORY).filter(function (name) { return ARCHIVES.indexOf(name) >= 0 || name === 'DIABDAT.MPQ'; }).map(function (name) {
        var stat = fs.stat(DIRECTORY + '/' + name);
        return { name: name, size: stat.size, isFile: fs.isFile(stat.mode) };
      }).filter(function (entry) { return entry.isFile; }).map(function (entry) { return { name: entry.name, size: entry.size }; });
    }
    function hasBase(files) {
      return files.some(function (entry) { return (entry.name.toLowerCase() === 'diabdat.mpq' || entry.name === 'spawn.mpq') && entry.size > 0; });
    }
    function state() { var files = listNow(); return { ready: hydrated, started: started, hasBase: hasBase(files), persistenceFailed: persistenceFailed, files: files }; }
    function mutable() { if (started) throw storageError('GAME_STARTED', 'Recarregue a página para alterar arquivos ou redefinir as configurações. A partida atual será preservada.'); }
    function requireRegular(path) {
      var stat = fs.stat(path);
      if (!fs.isFile(stat.mode)) throw storageError('NOT_A_FILE', 'O arquivo selecionado não é um arquivo regular.');
      return stat;
    }
    function persist() {
      emit('d3d-storage-status', { phase: 'saving' });
      return sync(false).then(function () {
        persistenceFailed = false;
        emit('d3d-storage-status', { phase: started ? 'running' : 'waiting', state: state() });
      }, function (error) { persistenceFailed = true; throw error; });
    }
    options.addRunDependency(dependency);
    var ready = Promise.resolve().then(function () {
      mkdir('/libsdl');
      mkdir('/libsdl/diasurgical');
      fs.mount(options.IDBFS, {}, '/libsdl/diasurgical');
      return sync(true);
    }).then(function () {
      mkdir(DIRECTORY);
      hydrated = true;
      var value = state();
      emit('d3d-storage-ready', value);
      emit('d3d-storage-status', { phase: 'waiting', state: value });
      return value;
    }, function (error) {
      if (!error.d3dStorageError) error = storageError('STORAGE_INIT_FAILED', 'Não foi possível preparar o armazenamento local.');
      report(error);
      throw error;
    });
    // Rejection stays observable through ready, without an unhandled rejection
    // while the shell has not attached its listeners yet.
    ready.catch(function () {});
    var queue = ready;
    function enqueue(job) {
      var operation = queue.then(function () { return ready; }).then(job);
      queue = operation.catch(function () {});
      return operation.catch(function (error) {
        if (!error.d3dStorageError) error = storageError('FILESYSTEM_ERROR', 'A operação de arquivo não pôde ser concluída.');
        report(error);
        throw error;
      });
    }
    function readFile(file) {
      if (options.readFile) return options.readFile(file);
      if (typeof file.arrayBuffer !== 'function') throw storageError('READ_UNSUPPORTED', 'Este navegador não oferece leitura de arquivos locais.');
      return file.arrayBuffer();
    }
    var api = {
      ready: ready,
      listFiles: function () { return enqueue(listNow); },
      importGameFiles: function (files) {
        return enqueue(async function () {
          mutable();
          var input = Array.from(files || []), seen = Object.create(null);
          if (!input.length) throw storageError('NO_FILES', 'Selecione os arquivos de jogo.');
          var entries = input.map(function (file) {
            var name = canonicalName(file.name);
            if (seen[name]) throw storageError('DUPLICATE_FILENAME', 'Selecione apenas uma cópia de cada arquivo de jogo.');
            seen[name] = true;
            if (!Number.isSafeInteger(file.size) || file.size <= 0) throw storageError('EMPTY_FILE', 'Um arquivo selecionado está vazio ou tem tamanho inválido.');
            return { name: name, file: file };
          });
          var imported = [], failure = null;
          emit('d3d-storage-status', { phase: 'importing' });
          for (var entry of entries) {
            var temporary = DIRECTORY + '/.d3d-import-' + (++transaction);
            try {
              var buffer = await readFile(entry.file);
              if (Object.prototype.toString.call(buffer) !== '[object ArrayBuffer]' || buffer.byteLength !== entry.file.size)
                throw storageError('READ_FAILED', 'A leitura de um arquivo não foi concluída. Tente selecioná-lo novamente.');
              var destination = DIRECTORY + '/' + entry.name;
              try { requireRegular(destination); } catch (error) { if (!missing(error)) throw error; }
              fs.writeFile(temporary, new Uint8Array(buffer));
              fs.rename(temporary, destination);
              imported.push(entry.name);
            } catch (error) {
              failure = error.d3dStorageError ? error : storageError('READ_OR_WRITE_FAILED', 'Um arquivo não pôde ser importado. Os arquivos já importados são identificados no resultado.');
              try { fs.unlink(temporary); } catch (cleanupError) { if (!missing(cleanupError)) persistenceFailed = true; }
              break;
            }
          }
          if (imported.length) {
            try { await persist(); } catch (error) { error.imported = imported; error.persisted = false; throw error; }
          }
          if (failure) { failure.imported = imported; failure.persisted = imported.length > 0; throw failure; }
          return { imported: imported, state: state() };
        });
      },
      removeGameFile: function (name) {
        return enqueue(async function () {
          mutable();
          var canonical = canonicalName(name);
          // Both native DIABDAT spellings can already exist in a restored
          // profile. A row for the lowercase original removes that exact row.
          var path = DIRECTORY + '/' + (name === 'diabdat.mpq' ? name : canonical);
          requireRegular(path);
          fs.unlink(path);
          await persist();
          return state();
        });
      },
      resetSettings: function () {
        return enqueue(async function () {
          mutable();
          var path = DIRECTORY + '/diablo.ini';
          try { requireRegular(path); fs.unlink(path); } catch (error) { if (!missing(error)) throw error; }
          await persist();
          return state();
        });
      },
      flush: function () { return enqueue(persist); },
      startGame: function () {
        return enqueue(function () {
          if (started) return state();
          var value = state();
          if (!value.hasBase) throw storageError('BASE_REQUIRED', 'Selecione DIABDAT.MPQ ou SPAWN.MPQ antes de iniciar.');
          if (persistenceFailed) throw storageError('PERSISTENCE_NOT_CONFIRMED', 'Confirme o salvamento local antes de iniciar.');
          started = true;
          if (gateHeld) {
            try { options.removeRunDependency(dependency); gateHeld = false; }
            catch (_) { started = false; throw storageError('START_FAILED', 'Não foi possível liberar a inicialização do jogo.'); }
          }
          emit('d3d-storage-status', { phase: 'running', state: state() });
          return state();
        });
      }
    };
    return Object.freeze(api);
  }
  if (typeof module === 'object' && module.exports) {
    module.exports = { createStorage: createStorage, canonicalName: canonicalName, archives: ARCHIVES, directory: DIRECTORY };
    return;
  }
  Module['preRun'] = Module['preRun'] || [];
  Module['preRun'].push(function () {
    if (Module['d3dBrowserStorage']) return;
    Module['d3dBrowserStorage'] = createStorage({ FS: FS, IDBFS: IDBFS,
      addRunDependency: function (name) { addRunDependency(name); },
      removeRunDependency: function (name) { removeRunDependency(name); },
      emit: function (type, detail) { window.dispatchEvent(new CustomEvent(type, { detail: detail })); },
      readFile: function (file) {
        if (file.arrayBuffer) return file.arrayBuffer();
        return new Promise(function (resolve, reject) {
          var reader = new FileReader();
          reader.onload = function () { resolve(reader.result); };
          reader.onerror = reader.onabort = function () { reject(storageError('READ_FAILED', 'Não foi possível ler o arquivo local.')); };
          reader.readAsArrayBuffer(file);
        });
      }
    });
    // The existing C++ save hook can keep calling this name. Attach a rejection
    // handler even though it cannot await JS; callers that can await still can.
    Module['saveToIndexedDB'] = function () {
      var operation = Module['d3dBrowserStorage'].flush();
      operation.catch(function () {});
      return operation;
    };
    installLifecycle();
  });
  // main uses a continuing Asyncify loop. postRun can occur only after exit;
  // register lifecycle before hydration/start, with queued flush awaiting ready.
  var lifecycleInstalled = false;
  function installLifecycle() {
    if (lifecycleInstalled) return;
    lifecycleInstalled = true;
    var pending = false;
    function bestEffortSave() {
      if (pending) return;
      pending = true;
      Module['saveToIndexedDB']().catch(function () {}).finally(function () { pending = false; });
    }
    setInterval(bestEffortSave, 30000);
    document.addEventListener('visibilitychange', function () { if (document.visibilityState === 'hidden') bestEffortSave(); });
    // Page teardown cannot await IndexedDB. This is best effort, not a promise
    // that a browser or process shutdown will complete a save.
    window.addEventListener('pagehide', bestEffortSave);
  }
})();
