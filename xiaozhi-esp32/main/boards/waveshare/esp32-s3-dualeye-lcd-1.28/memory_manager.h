// Copyright (c) 2026 Waveshare / Xiaozhi Project
// Persistent Memory & Persona Manager
#pragma once
#include <string>
#include <vector>

namespace MemoryManager {

void Initialize();

// File / Notes System
bool WriteNote(const std::string& title, const std::string& content);
std::string ReadNote(const std::string& title);
std::vector<std::string> ListNotes();
bool DeleteNote(const std::string& title);
std::string GetNotesSummaryJson();

// Persona & Soul System
void SetMood(const std::string& mood);
std::string GetMood();
bool SaveTrait(const std::string& key, const std::string& value);
std::string ReadTrait(const std::string& key);
std::string GetAllTraitsJson();

} // namespace MemoryManager
