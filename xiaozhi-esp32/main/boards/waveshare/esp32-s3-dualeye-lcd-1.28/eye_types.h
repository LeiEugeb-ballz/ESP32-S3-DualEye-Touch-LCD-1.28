// Copyright (c) 2026 Waveshare / Xiaozhi Project
// Clean asset and rendering definitions for Uncanny_Eyes texture engine
#pragma once

#include <cstdint>
#include <cstddef>

namespace EyeRenderer {

// Fast RGB565 component extraction
#define RGB565_R(c) (((c) >> 11) & 0x1F)
#define RGB565_G(c) (((c) >> 5)  & 0x3F)
#define RGB565_B(c) ((c)         & 0x1F)
#define TO_RGB565(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

// Blend two RGB565 pixels with integer alpha in [0..256]
static inline uint16_t AlphaBlendRGB565(uint16_t fg, uint16_t bg, uint16_t alpha) {
    if (alpha >= 256) return fg;
    if (alpha == 0) return bg;

    uint32_t fg_rb = fg & 0xF81F;
    uint32_t bg_rb = bg & 0xF81F;
    uint32_t out_rb = ((fg_rb * alpha + bg_rb * (256 - alpha)) >> 8) & 0xF81F;

    uint32_t fg_g = fg & 0x07E0;
    uint32_t bg_g = bg & 0x07E0;
    uint32_t out_g = ((fg_g * alpha + bg_g * (256 - alpha)) >> 8) & 0x07E0;

    return (uint16_t)(out_rb | out_g);
}

// Immutable Eye Asset Specification
struct EyeAssetConfig {
    const char* name;
    uint16_t screen_width;           // Display pixel width (240)
    uint16_t screen_height;          // Display pixel height (240)
    uint16_t sclera_width;           // Sclera texture width (300)
    uint16_t sclera_height;          // Sclera texture height (300)
    const uint16_t* sclera_map;      // 300x300 RGB565 sclera image
    uint16_t iris_width;             // Angular resolution (256)
    uint16_t iris_height;            // Radial resolution (128)
    const uint16_t* iris_map;        // 128x256 RGB565 polar iris texture
    const uint16_t* polar_map;       // 240x240 packed (angle<<8 | radius) LUT
    const uint8_t* upper_eyelid_map; // 240x240 upper eyelid heightfield
    const uint8_t* lower_eyelid_map; // 240x240 lower eyelid heightfield
    uint8_t default_pupil_radius;    // Default radius in polar steps (32)
    uint8_t aperture_half_width;     // Base almond aperture half-width (e.g., 102). 0 = disabled.
    uint8_t aperture_half_height;    // Base almond aperture half-height (e.g., 60).
};

// Render parameters for single frame execution
struct RenderParams {
    int16_t gaze_x = 0;                  // Gaze offset X [-30..+30]
    int16_t gaze_y = 0;                  // Gaze offset Y [-30..+30]
    uint8_t pupil_dilation = 32;         // Pupil radius in polar steps [12..65]
    uint8_t upper_eyelid_threshold = 0;  // 0 = open, 255 = fully closed
    uint8_t lower_eyelid_threshold = 0;  // 0 = open, 255 = fully closed
    uint16_t eyelid_color = 0x0000;      // Eyelid skin/background color (RGB565)
    bool enable_highlights = true;       // Render corneal specular highlights
    float scale = 0.86f;                 // Eye scale factor to fit full eye relatively into circular display (default 0.86)
};

} // namespace EyeRenderer
