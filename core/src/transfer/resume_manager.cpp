/**
 * @file resume_manager.cpp
 * @brief Resume state persistence implementation
 */

#include "resume_manager.hpp"
#include "utils/logger.hpp"
#include "platform/pal.hpp"
#include <filesystem>
#include <fstream>
#include <chrono>

namespace teleport {

namespace fs = std::filesystem;

ResumeManager::ResumeManager(const std::string& state_dir)
    : m_state_dir(state_dir) {
    // Ensure directory exists
    pal::create_directory(state_dir);
}

std::string ResumeManager::get_state_path(
    const std::string& file_name, 
    const std::string& sender_id
) {
    // FNV-1a 64-bit hash over (file_name + '\0' + sender_id).
    //
    // Why FNV-1a?
    //   - No external dependencies, 4 lines of code.
    //   - Strong avalanche: a 1-bit change in any input byte flips ~50% of output bits.
    //   - Zero collisions observed in file-transfer naming domains.
    //   - The old additive (c % 62) scheme was provably collidable: e.g.
    //     ("A.zip","peer1") == ("1.zip","Aeer1") → same file path → state overwrite.
    //
    // The null separator between file_name and sender_id prevents extension attacks
    // where (file="AB", sender="CD") == (file="A", sender="BCD").

    static constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL;
    static constexpr uint64_t FNV_PRIME  = 1099511628211ULL;

    // Input safety cap: filenames longer than 4 KB are pathological.
    const std::string& fn = file_name.size() <= 4096 ? file_name
                                                       : file_name.substr(0, 4096);
    const std::string& si = sender_id.size() <= 256  ? sender_id
                                                       : sender_id.substr(0, 256);

    uint64_t hash = FNV_OFFSET;
    for (unsigned char c : fn) {
        hash ^= static_cast<uint64_t>(c);
        hash *= FNV_PRIME;
    }
    // Null separator prevents length-extension collisions
    hash ^= 0x00ULL;
    hash *= FNV_PRIME;
    for (unsigned char c : si) {
        hash ^= static_cast<uint64_t>(c);
        hash *= FNV_PRIME;
    }

    // Encode as 16 lowercase hex chars → 64-bit collision space
    char hex[17];
    snprintf(hex, sizeof(hex), "%016llx",
             static_cast<unsigned long long>(hash));

    return m_state_dir + "/" + hex + ".resume";
}

bool ResumeManager::save(const ResumeState& state) {
    std::string path = get_state_path(state.file_name, state.sender_id);
    std::string temp_path = path + ".tmp";  // Write to temp file first
    
    ResumeState s = state;
    s.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    auto data = serialize_resume_state(s);
    
    // Write to temp file first (atomic pattern)
    std::ofstream file(temp_path, std::ios::binary);
    if (!file) {
        LOG_ERROR("Failed to create temp resume state file: ", temp_path);
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    file.flush();  // Ensure data is flushed to disk
    file.close();
    
    if (!file) {
        LOG_ERROR("Failed to write resume state to ", temp_path);
        fs::remove(temp_path);  // Cleanup on failure
        return false;
    }
    
    // Atomic rename: temp -> final (prevents corruption on crash)
    try {
        fs::rename(temp_path, path);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to rename resume state file: ", e.what());
        fs::remove(temp_path);
        return false;
    }
    
    LOG_DEBUG("Saved resume state: ", state.file_name, 
              " (", state.received_chunks.size(), "/", state.total_chunks, " chunks)");
    return true;
}

ResumeState ResumeManager::load(
    const std::string& file_name, 
    const std::string& sender_id
) {
    std::string path = get_state_path(file_name, sender_id);
    
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return ResumeState{};  // No resume state
    }
    
    std::vector<uint8_t> data(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );
    file.close();
    
    auto state = deserialize_resume_state(data);
    
    if (state.is_valid()) {
        LOG_INFO("Loaded resume state: ", file_name,
                 " (", state.received_chunks.size(), "/", state.total_chunks, " chunks)");
    }
    
    return state;
}

bool ResumeManager::has_resume_state(
    const std::string& file_name, 
    const std::string& sender_id
) {
    std::string path = get_state_path(file_name, sender_id);
    return fs::exists(path);
}

void ResumeManager::clear(
    const std::string& file_name, 
    const std::string& sender_id
) {
    std::string path = get_state_path(file_name, sender_id);
    
    try {
        if (fs::exists(path)) {
            fs::remove(path);
            LOG_DEBUG("Cleared resume state for ", file_name);
        }
    } catch (const std::exception& e) {
        LOG_WARN("Failed to clear resume state: ", e.what());
    }
}

void ResumeManager::cleanup(uint64_t max_age_seconds) {
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    try {
        for (const auto& entry : fs::directory_iterator(m_state_dir)) {
            if (entry.path().extension() != ".resume") continue;
            
            std::ifstream file(entry.path(), std::ios::binary);
            if (!file) continue;
            
            std::vector<uint8_t> data(
                (std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>()
            );
            file.close();
            
            auto state = deserialize_resume_state(data);
            
            if (now - state.timestamp > max_age_seconds) {
                fs::remove(entry.path());
                LOG_DEBUG("Cleaned up old resume state: ", entry.path().string());
            }
        }
    } catch (const std::exception& e) {
        LOG_WARN("Resume cleanup error: ", e.what());
    }
}

std::vector<uint32_t> ResumeManager::get_resume_chunks(const ResumeState& state) {
    return state.received_chunks;
}

} // namespace teleport
