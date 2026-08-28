// Copyright (c) 2026 Waveshare / Xiaozhi Project
// Uncanny_Eyes polar texture rasterization engine with direct DMA scanline streaming
#pragma once

#include "eye_types.h"
#include "default_eye_asset.h"

namespace EyeRenderer {

class UncannyEyeRenderer {
private:
    const EyeAssetConfig* asset_;

public:
    explicit UncannyEyeRenderer(const EyeAssetConfig* asset = &kDefaultEyeAsset);
    ~UncannyEyeRenderer() = default;

    void SetAsset(const EyeAssetConfig* asset);
    const EyeAssetConfig* GetAsset() const { return asset_; }

    // Renders a horizontal slice (chunk) of the 240x240 display into dest_buffer.
    // dest_buffer must contain at least 240 * num_lines * sizeof(uint16_t) bytes.
    // Output is byte-swapped and formatted directly for SPI DMA hardware streaming.
    void RenderChunk(uint16_t* dest_buffer, int y_start, int num_lines,
                     const RenderParams& params, bool is_left_eye = true);

    // Full frame fallback helper into dest_buffer (240x240 pixels)
    void RenderFrame(uint16_t* dest_buffer, const RenderParams& params, bool is_left_eye = true);
};

} // namespace EyeRenderer
