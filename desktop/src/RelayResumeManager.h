/**
 * @file RelayResumeManager.h
 * @brief Durable persistence of in-flight relay transfer state.
 *
 * When a WebSocket drops mid-relay the in-memory RelayTransfer is lost.
 * This manager writes a small JSON sidecar for every active incoming relay
 * transfer so the desktop can resume exactly where it left off on reconnect.
 *
 * Sidecar location: <stateDir>/<transferId>.json
 * (stateDir is typically  $downloadPath/.relay_state/)
 *
 * Thread safety:
 *   All public methods are safe to call from any thread (mutex-guarded).
 */

#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace teleport {

// Minimal, self-contained state record — mirrors the fields of RelayTransfer
// that are needed to reconstruct an interrupted receive session.
struct RelayResumeState {
    std::string transferId;
    std::string fromPeerId;
    std::string filename;
    size_t      totalSize     = 0;
    size_t      receivedBytes = 0;  // last durably-committed byte count
    std::string sha256Expected;
    std::string tempFilePath;
    bool        streaming     = false;
    int64_t     savedAt       = 0;  // Unix epoch seconds at last save
};

class RelayResumeManager {
public:
    /**
     * @param stateDir  Directory under which sidecar files are stored.
     *                  Created on first use if it does not exist.
     *                  Pass an empty string to run in no-op (disabled) mode.
     */
    explicit RelayResumeManager(const std::string& stateDir);

    /**
     * Persist (create / overwrite) the sidecar for @p state.
     * Call this: (a) when a relay-start begins, and (b) every
     * PERSIST_INTERVAL_BYTES received during chunked transfer.
     */
    void save(const RelayResumeState& state);

    /**
     * Load the sidecar for @p transferId into @p out.
     * @return true if a valid sidecar was found and parsed.
     */
    bool load(const std::string& transferId, RelayResumeState& out) const;

    /**
     * Delete the sidecar for @p transferId (on successful completion or
     * explicit cancel).
     */
    void remove(const std::string& transferId);

    /**
     * Scan the state directory and return all sidecars whose temp file
     * still exists on disk (i.e. genuinely resumable sessions).
     * Called once during reconnect to discover pending work.
     */
    std::vector<RelayResumeState> listPending() const;

    /**
     * How often (in bytes) to flush the durable byte-count to disk.
     * Smaller = fewer bytes re-sent on reconnect; larger = fewer I/O calls.
     */
    static constexpr size_t PERSIST_INTERVAL_BYTES = 256ULL * 1024; // 256 KB

private:
    std::string m_stateDir;
    mutable std::mutex m_mutex;

    bool ensureStateDir() const;
    std::string sidecarPath(const std::string& transferId) const;

    // Minimal hand-rolled JSON — avoids pulling in nlohmann in this header
    static std::string   toJson(const RelayResumeState& s);
    static bool          fromJson(const std::string& json, RelayResumeState& out);
    static std::string   jsonString(const std::string& v);
    static std::string   jsonStr(const std::string& json,
                                 const std::string& key);
    static size_t        jsonSizeT(const std::string& json,
                                   const std::string& key,
                                   size_t def = 0);
    static bool          jsonBool(const std::string& json,
                                  const std::string& key,
                                  bool def = false);
    static int64_t       jsonInt64(const std::string& json,
                                   const std::string& key,
                                   int64_t def = 0);

    // Validate that a transferId is safe to use as a filename
    static bool isSafeId(const std::string& id);
};

} // namespace teleport
