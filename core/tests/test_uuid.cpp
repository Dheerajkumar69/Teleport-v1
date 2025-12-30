/**
 * @file test_uuid.cpp
 * @brief Additional UUID tests
 */

#include <gtest/gtest.h>
#include "utils/uuid.hpp"
#include "security/token.hpp"
#include <set>
#include <thread>
#include <vector>

using namespace teleport;

TEST(UuidTest, ThreadSafety) {
    const int num_threads = 10;
    const int uuids_per_thread = 100;
    
    std::vector<std::set<std::string>> thread_uuids(num_threads);
    std::vector<std::thread> threads;
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([t, &thread_uuids, uuids_per_thread]() {
            for (int i = 0; i < uuids_per_thread; ++i) {
                thread_uuids[t].insert(generate_uuid());
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Check each thread generated unique UUIDs
    for (int t = 0; t < num_threads; ++t) {
        EXPECT_EQ(thread_uuids[t].size(), uuids_per_thread);
    }
    
    // Check no duplicates across threads
    std::set<std::string> all_uuids;
    for (const auto& set : thread_uuids) {
        for (const auto& uuid : set) {
            EXPECT_EQ(all_uuids.count(uuid), 0) << "Duplicate UUID across threads: " << uuid;
            all_uuids.insert(uuid);
        }
    }
    
    EXPECT_EQ(all_uuids.size(), num_threads * uuids_per_thread);
}

TEST(TokenTest, SessionTokenFormat) {
    std::string token = generate_session_token();
    
    EXPECT_EQ(token.length(), 32);
    EXPECT_TRUE(validate_token_format(token));
}

TEST(TokenTest, SessionTokenUniqueness) {
    std::set<std::string> tokens;
    
    for (int i = 0; i < 1000; ++i) {
        std::string token = generate_session_token();
        EXPECT_EQ(tokens.count(token), 0);
        tokens.insert(token);
    }
    
    EXPECT_EQ(tokens.size(), 1000);
}

TEST(TokenTest, ValidationRejectsInvalid) {
    EXPECT_FALSE(validate_token_format(""));
    EXPECT_FALSE(validate_token_format("short"));
    EXPECT_FALSE(validate_token_format("this_is_not_a_valid_hex_token!!"));
    EXPECT_FALSE(validate_token_format("0123456789abcdef0123456789abcde"));  // 31 chars
    EXPECT_FALSE(validate_token_format("0123456789abcdef0123456789abcdef0"));  // 33 chars
    
    EXPECT_TRUE(validate_token_format("0123456789abcdef0123456789abcdef"));
    EXPECT_TRUE(validate_token_format("ABCDEF0123456789ABCDEF0123456789"));
}
