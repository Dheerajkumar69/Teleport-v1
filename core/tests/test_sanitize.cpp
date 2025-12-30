/**
 * @file test_sanitize.cpp
 * @brief Unit tests for input sanitization utilities
 */

#include <gtest/gtest.h>
#include "utils/sanitize.hpp"

using namespace teleport;

class SanitizeTest : public ::testing::Test {};

// ============================================================================
// Filename Sanitization Tests
// ============================================================================

TEST_F(SanitizeTest, SanitizeFilename_Normal) {
    EXPECT_EQ(sanitize_filename("document.pdf"), "document.pdf");
    EXPECT_EQ(sanitize_filename("my_file.txt"), "my_file.txt");
    EXPECT_EQ(sanitize_filename("report-2024.docx"), "report-2024.docx");
}

TEST_F(SanitizeTest, SanitizeFilename_PathTraversal) {
    // Path traversal attempts MUST be blocked
    EXPECT_EQ(sanitize_filename("../../../etc/passwd"), "etc_passwd");
    EXPECT_EQ(sanitize_filename("..\\..\\windows\\system.ini"), "windows_system.ini");
    EXPECT_EQ(sanitize_filename("./hidden"), "hidden");
}

TEST_F(SanitizeTest, SanitizeFilename_PathSeparators) {
    // Path separators should be replaced
    EXPECT_EQ(sanitize_filename("folder/file.txt"), "folder_file.txt");
    EXPECT_EQ(sanitize_filename("folder\\file.txt"), "folder_file.txt");
}

TEST_F(SanitizeTest, SanitizeFilename_SpecialCharacters) {
    // Invalid characters replaced with underscore
    EXPECT_EQ(sanitize_filename("file<>:\"|?*.txt"), "file________.txt");
    EXPECT_EQ(sanitize_filename("file\x00name.txt"), "filename.txt"); // Null byte
}

TEST_F(SanitizeTest, SanitizeFilename_LeadingDots) {
    // Leading dots removed (hidden files, relative paths)
    EXPECT_EQ(sanitize_filename(".hidden"), "hidden");
    EXPECT_EQ(sanitize_filename("...dots"), "dots");
    EXPECT_EQ(sanitize_filename(".. ..file"), "file");
}

TEST_F(SanitizeTest, SanitizeFilename_TrailingSpacesAndDots) {
    // Windows issue - trailing dots/spaces
    EXPECT_EQ(sanitize_filename("file.txt   "), "file.txt");
    EXPECT_EQ(sanitize_filename("file..."), "file");
}

TEST_F(SanitizeTest, SanitizeFilename_ReservedNames) {
    // Windows reserved device names
    EXPECT_EQ(sanitize_filename("CON"), "_CON");
    EXPECT_EQ(sanitize_filename("con"), "_con");
    EXPECT_EQ(sanitize_filename("NUL.txt"), "_NUL.txt");
    EXPECT_EQ(sanitize_filename("COM1"), "_COM1");
    EXPECT_EQ(sanitize_filename("LPT9.doc"), "_LPT9.doc");
}

TEST_F(SanitizeTest, SanitizeFilename_Empty) {
    EXPECT_EQ(sanitize_filename(""), "unnamed");
    EXPECT_EQ(sanitize_filename("   "), "unnamed");
    EXPECT_EQ(sanitize_filename("..."), "unnamed");
}

TEST_F(SanitizeTest, SanitizeFilename_LongName) {
    std::string long_name(300, 'a');
    std::string result = sanitize_filename(long_name);
    EXPECT_LE(result.size(), 240);
}

TEST_F(SanitizeTest, SanitizeFilename_LongNameWithExtension) {
    std::string long_name = std::string(300, 'a') + ".pdf";
    std::string result = sanitize_filename(long_name);
    EXPECT_LE(result.size(), 240);
    EXPECT_TRUE(result.find(".pdf") != std::string::npos);
}

// ============================================================================
// IP Validation Tests
// ============================================================================

TEST_F(SanitizeTest, ValidateIPv4_Valid) {
    EXPECT_TRUE(validate_ipv4("192.168.1.1"));
    EXPECT_TRUE(validate_ipv4("10.0.0.1"));
    EXPECT_TRUE(validate_ipv4("0.0.0.0"));
    EXPECT_TRUE(validate_ipv4("255.255.255.255"));
    EXPECT_TRUE(validate_ipv4("172.16.0.1"));
}

TEST_F(SanitizeTest, ValidateIPv4_Invalid) {
    EXPECT_FALSE(validate_ipv4(""));
    EXPECT_FALSE(validate_ipv4("256.1.1.1"));
    EXPECT_FALSE(validate_ipv4("192.168.1"));
    EXPECT_FALSE(validate_ipv4("192.168.1.1.1"));
    EXPECT_FALSE(validate_ipv4("abc.def.ghi.jkl"));
    EXPECT_FALSE(validate_ipv4("192.168.1."));
    EXPECT_FALSE(validate_ipv4(".192.168.1.1"));
    EXPECT_FALSE(validate_ipv4("192.168.1.1."));
    EXPECT_FALSE(validate_ipv4("192..168.1.1"));
}

// ============================================================================
// Port Validation Tests
// ============================================================================

TEST_F(SanitizeTest, ValidatePort_Valid) {
    EXPECT_TRUE(validate_port(1));
    EXPECT_TRUE(validate_port(80));
    EXPECT_TRUE(validate_port(443));
    EXPECT_TRUE(validate_port(8080));
    EXPECT_TRUE(validate_port(45455));
    EXPECT_TRUE(validate_port(65535));
}

TEST_F(SanitizeTest, ValidatePort_Invalid) {
    EXPECT_FALSE(validate_port(0));
}

// ============================================================================
// Device Name Sanitization Tests
// ============================================================================

TEST_F(SanitizeTest, SanitizeDeviceName_Normal) {
    EXPECT_EQ(sanitize_device_name("My Laptop"), "My Laptop");
    EXPECT_EQ(sanitize_device_name("Server-01"), "Server-01");
    EXPECT_EQ(sanitize_device_name("Desktop_PC"), "Desktop_PC");
}

TEST_F(SanitizeTest, SanitizeDeviceName_SpecialChars) {
    EXPECT_EQ(sanitize_device_name("PC<script>"), "PCscript");
    EXPECT_EQ(sanitize_device_name("Device\x00Name"), "DeviceName");
}

TEST_F(SanitizeTest, SanitizeDeviceName_Empty) {
    EXPECT_EQ(sanitize_device_name(""), "Unknown Device");
    EXPECT_EQ(sanitize_device_name("   "), "Unknown Device");
}

TEST_F(SanitizeTest, SanitizeDeviceName_TooLong) {
    std::string long_name(100, 'A');
    std::string result = sanitize_device_name(long_name);
    EXPECT_LE(result.size(), 64);
}
