#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "teleport/teleport.h"
#include "teleport/types.h"

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void TestChunkHeaderRoundTrip() {
  teleport::ChunkHeader h{};
  h.file_id = std::numeric_limits<uint32_t>::max();
  h.chunk_id = 123456789u;
  h.offset = 42u;
  h.size = 65535u;

  uint8_t wire[teleport::ChunkHeader::HEADER_SIZE] = {0};
  h.serialize(wire);

  const teleport::ChunkHeader parsed = teleport::ChunkHeader::deserialize(wire);
  Require(parsed.file_id == h.file_id, "ChunkHeader file_id round-trip mismatch");
  Require(parsed.chunk_id == h.chunk_id,
          "ChunkHeader chunk_id round-trip mismatch");
  Require(parsed.offset == h.offset, "ChunkHeader offset round-trip mismatch");
  Require(parsed.size == h.size, "ChunkHeader size round-trip mismatch");
}

void TestTransferHandleDestroyNullSafe() { teleport_transfer_destroy(nullptr); }

void TestApiInvalidArguments() {
  const char *onePath[] = {"/definitely/not/found.file"};

  Require(teleport_create(nullptr, nullptr) == TELEPORT_ERROR_INVALID_ARGUMENT,
          "teleport_create should reject null out_engine");

  Require(teleport_send_files(nullptr, nullptr, onePath, 1, nullptr, nullptr,
                              nullptr, nullptr) ==
              TELEPORT_ERROR_INVALID_ARGUMENT,
          "teleport_send_files should reject null engine/target");

  Require(teleport_start_receiving(nullptr, "/tmp", nullptr, nullptr, nullptr,
                                   nullptr) == TELEPORT_ERROR_INVALID_ARGUMENT,
          "teleport_start_receiving should reject null engine");

  Require(teleport_start_receiving(reinterpret_cast<TeleportEngine *>(0x1),
                                   nullptr, nullptr, nullptr, nullptr,
                                   nullptr) == TELEPORT_ERROR_INVALID_ARGUMENT,
          "teleport_start_receiving should reject null output_dir");

  Require(teleport_transfer_pause(nullptr) == TELEPORT_ERROR_INVALID_ARGUMENT,
          "teleport_transfer_pause should reject null transfer");
  Require(teleport_transfer_resume(nullptr) == TELEPORT_ERROR_INVALID_ARGUMENT,
          "teleport_transfer_resume should reject null transfer");
  Require(teleport_transfer_cancel(nullptr) == TELEPORT_ERROR_INVALID_ARGUMENT,
          "teleport_transfer_cancel should reject null transfer");
}

void TestEngineCreateDestroyAndPreConnectFileValidation() {
  TeleportEngine *engine = nullptr;
  const TeleportError createErr = teleport_create(nullptr, &engine);
  Require(createErr == TELEPORT_OK && engine != nullptr,
          "teleport_create failed in selftest");

  TeleportDevice target{};
  std::strncpy(target.id, "test-device", sizeof(target.id) - 1);
  std::strncpy(target.name, "test", sizeof(target.name) - 1);
  std::strncpy(target.ip, "127.0.0.1", sizeof(target.ip) - 1);
  target.port = 6553;

  const char *missingFiles[] = {"/definitely/not/found.file"};
  const TeleportError sendErr =
      teleport_send_files(engine, &target, missingFiles, 1, nullptr, nullptr,
                          nullptr, nullptr);
  Require(sendErr == TELEPORT_ERROR_FILE_OPEN,
          "teleport_send_files should fail-fast on missing files");

  teleport_destroy(engine);
}

} // namespace

int main() {
  try {
    TestChunkHeaderRoundTrip();
    TestTransferHandleDestroyNullSafe();
    TestApiInvalidArguments();
    TestEngineCreateDestroyAndPreConnectFileValidation();
    std::cout << "teleport_transfer_selftest: PASS" << std::endl;
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "teleport_transfer_selftest: FAIL: " << e.what() << std::endl;
    return 1;
  }
}
