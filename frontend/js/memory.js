/**
 * memory.js
 * ──────────────────────────────────────────────────────────────
 * Renders the MEMORY panel.
 *
 * Features:
 *   • 8×5 grid of 40 word cells, colour-coded by owning PID
 *   • Hover tooltip showing full varName and value
 *   • Clicking a cell selects its process in the Inspector
 *   • pulse(index)  — 300 ms write-flash on a specific cell
 *   • shimmer(pid)  — fade all cells of a process on swap-out
 *   • DISK list below the grid for swapped-out processes
 *
 * Subscribes to state changes and re-renders automatically.
 * ──────────────────────────────────────────────────────────────
 */

var Memory = (function() {

  /* DOM refs */
  var gridEl = document.getElementById('memory-grid');
  var diskEl = document.getElementById('disk-list');

  /* Previous cell values — used to detect writes for pulse animation */
  var _prevValues = {};

  /* PID whose cells are currently group-highlighted; null = none */
  var _highlightedPid = null;

  /* ── Helpers ────────────────────────────────────────────────── */

  function _trunc(str, n) {
    if (!str) return '';
    return str.length > n ? str.slice(0, n) + '\u2026' : str;
  }

  function _esc(str) {
    return String(str)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;');
  }

  /* ── Build a single memory cell element ─────────────────────── */
  function _buildCell(word, isStart, isEnd) {
    var idx      = word.index;
    var varName  = word.varName  || '';
    var value    = word.value    || '';
    var pid      = word.pid;
    var isEmpty  = !pid && !varName && !value;

    var cell = document.createElement('div');
    cell.className = 'mem-cell' +
      (isEmpty      ? ' empty'       : '') +
      (pid          ? ' pid-' + pid  : '');
    cell.dataset.index = idx;
    if (pid) cell.dataset.pid = pid;

    /* Index badge */
    var idxEl = document.createElement('div');
    idxEl.className   = 'mem-index';
    idxEl.textContent = idx;

    /* Variable name — truncated to 15 chars */
    var nameEl = document.createElement('div');
    nameEl.className   = 'mem-varname';
    nameEl.textContent = _trunc(varName, 15);

    /* Value (truncated) */
    var valEl = document.createElement('div');
    valEl.className   = 'mem-value';
    valEl.textContent = isEmpty ? '\u2014' : _trunc(value, 24);

    /* Process Range Label */
    if (isStart && pid) {
      var label = document.createElement('div');
      label.className = 'mem-range-label range-start';
      label.textContent = 'P' + pid;
      cell.appendChild(label);
    }

    /* Hover tooltip with full content */
    var tip = document.createElement('div');
    tip.className = 'mem-tooltip';
    tip.innerHTML =
      '<div class="tt-row"><span class="tt-key">idx&nbsp;</span><span class="tt-value">' + idx + '</span></div>' +
      (pid     ? '<div class="tt-row"><span class="tt-key">pid&nbsp;</span><span class="tt-value">P' + pid + '</span></div>' : '') +
      (varName ? '<div class="tt-row"><span class="tt-key">var&nbsp;</span><span class="tt-value">' + _esc(varName) + '</span></div>' : '') +
      (value   ? '<div class="tt-row"><span class="tt-key">val&nbsp;</span><span class="tt-value">' + _esc(value)   + '</span></div>' : '');

    cell.appendChild(idxEl);
    cell.appendChild(nameEl);
    cell.appendChild(valEl);
    cell.appendChild(tip);

    /* Click: highlight all cells of this process, select in Inspector */
    if (pid) {
      cell.addEventListener('click', function() {
        /* Remove old group-highlight */
        if (_highlightedPid !== null) {
          gridEl.querySelectorAll('.pid-selected').forEach(function(el) {
            el.classList.remove('pid-selected');
          });
        }
        /* Toggle: clicking the same pid again deselects */
        if (_highlightedPid === pid) {
          _highlightedPid = null;
          Inspector.select(null);
          return;
        }
        /* Apply new group-highlight */
        _highlightedPid = pid;
        gridEl.querySelectorAll('[data-pid="' + pid + '"]').forEach(function(el) {
          el.classList.add('pid-selected');
        });
        Inspector.select(pid);
      });
    }

    return cell;
  }

  /* ── Build a disk entry row ─────────────────────────────────── */
  function _buildDiskEntry(entry) {
    var div = document.createElement('div');
    div.className = 'disk-entry';
    div.innerHTML =
      '<span class="disk-pid">P' + entry.pid + '</span>' +
      '<span class="disk-data">' + _esc(_trunc(entry.data || '(swapped)', 44)) + '</span>';
    return div;
  }

  /* ── Public render ──────────────────────────────────────────── */
  function render(state) {
    var memory = state.memory || [];
    var disk   = state.disk   || [];

    /* ── Rebuild memory grid ──────────────────────────────────── */
    gridEl.innerHTML = '';
    
    /* Map of PID -> {start, end} to detect boundaries */
    var bounds = {};
    (state.processes || []).forEach(function(p) {
      if (p.state !== 'finished' && p.memStart !== undefined) {
        bounds[p.pid] = { start: p.memStart, end: p.memEnd };
      }
    });

    memory.forEach(function(word) {
      var pid = word.pid;
      var isStart = (pid && bounds[pid] && bounds[pid].start === word.index);
      var isEnd   = (pid && bounds[pid] && bounds[pid].end   === word.index);
      
      var cell = _buildCell(word, isStart, isEnd);

      /* Pulse if value changed since last render */
      var prev = _prevValues[word.index];
      var cur  = word.value || '';
      if (cur && cur !== prev && prev !== undefined) {
        /* Use requestAnimationFrame so the element is in the DOM first */
        (function(c) {
          requestAnimationFrame(function() {
            c.classList.add('pulse-write');
            c.addEventListener('animationend', function() {
              c.classList.remove('pulse-write');
            }, { once: true });
          });
        }(cell));
      }
      _prevValues[word.index] = cur;

      gridEl.appendChild(cell);
    });

    /* Re-apply group-highlight after cells are rebuilt */
    if (_highlightedPid !== null) {
      gridEl.querySelectorAll('[data-pid="' + _highlightedPid + '"]').forEach(function(el) {
        el.classList.add('pid-selected');
      });
    }

    /* ── Rebuild disk list ────────────────────────────────────── */
    diskEl.innerHTML = '';
    if (disk.length === 0) {
      var empty = document.createElement('div');
      empty.className   = 'disk-empty';
      empty.textContent = '(empty)';
      diskEl.appendChild(empty);
    } else {
      disk.forEach(function(entry) {
        diskEl.appendChild(_buildDiskEntry(entry));
      });
    }
  }

  /* ── Animation helpers (callable externally) ────────────────── */

  /** Flash a single cell to indicate a variable write. */
  function pulse(index) {
    var cell = gridEl.querySelector('[data-index="' + index + '"]');
    if (!cell) return;
    cell.classList.remove('pulse-write');
    void cell.offsetWidth; /* force reflow to restart animation */
    cell.classList.add('pulse-write');
    cell.addEventListener('animationend', function() {
      cell.classList.remove('pulse-write');
    }, { once: true });
  }

  /** Shimmer-fade all cells belonging to `pid` (swap-out effect). */
  function shimmer(pid) {
    var cells = gridEl.querySelectorAll('[data-pid="' + pid + '"]');
    cells.forEach(function(cell) {
      cell.classList.remove('shimmer');
      void cell.offsetWidth;
      cell.classList.add('shimmer');
      cell.addEventListener('animationend', function() {
        cell.classList.remove('shimmer');
      }, { once: true });
    });
  }

  /* Subscribe so this module re-renders on every setState call */
  subscribe(render);

  return { render: render, pulse: pulse, shimmer: shimmer };

})();
