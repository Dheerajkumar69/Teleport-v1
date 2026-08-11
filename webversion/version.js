/**
 * Teleport — Version bridge.
 * The canonical version lives in sw.js (CACHE_NAME).
 * This file fetches sw.js, extracts the version, and exposes it as
 * window.TELEPORT_VERSION so app code can reference it.
 *
 * If the fetch fails (e.g. offline first load), falls back to 'v0'.
 */
(function () {
    'use strict';
    if (window.TELEPORT_VERSION) return; // already set
    fetch('sw.js', { cache: 'no-store' })
        .then(function (r) { return r.text(); })
        .then(function (src) {
            var m = src.match(/CACHE_NAME\s*=\s*'([^']+)'/);
            window.TELEPORT_VERSION = m ? m[1] : 'v0';
        })
        .catch(function () {
            window.TELEPORT_VERSION = 'v0';
        });
})();
