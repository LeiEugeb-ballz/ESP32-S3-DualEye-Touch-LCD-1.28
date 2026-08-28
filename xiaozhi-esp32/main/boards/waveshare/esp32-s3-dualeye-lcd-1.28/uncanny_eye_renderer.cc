// Copyright (c) 2026 Waveshare / Xiaozhi Project
// Ultra high-performance polar texture rasterization engine with direct DMA scanline streaming
#include "uncanny_eye_renderer.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace EyeRenderer {

// Fast 16-bit byte swap for hardware SPI DMA streaming
#define SPI_SWAP(c) (uint16_t)(((uint16_t)(c) >> 8) | ((uint16_t)(c) << 8))

UncannyEyeRenderer::UncannyEyeRenderer(const EyeAssetConfig* asset)
    : asset_(asset ? asset : &kDefaultEyeAsset) {
}

void UncannyEyeRenderer::SetAsset(const EyeAssetConfig* asset) {
    if (asset) {
        asset_ = asset;
    }
}

void UncannyEyeRenderer::RenderChunk(uint16_t* dest_buffer, int y_start, int num_lines,
                                     const RenderParams& params, bool is_left_eye) {
    if (!asset_ || !dest_buffer || num_lines <= 0 || y_start < 0 || (y_start + num_lines) > 240) {
        return;
    }

    const uint16_t* sclera = asset_->sclera_map;
    const uint16_t* iris = asset_->iris_map;
    const uint16_t* polar = asset_->polar_map;
    const uint8_t* upper_map = asset_->upper_eyelid_map;
    const uint8_t* lower_map = asset_->lower_eyelid_map;

    const uint16_t sclera_w = asset_->sclera_width;
    const uint16_t sclera_h = asset_->sclera_height;
    const uint16_t iris_w = (asset_->iris_width > 0) ? asset_->iris_width : 256;
    const uint16_t iris_h = (asset_->iris_height > 0) ? asset_->iris_height : 128;
    const uint8_t pupil_r = (params.pupil_dilation > 0) ? params.pupil_dilation : asset_->default_pupil_radius;

    const int16_t gx = params.gaze_x;
    const int16_t gy = params.gaze_y;

    // Specular highlight positions with 3D corneal parallax
    const float glint1_x = 96.0f - 0.20f * (float)gx;
    const float glint1_y = 94.0f - 0.20f * (float)gy;
    const float glint2_x = 142.0f - 0.20f * (float)gx;
    const float glint2_y = 142.0f - 0.20f * (float)gy;

    const int g1_min_y = (int)(glint1_y - 7.0f);
    const int g1_max_y = (int)(glint1_y + 7.0f);
    const int g2_min_y = (int)(glint2_y - 5.0f);
    const int g2_max_y = (int)(glint2_y + 5.0f);

    const uint8_t u_thresh = params.upper_eyelid_threshold;
    const uint8_t l_thresh = params.lower_eyelid_threshold;
    const uint16_t lid_col_raw = params.eyelid_color;
    const uint16_t lid_col = SPI_SWAP(lid_col_raw);

    // Precalculate 128-entry radial stretch LUT for current frame (0 FPU in inner loop)
    uint8_t v_lut[128];
    float iris_active_range = (float)(iris_h - 1 - pupil_r);
    float inv_active_range = (iris_active_range > 0.01f) ? (1.0f / iris_active_range) : 1.0f;
    for (int v = 0; v < 128; v++) {
        if (v < pupil_r) {
            v_lut[v] = 0xFF; // Pupil marker
        } else {
            int tex_v = (int)(((float)(v - pupil_r) * inv_active_range) * (float)(iris_h - 1));
            if (tex_v < 0) tex_v = 0;
            if (tex_v >= iris_h) tex_v = iris_h - 1;
            v_lut[v] = (uint8_t)tex_v;
        }
    }

    uint16_t* out_ptr = dest_buffer;

    for (int line = 0; line < num_lines; line++) {
        int y = y_start + line;

        // Scaled sclera row coordinate
        int sy = ((y - gy) * sclera_h) / 240;
        if (sy < 0) sy = 0;
        else if (sy >= sclera_h) sy = sclera_h - 1;
        const uint16_t* sclera_row = sclera + (sy * sclera_w);

        // Polar LUT row pointer
        int lut_y = y - gy;
        bool y_in_lut = (lut_y >= 0 && lut_y < 240);
        const uint16_t* polar_row = y_in_lut ? (polar + (lut_y * 240)) : nullptr;

        const uint8_t* upper_row = upper_map ? (upper_map + y * 240) : nullptr;
        const uint8_t* lower_row = lower_map ? (lower_map + y * 240) : nullptr;

        bool in_glint1_y = params.enable_highlights && (y >= g1_min_y && y <= g1_max_y);
        bool in_glint2_y = params.enable_highlights && (y >= g2_min_y && y <= g2_max_y);

        for (int x = 0; x < 240; x++) {
            // Eyelid coordinate (left-right mirrored for organic caruncle anatomy)
            int lid_x = is_left_eye ? x : (239 - x);

            // 1. Eyelid mask check
            if (u_thresh > 0 && upper_row && upper_row[lid_x] < u_thresh) {
                *out_ptr++ = lid_col;
                continue;
            }
            if (l_thresh > 0 && lower_row && lower_row[lid_x] < l_thresh) {
                *out_ptr++ = lid_col;
                continue;
            }

            // 2. Sclera background pixel
            int sx = ((x - gx) * sclera_w) / 240;
            if (sx < 0) sx = 0;
            else if (sx >= sclera_w) sx = sclera_w - 1;
            uint16_t pixel_color = sclera_row[sx];

            // 3. Polar Iris lookup
            int lut_x = x - gx;
            if (polar_row && lut_x >= 0 && lut_x < 240) {
                uint16_t p = polar_row[lut_x];
                if (p != 0) {
                    uint8_t u_raw = (uint8_t)(p >> 8);
                    uint8_t v = (uint8_t)((p & 0xFF) - 1);
                    uint8_t tex_v = v_lut[v];

                    if (tex_v == 0xFF) {
                        pixel_color = 0x0821; // Pupil black (#101014)
                    } else {
                        uint32_t u_coord = (iris_w == 1) ? 0 : ((uint32_t)u_raw * iris_w) >> 8;
                        if (u_coord >= iris_w) u_coord = iris_w - 1;
                        uint16_t iris_color = iris[(uint32_t)tex_v * iris_w + u_coord];
                        if (v >= 124) {
                            uint16_t alpha = (uint16_t)((128 - v) << 6); // [0..256]
                            pixel_color = AlphaBlendRGB565(iris_color, pixel_color, alpha);
                        } else {
                            pixel_color = iris_color;
                        }
                    }
                }
            }

            // 4. Specular Highlights (only computed inside bounded glint zones)
            if (in_glint1_y) {
                float dx1 = (float)x - glint1_x;
                float dy1 = (float)y - glint1_y;
                float d1_sq = dx1 * dx1 + dy1 * dy1;
                if (d1_sq < 42.25f) {
                    float d1 = sqrtf(d1_sq);
                    float g = 1.0f - (d1 * 0.153846f);
                    uint16_t alpha = (uint16_t)(g * g * 240.0f);
                    pixel_color = AlphaBlendRGB565(0xFFFF, pixel_color, alpha);
                }
            } else if (in_glint2_y) {
                float dx2 = (float)x - glint2_x;
                float dy2 = (float)y - glint2_y;
                float d2_sq = dx2 * dx2 + dy2 * dy2;
                if (d2_sq < 17.64f) {
                    float d2 = sqrtf(d2_sq);
                    float g2 = 1.0f - (d2 * 0.238095f);
                    uint16_t alpha2 = (uint16_t)(g2 * g2 * 148.0f);
                    pixel_color = AlphaBlendRGB565(0xFFFF, pixel_color, alpha2);
                }
            }

            // Byte-swap for direct SPI DMA transfer
            *out_ptr++ = SPI_SWAP(pixel_color);
        }
    }
}

void UncannyEyeRenderer::RenderFrame(uint16_t* dest_buffer, const RenderParams& params, bool is_left_eye) {
    RenderChunk(dest_buffer, 0, 240, params, is_left_eye);
}

} // namespace EyeRenderer
