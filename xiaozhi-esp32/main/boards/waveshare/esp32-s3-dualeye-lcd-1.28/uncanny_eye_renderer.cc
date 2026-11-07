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
    float iris_screen_span = 127.0f - (float)pupil_r;
    float inv_screen_span = (iris_screen_span > 1.0f) ? (1.0f / iris_screen_span) : 1.0f;
    for (int v = 0; v < 128; v++) {
        if (v < pupil_r) {
            v_lut[v] = 0xFF; // Pupil marker
        } else {
            float norm = (float)(v - pupil_r) * inv_screen_span;
            int tex_v = (int)(norm * (float)(iris_h - 1));
            if (tex_v < 0) tex_v = 0;
            if (tex_v >= iris_h) tex_v = iris_h - 1;
            v_lut[v] = (uint8_t)tex_v;
        }
    }

    // ---------- Eye-to-Display Fit Scaling ----------
    // We enforce scale = 1.0f to match the exact physical 240x240 display without scaling artifacts.
    // The geometry is masked by a mathematically correct, left/right asymmetric almond curve
    // that fits perfectly within the physical screen circle.

    static int16_t s_y_top_lut_left[240];
    static int16_t s_y_bot_lut_left[240];
    static int16_t s_y_top_lut_right[240];
    static int16_t s_y_bot_lut_right[240];
    static int s_cached_ap_w = -1;

    int ap_w = (asset_ && asset_->aperture_half_width > 0) ? asset_->aperture_half_width : 0;

    if (ap_w != s_cached_ap_w) {
        s_cached_ap_w = ap_w;
        for (int sx = 0; sx < 240; sx++) {
            if (ap_w > 0) {
                // Left Eye
                float u_l = (float)(sx - 120) / (float)ap_w;
                if (u_l < -1.0f || u_l > 1.0f) {
                    s_y_top_lut_left[sx] = 240;
                    s_y_bot_lut_left[sx] = -1;
                } else {
                    float u2 = u_l * u_l;
                    float slant = -12.0f * u_l;
                    s_y_top_lut_left[sx] = (int16_t)(120.0f - 52.0f * (1.0f - u2) * (1.0f + 0.15f * u_l) + slant);
                    s_y_bot_lut_left[sx] = (int16_t)(120.0f + 42.0f * (1.0f - u2) * (1.0f - 0.20f * u_l) + slant);
                }

                // Right Eye
                float u_r = (float)(120 - sx) / (float)ap_w;
                if (u_r < -1.0f || u_r > 1.0f) {
                    s_y_top_lut_right[sx] = 240;
                    s_y_bot_lut_right[sx] = -1;
                } else {
                    float u2 = u_r * u_r;
                    float slant = -12.0f * u_r;
                    s_y_top_lut_right[sx] = (int16_t)(120.0f - 52.0f * (1.0f - u2) * (1.0f + 0.15f * u_r) + slant);
                    s_y_bot_lut_right[sx] = (int16_t)(120.0f + 42.0f * (1.0f - u2) * (1.0f - 0.20f * u_r) + slant);
                }
            } else {
                s_y_top_lut_left[sx] = -1;
                s_y_bot_lut_left[sx] = 240;
                s_y_top_lut_right[sx] = -1;
                s_y_bot_lut_right[sx] = 240;
            }
        }
    }

    const int16_t* y_top_lut = is_left_eye ? s_y_top_lut_left : s_y_top_lut_right;
    const int16_t* y_bot_lut = is_left_eye ? s_y_bot_lut_left : s_y_bot_lut_right;

    uint16_t* out_ptr = dest_buffer;

    for (int line = 0; line < num_lines; line++) {
        int y = y_start + line;
        int ey = y;

        // Determine min and max X for the current Y to optimize filling
        int x_start = 0;
        int x_end = 240;
        if (ap_w > 0) {
            while (x_start < 240 && (y < y_top_lut[x_start] || y > y_bot_lut[x_start])) {
                x_start++;
            }
            while (x_end > x_start && (y < y_top_lut[x_end - 1] || y > y_bot_lut[x_end - 1])) {
                x_end--;
            }
        }

        // Fill non-visible pixels before aperture span
        if (x_start > 0) {
            std::fill_n(out_ptr, x_start, lid_col);
            out_ptr += x_start;
        }

        // Scaled sclera row coordinate (using eye-space ey)
        int sy = (sclera_h == 300) ? (((ey - gy) * 5) >> 2) : (((ey - gy) * sclera_h) / 240);
        if (sy < 0) sy = 0;
        else if (sy >= sclera_h) sy = sclera_h - 1;
        const uint16_t* sclera_row = sclera + (sy * sclera_w);

        // Polar LUT row pointer (eye-space)
        int lut_y = ey - gy;
        bool y_in_lut = (lut_y >= 0 && lut_y < 240);
        const uint16_t* polar_row = y_in_lut ? (polar + (lut_y * 240)) : nullptr;

        // Eyelid rows (eye-space)
        const uint8_t* upper_row = upper_map ? (upper_map + ey * 240) : nullptr;
        const uint8_t* lower_row = lower_map ? (lower_map + ey * 240) : nullptr;

        // Glint Y check uses eye-space ey so highlights track the scaled iris position
        bool in_glint1_y = params.enable_highlights && (ey >= g1_min_y && ey <= g1_max_y);
        bool in_glint2_y = params.enable_highlights && (ey >= g2_min_y && ey <= g2_max_y);

        int u_t = params.upper_eyelid_threshold;
        int l_t = params.lower_eyelid_threshold;

        for (int x = x_start; x < x_end; x++) {
            if (ap_w > 0) {
                int16_t yt = y_top_lut[x];
                int16_t yb = y_bot_lut[x];

                // Compute target closed mid-line (mostly upper lid moving down)
                int16_t y_mid = yb - (int16_t)(((yb - yt) * 38) / 256); // 38/256 is ~15%

                // Interpolate based on thresholds (0-255)
                int16_t curr_yt = yt + (int16_t)(((y_mid - yt) * u_t) / 255);
                int16_t curr_yb = yb - (int16_t)(((yb - y_mid) * l_t) / 255);

                if (y < curr_yt || y > curr_yb) {
                    *out_ptr++ = lid_col;
                    continue;
                }
            } else {
                if (y < y_top_lut[x] || y > y_bot_lut[x]) {
                    *out_ptr++ = lid_col;
                    continue;
                }
            }
            int ex = x;

            // Eyelid coordinate (left-right mirrored for organic caruncle anatomy)
            int lid_x = is_left_eye ? ex : (239 - ex);

            // 1. Eyelid mask check (Fallback for non-math presets)
            if (ap_w == 0) {
                if (u_thresh > 0 && upper_row && upper_row[lid_x] < u_thresh) {
                    *out_ptr++ = lid_col;
                    continue;
                }
                if (l_thresh > 0 && lower_row && lower_row[lid_x] < l_thresh) {
                    *out_ptr++ = lid_col;
                    continue;
                }
            }

            // 2. Sclera background pixel
            int sx = (sclera_w == 300) ? (((ex - gx) * 5) >> 2) : (((ex - gx) * sclera_w) / 240);
            if (sx < 0) sx = 0;
            else if (sx >= sclera_w) sx = sclera_w - 1;
            uint16_t pixel_color = sclera_row[sx];

            // 3. Polar Iris lookup
            int lut_x = ex - gx;
            if (polar_row && lut_x >= 0 && lut_x < 240) {
                uint16_t p = polar_row[lut_x];
                if (p != 0) {
                    uint8_t u_raw = (uint8_t)(p >> 8);
                    uint8_t v_raw = (uint8_t)(p & 0xFF);
                    uint8_t v = (v_raw > 0) ? (v_raw - 1) : 0;
                    if (v > 127) v = 127;
                    uint8_t tex_v = v_lut[v];

                    if (tex_v == 0xFF) {
                        pixel_color = 0x0821; // Pupil black (#101014)
                    } else {
                        uint32_t u_coord = (iris_w == 256) ? (uint32_t)u_raw : (((uint32_t)u_raw * iris_w) >> 8);
                        if (u_coord >= iris_w) u_coord = iris_w - 1;
                        if (tex_v >= iris_h) tex_v = iris_h - 1;
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
            if (in_glint1_y && std::abs((float)ex - glint1_x) < 6.5f) {
                float dx1 = (float)ex - glint1_x;
                float dy1 = (float)ey - glint1_y;
                float d1_sq = dx1 * dx1 + dy1 * dy1;
                if (d1_sq < 42.25f) {
                    float d1 = sqrtf(d1_sq);
                    float g = 1.0f - (d1 * 0.153846f);
                    uint16_t alpha = (uint16_t)(g * g * 240.0f);
                    pixel_color = AlphaBlendRGB565(0xFFFF, pixel_color, alpha);
                }
            } else if (in_glint2_y && std::abs((float)ex - glint2_x) < 4.2f) {
                float dx2 = (float)ex - glint2_x;
                float dy2 = (float)ey - glint2_y;
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

        // Fill non-visible pixels after aperture span
        if (x_end < 240) {
            std::fill_n(out_ptr, 240 - x_end, lid_col);
            out_ptr += (240 - x_end);
        }
    }
}

void UncannyEyeRenderer::RenderFrame(uint16_t* dest_buffer, const RenderParams& params, bool is_left_eye) {
    RenderChunk(dest_buffer, 0, 240, params, is_left_eye);
}

} // namespace EyeRenderer
