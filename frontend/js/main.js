/**
 * main.js
 * ──────────────────────────────────────────────────────────────
 * Entry point. Runs on DOMContentLoaded.
 *
 * Responsibilities:
 *   • Trigger the initial render of all panels with mock state
 *   • Wire up STEP / RUN / PAUSE / RESET button handlers
 *   • Sync speed slider, algorithm dropdown, and quantum input
 *     with the global state
 *   • Handle the Load JSON modal (open, cancel, confirm, Escape)
 *   • Keep controls bar in sync when state is loaded externally
 *   • Keyboard shortcut: Space → step (when not running)
 * ──────────────────────────────────────────────────────────────
 */

document.addEventListener('DOMContentLoaded', function() {

  /* ── DOM refs ───────────────────────────────────────────────── */
  var btnStep      = document.getElementById('btn-step');
  var btnRun       = document.getElementById('btn-run');
  var btnPause     = document.getElementById('btn-pause');
  var btnReset     = document.getElementById('btn-reset');
  var offlineBanner = document.getElementById('offline-banner');
  var speedSlider  = document.getElementById('speed-slider');
  var speedValue   = document.getElementById('speed-value');
  var algoSelect   = document.getElementById('algo-select');
  var quantumInput = document.getElementById('quantum-input');
  var btnLoad      = document.getElementById('btn-load');
  var arriveP1     = document.getElementById('arrive-p1');
  var arriveP2     = document.getElementById('arrive-p2');
  var arriveP3     = document.getElementById('arrive-p3');

  function _getArrivals() {
    return [
      Math.max(0, parseInt(arriveP1.value, 10) || 0),
      Math.max(0, parseInt(arriveP2.value, 10) || 0),
      Math.max(0, parseInt(arriveP3.value, 10) || 0)
    ];
  }

  var modalOverlay  = document.getElementById('modal-overlay');
  var modalTextarea = document.getElementById('modal-textarea');
  var modalCancel   = document.getElementById('modal-cancel');
  var modalConfirm  = document.getElementById('modal-confirm');

  /* ── Initial render ─────────────────────────────────────────── */
  /* Paint mock state immediately so the UI isn't blank,
     then try to fetch real state from the backend. */
  var s = getState();
  Memory.render(s);
  Scheduler.render(s);
  Inspector.render(s);
  Log.render(s);

  /* Sync controls to initial state values */
  algoSelect.value    = s.algorithm || 'RR';
  quantumInput.value  = s.timeSlice  || 2;

  /* Disable controls until backend confirms it's reachable */
  btnStep.disabled  = true;
  btnRun.disabled   = true;
  btnReset.disabled = true;

  /* Attempt to load real initial state from the C backend. */
  SimOS.reset()
    .then(function(result) {
      if (result) {
        /* Backend responded — hide banner, enable controls */
        offlineBanner.classList.add('hidden');
        btnStep.disabled  = false;
        btnRun.disabled   = false;
        btnReset.disabled = false;
      } else {
        /* Backend unreachable — show banner, keep controls disabled */
        offlineBanner.classList.remove('hidden');
      }
    })
    .catch(function() {
      offlineBanner.classList.remove('hidden');
    });

  /* ── Button handlers ────────────────────────────────────────── */

  btnStep.addEventListener('click', function() {
    SimOS.step();
  });

  btnRun.addEventListener('click', function() {
    SimOS.run();
    btnRun.disabled   = true;
    btnPause.disabled = false;
    btnStep.disabled  = true;
  });

  btnPause.addEventListener('click', function() {
    SimOS.pause();
    btnRun.disabled   = false;
    btnPause.disabled = true;
    btnStep.disabled  = false;
  });

  btnReset.addEventListener('click', function() {
    var algoMap = { 'HRRN': 1, 'RR': 2, 'MLFQ': 3 };
    var algoNum = algoMap[algoSelect.value] || 2;
    var quantum = parseInt(quantumInput.value, 10) || 2;
    fetch('http://localhost:8080/api/reset', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ algo: algoNum, quantum: quantum, arrivals: _getArrivals() })
    })
      .then(function(res) { return res.json(); })
      .then(function(data) {
        SimOS.loadState(data);
        offlineBanner.classList.add('hidden');
        btnStep.disabled  = false;
        btnRun.disabled   = false;
        btnReset.disabled = false;
      })
      .catch(function() {
        offlineBanner.classList.remove('hidden');
        btnStep.disabled  = true;
        btnRun.disabled   = true;
      });
    btnPause.disabled = true;
    /* Deselect any inspected process */
    Inspector.select(null);
  });

  /* ── Speed slider ───────────────────────────────────────────── */
  speedSlider.addEventListener('input', function() {
    var v = parseInt(this.value, 10);
    speedValue.textContent = v + 'x';
    SimOS.setSpeed(v);
  });

  /* ── Algorithm dropdown ─────────────────────────────────────── */
  algoSelect.addEventListener('change', function() {
    var algoMap = { 'HRRN': 1, 'RR': 2, 'MLFQ': 3 };
    var algoNum = algoMap[this.value] || 2;
    /* Reset backend with the new algorithm, then sync UI to returned state */
    fetch('http://localhost:8080/api/reset', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ algo: algoNum, quantum: parseInt(quantumInput.value, 10) || 2, arrivals: _getArrivals() })
    })
      .then(function(res) { return res.json(); })
      .then(function(data) {
        SimOS.loadState(data);
        /* Re-enable step/run controls after reset */
        document.getElementById('btn-run').disabled   = false;
        document.getElementById('btn-pause').disabled = true;
        document.getElementById('btn-step').disabled  = false;
        Inspector.select(null);
      })
      .catch(function() {
        /* Backend not available — update local state only */
        setState({ algorithm: algoSelect.value });
      });
  });

  /* ── Quantum input ──────────────────────────────────────────── */
  quantumInput.addEventListener('change', function() {
    var v = parseInt(this.value, 10);
    if (isNaN(v) || v < 1) v = 1;
    if (v > 16) v = 16;
    this.value = v;
    var algoMap = { 'HRRN': 1, 'RR': 2, 'MLFQ': 3 };
    var algoNum = algoMap[algoSelect.value] || 2;
    fetch('http://localhost:8080/api/reset', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ algo: algoNum, quantum: v, arrivals: _getArrivals() })
    })
      .then(function(res) { return res.json(); })
      .then(function(data) {
        SimOS.loadState(data);
        document.getElementById('btn-run').disabled   = false;
        document.getElementById('btn-pause').disabled = true;
        document.getElementById('btn-step').disabled  = false;
        Inspector.select(null);
      })
      .catch(function() {
        setState({ timeSlice: v });
      });
  });

  /* ── Load JSON modal ────────────────────────────────────────── */

  btnLoad.addEventListener('click', function() {
    modalOverlay.classList.remove('hidden');
    modalTextarea.focus();
  });

  modalCancel.addEventListener('click', function() {
    _closeModal();
  });

  /* Click outside the modal box to dismiss */
  modalOverlay.addEventListener('click', function(e) {
    if (e.target === modalOverlay) _closeModal();
  });

  modalConfirm.addEventListener('click', function() {
    var raw = modalTextarea.value.trim();
    if (!raw) return;
    try {
      var parsed = JSON.parse(raw);
      SimOS.loadState(parsed);
      _closeModal();
    } catch (e) {
      /* Highlight textarea in red briefly to signal parse error */
      modalTextarea.style.borderColor = '#ff4455';
      modalTextarea.title = 'JSON error: ' + e.message;
      setTimeout(function() {
        modalTextarea.style.borderColor = '';
        modalTextarea.title = '';
      }, 2200);
    }
  });

  function _closeModal() {
    modalOverlay.classList.add('hidden');
    modalTextarea.value        = '';
    modalTextarea.style.borderColor = '';
    modalTextarea.title        = '';
  }

  /* ── Input prompt modal ──────────────────────────────────────── */
  var inputOverlay = document.getElementById('input-overlay');
  var inputValue   = document.getElementById('input-value');
  var inputSubmit  = document.getElementById('input-submit');
  var inputPrompt  = document.getElementById('input-prompt-text');

  function _showInputModal(pid) {
    inputPrompt.textContent = 'Process P' + pid + ' needs a value';
    inputValue.value = '';
    inputOverlay.classList.remove('hidden');
    inputValue.focus();
    /* Disable step/run while waiting for input */
    btnStep.disabled = true;
    btnRun.disabled  = true;
  }

  function _hideInputModal() {
    inputOverlay.classList.add('hidden');
    inputValue.value = '';
    /* Re-enable controls */
    btnStep.disabled = false;
    btnRun.disabled  = false;
    btnPause.disabled = true;
  }

  inputSubmit.addEventListener('click', function() {
    var val = inputValue.value.trim();
    if (!val) return;
    SimOS.submitInput(val);
  });

  inputValue.addEventListener('keydown', function(e) {
    if (e.code === 'Enter') {
      e.preventDefault();
      inputSubmit.click();
    }
  });

  SimOS.onNeedsInput(_showInputModal);
  SimOS.onInputDone(_hideInputModal);

  /* ── Keyboard shortcuts ─────────────────────────────────────── */
  document.addEventListener('keydown', function(e) {
    /* Space — step (when focus is not in an input/textarea) */
    if (e.code === 'Space' &&
        !SimOS.isRunning &&
        e.target.tagName !== 'INPUT' &&
        e.target.tagName !== 'TEXTAREA' &&
        e.target.tagName !== 'SELECT') {
      e.preventDefault();
      SimOS.step();
    }
    /* Escape — close modal */
    if (e.code === 'Escape') {
      _closeModal();
    }
    /* R — run toggle */
    if (e.code === 'KeyR' &&
        e.target.tagName !== 'INPUT' &&
        e.target.tagName !== 'TEXTAREA') {
      if (SimOS.isRunning) {
        btnPause.click();
      } else {
        btnRun.click();
      }
    }
  });

  /* ── Memory panel resize ────────────────────────────────────── */
  /* Dragging #memory-resize-handle updates --memory-width on :root,
     which drives the grid-template-columns in main.css. */
  var resizeHandle  = document.getElementById('memory-resize-handle');
  var memoryPanel   = document.getElementById('panel-memory');
  var MIN_MEM_WIDTH = 200;
  var MAX_MEM_WIDTH = 500;
  var _dragging     = false;
  var _dragStartX   = 0;
  var _dragStartW   = 0;

  resizeHandle.addEventListener('mousedown', function(e) {
    _dragging   = true;
    _dragStartX = e.clientX;
    _dragStartW = memoryPanel.offsetWidth;
    resizeHandle.classList.add('dragging');
    /* Prevent text selection and cursor flicker during drag */
    document.body.style.cursor     = 'col-resize';
    document.body.style.userSelect = 'none';
    e.preventDefault();
  });

  document.addEventListener('mousemove', function(e) {
    if (!_dragging) return;
    var delta    = e.clientX - _dragStartX;
    var newWidth = Math.min(MAX_MEM_WIDTH, Math.max(MIN_MEM_WIDTH, _dragStartW + delta));
    document.documentElement.style.setProperty('--memory-width', newWidth + 'px');
  });

  document.addEventListener('mouseup', function() {
    if (!_dragging) return;
    _dragging = false;
    resizeHandle.classList.remove('dragging');
    document.body.style.cursor     = '';
    document.body.style.userSelect = '';
  });

  /* ── Arrival time inputs — reset simulation on change ─────── */
  function _onArrivalChange() {
    var algoMap = { 'HRRN': 1, 'RR': 2, 'MLFQ': 3 };
    var algoNum = algoMap[algoSelect.value] || 2;
    fetch('http://localhost:8080/api/reset', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ algo: algoNum, quantum: parseInt(quantumInput.value, 10) || 2, arrivals: _getArrivals() })
    })
      .then(function(res) { return res.json(); })
      .then(function(data) {
        SimOS.loadState(data);
        document.getElementById('btn-run').disabled   = false;
        document.getElementById('btn-pause').disabled = true;
        document.getElementById('btn-step').disabled  = false;
        Inspector.select(null);
      })
      .catch(function() {});
  }
  arriveP1.addEventListener('change', _onArrivalChange);
  arriveP2.addEventListener('change', _onArrivalChange);
  arriveP3.addEventListener('change', _onArrivalChange);

  /* ── Keep controls in sync when state loaded externally ─────── */
  /* e.g. after SimOS.loadState() or fetchState() from C backend */
  subscribe(function(state) {
    if (algoSelect.value !== (state.algorithm || 'RR')) {
      algoSelect.value = state.algorithm || 'RR';
    }
    var qt = String(state.timeSlice || 2);
    if (quantumInput.value !== qt) {
      quantumInput.value = qt;
    }
    if (state.arrivalTimes && state.arrivalTimes.length === 3) {
      /* Don't clobber an input the user is currently editing */
      if (document.activeElement !== arriveP1 &&
          String(arriveP1.value) !== String(state.arrivalTimes[0])) {
        arriveP1.value = state.arrivalTimes[0];
      }
      if (document.activeElement !== arriveP2 &&
          String(arriveP2.value) !== String(state.arrivalTimes[1])) {
        arriveP2.value = state.arrivalTimes[1];
      }
      if (document.activeElement !== arriveP3 &&
          String(arriveP3.value) !== String(state.arrivalTimes[2])) {
        arriveP3.value = state.arrivalTimes[2];
      }
    }
  });

});
