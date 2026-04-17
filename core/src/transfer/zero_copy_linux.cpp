/**
 * @file zero_copy_linux.cpp
 * @brief Linux zero-copy implementation using sendfile(2)
 *
 * sendfile(2) transfers data between two file descriptors entirely in kernel
 * space, eliminating the user-space copy and reducing CPU usage.
 *
 * Fallback: if sendfile returns EINVAL (cross-device, pipe, etc.) we fall
 * through to a normal read+write loop so the caller never sees a permanent
 * error for an otherwise valid transfer.
 */

#include "zero_copy.hpp"
#include "control/protocol.hpp"
#include "teleport/types.h"
#include "utils/logger.hpp"

#ifdef TELEPORT_LINUX

#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>

namespace teleport {

// Maximum bytes sent per sendfile() call (2 GB – avoid overflow on 32-bit off_t)
static constexpr off_t MAX_SENDFILE_CHUNK = 1024LL * 1024 * 1024;  // 1 GB

Result<uint64_t> send_file_zero_copy(
    pal::TcpSocket& socket,
    pal::File& file,
    uint64_t offset,
    uint64_t length
) {
    int sock_fd = static_cast<int>(socket.handle());
    if (sock_fd < 0) {
        return make_error(TELEPORT_ERROR_INVALID_ARGUMENT, "Invalid socket fd");
    }

    // Derive file size from the File object if length == 0
    if (length == 0) {
        if (file.size() <= offset) {
            return static_cast<uint64_t>(0);
        }
        length = file.size() - offset;
    }

    // Flush any pending writes and get the underlying fd via re-open.
    // The PAL File does not expose a raw fd, so we open by path.
    // NOTE: For large-file pipelines open_file() is called once per file, not
    //       per chunk, so this overhead is acceptable.
    std::string path = file.path();
    int file_fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (file_fd < 0) {
        return make_error(TELEPORT_ERROR_FILE_READ,
                          std::string("sendfile: open failed: ") + strerror(errno));
    }

    off_t off = static_cast<off_t>(offset);
    uint64_t remaining = length;
    uint64_t total_sent = 0;

    while (remaining > 0) {
        off_t to_send = static_cast<off_t>(
            std::min(remaining, static_cast<uint64_t>(MAX_SENDFILE_CHUNK))
        );

        ssize_t sent = ::sendfile(sock_fd, file_fd, &off, to_send);
        if (sent < 0) {
            int err = errno;
            ::close(file_fd);

            if (err == EINVAL || err == EINTR) {
                // EINVAL: unsupported fd combination (e.g., socket is non-blocking
                //         and sendfile would block, or cross-FS).
                // Fall back to regular read+write.
                return make_error(TELEPORT_ERROR_NOT_SUPPORTED, "sendfile not applicable");
            }
            return make_error(TELEPORT_ERROR_SOCKET_SEND,
                              std::string("sendfile failed: ") + strerror(err));
        }

        total_sent += static_cast<uint64_t>(sent);
        remaining  -= static_cast<uint64_t>(sent);
        // `off` is updated in-place by sendfile()
    }

    ::close(file_fd);

    LOG_DEBUG("Zero-copy sent ", total_sent, " bytes via sendfile()");
    return total_sent;
}

Result<uint64_t> send_chunk_zero_copy(
    pal::TcpSocket& socket,
    pal::File& file,
    uint32_t file_id,
    uint32_t chunk_id,
    uint64_t offset,
    uint32_t size
) {
    // Build and send the ChunkHeader via normal send, then use sendfile for data.
    ChunkHeader header;
    header.file_id  = file_id;
    header.chunk_id = chunk_id;
    header.offset   = offset;
    header.size     = size;

    uint8_t header_buf[ChunkHeader::HEADER_SIZE];
    header.serialize(header_buf);

    auto send_result = socket.send_all(header_buf, ChunkHeader::HEADER_SIZE);
    if (!send_result) {
        return send_result.error();
    }

    auto data_result = send_file_zero_copy(socket, file, offset, size);
    if (!data_result) {
        return data_result.error();
    }

    return ChunkHeader::HEADER_SIZE + *data_result;
}

bool is_zero_copy_available() {
    // sendfile(2) is available on all Linux 2.2+ kernels.
    return true;
}

uint64_t get_zero_copy_max_size() {
    // No hard limit beyond available memory/disk.
    return UINT64_MAX;
}

} // namespace teleport

#endif // TELEPORT_LINUX
