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
   INITIAL / MOCK STATE
   Pre-populated so the UI looks alive on first open.
   Reflects a mid-run MLFQ simulation with 3 processes.
   ════════════════════════════════════════════════════════════════ */
var INITIAL_STATE = {
  clock:      0,
  algorithm:  'RR',
  timeSlice:  2,
  runningPid: 1,

  readyQueue:   [2],
  blockedQueue: [
    { pid: 3, resource: 'userOutput' }
  ],

  processes: [
    {
      pid:           1,
      state:         'running',
      pc:            3,          /* about to execute semSignal userInput */
      memStart:      0,
      memEnd:        8,
      queueLevel:    2,
      waitingTime:   0,
      responseRatio: null,
      vars: [
        { name: 'x', value: '1' },
        { name: 'y', value: '5' }
      ],
      instructions: [
        'semWait userInput',    /* 0 — done */
        'assign x input',       /* 1 — done */
        'assign y input',       /* 2 — done */
        'semSignal userInput',  /* 3 ◄ PC   */
        'semWait userOutput',   /* 4        */
        'printFromTo x y',      /* 5        */
        'semSignal userOutput'  /* 6        */
      ],
      heldMutexes: ['userInput']
    },
    {
      pid:           2,
      state:         'ready',
      pc:            4,          /* blocked waiting for file mutex */
      memStart:      9,
      memEnd:        16,
      queueLevel:    1,
      waitingTime:   5,
      responseRatio: 2.25,
      vars: [
        { name: 'a', value: 'output.txt' },
        { name: 'b', value: 'HelloWorld' }
      ],
      instructions: [
        'semWait userInput',    /* 0 — done */
        'assign a input',       /* 1 — done */
        'assign b input',       /* 2 — done */
        'semSignal userInput',  /* 3 — done */
        'semWait file',         /* 4 ◄ PC   */
        'writeFile a b',        /* 5        */
        'semSignal file'        /* 6        */
      ],
      heldMutexes: []
    },
    {
      pid:           3,
      state:         'blocked',
      pc:            7,
      memStart:      17,
      memEnd:        25,
      queueLevel:    3,
      waitingTime:   3,
      responseRatio: 1.50,
      vars: [
        { name: 'a', value: 'output.txt' },
        { name: 'b', value: 'HelloWorld' }
      ],
      instructions: [
        'semWait userInput',    /* 0 — done */
        'assign a input',       /* 1 — done */
        'semSignal userInput',  /* 2 — done */
        'semWait file',         /* 3 — done */
        'assign b readFile a',  /* 4 — done */
        'semSignal file',       /* 5 — done */
        'semWait userOutput',   /* 6 — done */
        'print b',              /* 7 ◄ PC   */
        'semSignal userOutput'  /* 8        */
      ],
      heldMutexes: []
    }
  ],

  /* ── 40 memory words (indices 0 – 39) ──────────────────────── */
  memory: [
    /* Process 1: PCB at 0–4, vars at 5–6, code starts at 7 */
    { index:  0, varName: 'processId',   value: '1',                   pid: 1 },
    { index:  1, varName: 'state',       value: 'running',             pid: 1 },
    { index:  2, varName: 'programCtr',  value: '3',                   pid: 1 },
    { index:  3, varName: 'lowerBound',  value: '0',                   pid: 1 },
    { index:  4, varName: 'upperBound',  value: '8',                   pid: 1 },
    { index:  5, varName: 'x',           value: '1',                   pid: 1 },
    { index:  6, varName: 'y',           value: '5',                   pid: 1 },
    { index:  7, varName: 'code[0]',     value: 'semWait userInput',   pid: 1 },
    { index:  8, varName: 'code[1]',     value: 'assign x input',      pid: 1 },
    /* Process 2: PCB at 9–13, vars at 14–15, code at 16 */
    { index:  9, varName: 'processId',   value: '2',                   pid: 2 },
    { index: 10, varName: 'state',       value: 'ready',               pid: 2 },
    { index: 11, varName: 'programCtr',  value: '4',                   pid: 2 },
    { index: 12, varName: 'lowerBound',  value: '9',                   pid: 2 },
    { index: 13, varName: 'upperBound',  value: '16',                  pid: 2 },
    { index: 14, varName: 'a',           value: 'output.txt',          pid: 2 },
    { index: 15, varName: 'b',           value: 'HelloWorld',          pid: 2 },
    { index: 16, varName: 'code[0]',     value: 'semWait userInput',   pid: 2 },
    /* Process 3: PCB at 17–21, vars at 22–23, code at 24 */
    { index: 17, varName: 'processId',   value: '3',                   pid: 3 },
    { index: 18, varName: 'state',       value: 'blocked',             pid: 3 },
    { index: 19, varName: 'programCtr',  value: '7',                   pid: 3 },
    { index: 20, varName: 'lowerBound',  value: '17',                  pid: 3 },
    { index: 21, varName: 'upperBound',  value: '25',                  pid: 3 },
    { index: 22, varName: 'a',           value: 'output.txt',          pid: 3 },
    { index: 23, varName: 'b',           value: 'HelloWorld',          pid: 3 },
    { index: 24, varName: 'code[0]',     value: 'semWait userInput',   pid: 3 },
    /* Empty words 25–39 */
    { index: 25, varName: '', value: '', pid: null },
    { index: 26, varName: '', value: '', pid: null },
    { index: 27, varName: '', value: '', pid: null },
    { index: 28, varName: '', value: '', pid: null },
    { index: 29, varName: '', value: '', pid: null },
    { index: 30, varName: '', value: '', pid: null },
    { index: 31, varName: '', value: '', pid: null },
    { index: 32, varName: '', value: '', pid: null },
    { index: 33, varName: '', value: '', pid: null },
    { index: 34, varName: '', value: '', pid: null },
    { index: 35, varName: '', value: '', pid: null },
    { index: 36, varName: '', value: '', pid: null },
    { index: 37, varName: '', value: '', pid: null },
    { index: 38, varName: '', value: '', pid: null },
    { index: 39, varName: '', value: '', pid: null }
  ],

  disk: [],

  mutexes: {
    userInput:  { locked: true,  heldBy: 1,    waitQueue: []  },
    userOutput: { locked: true,  heldBy: null, waitQueue: [3] },
    file:       { locked: false, heldBy: null, waitQueue: []  }
  },

  log: []
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
