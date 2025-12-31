/**
 * @file Theme.cpp
 * @brief Modern dark glassmorphism theme implementation
 */

#include "Theme.h"
#include <cmath>
#include <algorithm>

// Font Awesome 6 Free - Regular subset embedded
// This is a minimal subset for icons used in the app
static const unsigned int FA_ICONS_DATA[] = {
    // Placeholder - actual font would be loaded from file or embedded
    0
};

namespace teleport::ui {

Theme::Theme() {
    InitColors();
}

void Theme::InitColors() {
    // Brand - Vibrant purple gradient
    colors_[ThemeColor::Primary] = ImVec4(0.486f, 0.228f, 0.929f, 1.0f);       // #7C3AED
    colors_[ThemeColor::PrimaryLight] = ImVec4(0.655f, 0.545f, 0.980f, 1.0f);  // #A78BFA
    colors_[ThemeColor::PrimaryDark] = ImVec4(0.365f, 0.173f, 0.698f, 1.0f);   // #5D2CB2
    colors_[ThemeColor::Accent] = ImVec4(0.133f, 0.827f, 0.933f, 1.0f);        // #22D3EE (Cyan)
    
    // Surfaces - Dark with glass effect
    colors_[ThemeColor::Background] = ImVec4(0.067f, 0.067f, 0.090f, 0.95f);   // Near black
    colors_[ThemeColor::Surface] = ImVec4(0.094f, 0.094f, 0.106f, 0.85f);      // #18181B
    colors_[ThemeColor::SurfaceLight] = ImVec4(0.153f, 0.153f, 0.165f, 0.80f); // #27272A
    colors_[ThemeColor::SidebarTop] = ImVec4(0.078f, 0.078f, 0.094f, 0.95f);   // Dark gradient top
    colors_[ThemeColor::SidebarBottom] = ImVec4(0.055f, 0.055f, 0.071f, 0.98f);// Dark gradient bottom
    colors_[ThemeColor::Card] = ImVec4(0.110f, 0.110f, 0.125f, 0.75f);         // Card background
    colors_[ThemeColor::CardHover] = ImVec4(0.140f, 0.140f, 0.160f, 0.85f);    // Card hover
    
    // Text
    colors_[ThemeColor::TextPrimary] = ImVec4(0.980f, 0.980f, 0.980f, 1.0f);   // #FAFAFA
    colors_[ThemeColor::TextSecondary] = ImVec4(0.631f, 0.631f, 0.667f, 1.0f); // #A1A1AA
    colors_[ThemeColor::TextDisabled] = ImVec4(0.400f, 0.400f, 0.430f, 1.0f);  // #666670
    
    // States
    colors_[ThemeColor::Success] = ImVec4(0.063f, 0.725f, 0.506f, 1.0f);       // #10B981
    colors_[ThemeColor::Warning] = ImVec4(0.961f, 0.620f, 0.043f, 1.0f);       // #F59E0B
    colors_[ThemeColor::Error] = ImVec4(0.937f, 0.267f, 0.267f, 1.0f);         // #EF4444
    colors_[ThemeColor::Info] = ImVec4(0.231f, 0.510f, 0.965f, 1.0f);          // #3B82F6
    
    // Special
    colors_[ThemeColor::Glow] = ImVec4(0.486f, 0.228f, 0.929f, 0.4f);          // Primary with alpha
    colors_[ThemeColor::Border] = ImVec4(0.200f, 0.200f, 0.220f, 0.5f);        // Subtle border
}

void Theme::Apply() {
    ImGuiStyle& style = ImGui::GetStyle();
    
    // Rounding
    style.WindowRounding = 0.0f;        // Full window, no rounding
    style.ChildRounding = CardRadius;
    style.FrameRounding = ButtonRadius;
    style.PopupRounding = CardRadius;
    style.ScrollbarRounding = SmallRadius;
    style.GrabRounding = SmallRadius;
    style.TabRounding = ButtonRadius;
    
    // Padding
    style.WindowPadding = ImVec2(0, 0);
    style.FramePadding = ImVec2(12, 8);
    style.CellPadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(12, 8);
    style.ItemInnerSpacing = ImVec2(8, 4);
    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 10.0f;
    
    // Borders
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    
    // Colors
    ImVec4* c = style.Colors;
    
    c[ImGuiCol_Text] = colors_[ThemeColor::TextPrimary];
    c[ImGuiCol_TextDisabled] = colors_[ThemeColor::TextDisabled];
    c[ImGuiCol_WindowBg] = colors_[ThemeColor::Background];
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = colors_[ThemeColor::Surface];
    c[ImGuiCol_Border] = colors_[ThemeColor::Border];
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    
    c[ImGuiCol_FrameBg] = colors_[ThemeColor::SurfaceLight];
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.180f, 0.180f, 0.200f, 0.80f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.220f, 0.220f, 0.240f, 0.85f);
    
    c[ImGuiCol_TitleBg] = colors_[ThemeColor::Surface];
    c[ImGuiCol_TitleBgActive] = colors_[ThemeColor::Surface];
    c[ImGuiCol_TitleBgCollapsed] = colors_[ThemeColor::Surface];
    
    c[ImGuiCol_MenuBarBg] = colors_[ThemeColor::Surface];
    
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.3f, 0.3f, 0.35f, 0.5f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.4f, 0.4f, 0.45f, 0.7f);
    c[ImGuiCol_ScrollbarGrabActive] = colors_[ThemeColor::Primary];
    
    c[ImGuiCol_CheckMark] = colors_[ThemeColor::Primary];
    c[ImGuiCol_SliderGrab] = colors_[ThemeColor::Primary];
    c[ImGuiCol_SliderGrabActive] = colors_[ThemeColor::PrimaryLight];
    
    c[ImGuiCol_Button] = colors_[ThemeColor::SurfaceLight];
    c[ImGuiCol_ButtonHovered] = ImVec4(0.486f, 0.228f, 0.929f, 0.7f);
    c[ImGuiCol_ButtonActive] = colors_[ThemeColor::Primary];
    
    c[ImGuiCol_Header] = colors_[ThemeColor::SurfaceLight];
    c[ImGuiCol_HeaderHovered] = ImVec4(0.486f, 0.228f, 0.929f, 0.5f);
    c[ImGuiCol_HeaderActive] = colors_[ThemeColor::Primary];
    
    c[ImGuiCol_Separator] = colors_[ThemeColor::Border];
    c[ImGuiCol_SeparatorHovered] = colors_[ThemeColor::Primary];
    c[ImGuiCol_SeparatorActive] = colors_[ThemeColor::Primary];
    
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = colors_[ThemeColor::Primary];
    c[ImGuiCol_ResizeGripActive] = colors_[ThemeColor::PrimaryLight];
    
    c[ImGuiCol_Tab] = colors_[ThemeColor::Surface];
    c[ImGuiCol_TabHovered] = colors_[ThemeColor::Primary];
    c[ImGuiCol_TabActive] = colors_[ThemeColor::Primary];
    c[ImGuiCol_TabUnfocused] = colors_[ThemeColor::Surface];
    c[ImGuiCol_TabUnfocusedActive] = colors_[ThemeColor::SurfaceLight];
    
    c[ImGuiCol_PlotLines] = colors_[ThemeColor::Primary];
    c[ImGuiCol_PlotLinesHovered] = colors_[ThemeColor::Accent];
    c[ImGuiCol_PlotHistogram] = colors_[ThemeColor::Primary];
    c[ImGuiCol_PlotHistogramHovered] = colors_[ThemeColor::Accent];
    
    c[ImGuiCol_TableHeaderBg] = colors_[ThemeColor::Surface];
    c[ImGuiCol_TableBorderStrong] = colors_[ThemeColor::Border];
    c[ImGuiCol_TableBorderLight] = ImVec4(0.15f, 0.15f, 0.17f, 0.5f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(0.1f, 0.1f, 0.12f, 0.3f);
    
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.486f, 0.228f, 0.929f, 0.35f);
    c[ImGuiCol_DragDropTarget] = colors_[ThemeColor::Accent];
    c[ImGuiCol_NavHighlight] = colors_[ThemeColor::Primary];
    c[ImGuiCol_NavWindowingHighlight] = colors_[ThemeColor::Primary];
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.5f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.6f);
}

void Theme::LoadFonts(ImGuiIO& io) {
    // DO NOT call io.Fonts->Clear() - it can leave ImGui in invalid state if fonts fail to load
    
    // Add default font first as ultimate fallback
    ImFont* defaultFont = io.Fonts->AddFontDefault();
    bodyFont_ = defaultFont;
    headingFont_ = defaultFont;
    iconFont_ = defaultFont;
    smallFont_ = defaultFont;
    
    // Try to load Segoe UI for body text
    ImFont* segoe = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\segoeui.ttf", 16.0f
    );
    if (segoe) {
        bodyFont_ = segoe;
    }
    
    // Try to load Segoe UI Bold for headings
    ImFont* segoeBold = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\segoeuib.ttf", 26.0f
    );
    if (segoeBold) {
        headingFont_ = segoeBold;
    }
    
    // Try to load smaller font for labels
    ImFont* segoeSmall = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\segoeui.ttf", 12.0f
    );
    if (segoeSmall) {
        smallFont_ = segoeSmall;
    }
    
    // Try to load icon font
    ImFont* icons = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\segmdl2.ttf", 18.0f
    );
    if (icons) {
        iconFont_ = icons;
    }
    
    // Build the font atlas
    io.Fonts->Build();
}

ImU32 Theme::GetColor(ThemeColor color) const {
    auto it = colors_.find(color);
    if (it != colors_.end()) {
        return ImGui::ColorConvertFloat4ToU32(it->second);
    }
    return IM_COL32(255, 255, 255, 255);
}

ImVec4 Theme::GetColorVec(ThemeColor color) const {
    auto it = colors_.find(color);
    if (it != colors_.end()) {
        return it->second;
    }
    return ImVec4(1, 1, 1, 1);
}

// Helper functions

void DrawGradientRect(ImDrawList* drawList, ImVec2 min, ImVec2 max, 
                      ImU32 topColor, ImU32 bottomColor, float rounding) {
    drawList->AddRectFilledMultiColor(min, max, topColor, topColor, bottomColor, bottomColor);
    
    // If rounding is needed, we apply it via clipping
    if (rounding > 0) {
        // For proper rounded gradient, we'd need a shader - this is a limitation of ImGui
        // Using simple rounded rect overlay as workaround
    }
}

void DrawGlow(ImDrawList* drawList, ImVec2 center, float radius, ImU32 color, float intensity) {
    const int numCircles = 8;
    for (int i = numCircles; i > 0; i--) {
        float r = radius * (1.0f + (float)i * 0.3f);
        float alpha = intensity * (1.0f / (float)(i * 2));
        
        ImU32 c = (color & 0x00FFFFFF) | ((ImU32)(alpha * 255) << 24);
        drawList->AddCircleFilled(center, r, c, 32);
    }
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float SmoothStep(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float EaseOutCubic(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    t = 1.0f - t;
    return 1.0f - t * t * t;
}

float EaseInOutCubic(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t < 0.5f 
        ? 4.0f * t * t * t 
        : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

} // namespace teleport::ui
