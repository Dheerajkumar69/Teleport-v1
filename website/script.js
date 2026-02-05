/**
 * Teleport Landing Page - JavaScript
 * Handles navigation, scroll animations, and interactions
 */

(function () {
    'use strict';

    // ============================================
    // DOM Elements
    // ============================================
    const nav = document.querySelector('.nav');
    const navToggle = document.querySelector('.nav-toggle');
    const navLinks = document.querySelector('.nav-links');
    const downloadButtons = document.querySelectorAll('.btn-download');
    const launchWebApp = document.getElementById('launch-web-app');

    // ============================================
    // Mobile Navigation Toggle
    // ============================================
    if (navToggle && navLinks) {
        navToggle.addEventListener('click', () => {
            navToggle.classList.toggle('active');
            navLinks.classList.toggle('active');
        });

        // Close menu when clicking a link
        navLinks.querySelectorAll('a').forEach(link => {
            link.addEventListener('click', () => {
                navToggle.classList.remove('active');
                navLinks.classList.remove('active');
            });
        });
    }

    // ============================================
    // Navbar Background on Scroll
    // ============================================
    let lastScroll = 0;

    window.addEventListener('scroll', () => {
        const currentScroll = window.pageYOffset;

        // Add subtle shadow when scrolled
        if (currentScroll > 50) {
            nav.style.boxShadow = '0 4px 20px rgba(0, 0, 0, 0.3)';
        } else {
            nav.style.boxShadow = 'none';
        }

        lastScroll = currentScroll;
    }, { passive: true });

    // ============================================
    // Smooth Scroll for Anchor Links
    // ============================================
    document.querySelectorAll('a[href^="#"]').forEach(anchor => {
        anchor.addEventListener('click', function (e) {
            const targetId = this.getAttribute('href');
            if (targetId === '#') return;

            const targetElement = document.querySelector(targetId);
            if (targetElement) {
                e.preventDefault();
                const navHeight = nav.offsetHeight;
                const targetPosition = targetElement.offsetTop - navHeight - 20;

                window.scrollTo({
                    top: targetPosition,
                    behavior: 'smooth'
                });
            }
        });
    });

    // ============================================
    // Intersection Observer for Scroll Animations
    // ============================================
    const observerOptions = {
        root: null,
        rootMargin: '0px 0px -100px 0px',
        threshold: 0.1
    };

    const observer = new IntersectionObserver((entries) => {
        entries.forEach(entry => {
            if (entry.isIntersecting) {
                entry.target.classList.add('visible');
                // Optionally stop observing once visible
                // observer.unobserve(entry.target);
            }
        });
    }, observerOptions);

    // Observe all animated elements
    const animatedElements = document.querySelectorAll(
        '.feature-card, .download-card, .step, .fade-in'
    );

    animatedElements.forEach(el => {
        observer.observe(el);
    });

    // ============================================
    // Download Button Interactions
    // ============================================
    downloadButtons.forEach(button => {
        button.addEventListener('click', function (e) {
            const platform = this.dataset.platform;

            if (platform === 'windows') {
                // In production, this would link to the actual .exe
                showToast('Desktop download starting...', 'info');
                // window.location.href = '/downloads/teleport-setup.exe';
            } else if (platform === 'android') {
                // In production, this would link to the APK
                showToast('Mobile APK download starting...', 'info');
                // window.location.href = '/downloads/teleport.apk';
            }
        });
    });

    // ============================================
    // Web App Launch
    // ============================================
    if (launchWebApp) {
        launchWebApp.addEventListener('click', function (e) {
            e.preventDefault();

            // Show launching message and redirect to web app
            showToast('Web App launching...', 'success');

            // Navigate to the web app after a brief moment
            setTimeout(() => {
                window.location.href = 'https://webversionteleport.vercel.app';
            }, 500);
        });
    }

    // ============================================
    // Toast Notification System
    // ============================================
    function showToast(message, type = 'info') {
        // Remove existing toast if any
        const existingToast = document.querySelector('.toast');
        if (existingToast) {
            existingToast.remove();
        }

        // Create toast element
        const toast = document.createElement('div');
        toast.className = `toast toast-${type}`;
        toast.innerHTML = `
            <span class="toast-message">${message}</span>
            <button class="toast-close" aria-label="Close">&times;</button>
        `;

        // Add styles dynamically if not already in CSS
        if (!document.querySelector('#toast-styles')) {
            const styles = document.createElement('style');
            styles.id = 'toast-styles';
            styles.textContent = `
                .toast {
                    position: fixed;
                    bottom: 24px;
                    left: 50%;
                    transform: translateX(-50%) translateY(100px);
                    display: flex;
                    align-items: center;
                    gap: 12px;
                    padding: 16px 24px;
                    background: rgba(24, 24, 27, 0.95);
                    border: 1px solid rgba(124, 58, 237, 0.3);
                    border-radius: 12px;
                    box-shadow: 0 10px 40px rgba(0, 0, 0, 0.3);
                    backdrop-filter: blur(10px);
                    z-index: 1000;
                    opacity: 0;
                    transition: all 0.3s ease;
                }
                .toast.show {
                    transform: translateX(-50%) translateY(0);
                    opacity: 1;
                }
                .toast-message {
                    font-size: 14px;
                    color: #FAFAFA;
                }
                .toast-close {
                    font-size: 20px;
                    color: #A1A1AA;
                    cursor: pointer;
                    transition: color 0.2s;
                }
                .toast-close:hover {
                    color: #FAFAFA;
                }
                .toast-success {
                    border-color: rgba(16, 185, 129, 0.5);
                }
                .toast-info {
                    border-color: rgba(59, 130, 246, 0.5);
                }
                .toast-warning {
                    border-color: rgba(245, 158, 11, 0.5);
                }
                .toast-error {
                    border-color: rgba(239, 68, 68, 0.5);
                }
            `;
            document.head.appendChild(styles);
        }

        document.body.appendChild(toast);

        // Trigger animation
        requestAnimationFrame(() => {
            toast.classList.add('show');
        });

        // Close button
        toast.querySelector('.toast-close').addEventListener('click', () => {
            toast.classList.remove('show');
            setTimeout(() => toast.remove(), 300);
        });

        // Auto-dismiss after 4 seconds
        setTimeout(() => {
            if (toast.parentNode) {
                toast.classList.remove('show');
                setTimeout(() => toast.remove(), 300);
            }
        }, 4000);
    }

    // ============================================
    // Parallax Effect for Hero (subtle)
    // ============================================
    const heroGlow = document.querySelector('.hero-glow');
    const heroGrid = document.querySelector('.hero-grid');

    if (heroGlow || heroGrid) {
        window.addEventListener('scroll', () => {
            const scrolled = window.pageYOffset;
            const heroHeight = document.querySelector('.hero').offsetHeight;

            if (scrolled < heroHeight) {
                const parallaxValue = scrolled * 0.3;
                if (heroGlow) {
                    heroGlow.style.transform = `translateX(-50%) translateY(${parallaxValue}px)`;
                }
                if (heroGrid) {
                    heroGrid.style.transform = `translateY(${parallaxValue * 0.5}px)`;
                }
            }
        }, { passive: true });
    }

    // ============================================
    // Keyboard Navigation Support
    // ============================================
    document.addEventListener('keydown', (e) => {
        // Escape closes mobile menu
        if (e.key === 'Escape') {
            if (navLinks && navLinks.classList.contains('active')) {
                navToggle.classList.remove('active');
                navLinks.classList.remove('active');
            }
        }
    });

    // ============================================
    // Performance: Lazy load non-critical elements
    // ============================================
    if ('IntersectionObserver' in window) {
        // Already using IntersectionObserver for animations
        console.log('Teleport website loaded successfully ⚡');
    }

    // ============================================
    // Copyright Year Auto-Update
    // ============================================
    const copyrightYear = document.querySelector('.footer-bottom p');
    if (copyrightYear) {
        const currentYear = new Date().getFullYear();
        copyrightYear.textContent = copyrightYear.textContent.replace(/\d{4}/, currentYear);
    }

})();
