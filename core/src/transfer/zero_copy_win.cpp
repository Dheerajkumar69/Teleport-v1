/**
 * @file zero_copy_win.cpp
 * @brief Windows zero-copy implementation using TransmitFile
 */

#include "zero_copy.hpp"
#include "transfer/chunk.hpp"
#include "utils/logger.hpp"

#ifdef _WIN32

#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>

#pragma comment(lib, "mswsock.lib")

namespace teleport {

Result<uint64_t> send_file_zero_copy(
    pal::TcpSocket& socket,
    pal::File& file,
    uint64_t offset,
    uint64_t length
) {
    SOCKET sock = socket.native_handle();
    HANDLE file_handle = file.native_handle();
    
    if (sock == INVALID_SOCKET || file_handle == INVALID_HANDLE_VALUE) {
        return make_error(TELEPORT_ERROR_INVALID_ARGUMENT, "Invalid socket or file handle");
    }
    
    // Get file size if length not specified
    if (length == 0) {
        LARGE_INTEGER size;
        if (!GetFileSizeEx(file_handle, &size)) {
            return make_error(TELEPORT_ERROR_FILE_READ, "Failed to get file size");
        }
        length = size.QuadPart - offset;
    }
    
    // Set file position
    if (offset > 0) {
        LARGE_INTEGER pos;
        pos.QuadPart = offset;
        if (!SetFilePointerEx(file_handle, pos, nullptr, FILE_BEGIN)) {
            return make_error(TELEPORT_ERROR_FILE_READ, "Failed to seek file");
        }
    }
    
    // TransmitFile can only send up to 2GB at a time
    constexpr uint64_t MAX_TRANSMIT = 2ULL * 1024 * 1024 * 1024 - 1;
    uint64_t total_sent = 0;
    
    while (total_sent < length) {
        DWORD to_send = static_cast<DWORD>(
            std::min(length - total_sent, MAX_TRANSMIT)
        );
        
        OVERLAPPED overlapped = {};
        overlapped.Offset = static_cast<DWORD>(offset + total_sent);
        overlapped.OffsetHigh = static_cast<DWORD>((offset + total_sent) >> 32);
        
        BOOL result = TransmitFile(
            sock,
            file_handle,
            to_send,
            0,  // Use default send size
            &overlapped,
            nullptr,  // No header/trailer
            TF_USE_KERNEL_APC
        );
        
        if (!result) {
            DWORD err = WSAGetLastError();
            if (err == WSA_IO_PENDING) {
                // Wait for completion
                DWORD transferred = 0;
                DWORD flags = 0;
                if (!WSAGetOverlappedResult(sock, &overlapped, &transferred, TRUE, &flags)) {
                    return make_error(TELEPORT_ERROR_SOCKET_SEND, 
                        "TransmitFile failed: " + std::to_string(WSAGetLastError()));
                }
                total_sent += transferred;
            } else {
                return make_error(TELEPORT_ERROR_SOCKET_SEND,
                    "TransmitFile failed: " + std::to_string(err));
            }
        } else {
            total_sent += to_send;
        }
    }
    
    LOG_DEBUG("Zero-copy sent ", total_sent, " bytes");
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
    SOCKET sock = socket.native_handle();
    HANDLE file_handle = file.native_handle();
    
    if (sock == INVALID_SOCKET || file_handle == INVALID_HANDLE_VALUE) {
        return make_error(TELEPORT_ERROR_INVALID_ARGUMENT, "Invalid handles");
    }
    
    // Build chunk header
    ChunkHeader header;
    header.file_id = file_id;
    header.chunk_id = chunk_id;
    header.offset = static_cast<uint32_t>(offset % UINT32_MAX);
    header.size = size;
    
    uint8_t header_buf[ChunkHeader::HEADER_SIZE];
    header.serialize(header_buf);
    
    // Set file position
    LARGE_INTEGER pos;
    pos.QuadPart = offset;
    if (!SetFilePointerEx(file_handle, pos, nullptr, FILE_BEGIN)) {
        return make_error(TELEPORT_ERROR_FILE_READ, "Failed to seek");
    }
    
    // Use TransmitFile with header
    TRANSMIT_FILE_BUFFERS buffers = {};
    buffers.Head = header_buf;
    buffers.HeadLength = ChunkHeader::HEADER_SIZE;
    
    OVERLAPPED overlapped = {};
    overlapped.Offset = static_cast<DWORD>(offset);
    overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
    
    BOOL result = TransmitFile(
        sock,
        file_handle,
        size,
        0,
        &overlapped,
        &buffers,
        TF_USE_KERNEL_APC
    );
    
    if (!result) {
        DWORD err = WSAGetLastError();
        if (err == WSA_IO_PENDING) {
            DWORD transferred = 0;
            DWORD flags = 0;
            if (!WSAGetOverlappedResult(sock, &overlapped, &transferred, TRUE, &flags)) {
                return make_error(TELEPORT_ERROR_SOCKET_SEND,
                    "TransmitFile failed: " + std::to_string(WSAGetLastError()));
            }
            return static_cast<uint64_t>(transferred);
        }
        return make_error(TELEPORT_ERROR_SOCKET_SEND,
            "TransmitFile failed: " + std::to_string(err));
    }
    
    return static_cast<uint64_t>(ChunkHeader::HEADER_SIZE + size);
}

bool is_zero_copy_available() {
    return true;  // TransmitFile available on all Windows versions
}

uint64_t get_zero_copy_max_size() {
    return 2ULL * 1024 * 1024 * 1024 - 1;  // 2GB - 1
}

} // namespace teleport

#else

// Stub for non-Windows
namespace teleport {

Result<uint64_t> send_file_zero_copy(
    pal::TcpSocket& socket,
    pal::File& file,
    uint64_t offset,
    uint64_t length
) {
    return make_error(TELEPORT_ERROR_NOT_SUPPORTED, "Zero-copy not implemented");
}

Result<uint64_t> send_chunk_zero_copy(
    pal::TcpSocket& socket,
    pal::File& file,
    uint32_t file_id,
    uint32_t chunk_id,
    uint64_t offset,
    uint32_t size
) {
    return make_error(TELEPORT_ERROR_NOT_SUPPORTED, "Zero-copy not implemented");
}

bool is_zero_copy_available() { return false; }
uint64_t get_zero_copy_max_size() { return 0; }

} // namespace teleport

#endif
