/**
 * inspector.js
 * ──────────────────────────────────────────────────────────────
 * Renders the PROCESS INSPECTOR panel for the selected process.
 *
 * Sections rendered:
 *   PCB          — processId, state (coloured badge), PC, memory range
 *   Variables    — name → value grid
 *   Instructions — full list with line numbers, done/current/pending
 *                  states, and ▶ pointer at current PC
 *   Mutexes      — held mutex badges + global mutex state table
 *
 * API:
 *   Inspector.select(pid)   — select a process and re-render
 *   Inspector.render(state) — re-render with updated state
 *
 * Subscribes to state changes so the inspector stays live
 * while a process is selected.
 * ──────────────────────────────────────────────────────────────
 */

var Inspector = (function() {

  var bodyEl = document.getElementById('inspector-body');

  /* Currently inspected PID; null means "no selection" */
  var _selectedPid = null;

  /* ── Helpers ────────────────────────────────────────────────── */
  function _esc(str) {
    return String(str)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;');
  }

  function _stateBadge(stateStr) {
    var cls = {
      running:  'badge--running',
      ready:    'badge--ready',
      blocked:  'badge--blocked',
      finished: 'badge--finished'
    }[stateStr] || 'badge--ready';
    return '<span class="badge ' + cls + '">' + _esc(stateStr) + '</span>';
  }

  /* ── Section: PCB ───────────────────────────────────────────── */
  function _buildPCB(proc) {
    return '<div class="insp-section">' +
      '<div class="insp-section-label">PCB</div>' +
      '<div class="insp-row">' +
        '<span class="insp-key">PID</span>' +
        '<span class="insp-val">' + proc.pid + '</span>' +
      '</div>' +
      '<div class="insp-row">' +
        '<span class="insp-key">State</span>' +
        '<span class="insp-val">' + _stateBadge(proc.state) + '</span>' +
      '</div>' +
      '<div class="insp-row">' +
        '<span class="insp-key">Program Counter</span>' +
        '<span class="insp-val">' + proc.pc + '</span>' +
      '</div>' +
      '<div class="insp-row">' +
        '<span class="insp-key">Memory</span>' +
        '<span class="insp-val">[' + proc.memStart + ' \u2192 ' + proc.memEnd + ']</span>' +
      '</div>' +
    '</div>';
  }

  /* ── Section: Variables ─────────────────────────────────────── */
  function _buildVars(proc) {
    var vars = proc.vars || [];
    var rows = '';
    if (vars.length === 0) {
      rows = '<span style="color:var(--text-dim);font-style:italic">(none)</span>';
    } else {
      vars.forEach(function(v) {
        rows +=
          '<div class="var-name">' + _esc(v.name)  + '</div>' +
          '<div class="var-value">' + _esc(v.value) + '</div>';
      });
    }
    return '<div class="insp-section">' +
      '<div class="insp-section-label">Variables</div>' +
      '<div class="var-table">' + rows + '</div>' +
    '</div>';
  }

  /* ── Section: Instructions ──────────────────────────────────── */
  function _buildInstructions(proc) {
    var instrs = proc.instructions || [];
    var lines  = '';

    if (instrs.length === 0) {
      lines = '<div class="instr-line"><span class="instr-text" ' +
              'style="color:var(--border)">(no instructions)</span></div>';
    } else {
      instrs.forEach(function(line, i) {
        var isCurrent = (i === proc.pc);
        var isDone    = (i  <  proc.pc);
        var cls = 'instr-line' +
          (isCurrent ? ' current' : '') +
          (isDone    ? ' done'    : '');
        lines +=
          '<div class="' + cls + '">' +
            '<span class="instr-num">' + i + '</span>' +
            '<span class="instr-ptr">' + (isCurrent ? '\u25b6' : '') + '</span>' +
            '<span class="instr-text">' + _esc(line) + '</span>' +
          '</div>';
      });
    }

    return '<div class="insp-section">' +
      '<div class="insp-section-label">Instructions</div>' +
      '<div class="instr-list">' + lines + '</div>' +
    '</div>';
  }

  /* ── Section: Mutexes ───────────────────────────────────────── */
  function _buildMutexes(proc, mutexes) {
    var held   = proc.heldMutexes || [];
    var badges = '';

    if (held.length === 0) {
      badges = '<span class="mutex-badge none">none</span>';
    } else {
      held.forEach(function(m) {
        badges += '<span class="mutex-badge">' + _esc(m) + '</span>';
      });
    }

    /* Global mutex state table */
    var statusRows = '';
    if (mutexes) {
      Object.keys(mutexes).forEach(function(name) {
        var mx     = mutexes[name];
        var status, cls;
        if (!mx.locked) {
          status = 'free';
          cls    = 'mx-state mx-free';
        } else {
          status = mx.heldBy ? 'held by P' + mx.heldBy : 'locked';
          cls    = 'mx-state mx-held';
        }
        var waitStr = (mx.waitQueue && mx.waitQueue.length > 0)
          ? ' (waiting: ' + mx.waitQueue.map(function(p){ return 'P'+p; }).join(', ') + ')'
          : '';
        statusRows +=
          '<div class="mutex-status-row">' +
            '<span class="mx-name">' + _esc(name) + '</span>' +
            '<span class="' + cls + '">' + status + waitStr + '</span>' +
          '</div>';
      });
    }

    return '<div class="insp-section">' +
      '<div class="insp-section-label">Held Mutexes</div>' +
      '<div class="mutex-list">' + badges + '</div>' +
      (statusRows
        ? '<div class="insp-section-label" style="margin-top:8px">Mutex State</div>' +
          '<div class="mutex-status-table">' + statusRows + '</div>'
        : '') +
    '</div>';
  }

  /* ── Section: Output Console ────────────────────────────────── */
  function _buildOutput(proc, state) {
    var logs = state.log || [];
    var outputLines = '';
    
    logs.forEach(function(entry) {
      if (entry.pid === proc.pid && entry.type === 'output') {
        var eventStr = entry.event || '';
        // Extract just the output text after "P#: " if present
        var match = eventStr.match(/^P\d+:\s*(.*)$/);
        if (match) {
          eventStr = match[1];
        }
        outputLines += _esc(eventStr) + '\n';
      }
    });

    if (outputLines === '') {
      outputLines = '<span style="color:var(--text-dim);font-style:italic">(no output)</span>';
    }

    return '<div class="insp-section">' +
      '<div class="insp-section-label">Output Console</div>' +
      '<div class="output-console">' + outputLines + '</div>' +
    '</div>';
  }

  /* ── Public: render ─────────────────────────────────────────── */
  /* Called by subscribe() on every setState — keeps the panel live. */
  function render(state) {
    if (_selectedPid === null) {
      bodyEl.innerHTML = '<div id="inspector-empty">click a process to inspect</div>';
      return;
    }

    var processes = state.processes || [];
    var proc = null;
    for (var i = 0; i < processes.length; i++) {
      if (processes[i].pid === _selectedPid) { proc = processes[i]; break; }
    }

    if (!proc) {
      bodyEl.innerHTML =
        '<div id="inspector-empty">P' + _selectedPid + ' not found in state</div>';
      return;
    }

    bodyEl.innerHTML =
      _buildPCB(proc) +
      _buildVars(proc) +
      _buildInstructions(proc) +
      _buildMutexes(proc, state.mutexes) +
      _buildOutput(proc, state);
  }

  /* ── Public: select ─────────────────────────────────────────── */
  /**
   * Select a process by PID and immediately re-render the panel.
   * Pass null (or nothing) to deselect and show the placeholder.
   *
   * Always forces a re-render so the panel stays current even when
   * selecting the same PID after a state change.
   */
  function select(pid) {
    /* Accept numeric pid or null/undefined for deselect */
    _selectedPid = (pid != null) ? pid : null;

    /* Force immediate re-render with current state */
    var state = getState();
    if (_selectedPid === null) {
      bodyEl.innerHTML = '<div id="inspector-empty">click a process to inspect</div>';
    } else {
      var proc = null;
      var processes = state.processes || [];
      for (var i = 0; i < processes.length; i++) {
        if (processes[i].pid === _selectedPid) { proc = processes[i]; break; }
      }
      if (proc) {
        bodyEl.innerHTML =
          _buildPCB(proc) +
          _buildVars(proc) +
          _buildInstructions(proc) +
          _buildMutexes(proc, state.mutexes) +
          _buildOutput(proc, state);
      } else {
        bodyEl.innerHTML =
          '<div id="inspector-empty">P' + _selectedPid + ' not found in state</div>';
      }
    }

    /* Sync selected highlight on all queue cards */
    var cards = document.querySelectorAll('.queue-card');
    for (var i = 0; i < cards.length; i++) {
      var cardPid = parseInt(cards[i].dataset.pid, 10);
      cards[i].classList.toggle('selected', cardPid === _selectedPid);
    }
  }

  subscribe(render);

  return { render: render, select: select };

})();
