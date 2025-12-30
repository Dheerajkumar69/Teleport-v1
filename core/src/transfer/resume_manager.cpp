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
    // Create unique filename from file name + sender
    std::string hash;
    for (char c : file_name + sender_id) {
        hash += std::to_string(static_cast<int>(c) % 62);
    }
    if (hash.size() > 32) hash = hash.substr(0, 32);
    
    return m_state_dir + "/" + hash + ".resume";
}

bool ResumeManager::save(const ResumeState& state) {
    std::string path = get_state_path(state.file_name, state.sender_id);
    
    ResumeState s = state;
    s.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    auto data = serialize_resume_state(s);
    
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        LOG_ERROR("Failed to save resume state to ", path);
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    file.close();
    
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
