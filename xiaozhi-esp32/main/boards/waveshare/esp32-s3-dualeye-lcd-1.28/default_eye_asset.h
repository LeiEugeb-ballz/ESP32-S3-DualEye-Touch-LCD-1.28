// Copyright (c) 2026 Waveshare / Xiaozhi Project
// Photographic eye texture tables ported from Adafruit Uncanny_Eyes architecture
#pragma once
#include "eye_types.h"

namespace EyeRenderer {

extern const uint16_t kDefaultScleraMap[300 * 300];
extern const uint16_t kDefaultIrisMap[128 * 256];
extern const uint16_t kDefaultPolarMap[240 * 240];
extern const uint8_t  kDefaultUpperEyelidMap[240 * 240];
extern const uint8_t  kDefaultLowerEyelidMap[240 * 240];

extern const EyeAssetConfig kDefaultEyeAsset;

} // namespace EyeRenderer
