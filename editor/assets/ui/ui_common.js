(function () {
  function clamp(v, lo, hi) {
    return Math.max(lo, Math.min(hi, v));
  }

  function safeJsonParse(s) {
    try { return JSON.parse(s); } catch (_) { return null; }
  }

  // Pending callbacks for async postMessage responses
  const pendingCallbacks = new Map();
  let callId = 0;

  // Check if we're in an iframe
  function isInIframe() {
    try {
      return window.parent && window.parent !== window;
    } catch (e) {
      return false;
    }
  }

  // Setup listener for postMessage responses (for iframes)
  if (isInIframe()) {
    window.addEventListener('message', (event) => {
      const data = event && event.data;
      if (data && data.type === 'hep_callCpp_response' && data.callId !== undefined) {
        const cb = pendingCallbacks.get(data.callId);
        if (cb) {
          pendingCallbacks.delete(data.callId);
          cb(data.response);
        }
      }
    });
  }

  function resolveHepSend() {
    if (window.hepSend) return window.hepSend;
    return null;
  }

  // Synchronous callCpp - only works in top-level view
  function callCppSync(name, payload) {
    if (window.hepSend) {
      const msg = { v: 1, name: name, payload: payload || {} };
      const msgStr = JSON.stringify(msg);
      console.warn('[callCppSync] Calling hepSend with:', msgStr.substring(0, 100));
      const respStr = window.hepSend(msgStr);
      console.warn('[callCppSync] hepSend returned type:', typeof respStr, 'value:', String(respStr).substring(0, 100));
      const obj = safeJsonParse(respStr);
      console.warn('[callCppSync] Parsed result:', obj ? 'ok' : 'null', obj ? (obj.ok ? 'response ok' : 'response not ok') : '');
      return obj || { ok: false, error: 'bad_json', raw: respStr };
    }
    return { ok: false, error: 'hepSend_not_available' };
  }

  // Async callCpp - works in both top-level and iframes
  function callCppAsync(name, payload) {
    return new Promise((resolve) => {
      // If we have direct hepSend, use it synchronously
      if (window.hepSend) {
        resolve(callCppSync(name, payload));
        return;
      }
      // If in iframe, try direct parent.hepSend fallback
      if (isInIframe()) {
        try {
          if (window.parent && window.parent.hepSend) {
            const msg = { v: 1, name: name, payload: payload || {} };
            const respStr = window.parent.hepSend(JSON.stringify(msg));
            const obj = safeJsonParse(respStr);
            resolve(obj || { ok: false, error: 'bad_json', raw: respStr });
            return;
          }
        } catch (e) {
          console.warn('parent.hepSend fallback failed:', e);
        }
        // If in iframe, use postMessage
        const id = ++callId;
        pendingCallbacks.set(id, resolve);
        window.parent.postMessage({
          type: 'hep_callCpp_request',
          callId: id,
          name: name,
          payload: payload || {}
        }, '*');
        // Timeout after 5 seconds
        setTimeout(() => {
          if (pendingCallbacks.has(id)) {
            pendingCallbacks.delete(id);
            resolve({ ok: false, error: 'timeout' });
          }
        }, 5000);
        return;
      }
      resolve({ ok: false, error: 'no_transport' });
    });
  }

  // Main callCpp function - returns sync result if possible, otherwise returns Promise
  function callCpp(name, payload) {
    // If direct hepSend is available, use sync with JSON parsing
    if (window.hepSend) {
      return callCppSync(name, payload);
    }
    // Otherwise return async promise
    return callCppAsync(name, payload);
  }

  // Wrapper that ensures JSON parsing even if native hep.callCpp returns string
  function callCppWrapper(name, payload) {
    // Check if native hep.callCpp exists (set by C++ OnDOMReady)
    if (window._nativeHepCallCpp) {
      const result = window._nativeHepCallCpp(name, payload);
      // If result is a string, parse it
      if (typeof result === 'string') {
        return safeJsonParse(result) || { ok: false, error: 'bad_json', raw: result };
      }
      return result;
    }
    // Fall back to our implementation
    return callCpp(name, payload);
  }

  function onReady(fn) {
    let attempts = 0;
    const maxAttempts = 100; // 5 seconds max
    const tick = setInterval(() => {
      attempts++;
      let ready = false;
      if (window.hepSend) {
        console.warn('[onReady] hepSend found after', attempts, 'attempts');
        ready = true;
      } else if (isInIframe()) {
        try {
          if (window.parent && window.parent.hepSend) {
            ready = true;
          } else {
            // For iframes, assume ready since we use postMessage
            ready = true;
          }
        } catch (e) {
          ready = true;
        }
      }
      if (ready || attempts >= maxAttempts) {
        if (!ready) {
          console.warn('[onReady] Timed out after', attempts, 'attempts, hepSend still not available');
        }
        clearInterval(tick);
        fn();
      }
    }, 50);
  }

  function setUiScale(scale) {
    const s = Number(scale);
    if (!Number.isFinite(s) || s <= 0) return;
    document.documentElement.style.setProperty('--uiScale', String(s));
  }

  function getAutoScaleOptions(overrides) {
    const d = (document.body && document.body.dataset) ? document.body.dataset : {};
    const baseWidth = Number(d.uiScaleBaseWidth || (overrides && overrides.baseWidth) || 520);
    const baseHeight = Number(d.uiScaleBaseHeight || (overrides && overrides.baseHeight) || 420);
    const min = Number(d.uiScaleMin || (overrides && overrides.min) || 1.0);
    const max = Number(d.uiScaleMax || (overrides && overrides.max) || 1.75);
    return {
      baseWidth: Number.isFinite(baseWidth) && baseWidth > 0 ? baseWidth : 520,
      baseHeight: Number.isFinite(baseHeight) && baseHeight > 0 ? baseHeight : 420,
      min: Number.isFinite(min) ? min : 1.0,
      max: Number.isFinite(max) ? max : 1.75,
    };
  }

  function computeAutoScale(opts) {
    const w = document.documentElement.clientWidth || window.innerWidth || 0;
    const h = document.documentElement.clientHeight || window.innerHeight || 0;
    if (!w || !h) return 1.0;
    const s = Math.min(w / opts.baseWidth, h / opts.baseHeight);
    return clamp(s, opts.min, opts.max);
  }

  // Auto-scale UI based on the panel/window size.
  // Default behavior: never scale below 1.0, scale up to a cap.
  function enableAutoScale(overrides) {
    if (window.hep && window.hep._autoScaleEnabled) return;
    const opts = getAutoScaleOptions(overrides);

    let raf = 0;
    function update() {
      raf = 0;
      setUiScale(computeAutoScale(opts));
    }
    function schedule() {
      if (raf) return;
      raf = window.requestAnimationFrame(update);
    }

    window.addEventListener('resize', schedule);
    if (window.visualViewport) window.visualViewport.addEventListener('resize', schedule);
    if (window.ResizeObserver) {
      const ro = new ResizeObserver(schedule);
      ro.observe(document.documentElement);
    }
    schedule();

    window.hep = window.hep || {};
    window.hep._autoScaleEnabled = true;
  }

  // Small helper for UE-like collapsible sections
  function wireCollapsible(sectionHeaderEl, bodyEl, storageKey) {
    const k = storageKey || null;
    let open = true;
    if (k) {
      const v = window.localStorage ? localStorage.getItem(k) : null;
      if (v === '0') open = false;
    }
    function apply() {
      bodyEl.style.display = open ? '' : 'none';
      sectionHeaderEl.style.opacity = open ? '1' : '0.85';
      if (k && window.localStorage) localStorage.setItem(k, open ? '1' : '0');
    }
    sectionHeaderEl.addEventListener('click', () => {
      open = !open;
      apply();
    });
    apply();
  }

  window.hep = window.hep || {};
  // Store our callCpp that handles JSON parsing properly
  // This is called at runtime, so hepSend should be available by then
  window.hep.callCpp = function(name, payload) {
    console.warn('[hep.callCpp] name:', name, 'hepSend available:', !!window.hepSend);
    // If hepSend available, use it (parses JSON)
    if (window.hepSend) {
      return callCppSync(name, payload);
    }
    // Check for native C++ callCpp (set by OnDOMReady)
    if (window.hep && window.hep._nativeCallCpp) {
      console.warn('[hep.callCpp] Using _nativeCallCpp fallback');
      const result = window.hep._nativeCallCpp(name, payload);
      // Native returns string, parse it
      if (typeof result === 'string') {
        const parsed = safeJsonParse(result);
        console.warn('[hep.callCpp] Parsed native result:', parsed ? 'ok' : 'null');
        return parsed || { ok: false, error: 'bad_json', raw: result };
      }
      return result;
    }
    console.warn('[hep.callCpp] No transport available, using async');
    // Otherwise try async
    return callCppAsync(name, payload);
  };
  window.hep.onReady = onReady;
  window.hep.wireCollapsible = wireCollapsible;
  window.hep.setUiScale = setUiScale;
  window.hep.enableAutoScale = enableAutoScale;

  // Default: fixed scale. Opt-in to auto scale via data-ui-scale-auto="true" on <body>.
  const d = (document.body && document.body.dataset) ? document.body.dataset : {};
  if (d.uiScaleFixed) {
    setUiScale(Number(d.uiScaleFixed));
  }

  const autoFlag = String(d.uiScaleAuto || '').toLowerCase();
  if (autoFlag === '1' || autoFlag === 'true' || autoFlag === 'yes') {
    enableAutoScale();
  }
})();
