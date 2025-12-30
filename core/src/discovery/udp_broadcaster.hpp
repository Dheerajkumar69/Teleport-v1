/**
 * @file udp_broadcaster.hpp
 * @brief UDP broadcast sender and receiver for device discovery
 */

#ifndef TELEPORT_UDP_BROADCASTER_HPP
#define TELEPORT_UDP_BROADCASTER_HPP

#include "teleport/teleport.h"
#include "teleport/errors.h"
#include "teleport/types.h"
#include "platform/pal.hpp"
#include <atomic>
#include <thread>
#include <functional>

namespace teleport {

/**
 * @brief Callback for received discovery packets
 */
using DiscoveryPacketCallback = std::function<void(const Device& device)>;

/**
 * @brief UDP broadcaster for sending discovery announcements
 */
class UdpBroadcaster {
public:
    UdpBroadcaster(uint16_t port = TELEPORT_DISCOVERY_PORT);
    ~UdpBroadcaster();
    
    /**
     * @brief Start broadcasting device presence
     * @param device Our device information
     * @param interval_ms Broadcast interval
     * @return Success result
     */
    Result<void> start(const Device& device, uint32_t interval_ms = 1000);
    
    /**
     * @brief Stop broadcasting
     */
    void stop();
    
    /**
     * @brief Check if broadcasting
     */
    bool is_running() const { return m_running.load(); }
    
    /**
     * @brief Broadcast a single packet immediately
     */
    Result<void> broadcast_once(const Device& device);
    
private:
    void broadcast_loop();
    std::string serialize_device(const Device& device);
    
    uint16_t m_port;
    std::unique_ptr<pal::UdpSocket> m_socket;
    std::atomic<bool> m_running;
    std::thread m_thread;
    Device m_device;
    uint32_t m_interval_ms;
    std::string m_broadcast_addr;
};

/**
 * @brief UDP listener for receiving discovery announcements
 */
class UdpListener {
public:
    UdpListener(uint16_t port = TELEPORT_DISCOVERY_PORT);
    ~UdpListener();
    
    /**
     * @brief Start listening for discovery packets
     * @param callback Function to call for each received device
     * @return Success result
     */
    Result<void> start(DiscoveryPacketCallback callback);
    
    /**
     * @brief Stop listening
     */
    void stop();
    
    /**
     * @brief Check if listening
     */
    bool is_running() const { return m_running.load(); }
    
    /**
     * @brief Set our own device ID to filter self-discovery
     */
    void set_self_id(const std::string& id) { m_self_id = id; }
    
private:
    void listen_loop();
    std::optional<Device> parse_packet(const uint8_t* data, size_t len, 
                                        const std::string& sender_ip);
    
    uint16_t m_port;
    std::unique_ptr<pal::UdpSocket> m_socket;
    std::atomic<bool> m_running;
    std::thread m_thread;
    DiscoveryPacketCallback m_callback;
    std::string m_self_id;
};

} // namespace teleport

#endif // TELEPORT_UDP_BROADCASTER_HPP
