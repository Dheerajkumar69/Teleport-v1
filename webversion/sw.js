/**
 * Teleport ServiceWorker - Offline Support
 * Caches app shell for offline functionality
 */

const CACHE_NAME = 'teleport-v4'; // Bumped: TURN fix, sanitizer, resume, peer-joined tracking
const STATIC_ASSETS = [
    '/app.html',
    '/teleport-webrtc.js',
    '/app-lovable.js',
    '/streamsaver.min.js',
    '/assets/favicon.svg'
];

// Install - cache static assets
self.addEventListener('install', (event) => {
    event.waitUntil(
        caches.open(CACHE_NAME)
            .then((cache) => {
                console.log('🚀 Teleport SW: Caching app shell');
                return cache.addAll(STATIC_ASSETS);
            })
            .then(() => self.skipWaiting())
    );
});

// Activate - clean up old caches
self.addEventListener('activate', (event) => {
    event.waitUntil(
        caches.keys().then((cacheNames) => {
            return Promise.all(
                cacheNames
                    .filter((name) => name !== CACHE_NAME)
                    .map((name) => caches.delete(name))
            );
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
                                    .then((cache) => cache.put(event.request, response));
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
            .catch(() => {
                // Return offline page for navigation requests
                if (event.request.mode === 'navigate') {
                    return caches.match('/app.html');
                }
            })
    );
});

// Handle messages from main thread
self.addEventListener('message', (event) => {
    if (event.data === 'skipWaiting') {
        self.skipWaiting();
    }
});

console.log('🚀 Teleport ServiceWorker loaded');
