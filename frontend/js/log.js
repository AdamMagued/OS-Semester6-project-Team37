/**
 * log.js
 * ──────────────────────────────────────────────────────────────
 * Renders the LOG panel: scrollable terminal-style event feed.
 *
 * Features:
 *   • Entries formatted as: [CLOCK XX]   [PID X]   event text
 *   • Type-based colouring: scheduling (mint), blocking (amber),
 *     memory-swap (blue), execution (gray), finish (white)
 *   • New entries slide in from the left
 *   • Auto-scrolls to the latest entry
 *   • Clock cycle badge in the top-right corner
 *   • Incremental append — only new log entries are rendered,
 *     preserving scroll position between state updates
 *
 * API:
 *   Log.render(state)   — incremental re-render on state change
 *   Log.append(entry)   — manually push a single entry
 * ──────────────────────────────────────────────────────────────
 */

var Log = (function() {

  var entriesEl = document.getElementById('log-entries');
  var clockEl   = document.getElementById('clock-display');

  /* Length of the log array at the last render — used to detect additions */
  var _renderedCount = 0;
  var _lastLogRef    = null; /* identity check for loadState() replacement */

  /* ── Build a single log entry element ───────────────────────── */
  function _buildEntry(entry, isNew) {
    var div = document.createElement('div');
    div.className = 'log-entry ' + (entry.type || 'execution') +
                    (isNew ? ' slide-in' : '');

    /* Zero-pad clock number */
    var clk = entry.clock != null ? String(entry.clock) : '?';
    if (clk.length < 2) clk = '0' + clk;

    var clockStr = '[CLOCK ' + clk + ']';
    var pidStr   = entry.pid ? '[PID ' + entry.pid + ']' : '[SYS ]';
    var eventStr = entry.event || '';

    div.innerHTML =
      '<span class="log-clock">' + clockStr + '</span>' +
      '<span class="log-pid">&nbsp;&nbsp;' + pidStr + '</span>' +
      '<span class="log-event">&nbsp;&nbsp;' + eventStr + '</span>';

    return div;
  }

  /* ── Scroll helper ──────────────────────────────────────────── */
  function _scrollBottom() {
    requestAnimationFrame(function() {
      entriesEl.scrollTop = entriesEl.scrollHeight;
    });
  }

  /* ── Public: render ─────────────────────────────────────────── */
  function render(state) {
    var log   = state.log   || [];
    var clock = state.clock != null ? state.clock : 0;

    /* Update the clock badge */
    if (clockEl) clockEl.textContent = clock;

    /* Detect full replacement (loadState / reset) by identity or shrink */
    var replaced = (log !== _lastLogRef && _lastLogRef !== null) ||
                   (log.length < _renderedCount);
    _lastLogRef = log;

    if (replaced) {
      /* Full rebuild */
      entriesEl.innerHTML = '';
      log.forEach(function(entry) {
        entriesEl.appendChild(_buildEntry(entry, false));
      });
      _scrollBottom();
    } else if (log.length > _renderedCount) {
      /* Append only the new tail — avoids full repaint + keeps scroll */
      for (var i = _renderedCount; i < log.length; i++) {
        entriesEl.appendChild(_buildEntry(log[i], true /* isNew */));
      }
      _scrollBottom();
    }
    /* log.length === _renderedCount && same ref → nothing new, skip DOM work */

    _renderedCount = log.length;
  }

  /* ── Public: append ─────────────────────────────────────────── */
  /**
   * Manually push a single entry without a full state update.
   * Useful for client-side events (e.g., offline step fallback).
   */
  function append(entry) {
    entriesEl.appendChild(_buildEntry(entry, true));
    _scrollBottom();
    _renderedCount++;
  }

  subscribe(render);

  return { render: render, append: append };

})();
