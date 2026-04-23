/**
 * state.js
 * ──────────────────────────────────────────────────────────────
 * Single source of truth for the entire simulation.
 *
 * API:
 *   getState()        → current state snapshot (plain object)
 *   setState(partial) → shallow-merge partial into state, notify all subscribers
 *   subscribe(fn)     → register fn(state) called on every setState
 *   resetState()      → restore INITIAL_STATE and notify
 *
 * All render modules call subscribe() on load and re-render
 * whenever state changes.
 * ──────────────────────────────────────────────────────────────
 */

/* ════════════════════════════════════════════════════════════════
   INITIAL STATE
   Clean empty state used until the C backend responds.
   ════════════════════════════════════════════════════════════════ */
var INITIAL_STATE = {
  clock:        0,
  algorithm:    'RR',
  timeSlice:    2,
  runningPid:   null,
  needsInput:   false,
  needsInputPid: null,

  readyQueue:   [],
  blockedQueue: [],
  processes:    [],

  memory: (function() {
    var m = [];
    for (var i = 0; i < 40; i++)
      m.push({ index: i, varName: '', value: '', pid: null });
    return m;
  }()),

  disk:    [],
  mutexes: {},
  log:     []
};

/* ════════════════════════════════════════════════════════════════
   Internal state + subscriber registry
   ════════════════════════════════════════════════════════════════ */
var _state       = _deepClone(INITIAL_STATE);
var _subscribers = [];

function _deepClone(obj) {
  return JSON.parse(JSON.stringify(obj));
}

function _notify() {
  for (var i = 0; i < _subscribers.length; i++) {
    _subscribers[i](_state);
  }
}

/* ════════════════════════════════════════════════════════════════
   Public API
   ════════════════════════════════════════════════════════════════ */

/** Return the current state (read-only by convention). */
function getState() {
  return _state;
}

/**
 * Merge `partial` into the current state and notify all subscribers.
 * Top-level keys from `partial` overwrite existing keys.
 *
 * Example:
 *   setState({ clock: 5, runningPid: 2 });
 */
function setState(partial) {
  _state = Object.assign({}, _state, partial);
  _notify();
}

/** Register a callback invoked with the new state on every setState call. */
function subscribe(fn) {
  _subscribers.push(fn);
}

/** Restore factory defaults and re-render all panels. */
function resetState() {
  _state = _deepClone(INITIAL_STATE);
  _notify();
}
