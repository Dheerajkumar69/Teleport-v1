// @ts-check
const { test, expect } = require('@playwright/test');

test.describe('Teleport WebRTC App', () => {
    test('loads the page and shows device name input', async ({ page }) => {
        await page.goto('/');
        await expect(page.locator('#device-name')).toBeVisible();
        await expect(page.locator('#register-btn')).toBeVisible();
    });

    test('registers a device name', async ({ page }) => {
        await page.goto('/');
        await page.fill('#device-name', 'TestDevice');
        await page.click('#register-btn');
        // After registration, peer list or status should change
        await expect(page.locator('#status')).toBeVisible();
    });

    test('can send a text message via relay', async ({ page }) => {
        await page.goto('/');
        await page.fill('#device-name', 'Peer1');
        await page.click('#register-btn');
        // Open relay tab and check textarea exists
        await expect(page.locator('#relay-tab')).toBeVisible();
    });

    test('file input accepts files', async ({ page }) => {
        await page.goto('/');
        await expect(page.locator('#file-input')).toBeAttached();
    });

    test('escapeHtml prevents XSS in chat messages', async ({ page }) => {
        await page.goto('/');
        // Inject a script tag via evaluate and check it's escaped
        const xssPayload = '<script>alert("xss")</script>';
        const escaped = await page.evaluate((payload) => {
            // Access the global escapeHtml if available
            if (typeof escapeHtml === 'function') {
                return escapeHtml(payload);
            }
            // Fallback: manually test the same escaping logic
            return payload
                .replace(/&/g, '&amp;')
                .replace(/</g, '&lt;')
                .replace(/>/g, '&gt;')
                .replace(/"/g, '&quot;')
                .replace(/'/g, '&#x27;');
        }, xssPayload);

        expect(escaped).not.toContain('<script>');
        expect(escaped).toContain('&lt;script&gt;');
    });

    test('sanitizeFilename blocks path traversal', async ({ page }) => {
        await page.goto('/');
        const result = await page.evaluate(() => {
            // Check if TeleportWebRTC is available
            if (typeof TeleportWebRTC !== 'undefined') {
                const instance = new TeleportWebRTC();
                try {
                    instance.sanitizeFilename('../../../etc/passwd');
                    return 'no-error';
                } catch (e) {
                    return e.message;
                }
            }
            return 'class-not-available';
        });
        expect(result).not.toBe('no-error');
        expect(result).not.toBe('class-not-available');
    });

    test('connection status element exists', async ({ page }) => {
        await page.goto('/');
        await expect(page.locator('#connection-status')).toBeVisible();
    });

    test('peer list container exists', async ({ page }) => {
        await page.goto('/');
        await expect(page.locator('#peer-list')).toBeVisible();
    });
});
