/**
 * @file resume_manager.hpp
 * @brief Resume state persistence and recovery
 * 
 * Saves transfer progress to disk so interrupted transfers can resume.
 */

#ifndef TELEPORT_RESUME_MANAGER_HPP
#define TELEPORT_RESUME_MANAGER_HPP

#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include "transfer/parallel_transfer.hpp"

namespace teleport {

/**
 * @brief Persisted resume state for a file transfer
 */
struct ResumeState {
    static constexpr uint32_t MAGIC = 0x54504C52;  // "TPLR"
    static constexpr uint32_t VERSION = 1;
    
    std::string file_name;
    uint64_t file_size = 0;
    uint32_t file_id = 0;
    uint32_t chunk_size = 0;
    uint32_t total_chunks = 0;
    std::vector<uint32_t> received_chunks;
    std::string sender_id;
    std::string session_token;
    uint64_t timestamp = 0;
    
    bool is_valid() const {
        return file_size > 0 && total_chunks > 0 && !file_name.empty();
    }
    
    float progress() const {
        return total_chunks ? float(received_chunks.size()) / total_chunks : 0;
    }
};

/**
 * @brief Manages resume state persistence
 */
class ResumeManager {
public:
    explicit ResumeManager(const std::string& state_dir);
    
    /**
     * @brief Save transfer state to disk
     */
    bool save(const ResumeState& state);
    
    /**
     * @brief Load transfer state from disk
     * @param file_name Name of the file being transferred
     * @param sender_id ID of the sender device
     */
    ResumeState load(const std::string& file_name, const std::string& sender_id);
    
    /**
     * @brief Check if resume state exists
     */
    bool has_resume_state(const std::string& file_name, const std::string& sender_id);
    
    /**
     * @brief Clear resume state after successful transfer
     */
    void clear(const std::string& file_name, const std::string& sender_id);
    
    /**
     * @brief Clear all resume states older than max_age_seconds
     */
    void cleanup(uint64_t max_age_seconds = 86400);  // 24 hours default
    
    /**
     * @brief Create ResumeRequestMessage payload from state
     */
    static std::vector<uint32_t> get_resume_chunks(const ResumeState& state);

private:
    std::string get_state_path(const std::string& file_name, const std::string& sender_id);
    std::string m_state_dir;
};

/**
 * @brief Serialize resume state to binary
 */
inline std::vector<uint8_t> serialize_resume_state(const ResumeState& state) {
    std::vector<uint8_t> data;
    
    auto write_u32 = [&](uint32_t v) {
        data.push_back(v >> 24);
        data.push_back((v >> 16) & 0xFF);
        data.push_back((v >> 8) & 0xFF);
        data.push_back(v & 0xFF);
    };
    
    auto write_u64 = [&](uint64_t v) {
        for (int i = 7; i >= 0; --i) {
            data.push_back((v >> (i * 8)) & 0xFF);
        }
    };
    
    auto write_str = [&](const std::string& s) {
        write_u32(static_cast<uint32_t>(s.size()));
        data.insert(data.end(), s.begin(), s.end());
    };
    
    // Header
    write_u32(ResumeState::MAGIC);
    write_u32(ResumeState::VERSION);
    
    // Data
    write_str(state.file_name);
    write_u64(state.file_size);
    write_u32(state.file_id);
    write_u32(state.chunk_size);
    write_u32(state.total_chunks);
    write_u32(static_cast<uint32_t>(state.received_chunks.size()));
    for (uint32_t c : state.received_chunks) {
        write_u32(c);
    }
    write_str(state.sender_id);
    write_str(state.session_token);
    write_u64(state.timestamp);
    
    return data;
}

/**
 * @brief Deserialize resume state from binary
 */
inline ResumeState deserialize_resume_state(const std::vector<uint8_t>& data) {
    ResumeState state;
    size_t pos = 0;
    
    auto read_u32 = [&]() -> uint32_t {
        if (pos + 4 > data.size()) return 0;
        uint32_t v = (data[pos] << 24) | (data[pos+1] << 16) | 
                     (data[pos+2] << 8) | data[pos+3];
        pos += 4;
        return v;
    };
    
    auto read_u64 = [&]() -> uint64_t {
        if (pos + 8 > data.size()) return 0;
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v = (v << 8) | data[pos + i];
        }
        pos += 8;
        return v;
    };
    
    auto read_str = [&]() -> std::string {
        uint32_t len = read_u32();
        if (pos + len > data.size()) return "";
        std::string s(data.begin() + pos, data.begin() + pos + len);
        pos += len;
        return s;
    };
    
    // Validate header
    uint32_t magic = read_u32();
    uint32_t version = read_u32();
    
    if (magic != ResumeState::MAGIC || version != ResumeState::VERSION) {
        return state;  // Invalid
    }
    
    // Read data
    state.file_name = read_str();
    state.file_size = read_u64();
    state.file_id = read_u32();
    state.chunk_size = read_u32();
    state.total_chunks = read_u32();
    
    uint32_t chunk_count = read_u32();
    state.received_chunks.reserve(chunk_count);
    for (uint32_t i = 0; i < chunk_count; ++i) {
        state.received_chunks.push_back(read_u32());
    }
    
    state.sender_id = read_str();
    state.session_token = read_str();
    state.timestamp = read_u64();
    
    return state;
}

} // namespace teleport

#endif // TELEPORT_RESUME_MANAGER_HPP
