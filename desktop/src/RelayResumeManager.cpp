/**
 * @file RelayResumeManager.cpp
 * @brief Durable persistence of relay transfer state (see header for design).
 */

#include "RelayResumeManager.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace teleport {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

RelayResumeManager::RelayResumeManager(const std::string& stateDir)
    : m_stateDir(stateDir) {}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void RelayResumeManager::save(const RelayResumeState& state) {
    if (m_stateDir.empty()) return;
    if (!isSafeId(state.transferId)) return;

    std::lock_guard<std::mutex> lk(m_mutex);
    if (!ensureStateDir()) return;

    const std::string path = sidecarPath(state.transferId);
    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open()) return;

    f << toJson(state);
    f.flush();
}

bool RelayResumeManager::load(const std::string& transferId,
                               RelayResumeState& out) const {
    if (m_stateDir.empty()) return false;
    if (!isSafeId(transferId)) return false;

    std::lock_guard<std::mutex> lk(m_mutex);
    const std::string path = sidecarPath(transferId);

    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::ostringstream ss;
    ss << f.rdbuf();
    return fromJson(ss.str(), out);
}

void RelayResumeManager::remove(const std::string& transferId) {
    if (m_stateDir.empty()) return;
    if (!isSafeId(transferId)) return;

    std::lock_guard<std::mutex> lk(m_mutex);
    const std::string path = sidecarPath(transferId);
    std::remove(path.c_str());
}

std::vector<RelayResumeState> RelayResumeManager::listPending() const {
    std::vector<RelayResumeState> result;
    if (m_stateDir.empty()) return result;

    std::lock_guard<std::mutex> lk(m_mutex);

    DIR* dir = opendir(m_stateDir.c_str());
    if (!dir) return result;

    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string name = entry->d_name;
        // Only process "*.json" sidecars
        if (name.size() <= 5 ||
            name.substr(name.size() - 5) != ".json") {
            continue;
        }

        const std::string fullPath = m_stateDir + "/" + name;
        std::ifstream f(fullPath);
        if (!f.is_open()) continue;

        std::ostringstream ss;
        ss << f.rdbuf();

        RelayResumeState state;
        if (!fromJson(ss.str(), state)) continue;

        // Only report truly resumable transfers: temp file must still exist
        // and not be empty (receivedBytes > 0 means real progress was made).
        if (state.tempFilePath.empty() || state.receivedBytes == 0) continue;

        struct stat st;
        if (::stat(state.tempFilePath.c_str(), &st) != 0) {
            // Temp file gone — clean up orphaned sidecar
            std::remove(fullPath.c_str());
            continue;
        }

        result.push_back(std::move(state));
    }
    closedir(dir);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private helpers
// ─────────────────────────────────────────────────────────────────────────────

bool RelayResumeManager::ensureStateDir() const {
    struct stat st;
    if (::stat(m_stateDir.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    // Attempt to create
    return ::mkdir(m_stateDir.c_str(), 0700) == 0;
}

std::string RelayResumeManager::sidecarPath(const std::string& id) const {
    return m_stateDir + "/" + id + ".json";
}

bool RelayResumeManager::isSafeId(const std::string& id) {
    if (id.empty() || id.size() > 128) return false;
    for (char c : id) {
        // Allow alphanumerics, hyphens, underscores, dots — reject path chars
        if (!std::isalnum(static_cast<unsigned char>(c)) &&
            c != '-' && c != '_' && c != '.') {
            return false;
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Minimal JSON serializer
// ─────────────────────────────────────────────────────────────────────────────

/* Escape a C++ string for embedding in JSON */
std::string RelayResumeManager::jsonString(const std::string& v) {
    std::string out;
    out.reserve(v.size() + 2);
    out += '"';
    for (char c : v) {
        if      (c == '"')  { out += "\\\""; }
        else if (c == '\\') { out += "\\\\"; }
        else if (c == '\n') { out += "\\n";  }
        else if (c == '\r') { out += "\\r";  }
        else if (c == '\t') { out += "\\t";  }
        else if (static_cast<unsigned char>(c) < 0x20) {
            // Control character — skip
        } else {
            out += c;
        }
    }
    out += '"';
    return out;
}

std::string RelayResumeManager::toJson(const RelayResumeState& s) {
    const int64_t now = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    std::ostringstream o;
    o << "{\n"
      << "  \"transferId\":"     << jsonString(s.transferId)     << ",\n"
      << "  \"fromPeerId\":"     << jsonString(s.fromPeerId)     << ",\n"
      << "  \"filename\":"       << jsonString(s.filename)       << ",\n"
      << "  \"totalSize\":"      << s.totalSize                  << ",\n"
      << "  \"receivedBytes\":"  << s.receivedBytes              << ",\n"
      << "  \"sha256Expected\":" << jsonString(s.sha256Expected) << ",\n"
      << "  \"tempFilePath\":"   << jsonString(s.tempFilePath)   << ",\n"
      << "  \"streaming\":"      << (s.streaming ? "true" : "false") << ",\n"
      << "  \"savedAt\":"        << now                          << "\n"
      << "}\n";
    return o.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Minimal JSON parser (key-value extraction only — no nested objects)
// ─────────────────────────────────────────────────────────────────────────────

/* Extract a string value for "key" from flat JSON. Returns "" on not-found. */
std::string RelayResumeManager::jsonStr(const std::string& json,
                                         const std::string& key) {
    const std::string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    // Skip whitespace
    while (pos < json.size() && json[pos] == ' ') ++pos;
    if (pos >= json.size() || json[pos] != '"') return "";
    ++pos;
    std::string out;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            char esc = json[pos + 1];
            if      (esc == '"')  { out += '"';  pos += 2; }
            else if (esc == '\\') { out += '\\'; pos += 2; }
            else if (esc == 'n')  { out += '\n'; pos += 2; }
            else if (esc == 'r')  { out += '\r'; pos += 2; }
            else if (esc == 't')  { out += '\t'; pos += 2; }
            else                  { out += esc;  pos += 2; }
        } else {
            out += json[pos++];
        }
    }
    return out;
}

size_t RelayResumeManager::jsonSizeT(const std::string& json,
                                      const std::string& key,
                                      size_t def) {
    const std::string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return def;
    pos += needle.size();
    while (pos < json.size() && json[pos] == ' ') ++pos;
    if (pos >= json.size()) return def;
    size_t val = 0;
    bool found = false;
    while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos]))) {
        val = val * 10 + static_cast<size_t>(json[pos++] - '0');
        found = true;
    }
    return found ? val : def;
}

int64_t RelayResumeManager::jsonInt64(const std::string& json,
                                       const std::string& key,
                                       int64_t def) {
    return static_cast<int64_t>(jsonSizeT(json, key, static_cast<size_t>(def)));
}

bool RelayResumeManager::jsonBool(const std::string& json,
                                   const std::string& key,
                                   bool def) {
    const std::string needleTrue  = "\"" + key + "\": true";
    const std::string needleFalse = "\"" + key + "\": false";
    // Also without space
    const std::string needleTrue2  = "\"" + key + "\":true";
    const std::string needleFalse2 = "\"" + key + "\":false";

    if (json.find(needleTrue)  != std::string::npos) return true;
    if (json.find(needleTrue2) != std::string::npos) return true;
    if (json.find(needleFalse)  != std::string::npos) return false;
    if (json.find(needleFalse2) != std::string::npos) return false;
    return def;
}

bool RelayResumeManager::fromJson(const std::string& json,
                                   RelayResumeState& out) {
    // Minimal sanity check
    if (json.empty() || json.find('{') == std::string::npos) return false;

    out.transferId     = jsonStr(json, "transferId");
    if (out.transferId.empty()) return false;

    out.fromPeerId     = jsonStr(json, "fromPeerId");
    out.filename       = jsonStr(json, "filename");
    out.totalSize      = jsonSizeT(json, "totalSize");
    out.receivedBytes  = jsonSizeT(json, "receivedBytes");
    out.sha256Expected = jsonStr(json, "sha256Expected");
    out.tempFilePath   = jsonStr(json, "tempFilePath");
    out.streaming      = jsonBool(json, "streaming");
    out.savedAt        = jsonInt64(json, "savedAt");

    return !out.filename.empty();
}

} // namespace teleport
