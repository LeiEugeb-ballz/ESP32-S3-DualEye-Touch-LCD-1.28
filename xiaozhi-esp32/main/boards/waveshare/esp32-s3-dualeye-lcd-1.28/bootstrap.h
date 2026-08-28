#pragma once

#include <string>

namespace Bootstrap {

// Initialize the bootstrap system manifest, default soul traits, and documentation on flash
void Initialize();

// Returns the complete system architecture and capability manifest
const char* GetManifest();

} // namespace Bootstrap
