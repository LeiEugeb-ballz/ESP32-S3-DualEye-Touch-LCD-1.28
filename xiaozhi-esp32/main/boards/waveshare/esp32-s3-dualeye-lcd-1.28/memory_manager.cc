// Copyright (c) 2026 Waveshare / Xiaozhi Project
// Persistent Memory & Persona Implementation
#include "memory_manager.h"
#include "settings.h"
#include "display_manager.h"
#include <esp_log.h>
#include <cJSON.h>
#include <sstream>
#include <algorithm>
#include <mutex>

static const char* TAG = "MemoryManager";
static std::string s_current_mood = "curious";
static std::mutex s_mem_mutex;

namespace MemoryManager {

void Initialize() {
    ESP_LOGI(TAG, "Initializing Persistent Memory & Soul System...");
    Settings persona_set("persona", false);
    std::string saved_mood = persona_set.GetString("mood", "curious");
    if (!saved_mood.empty()) {
        s_current_mood = saved_mood;
    }
}

// ---------------------------------------------------------------------------
// File / Notes System
// ---------------------------------------------------------------------------
static std::vector<std::string> GetNotesListInternal() {
    Settings notes_set("notes_idx", false);
    std::string index_str = notes_set.GetString("index", "");
    std::vector<std::string> list;
    if (index_str.empty()) return list;

    std::stringstream ss(index_str);
    std::string item;
    while (std::getline(ss, item, '|')) {
        if (!item.empty()) {
            list.push_back(item);
        }
    }
    return list;
}

static void SaveNotesListInternal(const std::vector<std::string>& list) {
    Settings notes_set("notes_idx", true);
    std::string combined = "";
    for (size_t i = 0; i < list.size(); i++) {
        combined += list[i];
        if (i + 1 < list.size()) combined += "|";
    }
    notes_set.SetString("index", combined);
}

bool WriteNote(const std::string& title, const std::string& content) {
    if (title.empty()) return false;
    std::lock_guard<std::mutex> lock(s_mem_mutex);

    Settings notes_doc("notes_data", true);
    notes_doc.SetString(title, content);

    auto list = GetNotesListInternal();
    if (std::find(list.begin(), list.end(), title) == list.end()) {
        list.push_back(title);
        SaveNotesListInternal(list);
    }
    ESP_LOGI(TAG, "Saved note [%s] (%u bytes)", title.c_str(), (unsigned int)content.length());
    return true;
}

std::string ReadNote(const std::string& title) {
    if (title.empty()) return "";
    std::lock_guard<std::mutex> lock(s_mem_mutex);

    Settings notes_doc("notes_data", false);
    return notes_doc.GetString(title, "");
}

std::vector<std::string> ListNotes() {
    std::lock_guard<std::mutex> lock(s_mem_mutex);
    return GetNotesListInternal();
}

bool DeleteNote(const std::string& title) {
    if (title.empty()) return false;
    std::lock_guard<std::mutex> lock(s_mem_mutex);

    Settings notes_doc("notes_data", true);
    notes_doc.EraseKey(title);

    auto list = GetNotesListInternal();
    auto it = std::find(list.begin(), list.end(), title);
    if (it != list.end()) {
        list.erase(it);
        SaveNotesListInternal(list);
    }
    ESP_LOGI(TAG, "Deleted note [%s]", title.c_str());
    return true;
}

std::string GetNotesSummaryJson() {
    auto list = ListNotes();
    cJSON* root = cJSON_CreateArray();
    Settings notes_doc("notes_data", false);
    for (const auto& item : list) {
        cJSON* obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "title", item.c_str());
        std::string preview = notes_doc.GetString(item, "");
        if (preview.length() > 60) preview = preview.substr(0, 57) + "...";
        cJSON_AddStringToObject(obj, "preview", preview.c_str());
        cJSON_AddItemToArray(root, obj);
    }
    char* str = cJSON_PrintUnformatted(root);
    std::string res = str ? str : "[]";
    if (str) free(str);
    cJSON_Delete(root);
    return res;
}

// ---------------------------------------------------------------------------
// Persona & Soul System
// ---------------------------------------------------------------------------
void SetMood(const std::string& mood) {
    std::lock_guard<std::mutex> lock(s_mem_mutex);
    s_current_mood = mood;
    Settings persona_set("persona", true);
    persona_set.SetString("mood", mood);
    ESP_LOGI(TAG, "Persona mood set to: %s", mood.c_str());

    // Adjust gaze and pupil parameters according to emotional disposition
    if (mood == "curious" || mood == "excited") {
        DisplayManager::SetAudioEnergy(0.8f, true);
    } else if (mood == "sleepy" || mood == "relaxed") {
        DisplayManager::SetAudioEnergy(0.1f, false);
    } else if (mood == "thoughtful" || mood == "focused") {
        DisplayManager::SetAudioEnergy(0.4f, false);
    }
}

std::string GetMood() {
    std::lock_guard<std::mutex> lock(s_mem_mutex);
    return s_current_mood;
}

bool SaveTrait(const std::string& key, const std::string& value) {
    if (key.empty()) return false;
    std::lock_guard<std::mutex> lock(s_mem_mutex);

    Settings traits("persona", true);
    traits.SetString(key, value);

    Settings trait_idx("trait_idx", true);
    std::string idx = trait_idx.GetString("index", "");
    std::vector<std::string> list;
    if (!idx.empty()) {
        std::stringstream ss(idx);
        std::string item;
        while (std::getline(ss, item, '|')) {
            if (!item.empty()) list.push_back(item);
        }
    }
    if (std::find(list.begin(), list.end(), key) == list.end()) {
        list.push_back(key);
        std::string combined = "";
        for (size_t i = 0; i < list.size(); i++) {
            combined += list[i];
            if (i + 1 < list.size()) combined += "|";
        }
        trait_idx.SetString("index", combined);
    }
    ESP_LOGI(TAG, "Saved persona trait [%s = %s]", key.c_str(), value.c_str());
    return true;
}

std::string ReadTrait(const std::string& key) {
    if (key.empty()) return "";
    std::lock_guard<std::mutex> lock(s_mem_mutex);
    Settings traits("persona", false);
    return traits.GetString(key, "");
}

std::string GetAllTraitsJson() {
    std::lock_guard<std::mutex> lock(s_mem_mutex);
    Settings trait_idx("trait_idx", false);
    std::string idx = trait_idx.GetString("index", "");
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "current_mood", s_current_mood.c_str());

    if (!idx.empty()) {
        std::stringstream ss(idx);
        std::string item;
        Settings traits("persona", false);
        while (std::getline(ss, item, '|')) {
            if (!item.empty()) {
                std::string val = traits.GetString(item, "");
                cJSON_AddStringToObject(root, item.c_str(), val.c_str());
            }
        }
    }
    char* str = cJSON_PrintUnformatted(root);
    std::string res = str ? str : "{}";
    if (str) free(str);
    cJSON_Delete(root);
    return res;
}

} // namespace MemoryManager
