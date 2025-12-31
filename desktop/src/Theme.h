/**
 * @file Theme.h
 * @brief Modern dark glassmorphism theme for Teleport UI
 */

#pragma once

#include "imgui.h"
#include <string>
#include <unordered_map>

namespace teleport::ui {

/**
 * @brief Theme color identifiers
 */
enum class ThemeColor {
    // Brand colors
    Primary,
    PrimaryLight,
    PrimaryDark,
    Accent,
    
    // Surfaces
    Background,
    Surface,
    SurfaceLight,
    SidebarTop,
    SidebarBottom,
    Card,
    CardHover,
    
    // Text
    TextPrimary,
    TextSecondary,
    TextDisabled,
    
    // States
    Success,
    Warning,
    Error,
    Info,
    
    // Special
    Glow,
    Border
};

/**
 * @brief Premium dark theme with glassmorphism effects
 */
class Theme {
public:
    Theme();
    ~Theme() = default;
    
    /**
     * @brief Apply theme to ImGui
     */
    void Apply();
    
    /**
     * @brief Load custom fonts
     */
    void LoadFonts(ImGuiIO& io);
    
    /**
     * @brief Get color as ImU32
     */
    ImU32 GetColor(ThemeColor color) const;
    
    /**
     * @brief Get color as ImVec4
     */
    ImVec4 GetColorVec(ThemeColor color) const;
    
    /**
     * @brief Get icon font
     */
    ImFont* GetIconFont() const { return iconFont_; }
    
    /**
     * @brief Get heading font
     */
    ImFont* GetHeadingFont() const { return headingFont_; }
    
    /**
     * @brief Get body font
     */
    ImFont* GetBodyFont() const { return bodyFont_; }
    
    /**
     * @brief Animation timing
     */
    static constexpr float FastDuration = 0.15f;
    static constexpr float NormalDuration = 0.25f;
    static constexpr float SlowDuration = 0.4f;
    
    /**
     * @brief UI constants
     */
    static constexpr float CardRadius = 12.0f;
    static constexpr float ButtonRadius = 8.0f;
    static constexpr float SmallRadius = 4.0f;
    
private:
    void InitColors();
    
    std::unordered_map<ThemeColor, ImVec4> colors_;
    ImFont* iconFont_ = nullptr;
    ImFont* headingFont_ = nullptr;
    ImFont* bodyFont_ = nullptr;
    ImFont* smallFont_ = nullptr;
};

/**
 * @brief Helper to draw rounded rectangle with gradient
 */
void DrawGradientRect(
    ImDrawList* drawList,
    ImVec2 min, ImVec2 max,
    ImU32 topColor, ImU32 bottomColor,
    float rounding = 0.0f
);

/**
 * @brief Helper to draw glow effect
 */
void DrawGlow(
    ImDrawList* drawList,
    ImVec2 center,
    float radius,
    ImU32 color,
    float intensity = 1.0f
);

/**
 * @brief Helper for smooth animation interpolation
 */
float Lerp(float a, float b, float t);
float SmoothStep(float t);
float EaseOutCubic(float t);
float EaseInOutCubic(float t);

} // namespace teleport::ui
