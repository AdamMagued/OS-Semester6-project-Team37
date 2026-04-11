/**
 * scheduler.js
 * ──────────────────────────────────────────────────────────────
 * Renders the SCHEDULER panel.
 *
 * Sections:
 *   RUNNING  — large card showing PID, current instruction, and
 *              an animated time-slice bar that depletes left→right.
 *              Flashes mint on context switch.
 *   READY    — list of process cards.  Stats shown are algorithm-
 *              adaptive: RR shows waiting time, HRRN shows response
 *              ratio, MLFQ shows queue level badge.
 *   BLOCKED  — same card format, dimmed, shows blocked resource name.
 *
 * Clicking any card calls Inspector.select(pid).
 * Subscribes to state changes and re-renders automatically.
 * ──────────────────────────────────────────────────────────────
 */

var Scheduler = (function() {

  /* DOM refs */
  var runningCardEl  = document.getElementById('running-card');
  var timesliceBarEl = document.getElementById('timeslice-bar');
  var readyQueueEl   = document.getElementById('ready-queue');
  var blockedQueueEl = document.getElementById('blocked-queue');

  /* State carried between renders */
  var _lastRunningPid = null;
  var _sliceProgress  = 1.0;   /* 1.0 = full, 0.0 = empty */
  var _lastClock      = -1;

  /* ── Lookup helper ──────────────────────────────────────────── */
  function _findProc(processes, pid) {
    for (var i = 0; i < processes.length; i++) {
      if (processes[i].pid === pid) return processes[i];
    }
    return null;
  }

  /* ── Current instruction text ───────────────────────────────── */
  function _curInstr(proc) {
    if (!proc || !proc.instructions) return '\u2014';
    var instr = proc.instructions;
    if (proc.pc >= instr.length) return '(finished)';
    return instr[proc.pc] || '\u2014';
  }

  /* ── Algorithm-adaptive stats string ───────────────────────── */
  function _statsHTML(proc, algo, blockedResource) {
    if (blockedResource) {
      return '<span class="card-stat">waiting: <span class="card-resource">' +
             blockedResource + '</span></span>';
    }
    var parts = [];
    parts.push('<span class="card-stat">PC:&nbsp;<span>' + proc.pc + '</span></span>');
    if (algo === 'HRRN' && proc.responseRatio != null) {
      parts.push('<span class="card-stat">RR:&nbsp;<span>' +
                 Number(proc.responseRatio).toFixed(2) + '</span></span>');
    } else if (algo === 'MLFQ' && proc.queueLevel != null) {
      parts.push('<span class="card-stat">queue:&nbsp;<span>Q' +
                 proc.queueLevel + '</span></span>');
    } else {
      parts.push('<span class="card-stat">wt:&nbsp;<span>' +
                 (proc.waitingTime || 0) + '</span></span>');
    }
    return parts.join('');
  }

  /* ── Build a queue card element ─────────────────────────────── */
  function _buildCard(proc, algo, isBlocked, blockedResource) {
    var card = document.createElement('div');
    card.className   = 'queue-card pid-' + proc.pid + (isBlocked ? ' blocked' : '');
    card.dataset.pid = proc.pid;

    /* Left colour stripe */
    var stripe = document.createElement('div');
    stripe.className = 'queue-card-stripe';

    /* Main body */
    var body = document.createElement('div');
    body.className = 'queue-card-body';
    body.innerHTML =
      '<div class="card-pid-label">P' + proc.pid +
        '<span class="pid-state">\u00b7 ' + (proc.state || '') + '</span></div>' +
      '<div class="card-instruction">' + _curInstr(proc) + '</div>' +
      '<div class="card-stats">' + _statsHTML(proc, algo, blockedResource) + '</div>';

    card.appendChild(stripe);
    card.appendChild(body);

    /* MLFQ queue-level badge on the right */
    if (algo === 'MLFQ' && proc.queueLevel != null) {
      var badge = document.createElement('div');
      badge.className   = 'queue-level-badge';
      badge.textContent = 'Q' + proc.queueLevel;
      card.appendChild(badge);
    }

    /* Click → inspector + visual selection */
    card.addEventListener('click', function() {
      /* Remove selection from all other cards first */
      var cards = document.querySelectorAll('.queue-card.selected');
      for (var i = 0; i < cards.length; i++) {
        cards[i].classList.remove('selected');
      }
      card.classList.add('selected');
      Inspector.select(proc.pid);
    });

    return card;
  }

  /* ── RUNNING section ────────────────────────────────────────── */
  function _renderRunning(state) {
    var pid       = state.runningPid;
    var processes = state.processes || [];
    var algo      = state.algorithm;
    var timeSlice = state.timeSlice || 2;
    var clock     = state.clock;

    var proc       = pid ? _findProc(processes, pid) : null;
    var pidChanged = pid !== _lastRunningPid;

    runningCardEl.innerHTML = '';
    runningCardEl.className = '';

    if (!proc) {
      /* Nothing running */
      runningCardEl.classList.add('empty');
      runningCardEl.innerHTML = '<span class="running-idle">\u2014 idle \u2014</span>';
      timesliceBarEl.style.width = '0%';
      _lastRunningPid = null;
      return;
    }

    /* Build card content */
    var metaStr = 'PC:&nbsp;' + proc.pc + '&nbsp;&nbsp;|&nbsp;&nbsp;';
    if (algo === 'MLFQ')       metaStr += 'Q' + (proc.queueLevel || 1);
    else if (algo === 'HRRN')  metaStr += 'wt:&nbsp;' + (proc.waitingTime || 0);
    else                       metaStr += 'quantum:&nbsp;' + timeSlice;

    runningCardEl.innerHTML =
      '<div class="running-pid">P' + proc.pid + '</div>' +
      '<div class="running-instruction">' + _curInstr(proc) + '</div>' +
      '<div class="running-meta">' + metaStr + '</div>';

    /* Context switch or clock reset: flash + reset timeslice bar */
    var clockReset = (clock < _lastClock);
    if (pidChanged || clockReset) {
      if (pidChanged) {
        runningCardEl.classList.add('flash');
        runningCardEl.addEventListener('animationend', function() {
          runningCardEl.classList.remove('flash');
        }, { once: true });
      }

      /* Reset bar instantly, then re-enable transition */
      _sliceProgress = 1.0;
      timesliceBarEl.style.transition = 'none';
      timesliceBarEl.style.width = '100%';
      timesliceBarEl.classList.remove('depleted');
      requestAnimationFrame(function() {
        timesliceBarEl.style.transition = 'width 0.85s linear, background 0.3s';
      });
    } else if (clock > _lastClock && _lastClock !== -1) {
      /* Clock ticked: deplete bar by 1/quantum */
      var quantum = (algo === 'MLFQ')
        ? Math.pow(2, (proc.queueLevel || 1))
        : timeSlice;
      _sliceProgress = Math.max(0, _sliceProgress - (1 / quantum));
      if (_sliceProgress <= 0) _sliceProgress = 1.0; /* prevent permanent lockup */
      timesliceBarEl.style.width = (_sliceProgress * 100).toFixed(1) + '%';
      timesliceBarEl.classList.toggle('depleted', _sliceProgress < 0.15);
    }

    _lastRunningPid = pid;
    _lastClock      = clock;
  }

  /* ── READY queue ────────────────────────────────────────────── */
  function _renderReady(state) {
    readyQueueEl.innerHTML = '';
    var readyQueue = state.readyQueue || [];
    var processes  = state.processes || [];
    var algo       = state.algorithm;

    if (readyQueue.length === 0) {
      readyQueueEl.innerHTML = '<div class="empty-queue-msg">(empty)</div>';
      return;
    }

    readyQueue.forEach(function(pid) {
      var proc = _findProc(processes, pid);
      if (!proc) return;
      readyQueueEl.appendChild(_buildCard(proc, algo, false, null));
    });
  }

  /* ── BLOCKED queue ──────────────────────────────────────────── */
  function _renderBlocked(state) {
    blockedQueueEl.innerHTML = '';
    var blockedQueue = state.blockedQueue || [];
    var processes    = state.processes    || [];
    var algo         = state.algorithm;

    if (blockedQueue.length === 0) {
      blockedQueueEl.innerHTML = '<div class="empty-queue-msg">(empty)</div>';
      return;
    }

    blockedQueue.forEach(function(entry) {
      /* entry can be a plain pid or { pid, resource } */
      var pid      = (typeof entry === 'object') ? entry.pid      : entry;
      var resource = (typeof entry === 'object') ? entry.resource : null;
      var proc     = _findProc(processes, pid);
      if (!proc) return;
      blockedQueueEl.appendChild(_buildCard(proc, algo, true, resource));
    });
  }

  /* ── Public render ──────────────────────────────────────────── */
  function render(state) {
    _renderRunning(state);
    _renderReady(state);
    _renderBlocked(state);
  }

  subscribe(render);

  return { render: render };

})();
