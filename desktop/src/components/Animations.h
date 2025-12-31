/**
 * @file Animations.h
 * @brief Animation utility functions
 */

#pragma once

#include <cmath>
#include <algorithm>

namespace teleport::ui {

/**
 * @brief Linear interpolation
 */
inline float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

/**
 * @brief Smooth step (ease in-out)
 */
inline float SmoothStep(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/**
 * @brief Ease out cubic
 */
inline float EaseOutCubic(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    float f = 1.0f - t;
    return 1.0f - f * f * f;
}

/**
 * @brief Ease in out cubic
 */
inline float EaseInOutCubic(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t < 0.5f 
        ? 4.0f * t * t * t 
        : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

/**
 * @brief Ease out elastic (bouncy)
 */
inline float EaseOutElastic(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    
    const float c4 = (2.0f * 3.14159f) / 3.0f;
    return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
}

/**
 * @brief Ease out back (overshoot)
 */
inline float EaseOutBack(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    t = t - 1.0f;
    return 1.0f + c3 * t * t * t + c1 * t * t;
}

/**
 * @brief Spring animation helper
 */
class SpringAnimation {
public:
    float value = 0.0f;
    float target = 0.0f;
    float velocity = 0.0f;
    
    float stiffness = 180.0f;
    float damping = 12.0f;
    
    void Update(float dt) {
        float force = stiffness * (target - value);
        float damping_force = damping * velocity;
        float acceleration = force - damping_force;
        
        velocity += acceleration * dt;
        value += velocity * dt;
    }
    
    bool IsSettled(float threshold = 0.001f) const {
        return std::abs(target - value) < threshold && std::abs(velocity) < threshold;
    }
};

} // namespace teleport::ui
