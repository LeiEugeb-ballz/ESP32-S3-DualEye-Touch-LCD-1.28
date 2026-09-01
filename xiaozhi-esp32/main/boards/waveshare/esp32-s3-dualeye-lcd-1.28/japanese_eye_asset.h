// Copyright (c) 2026 Waveshare / Xiaozhi Project
// Photorealistic Japanese Human Eye Preset
#pragma once
#include "eye_types.h"

namespace EyeRenderer {

extern const uint16_t kJapaneseScleraMap[240 * 240];
extern const uint16_t kJapaneseIrisMap[128 * 512];
extern const uint8_t  kJapaneseUpperLid[240];
extern const uint8_t  kJapaneseLowerLid[240];

extern const EyeAssetConfig kJapaneseEyeAsset;

} // namespace EyeRenderer
