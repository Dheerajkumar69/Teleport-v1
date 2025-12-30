/**
 * @file test_discovery.cpp
 * @brief Unit tests for discovery protocol
 */

#include <gtest/gtest.h>
#include "discovery/device_list.hpp"
#include "discovery/udp_broadcaster.hpp"
#include "utils/uuid.hpp"
#include "platform/pal.hpp"

using namespace teleport;

class DiscoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        pal::platform_init();
    }
    
    void TearDown() override {
        pal::platform_cleanup();
    }
};

// ============================================================================
// UUID Tests
// ============================================================================

TEST_F(DiscoveryTest, UuidFormat) {
    std::string uuid = generate_uuid();
    
    // Check length (36 chars: 8-4-4-4-12)
    EXPECT_EQ(uuid.length(), 36);
    
    // Check dashes
    EXPECT_EQ(uuid[8], '-');
    EXPECT_EQ(uuid[13], '-');
    EXPECT_EQ(uuid[18], '-');
    EXPECT_EQ(uuid[23], '-');
    
    // Check version (position 14 should be '4')
    EXPECT_EQ(uuid[14], '4');
}

TEST_F(DiscoveryTest, UuidUniqueness) {
    std::set<std::string> uuids;
    
    for (int i = 0; i < 1000; ++i) {
        std::string uuid = generate_uuid();
        EXPECT_EQ(uuids.count(uuid), 0) << "Duplicate UUID: " << uuid;
        uuids.insert(uuid);
    }
    
    EXPECT_EQ(uuids.size(), 1000);
}

// ============================================================================
// DeviceList Tests
// ============================================================================

TEST_F(DiscoveryTest, DeviceListAdd) {
    DeviceList list(5000);
    
    Device device;
    device.id = generate_uuid();
    device.name = "Test Device";
    device.os = OperatingSystem::Windows;
    device.address.ip = "192.168.1.100";
    device.address.port = 45455;
    device.last_seen_ms = now_ms();
    
    bool is_new = list.upsert(device);
    EXPECT_TRUE(is_new);
    EXPECT_EQ(list.count(), 1);
    
    // Update same device
    device.last_seen_ms = now_ms();
    is_new = list.upsert(device);
    EXPECT_FALSE(is_new);
    EXPECT_EQ(list.count(), 1);
}

TEST_F(DiscoveryTest, DeviceListExpiration) {
    // Short TTL for testing
    DeviceList list(100);  // 100ms TTL
    
    Device device;
    device.id = generate_uuid();
    device.name = "Expiring Device";
    device.last_seen_ms = now_ms();
    
    list.upsert(device);
    EXPECT_EQ(list.count(), 1);
    
    // Wait for expiration
    pal::sleep_ms(200);
    
    auto expired = list.remove_expired();
    EXPECT_EQ(expired.size(), 1);
    EXPECT_EQ(expired[0], device.id);
    EXPECT_EQ(list.count(), 0);
}

TEST_F(DiscoveryTest, DeviceListIndexAccess) {
    DeviceList list(5000);
    
    // Add multiple devices
    std::vector<std::string> ids;
    for (int i = 0; i < 5; ++i) {
        Device device;
        device.id = generate_uuid();
        device.name = "Device " + std::to_string(i);
        device.last_seen_ms = now_ms();
        ids.push_back(device.id);
        list.upsert(device);
    }
    
    EXPECT_EQ(list.count(), 5);
    
    // Access by index (in insertion order)
    for (size_t i = 0; i < ids.size(); ++i) {
        auto device = list.get_by_index(i);
        ASSERT_TRUE(device.has_value());
        EXPECT_EQ(device->id, ids[i]);
    }
    
    // Out of bounds
    auto invalid = list.get_by_index(10);
    EXPECT_FALSE(invalid.has_value());
}

TEST_F(DiscoveryTest, DeviceListClear) {
    DeviceList list(5000);
    
    for (int i = 0; i < 10; ++i) {
        Device device;
        device.id = generate_uuid();
        device.last_seen_ms = now_ms();
        list.upsert(device);
    }
    
    EXPECT_EQ(list.count(), 10);
    
    list.clear();
    EXPECT_EQ(list.count(), 0);
}

// ============================================================================
// OS Type Conversion Tests
// ============================================================================

TEST_F(DiscoveryTest, OsTypeConversion) {
    EXPECT_EQ(os_to_string(OperatingSystem::Windows), "Windows");
    EXPECT_EQ(os_to_string(OperatingSystem::macOS), "macOS");
    EXPECT_EQ(os_to_string(OperatingSystem::Linux), "Linux");
    EXPECT_EQ(os_to_string(OperatingSystem::Android), "Android");
    
    EXPECT_EQ(os_from_string("Windows"), OperatingSystem::Windows);
    EXPECT_EQ(os_from_string("macOS"), OperatingSystem::macOS);
    EXPECT_EQ(os_from_string("Linux"), OperatingSystem::Linux);
    EXPECT_EQ(os_from_string("Android"), OperatingSystem::Android);
    EXPECT_EQ(os_from_string("Invalid"), OperatingSystem::Unknown);
}

// ============================================================================
// Capability Tests
// ============================================================================

TEST_F(DiscoveryTest, CapabilityFlags) {
    Capability caps = Capability::Parallel | Capability::Resume;
    
    EXPECT_TRUE(has_capability(caps, Capability::Parallel));
    EXPECT_TRUE(has_capability(caps, Capability::Resume));
    EXPECT_FALSE(has_capability(caps, Capability::Compress));
    EXPECT_FALSE(has_capability(caps, Capability::Encrypt));
}

// ============================================================================
// Network Address Tests
// ============================================================================

TEST_F(DiscoveryTest, NetworkAddressToString) {
    NetworkAddress addr;
    addr.ip = "192.168.1.100";
    addr.port = 45455;
    
    EXPECT_EQ(addr.to_string(), "192.168.1.100:45455");
}

TEST_F(DiscoveryTest, NetworkAddressEquality) {
    NetworkAddress addr1{"192.168.1.100", 45455};
    NetworkAddress addr2{"192.168.1.100", 45455};
    NetworkAddress addr3{"192.168.1.101", 45455};
    
    EXPECT_TRUE(addr1 == addr2);
    EXPECT_FALSE(addr1 == addr3);
}

// ============================================================================
// Platform Tests
// ============================================================================

TEST_F(DiscoveryTest, GetLocalIps) {
    auto ips = pal::get_local_ips();
    
    // Should have at least one IP (unless no network)
    // We don't fail on 0 IPs since CI might have no network
    for (const auto& ip : ips) {
        // Each IP should be valid format
        EXPECT_FALSE(ip.empty());
        EXPECT_NE(ip, "0.0.0.0");
        EXPECT_NE(ip, "127.0.0.1");  // Loopback should be filtered
    }
}

TEST_F(DiscoveryTest, GetHostname) {
    std::string hostname = pal::get_hostname();
    EXPECT_FALSE(hostname.empty());
}

TEST_F(DiscoveryTest, GetDeviceName) {
    std::string name = pal::get_device_name();
    EXPECT_FALSE(name.empty());
}

TEST_F(DiscoveryTest, GetOsType) {
    OperatingSystem os = pal::get_os_type();
    
    #ifdef TELEPORT_WINDOWS
    EXPECT_EQ(os, OperatingSystem::Windows);
    #elif defined(TELEPORT_DARWIN)
    EXPECT_EQ(os, OperatingSystem::macOS);
    #elif defined(TELEPORT_LINUX)
    EXPECT_EQ(os, OperatingSystem::Linux);
    #endif
}
