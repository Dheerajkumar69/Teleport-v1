/**
 * @file test_transfer.cpp
 * @brief Unit tests for transfer components
 */

#include <gtest/gtest.h>
#include "transfer/chunk_reader.hpp"
#include "transfer/chunk_writer.hpp"
#include "platform/pal.hpp"
#include <fstream>
#include <filesystem>
#include <random>

using namespace teleport;
namespace fs = std::filesystem;

class TransferTest : public ::testing::Test {
protected:
    std::string test_dir;
    
    void SetUp() override {
        pal::platform_init();
        test_dir = "test_transfer_temp";
        fs::create_directories(test_dir);
    }
    
    void TearDown() override {
        fs::remove_all(test_dir);
        pal::platform_cleanup();
    }
    
    // Helper to create a test file with random content
    std::string create_test_file(const std::string& name, size_t size) {
        std::string path = test_dir + "/" + name;
        std::ofstream file(path, std::ios::binary);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, 255);
        
        for (size_t i = 0; i < size; ++i) {
            char c = static_cast<char>(dist(gen));
            file.write(&c, 1);
        }
        
        return path;
    }
    
    // Helper to compare file contents
    bool files_equal(const std::string& path1, const std::string& path2) {
        std::ifstream file1(path1, std::ios::binary);
        std::ifstream file2(path2, std::ios::binary);
        
        if (!file1.is_open() || !file2.is_open()) return false;
        
        file1.seekg(0, std::ios::end);
        file2.seekg(0, std::ios::end);
        
        if (file1.tellg() != file2.tellg()) return false;
        
        file1.seekg(0);
        file2.seekg(0);
        
        char c1, c2;
        while (file1.get(c1) && file2.get(c2)) {
            if (c1 != c2) return false;
        }
        
        return true;
    }
};

// ============================================================================
// ChunkReader Tests
// ============================================================================

TEST_F(TransferTest, ChunkReaderOpen) {
    auto path = create_test_file("reader_test.bin", 10000);
    
    ChunkReader reader(path, 1024);
    
    EXPECT_TRUE(reader.is_open());
    EXPECT_EQ(reader.size(), 10000);
    EXPECT_EQ(reader.chunk_count(), 10);  // ceil(10000/1024) = 10
}

TEST_F(TransferTest, ChunkReaderInvalidFile) {
    ChunkReader reader("nonexistent_file.bin", 1024);
    
    EXPECT_FALSE(reader.is_open());
    EXPECT_EQ(reader.size(), 0);
}

TEST_F(TransferTest, ChunkReaderSequential) {
    auto path = create_test_file("sequential.bin", 2500);
    
    ChunkReader reader(path, 1000);
    ASSERT_TRUE(reader.is_open());
    EXPECT_EQ(reader.chunk_count(), 3);
    
    std::vector<uint8_t> buffer(1000);
    
    // Read chunk 0
    auto result = reader.read_next(buffer.data());
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(*result, 1000);
    EXPECT_EQ(reader.current_chunk(), 1);
    
    // Read chunk 1
    result = reader.read_next(buffer.data());
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(*result, 1000);
    EXPECT_EQ(reader.current_chunk(), 2);
    
    // Read chunk 2 (partial)
    result = reader.read_next(buffer.data());
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(*result, 500);  // Remaining bytes
    EXPECT_EQ(reader.current_chunk(), 3);
    
    // EOF
    result = reader.read_next(buffer.data());
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(*result, 0);
}

TEST_F(TransferTest, ChunkReaderRandomAccess) {
    auto path = create_test_file("random.bin", 5000);
    
    ChunkReader reader(path, 1000);
    ASSERT_TRUE(reader.is_open());
    
    std::vector<uint8_t> buffer(1000);
    
    // Read chunk 3 directly
    auto result = reader.read_chunk(3, buffer.data());
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(*result, 1000);
    
    // Read chunk 0
    result = reader.read_chunk(0, buffer.data());
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(*result, 1000);
    
    // Read last chunk (partial)
    result = reader.read_chunk(4, buffer.data());
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(*result, 1000);
}

TEST_F(TransferTest, ChunkReaderReset) {
    auto path = create_test_file("reset.bin", 3000);
    
    ChunkReader reader(path, 1000);
    ASSERT_TRUE(reader.is_open());
    
    std::vector<uint8_t> buffer(1000);
    
    // Read two chunks
    reader.read_next(buffer.data());
    reader.read_next(buffer.data());
    EXPECT_EQ(reader.current_chunk(), 2);
    
    // Reset
    auto result = reader.reset();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(reader.current_chunk(), 0);
}

// ============================================================================
// ChunkWriter Tests
// ============================================================================

TEST_F(TransferTest, ChunkWriterCreate) {
    std::string path = test_dir + "/writer_test.bin";
    
    ChunkWriter writer(path, 5000, 1000);
    
    EXPECT_TRUE(writer.is_open());
    EXPECT_EQ(writer.expected_size(), 5000);
    EXPECT_EQ(writer.bytes_written(), 0);
    EXPECT_FALSE(writer.is_complete());
}

TEST_F(TransferTest, ChunkWriterSequential) {
    std::string path = test_dir + "/sequential_write.bin";
    
    ChunkWriter writer(path, 2500, 1000);
    ASSERT_TRUE(writer.is_open());
    
    std::vector<uint8_t> data(1000, 0xAB);
    
    // Write chunks
    auto result = writer.write_next(data.data(), 1000);
    ASSERT_TRUE(result.ok());
    
    result = writer.write_next(data.data(), 1000);
    ASSERT_TRUE(result.ok());
    
    result = writer.write_next(data.data(), 500);  // Last partial chunk
    ASSERT_TRUE(result.ok());
    
    EXPECT_EQ(writer.bytes_written(), 2500);
    EXPECT_TRUE(writer.is_complete());
    
    writer.finalize();
    
    // Verify file size
    EXPECT_EQ(fs::file_size(path), 2500);
}

TEST_F(TransferTest, ChunkWriterRandomOrder) {
    std::string path = test_dir + "/random_write.bin";
    
    ChunkWriter writer(path, 3000, 1000);
    ASSERT_TRUE(writer.is_open());
    
    std::vector<uint8_t> data(1000, 0xCD);
    
    // Write chunks out of order
    auto result = writer.write_chunk(2, data.data(), 1000);
    ASSERT_TRUE(result.ok());
    
    result = writer.write_chunk(0, data.data(), 1000);
    ASSERT_TRUE(result.ok());
    
    result = writer.write_chunk(1, data.data(), 1000);
    ASSERT_TRUE(result.ok());
    
    EXPECT_TRUE(writer.is_complete());
    EXPECT_EQ(writer.bytes_written(), 3000);
    
    auto received = writer.received_chunks();
    EXPECT_EQ(received.size(), 3);
    
    auto missing = writer.missing_chunks();
    EXPECT_EQ(missing.size(), 0);
}

TEST_F(TransferTest, ChunkWriterResume) {
    std::string path = test_dir + "/resume_write.bin";
    
    ChunkWriter writer(path, 5000, 1000);
    ASSERT_TRUE(writer.is_open());
    
    std::vector<uint8_t> data(1000, 0xEF);
    
    // Write some chunks (simulating partial transfer)
    writer.write_chunk(0, data.data(), 1000);
    writer.write_chunk(1, data.data(), 1000);
    writer.write_chunk(3, data.data(), 1000);  // Skip chunk 2
    
    EXPECT_FALSE(writer.is_complete());
    
    auto missing = writer.missing_chunks();
    EXPECT_EQ(missing.size(), 2);  // Chunks 2 and 4
    EXPECT_EQ(missing[0], 2);
    EXPECT_EQ(missing[1], 4);
    
    // Complete transfer
    writer.write_chunk(2, data.data(), 1000);
    writer.write_chunk(4, data.data(), 1000);
    
    EXPECT_TRUE(writer.is_complete());
    EXPECT_EQ(writer.missing_chunks().size(), 0);
}

// ============================================================================
// Round-Trip Transfer Test
// ============================================================================

TEST_F(TransferTest, ChunkRoundTrip) {
    // Create source file
    auto src_path = create_test_file("roundtrip_src.bin", 7500);
    std::string dst_path = test_dir + "/roundtrip_dst.bin";
    
    const uint32_t chunk_size = 2000;
    
    // Read and write
    ChunkReader reader(src_path, chunk_size);
    ChunkWriter writer(dst_path, reader.size(), chunk_size);
    
    ASSERT_TRUE(reader.is_open());
    ASSERT_TRUE(writer.is_open());
    
    std::vector<uint8_t> buffer(chunk_size);
    uint32_t chunk_id = 0;
    
    while (true) {
        auto read_result = reader.read_next(buffer.data());
        ASSERT_TRUE(read_result.ok());
        
        size_t bytes = *read_result;
        if (bytes == 0) break;
        
        auto write_result = writer.write_chunk(chunk_id++, buffer.data(), bytes);
        ASSERT_TRUE(write_result.ok());
    }
    
    writer.finalize();
    
    EXPECT_TRUE(writer.is_complete());
    EXPECT_TRUE(files_equal(src_path, dst_path));
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(TransferTest, EmptyFile) {
    auto path = create_test_file("empty.bin", 0);
    
    ChunkReader reader(path, 1024);
    EXPECT_TRUE(reader.is_open());
    EXPECT_EQ(reader.size(), 0);
    EXPECT_EQ(reader.chunk_count(), 0);
}

TEST_F(TransferTest, ExactChunkBoundary) {
    auto path = create_test_file("exact.bin", 2048);
    
    ChunkReader reader(path, 1024);
    EXPECT_EQ(reader.chunk_count(), 2);
    
    std::vector<uint8_t> buffer(1024);
    
    auto result = reader.read_chunk(0, buffer.data());
    EXPECT_EQ(*result, 1024);
    
    result = reader.read_chunk(1, buffer.data());
    EXPECT_EQ(*result, 1024);
}

TEST_F(TransferTest, SingleByteChunks) {
    auto path = create_test_file("tiny.bin", 10);
    
    ChunkReader reader(path, 1);
    EXPECT_EQ(reader.chunk_count(), 10);
}

TEST_F(TransferTest, LargeChunkSize) {
    auto path = create_test_file("small.bin", 100);
    
    ChunkReader reader(path, 1024 * 1024);  // 1MB chunks
    EXPECT_EQ(reader.chunk_count(), 1);
    
    std::vector<uint8_t> buffer(1024 * 1024);
    auto result = reader.read_chunk(0, buffer.data());
    EXPECT_EQ(*result, 100);
}
