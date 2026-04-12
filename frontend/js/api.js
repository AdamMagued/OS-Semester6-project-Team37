/**
 * api.js
 * ──────────────────────────────────────────────────────────────
 * Backend communication layer for the C HTTP server at :8080.
 * Gracefully degrades when the backend is offline — the UI
 * continues working with the local mock state in state.js.
 *
 * Exposes window.SimOS for external control:
 *   SimOS.step()             advance one clock cycle
 *   SimOS.run()              begin continuous polling
 *   SimOS.pause()            stop continuous polling
 *   SimOS.reset()            reset simulation
 *   SimOS.loadState(obj)     load a raw state object
 *   SimOS.setSpeed(n)        set polling speed 1–10
 *   SimOS.isRunning          boolean getter
 * ──────────────────────────────────────────────────────────────
 */

var API_BASE = 'http://localhost:8080/api';

/* Active interval handle for RUN mode polling */
var _pollHandle    = null;
var _isRunning     = false;
var _speed         = 1;       /* 1–10  multiplier */
var _stepPending   = false;   /* guard against re-entrance */
var _pendingInput  = null;    /* value queued by submitInput(), sent with next step */
var _needsInput    = false;   /* true when backend says input is required */
var _needsInputPid = null;    /* PID that needs the input */

/* Callbacks set by main.js to control the input modal */
var _onNeedsInput  = null;    /* called with (pid) when input is needed */
var _onInputDone   = null;    /* called when input is no longer needed */

/**
 * Offline fallback step: increment clock, advance PC of running process,
 * rotate ready queue when timeslice expires, and append a log entry.
 */
function _offlineStep() {
  var s          = getState();
  var clock      = (s.clock || 0) + 1;
  var processes  = JSON.parse(JSON.stringify(s.processes || []));
  var readyQueue = (s.readyQueue || []).slice();
  var runningPid = s.runningPid;
  var timeSlice  = s.timeSlice || 2;
  var logArr     = (s.log || []).slice();
  var event      = '';

  /* Find running process and advance its PC */
  var runProc = null;
  for (var i = 0; i < processes.length; i++) {
    if (processes[i].pid === runningPid) { runProc = processes[i]; break; }
  }

  if (runProc) {
    runProc.pc = (runProc.pc || 0) + 1;
    var instr = (runProc.instructions || [])[runProc.pc - 1] || '(step)';
    event = 'P' + runProc.pid + ' executed: ' + instr;

    /* Check if quantum expired — rotate ready queue */
    if (clock % timeSlice === 0 && readyQueue.length > 0) {
      runProc.state = 'ready';
      readyQueue.push(runProc.pid);
      var nextPid = readyQueue.shift();
      runningPid = nextPid;
      for (var j = 0; j < processes.length; j++) {
        if (processes[j].pid === nextPid) { processes[j].state = 'running'; break; }
      }
      event += ' — quantum expired, context switch → P' + nextPid;
    }
  } else {
    /* No running process — try to pick one from ready */
    if (readyQueue.length > 0) {
      runningPid = readyQueue.shift();
      for (var k = 0; k < processes.length; k++) {
        if (processes[k].pid === runningPid) { processes[k].state = 'running'; break; }
      }
      event = 'P' + runningPid + ' scheduled → RUNNING';
    } else {
      event = 'idle — no processes ready';
    }
  }

  logArr.push({ clock: clock, pid: runningPid, event: event, type: 'scheduling' });

  setState({
    clock:      clock,
    processes:  processes,
    readyQueue: readyQueue,
    runningPid: runningPid,
    log:        logArr
  });
}

/* ── HTTP helpers ─────────────────────────────────────────────── */

/**
 * GET /api/state — fetch full simulation state from the C backend.
 * On success: calls setState(data) to update all subscribers.
 * On failure: logs a warning, leaves current state untouched.
 */
function fetchState() {
  return fetch(API_BASE + '/state')
    .then(function(res) {
      if (!res.ok) throw new Error('HTTP ' + res.status);
      return res.json();
    })
    .then(function(data) {
      setState(data);
      /* Check if backend needs input */
      if (data.needsInput && !_needsInput) {
        _needsInput    = true;
        _needsInputPid = data.needsInputPid;
        if (_isRunning) { SimOS.pause(); }
        if (_onNeedsInput) _onNeedsInput(data.needsInputPid);
      }
      return data;
    })
    .catch(function(e) {
      console.warn('[SimOS api] fetchState failed:', e.message);
      return null;
    });
}

/**
 * POST /api/<cmd> with optional JSON payload.
 * Returns a Promise resolving to the response JSON, or null on error.
 */
function _sendCommand(cmd, payload) {
  return fetch(API_BASE + '/' + cmd, {
    method:  'POST',
    headers: { 'Content-Type': 'application/json' },
    body:    JSON.stringify(payload || {})
  })
    .then(function(res) {
      if (!res.ok) throw new Error('HTTP ' + res.status);
      return res.json();
    })
    .catch(function(e) {
      console.warn('[SimOS api] command "' + cmd + '" failed:', e.message);
      return null;
    });
}

/* ── Polling ──────────────────────────────────────────────────── */

function _startPolling() {
  if (_pollHandle) return;
  /* Base interval 1 000 ms ÷ speed — faster speed = shorter interval */
  var interval = Math.round(1000 / _speed);
  _pollHandle = setInterval(function() { SimOS.step(); }, interval);
}

function _stopPolling() {
  if (_pollHandle) {
    clearInterval(_pollHandle);
    _pollHandle = null;
  }
}

/* ════════════════════════════════════════════════════════════════
   window.SimOS — public control interface
   ════════════════════════════════════════════════════════════════ */
window.SimOS = {

  /**
   * Advance by one clock cycle.
   * Tries the backend first; falls back to a local mock step
   * so the UI stays responsive during offline development.
   */
  step: function() {
    if (_stepPending || _needsInput) return Promise.resolve();
    _stepPending = true;

    /* Attach pending input value if the user just submitted one */
    var payload = {};
    if (_pendingInput !== null) {
      payload.input = _pendingInput;
      _pendingInput = null;
    }

    return _sendCommand('step', payload).then(function(result) {
      _stepPending = false;
      if (result) {
        setState(result);
        /* Check if backend now needs input */
        if (result.needsInput) {
          _needsInput    = true;
          _needsInputPid = result.needsInputPid;
          /* Auto-pause RUN mode so user can type */
          if (_isRunning) { SimOS.pause(); }
          if (_onNeedsInput) _onNeedsInput(result.needsInputPid);
        } else if (_needsInput) {
          _needsInput    = false;
          _needsInputPid = null;
          if (_onInputDone) _onInputDone();
        }
      } else {
        /* Offline fallback: rotate ready queue, advance PC, log it */
        _offlineStep();
      }
    }).catch(function() { _stepPending = false; });
  },

  /** Start continuous simulation at the current speed. */
  run: function() {
    if (_isRunning) return;
    _isRunning = true;
    _startPolling();
  },

  /** Pause continuous simulation without resetting. */
  pause: function() {
    _isRunning = false;
    _stopPolling();
  },

  /** Reset simulation to initial state (calls backend then resetState). */
  reset: function() {
    _stopPolling();
    _isRunning     = false;
    _needsInput    = false;
    _needsInputPid = null;
    _pendingInput  = null;
    if (_onInputDone) _onInputDone();
    return _sendCommand('reset').then(function(result) {
      if (result) {
        setState(result);
      } else {
        resetState();
      }
      return result;
    });
  },

  /**
   * Directly inject a state object (e.g. pasted JSON from the modal).
   * Validates that the object is non-null before applying.
   */
  loadState: function(newState) {
    if (!newState || typeof newState !== 'object') {
      console.error('[SimOS] loadState: argument must be a plain object');
      return;
    }
    setState(newState);
  },

  /**
   * Update polling speed (1 = 1×/s … 10 = 10×/s).
   * Restarts the interval if currently running.
   */
  setSpeed: function(v) {
    _speed = Math.max(1, Math.min(10, v | 0));
    if (_isRunning) {
      _stopPolling();
      _startPolling();
    }
  },

  /**
   * Submit an input value in response to a needsInput prompt.
   * The value is sent with the very next step() call.
   */
  submitInput: function(value) {
    _pendingInput  = value;
    _needsInput    = false;
    _needsInputPid = null;
    if (_onInputDone) _onInputDone();
    /* Immediately step to consume the input */
    return SimOS.step();
  },

  /** Register callbacks for the input-needed flow. */
  onNeedsInput: function(fn) { _onNeedsInput = fn; },
  onInputDone:  function(fn) { _onInputDone  = fn; },

  get isRunning()   { return _isRunning; },
  get needsInput()  { return _needsInput; },
  get needsInputPid() { return _needsInputPid; }
};
