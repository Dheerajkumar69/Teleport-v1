/**
 * @file test_integration.cpp
 * @brief Integration tests for end-to-end file transfer
 * 
 * Tests actual transfer functionality including parallel streams,
 * resume, and error handling.
 */

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <fstream>
#include <random>
#include <filesystem>

#include "transfer/parallel_transfer.hpp"
#include "transfer/resume_manager.hpp"
#include "transfer/transfer_manager.hpp"
#include "discovery/discovery.hpp"
#include "control/control_client.hpp"
#include "control/control_server.hpp"
#include "platform/pal.hpp"
#include "utils/logger.hpp"

namespace fs = std::filesystem;
using namespace teleport;

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temp directories
        m_test_dir = fs::temp_directory_path() / "teleport_test";
        m_send_dir = m_test_dir / "send";
        m_recv_dir = m_test_dir / "recv";
        m_state_dir = m_test_dir / "state";
        
        fs::create_directories(m_send_dir);
        fs::create_directories(m_recv_dir);
        fs::create_directories(m_state_dir);
    }
    
    void TearDown() override {
        try {
            fs::remove_all(m_test_dir);
        } catch (...) {}
    }
    
    // Create a test file with random content
    std::string create_test_file(const std::string& name, size_t size) {
        std::string path = (m_send_dir / name).string();
        
        std::ofstream file(path, std::ios::binary);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        std::vector<char> buffer(std::min(size, size_t(1024 * 1024)));
        size_t remaining = size;
        
        while (remaining > 0) {
            size_t chunk = std::min(remaining, buffer.size());
            for (size_t i = 0; i < chunk; ++i) {
                buffer[i] = static_cast<char>(dis(gen));
            }
            file.write(buffer.data(), chunk);
            remaining -= chunk;
        }
        
        file.close();
        return path;
    }
    
    // Verify two files are identical
    bool files_equal(const std::string& path1, const std::string& path2) {
        std::ifstream f1(path1, std::ios::binary);
        std::ifstream f2(path2, std::ios::binary);
        
        if (!f1 || !f2) return false;
        
        constexpr size_t CHUNK = 64 * 1024;
        std::vector<char> buf1(CHUNK), buf2(CHUNK);
        
        while (f1 && f2) {
            f1.read(buf1.data(), CHUNK);
            f2.read(buf2.data(), CHUNK);
            
            if (f1.gcount() != f2.gcount()) return false;
            if (memcmp(buf1.data(), buf2.data(), f1.gcount()) != 0) return false;
        }
        
        return f1.eof() && f2.eof();
    }
    
    fs::path m_test_dir;
    fs::path m_send_dir;
    fs::path m_recv_dir;
    fs::path m_state_dir;
};

// ============================================================================
// Parallel Transfer Tests
// ============================================================================

TEST_F(IntegrationTest, ParallelTransfer_SmallFile) {
    // Create 1MB test file
    auto src_path = create_test_file("small.bin", 1 * 1024 * 1024);
    auto dst_path = (m_recv_dir / "small.bin").string();
    
    // Set up sender and receiver
    ParallelTransfer::Config config;
    config.num_streams = 4;
    config.chunk_size = 256 * 1024;  // 256KB chunks
    
    ParallelTransfer sender(config);
    ParallelTransfer receiver(config);
    
    // Create listening socket
    pal::SocketOptions opts;
    auto listen_sock = pal::create_tcp_socket(opts);
    ASSERT_TRUE(listen_sock->bind(0).has_value());
    ASSERT_TRUE(listen_sock->listen(5).has_value());
    uint16_t port = listen_sock->local_port();
    
    // Receiver thread
    std::thread recv_thread([&]() {
        auto accept_result = receiver.accept(*listen_sock);
        EXPECT_TRUE(accept_result.has_value());
        
        auto recv_result = receiver.receive_file(dst_path, 1, 1024 * 1024, {});
        EXPECT_TRUE(recv_result.has_value());
    });
    
    // Give receiver time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Sender connects and sends
    auto connect_result = sender.connect("127.0.0.1", port);
    ASSERT_TRUE(connect_result.has_value());
    
    auto send_result = sender.send_file(src_path, 1, {});
    EXPECT_TRUE(send_result.has_value());
    
    recv_thread.join();
    
    // Verify files match
    EXPECT_TRUE(files_equal(src_path, dst_path));
}

TEST_F(IntegrationTest, ParallelTransfer_LargeFile) {
    // Create 100MB test file
    auto src_path = create_test_file("large.bin", 100 * 1024 * 1024);
    auto dst_path = (m_recv_dir / "large.bin").string();
    
    ParallelTransfer::Config config;
    config.num_streams = 4;
    config.chunk_size = 2 * 1024 * 1024;  // 2MB chunks
    
    ParallelTransfer sender(config);
    ParallelTransfer receiver(config);
    
    uint64_t recv_bytes = 0;
    receiver.set_progress_callback([&](const ParallelTransfer::Stats& stats) {
        recv_bytes = stats.bytes_received;
    });
    
    pal::SocketOptions opts;
    auto listen_sock = pal::create_tcp_socket(opts);
    ASSERT_TRUE(listen_sock->bind(0).has_value());
    ASSERT_TRUE(listen_sock->listen(5).has_value());
    uint16_t port = listen_sock->local_port();
    
    std::thread recv_thread([&]() {
        receiver.accept(*listen_sock);
        receiver.receive_file(dst_path, 1, 100 * 1024 * 1024, {});
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    sender.connect("127.0.0.1", port);
    sender.send_file(src_path, 1, {});
    
    recv_thread.join();
    
    EXPECT_TRUE(files_equal(src_path, dst_path));
    EXPECT_EQ(recv_bytes, 100 * 1024 * 1024);
}

// ============================================================================
// Resume Tests
// ============================================================================

TEST_F(IntegrationTest, Resume_SaveAndLoad) {
    ResumeManager mgr(m_state_dir.string());
    
    ResumeState state;
    state.file_name = "test.bin";
    state.file_size = 10 * 1024 * 1024;
    state.file_id = 42;
    state.chunk_size = 1024 * 1024;
    state.total_chunks = 10;
    state.received_chunks = {0, 1, 2, 5, 7};
    state.sender_id = "sender-123";
    state.session_token = "token-abc";
    
    EXPECT_TRUE(mgr.save(state));
    EXPECT_TRUE(mgr.has_resume_state("test.bin", "sender-123"));
    
    auto loaded = mgr.load("test.bin", "sender-123");
    
    EXPECT_TRUE(loaded.is_valid());
    EXPECT_EQ(loaded.file_name, "test.bin");
    EXPECT_EQ(loaded.file_size, 10 * 1024 * 1024);
    EXPECT_EQ(loaded.received_chunks.size(), 5);
    EXPECT_FLOAT_EQ(loaded.progress(), 0.5f);
}

TEST_F(IntegrationTest, ChunkTracker_Operations) {
    ChunkTracker tracker(100);
    
    EXPECT_EQ(tracker.received_count(), 0);
    EXPECT_FALSE(tracker.is_complete());
    
    tracker.mark_received(0);
    tracker.mark_received(50);
    tracker.mark_received(99);
    
    EXPECT_EQ(tracker.received_count(), 3);
    EXPECT_TRUE(tracker.is_received(0));
    EXPECT_TRUE(tracker.is_received(50));
    EXPECT_FALSE(tracker.is_received(1));
    
    auto missing = tracker.get_missing_chunks();
    EXPECT_EQ(missing.size(), 97);
    
    // Mark all as received
    for (uint32_t i = 0; i < 100; ++i) {
        tracker.mark_received(i);
    }
    EXPECT_TRUE(tracker.is_complete());
}

// ============================================================================
// Performance Benchmark
// ============================================================================

TEST_F(IntegrationTest, Benchmark_Throughput) {
    // Create 500MB test file
    auto src_path = create_test_file("bench.bin", 500 * 1024 * 1024);
    auto dst_path = (m_recv_dir / "bench.bin").string();
    
    ParallelTransfer::Config config;
    config.num_streams = 4;
    config.chunk_size = 2 * 1024 * 1024;
    
    ParallelTransfer sender(config);
    ParallelTransfer receiver(config);
    
    pal::SocketOptions opts;
    auto listen_sock = pal::create_tcp_socket(opts);
    listen_sock->bind(0);
    listen_sock->listen(5);
    uint16_t port = listen_sock->local_port();
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::thread recv_thread([&]() {
        receiver.accept(*listen_sock);
        receiver.receive_file(dst_path, 1, 500 * 1024 * 1024, {});
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    sender.connect("127.0.0.1", port);
    sender.send_file(src_path, 1, {});
    
    recv_thread.join();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    double mb = 500.0;
    double seconds = duration.count() / 1000.0;
    double throughput = mb / seconds;
    
    std::cout << "\n=== BENCHMARK RESULTS ===" << std::endl;
    std::cout << "File size: 500 MB" << std::endl;
    std::cout << "Streams: 4" << std::endl;
    std::cout << "Duration: " << seconds << " seconds" << std::endl;
    std::cout << "Throughput: " << throughput << " MB/s" << std::endl;
    std::cout << "========================\n" << std::endl;
    
    // Expect at least 100 MB/s on localhost
    EXPECT_GT(throughput, 100.0) << "Throughput below target";
    
    EXPECT_TRUE(files_equal(src_path, dst_path));
}

// Main
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
