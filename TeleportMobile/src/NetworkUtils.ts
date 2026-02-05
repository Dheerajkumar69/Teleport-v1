/**
 * Network Utilities - Timeout, Retry, and Offline Detection
 * Production-grade utilities for robust network operations
 */
import NetInfo from '@react-native-community/netinfo';

// Default timeout for network operations (30 seconds)
const DEFAULT_TIMEOUT_MS = 30000;

// Retry configuration
const MAX_RETRIES = 3;
const BASE_DELAY_MS = 1000;
const MAX_DELAY_MS = 30000;

/**
 * Wraps a promise with a timeout. Rejects if the promise doesn't resolve in time.
 * @param promise The promise to wrap
 * @param timeoutMs Timeout in milliseconds (default 30s)
 * @param errorMessage Custom error message on timeout
 */
export function withTimeout<T>(
    promise: Promise<T>,
    timeoutMs: number = DEFAULT_TIMEOUT_MS,
    errorMessage: string = 'Operation timed out'
): Promise<T> {
    let timeoutId: NodeJS.Timeout;

    const timeoutPromise = new Promise<never>((_, reject) => {
        timeoutId = setTimeout(() => {
            reject(new Error(errorMessage));
        }, timeoutMs);
    });

    return Promise.race([promise, timeoutPromise]).finally(() => {
        clearTimeout(timeoutId);
    });
}

/**
 * Retry a function with exponential backoff
 * @param fn Async function to retry
 * @param maxRetries Maximum number of retries (default 3)
 * @param onRetry Optional callback when retry occurs
 */
export async function withRetry<T>(
    fn: () => Promise<T>,
    maxRetries: number = MAX_RETRIES,
    onRetry?: (attempt: number, error: Error, nextDelayMs: number) => void
): Promise<T> {
    let lastError: Error;

    for (let attempt = 0; attempt <= maxRetries; attempt++) {
        try {
            return await fn();
        } catch (error) {
            lastError = error instanceof Error ? error : new Error(String(error));

            if (attempt === maxRetries) {
                break;
            }

            // Exponential backoff with jitter
            const delay = Math.min(
                BASE_DELAY_MS * Math.pow(2, attempt) + Math.random() * 1000,
                MAX_DELAY_MS
            );

            onRetry?.(attempt + 1, lastError, delay);

            await new Promise(resolve => setTimeout(resolve, delay));
        }
    }

    throw lastError!;
}

/**
 * Check if device is currently online
 */
export async function isOnline(): Promise<boolean> {
    try {
        const state = await NetInfo.fetch();
        return state.isConnected === true && state.isInternetReachable !== false;
    } catch (error) {
        console.warn('[NetworkUtils] NetInfo error:', error);
        return true; // Assume online if check fails
    }
}

/**
 * Ensure device is online before proceeding
 * @throws Error if offline
 */
export async function requireOnline(): Promise<void> {
    const online = await isOnline();
    if (!online) {
        throw new Error('No internet connection. Please check your network and try again.');
    }
}

/**
 * Subscribe to network state changes
 * @param callback Called when network state changes
 * @returns Unsubscribe function
 */
export function onNetworkChange(
    callback: (isConnected: boolean) => void
): () => void {
    const unsubscribe = NetInfo.addEventListener(state => {
        callback(state.isConnected === true);
    });
    return unsubscribe;
}

/**
 * Debounce a function - prevents rapid repeated calls
 * @param fn Function to debounce
 * @param delayMs Delay in milliseconds
 */
export function debounce<T extends (...args: any[]) => void>(
    fn: T,
    delayMs: number
): T {
    let timeoutId: NodeJS.Timeout | null = null;

    return ((...args: Parameters<T>) => {
        if (timeoutId) {
            clearTimeout(timeoutId);
        }
        timeoutId = setTimeout(() => {
            fn(...args);
            timeoutId = null;
        }, delayMs);
    }) as T;
}

/**
 * Throttle a function - limits call frequency
 * @param fn Function to throttle
 * @param limitMs Minimum time between calls
 */
export function throttle<T extends (...args: any[]) => void>(
    fn: T,
    limitMs: number
): T {
    let lastCall = 0;

    return ((...args: Parameters<T>) => {
        const now = Date.now();
        if (now - lastCall >= limitMs) {
            lastCall = now;
            fn(...args);
        }
    }) as T;
}
