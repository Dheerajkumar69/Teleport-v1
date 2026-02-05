/**
 * Jest Unit Tests for NetworkUtils
 * Tests timeout, retry, throttle, debounce functions
 */

import {
    withTimeout,
    withRetry,
    throttle,
    debounce,
} from '../NetworkUtils';

// Mock NetInfo
jest.mock('@react-native-community/netinfo', () => ({
    fetch: jest.fn().mockResolvedValue({ isConnected: true, isInternetReachable: true }),
    addEventListener: jest.fn(() => jest.fn()),
}));

describe('withTimeout', () => {
    beforeEach(() => {
        jest.useFakeTimers();
    });

    afterEach(() => {
        jest.useRealTimers();
    });

    it('should resolve if promise completes before timeout', async () => {
        const promise = Promise.resolve('success');
        const result = await withTimeout(promise, 1000);
        expect(result).toBe('success');
    });

    it('should reject with timeout error if promise takes too long', async () => {
        const slowPromise = new Promise((resolve) => {
            setTimeout(resolve, 5000);
        });

        const timeoutPromise = withTimeout(slowPromise, 100, 'Timed out!');
        jest.advanceTimersByTime(150);

        await expect(timeoutPromise).rejects.toThrow('Timed out!');
    });
});

describe('withRetry', () => {
    it('should resolve on first success', async () => {
        const fn = jest.fn().mockResolvedValue('success');

        const result = await withRetry(fn);

        expect(result).toBe('success');
        expect(fn).toHaveBeenCalledTimes(1);
    });

    it('should retry on failure', async () => {
        const fn = jest.fn()
            .mockRejectedValueOnce(new Error('fail 1'))
            .mockRejectedValueOnce(new Error('fail 2'))
            .mockResolvedValue('success');

        const result = await withRetry(fn, 3);

        expect(result).toBe('success');
        expect(fn).toHaveBeenCalledTimes(3);
    }, 10000);

    it('should call onRetry callback', async () => {
        const fn = jest.fn()
            .mockRejectedValueOnce(new Error('fail'))
            .mockResolvedValue('success');
        const onRetry = jest.fn();

        await withRetry(fn, 1, onRetry);

        expect(onRetry).toHaveBeenCalledTimes(1);
        expect(onRetry).toHaveBeenCalledWith(1, expect.any(Error), expect.any(Number));
    });

    it('should throw after max retries', async () => {
        const fn = jest.fn().mockRejectedValue(new Error('always fail'));

        await expect(withRetry(fn, 2)).rejects.toThrow('always fail');
        expect(fn).toHaveBeenCalledTimes(3); // 1 initial + 2 retries
    }, 10000);
});

describe('throttle', () => {
    beforeEach(() => {
        jest.useFakeTimers();
    });

    afterEach(() => {
        jest.useRealTimers();
    });

    it('should call function immediately on first call', () => {
        const fn = jest.fn();
        const throttled = throttle(fn, 100);

        throttled();

        expect(fn).toHaveBeenCalledTimes(1);
    });

    it('should skip calls within throttle period', () => {
        const fn = jest.fn();
        const throttled = throttle(fn, 100);

        throttled();
        throttled();
        throttled();

        expect(fn).toHaveBeenCalledTimes(1);
    });

    it('should allow calls after throttle period', () => {
        const fn = jest.fn();
        const throttled = throttle(fn, 100);

        throttled();
        jest.advanceTimersByTime(150);
        throttled();

        expect(fn).toHaveBeenCalledTimes(2);
    });
});

describe('debounce', () => {
    beforeEach(() => {
        jest.useFakeTimers();
    });

    afterEach(() => {
        jest.useRealTimers();
    });

    it('should delay function call', () => {
        const fn = jest.fn();
        const debounced = debounce(fn, 100);

        debounced();
        expect(fn).not.toHaveBeenCalled();

        jest.advanceTimersByTime(100);
        expect(fn).toHaveBeenCalledTimes(1);
    });

    it('should reset delay on subsequent calls', () => {
        const fn = jest.fn();
        const debounced = debounce(fn, 100);

        debounced();
        jest.advanceTimersByTime(50);
        debounced();
        jest.advanceTimersByTime(50);
        debounced();
        jest.advanceTimersByTime(100);

        expect(fn).toHaveBeenCalledTimes(1);
    });
});
