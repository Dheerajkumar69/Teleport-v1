/**
 * Teleport ServiceWorker - Offline Support + Update Detection
 * Caches app shell for offline functionality
 * Notifies clients when a new version is available.
 */

const CACHE_NAME = 'teleport-v6';
// NOTE: Bump CACHE_NAME above when releasing a new version.
// index.html reads this value via a regex so all files stay in sync.
const APP_SCOPE = self.registration.scope;
const ROOT_URL = new URL('./', APP_SCOPE).toString();
const OFFLINE_URL = new URL('index.html', APP_SCOPE).toString();
const STATIC_ASSETS = [
    ROOT_URL,
    OFFLINE_URL,
    new URL('version.js', APP_SCOPE).toString(),
    new URL('teleport-webrtc.js', APP_SCOPE).toString(),
    new URL('app-lovable.js', APP_SCOPE).toString(),
    new URL('streamsaver.min.js', APP_SCOPE).toString(),
    new URL('assets/favicon.svg', APP_SCOPE).toString()
];

async function cacheAppShell() {
    const cache = await caches.open(CACHE_NAME);
    const results = await Promise.allSettled(
        STATIC_ASSETS.map((assetUrl) => {
            return cache.add(new Request(assetUrl, { cache: 'reload' }));
        })
    );

    const failedCount = results.filter(r => r.status === 'rejected').length;
    if (failedCount > 0) {
        console.warn(`[Teleport SW] ${failedCount} app-shell asset(s) failed to cache`);
    }
}

// Install - cache static assets
self.addEventListener('install', (event) => {
    event.waitUntil(
        cacheAppShell().then(() => self.skipWaiting())
    );
});

// Activate - clean up old caches and notify clients of update
self.addEventListener('activate', (event) => {
    event.waitUntil(
        caches.keys().then((cacheNames) => {
            return Promise.all(
                cacheNames
                    .filter((name) => name !== CACHE_NAME)
                    .map((name) => caches.delete(name))
            );
        }).then(() => {
            // Notify all open clients that a new version is ready
            return self.clients.matchAll().then((clients) => {
                clients.forEach((client) => client.postMessage({ type: 'SW_UPDATED', version: CACHE_NAME }));
            });
        }).then(() => self.clients.claim())
    );
});

// Fetch - serve from cache, fallback to network
self.addEventListener('fetch', (event) => {
    // Skip non-GET requests
    if (event.request.method !== 'GET') return;

    // Skip WebSocket requests
    if (event.request.url.includes('ws://') || event.request.url.includes('wss://')) return;

    // Skip external requests (fonts, etc.)
    if (!event.request.url.startsWith(self.location.origin)) return;

    event.respondWith(
        caches.match(event.request)
            .then((cachedResponse) => {
                // Return cached response if available
                if (cachedResponse) {
                    // Fetch in background to update cache
                    fetch(event.request)
                        .then((response) => {
                            if (response && response.status === 200) {
                                caches.open(CACHE_NAME)
                                    .then((cache) => cache.put(event.request, response.clone()));
                            }
                        })
                        .catch(() => { });

                    return cachedResponse;
                }

                // Otherwise fetch from network and cache
                return fetch(event.request)
                    .then((response) => {
                        if (!response || response.status !== 200) {
                            return response;
                        }

                        const responseToCache = response.clone();
                        caches.open(CACHE_NAME)
                            .then((cache) => cache.put(event.request, responseToCache));

                        return response;
                    });
            })
            .catch(async () => {
                // Return offline page for navigation requests
                if (event.request.mode === 'navigate') {
                    return (await caches.match(OFFLINE_URL)) || (await caches.match(ROOT_URL));
                }

                return new Response(null, { status: 503, statusText: 'Service Unavailable' });
            })
    );
});

// Handle messages from main thread
self.addEventListener('message', (event) => {
    if (event.data === 'skipWaiting') {
        self.skipWaiting();
    }
});

console.log('Teleport ServiceWorker ' + CACHE_NAME + ' loaded');
