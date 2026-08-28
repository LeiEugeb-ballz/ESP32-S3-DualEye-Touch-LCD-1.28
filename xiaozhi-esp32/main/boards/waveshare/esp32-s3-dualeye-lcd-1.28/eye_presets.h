// Copyright (c) 2026 Waveshare / Xiaozhi Project
// Photorealistic Eye Presets Pack (Flash RODATA)
#pragma once

#include "eye_types.h"

namespace EyeRenderer {

enum EyePresetId {
    PRESET_DEFAULT = 0,
    PRESET_CAT,
    PRESET_DRAGON,
    PRESET_TERMINATOR,
    PRESET_DOE,
    PRESET_OWL,
    PRESET_GOAT,
    PRESET_NAUGA,
    PRESET_NEWT,
    PRESET_NO_SCLERA,
    PRESET_COUNT
};

extern const EyeAssetConfig kDefaultEyeAsset;
extern const EyeAssetConfig kCatEyeAsset;
extern const EyeAssetConfig kDragonEyeAsset;
extern const EyeAssetConfig kTerminatorEyeAsset;
extern const EyeAssetConfig kDoeEyeAsset;
extern const EyeAssetConfig kOwlEyeAsset;
extern const EyeAssetConfig kGoatEyeAsset;
extern const EyeAssetConfig kNaugaEyeAsset;
extern const EyeAssetConfig kNewtEyeAsset;
extern const EyeAssetConfig kNoScleraEyeAsset;

const EyeAssetConfig* GetEyePreset(EyePresetId id);
const EyeAssetConfig* GetEyePresetByName(const char* name);

} // namespace EyeRenderer
