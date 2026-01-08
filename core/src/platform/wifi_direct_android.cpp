/**
 * @file wifi_direct_android.cpp
 * @brief Android WiFi Direct implementation stub
 * 
 * The actual implementation is in Kotlin (WifiDirectManager.kt)
 * This file provides the JNI bridge to call into Java/Kotlin code.
 */

#include "wifi_direct.hpp"

#ifdef __ANDROID__

#include <jni.h>
#include <android/log.h>
#include "../utils/logger.hpp"

namespace teleport {

// Forward declarations for JNI
extern JavaVM* g_jvm;
extern jobject g_wifi_direct_manager;

/**
 * @brief Android WiFi Direct implementation via JNI to Kotlin
 */
class AndroidWifiDirect : public WifiDirect {
public:
    AndroidWifiDirect() {
        m_state = WifiDirectState::Idle;
    }
    
    ~AndroidWifiDirect() override {
        disconnect();
        stop_discovery();
    }
    
    bool is_available() const override {
        // Check via JNI if WiFi Direct is available
        JNIEnv* env = get_jni_env();
        if (!env || !g_wifi_direct_manager) return false;
        
        jclass cls = env->GetObjectClass(g_wifi_direct_manager);
        jmethodID method = env->GetMethodID(cls, "isAvailable", "()Z");
        return env->CallBooleanMethod(g_wifi_direct_manager, method);
    }
    
    WifiDirectState state() const override {
        return m_state.load();
    }
    
    Result<void> start_discovery(
        OnWifiDirectPeerFound on_found,
        OnWifiDirectPeerLost on_lost
    ) override {
        m_on_peer_found = std::move(on_found);
        m_on_peer_lost = std::move(on_lost);
        
        JNIEnv* env = get_jni_env();
        if (!env || !g_wifi_direct_manager) {
            return Error{-1, "JNI not initialized"};
        }
        
        jclass cls = env->GetObjectClass(g_wifi_direct_manager);
        jmethodID method = env->GetMethodID(cls, "startDiscovery", "()Z");
        bool success = env->CallBooleanMethod(g_wifi_direct_manager, method);
        
        if (success) {
            m_state = WifiDirectState::Discovering;
            return {};
        }
        return Error{-1, "Failed to start discovery"};
    }
    
    void stop_discovery() override {
        JNIEnv* env = get_jni_env();
        if (env && g_wifi_direct_manager) {
            jclass cls = env->GetObjectClass(g_wifi_direct_manager);
            jmethodID method = env->GetMethodID(cls, "stopDiscovery", "()V");
            env->CallVoidMethod(g_wifi_direct_manager, method);
        }
        m_state = WifiDirectState::Idle;
    }
    
    Result<void> connect(
        const std::string& mac_address,
        OnWifiDirectConnected on_connected,
        OnWifiDirectError on_error
    ) override {
        m_on_connected = std::move(on_connected);
        m_on_error = std::move(on_error);
        
        JNIEnv* env = get_jni_env();
        if (!env || !g_wifi_direct_manager) {
            return Error{-1, "JNI not initialized"};
        }
        
        jclass cls = env->GetObjectClass(g_wifi_direct_manager);
        jmethodID method = env->GetMethodID(cls, "connect", "(Ljava/lang/String;)Z");
        jstring jmac = env->NewStringUTF(mac_address.c_str());
        bool success = env->CallBooleanMethod(g_wifi_direct_manager, method, jmac);
        env->DeleteLocalRef(jmac);
        
        if (success) {
            m_state = WifiDirectState::Connecting;
            return {};
        }
        return Error{-1, "Failed to connect"};
    }
    
    void disconnect() override {
        JNIEnv* env = get_jni_env();
        if (env && g_wifi_direct_manager) {
            jclass cls = env->GetObjectClass(g_wifi_direct_manager);
            jmethodID method = env->GetMethodID(cls, "disconnect", "()V");
            env->CallVoidMethod(g_wifi_direct_manager, method);
        }
        m_state = WifiDirectState::Idle;
    }
    
    std::optional<WifiDirectConnection> get_connection_info() const override {
        std::lock_guard<std::mutex> lock(m_conn_mutex);
        return m_connection;
    }
    
    std::vector<WifiDirectPeer> get_peers() const override {
        std::lock_guard<std::mutex> lock(m_peers_mutex);
        std::vector<WifiDirectPeer> result;
        for (const auto& [mac, peer] : m_peers) {
            result.push_back(peer);
        }
        return result;
    }
    
    Result<void> start_advertising() override {
        // Android WiFi P2P discovers bidirectionally
        return {};
    }
    
    void stop_advertising() override {}
    
    void set_disconnect_callback(OnWifiDirectDisconnected callback) override {
        m_on_disconnected = std::move(callback);
    }
    
    void set_state_callback(OnWifiDirectStateChanged callback) override {
        m_on_state_changed = std::move(callback);
    }
    
    void cancel_connect() override {
        disconnect();
    }
    
    // JNI callbacks (called from Kotlin)
    void on_peer_found(const WifiDirectPeer& peer) {
        {
            std::lock_guard<std::mutex> lock(m_peers_mutex);
            m_peers[peer.mac_address] = peer;
        }
        if (m_on_peer_found) {
            m_on_peer_found(peer);
        }
    }
    
    void on_peer_lost(const std::string& mac) {
        {
            std::lock_guard<std::mutex> lock(m_peers_mutex);
            m_peers.erase(mac);
        }
        if (m_on_peer_lost) {
            m_on_peer_lost(mac);
        }
    }
    
    void on_connected(const WifiDirectConnection& conn) {
        {
            std::lock_guard<std::mutex> lock(m_conn_mutex);
            m_connection = conn;
        }
        m_state = WifiDirectState::Connected;
        if (m_on_connected) {
            m_on_connected(conn);
        }
    }
    
    void on_disconnected() {
        std::string mac;
        {
            std::lock_guard<std::mutex> lock(m_conn_mutex);
            if (m_connection) {
                mac = m_connection->peer_mac;
            }
            m_connection.reset();
        }
        m_state = WifiDirectState::Idle;
        if (m_on_disconnected && !mac.empty()) {
            m_on_disconnected(mac);
        }
    }

private:
    JNIEnv* get_jni_env() const {
        if (!g_jvm) return nullptr;
        JNIEnv* env;
        if (g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK) {
            return env;
        }
        return nullptr;
    }
    
    std::atomic<WifiDirectState> m_state{WifiDirectState::Disabled};
    
    mutable std::mutex m_peers_mutex;
    std::unordered_map<std::string, WifiDirectPeer> m_peers;
    
    mutable std::mutex m_conn_mutex;
    std::optional<WifiDirectConnection> m_connection;
    
    OnWifiDirectPeerFound m_on_peer_found;
    OnWifiDirectPeerLost m_on_peer_lost;
    OnWifiDirectConnected m_on_connected;
    OnWifiDirectDisconnected m_on_disconnected;
    OnWifiDirectError m_on_error;
    OnWifiDirectStateChanged m_on_state_changed;
};

// Global instance for JNI callbacks
static std::unique_ptr<AndroidWifiDirect> g_wifi_direct_instance;

std::unique_ptr<WifiDirect> create_wifi_direct() {
    g_wifi_direct_instance = std::make_unique<AndroidWifiDirect>();
    return std::make_unique<AndroidWifiDirect>();
}

bool is_wifi_direct_supported() {
    return true;  // Most Android devices support WiFi P2P
}

// JNI exports
extern "C" {

JNIEXPORT void JNICALL
Java_com_teleportmobile_WifiDirectManager_nativeOnPeerFound(
    JNIEnv* env, jobject thiz,
    jstring mac, jstring name, jint signal) {
    
    if (g_wifi_direct_instance) {
        WifiDirectPeer peer;
        const char* mac_str = env->GetStringUTFChars(mac, nullptr);
        const char* name_str = env->GetStringUTFChars(name, nullptr);
        peer.mac_address = mac_str;
        peer.device_name = name_str;
        peer.signal_strength = signal;
        peer.last_seen_ms = now_ms();
        env->ReleaseStringUTFChars(mac, mac_str);
        env->ReleaseStringUTFChars(name, name_str);
        
        g_wifi_direct_instance->on_peer_found(peer);
    }
}

JNIEXPORT void JNICALL
Java_com_teleportmobile_WifiDirectManager_nativeOnPeerLost(
    JNIEnv* env, jobject thiz, jstring mac) {
    
    if (g_wifi_direct_instance) {
        const char* mac_str = env->GetStringUTFChars(mac, nullptr);
        g_wifi_direct_instance->on_peer_lost(mac_str);
        env->ReleaseStringUTFChars(mac, mac_str);
    }
}

JNIEXPORT void JNICALL
Java_com_teleportmobile_WifiDirectManager_nativeOnConnected(
    JNIEnv* env, jobject thiz,
    jstring peer_mac, jstring peer_name, jstring go_ip, jstring local_ip, jboolean is_go) {
    
    if (g_wifi_direct_instance) {
        WifiDirectConnection conn;
        
        const char* s = env->GetStringUTFChars(peer_mac, nullptr);
        conn.peer_mac = s;
        env->ReleaseStringUTFChars(peer_mac, s);
        
        s = env->GetStringUTFChars(peer_name, nullptr);
        conn.peer_name = s;
        env->ReleaseStringUTFChars(peer_name, s);
        
        s = env->GetStringUTFChars(go_ip, nullptr);
        conn.group_owner_ip = s;
        env->ReleaseStringUTFChars(go_ip, s);
        
        s = env->GetStringUTFChars(local_ip, nullptr);
        conn.local_ip = s;
        env->ReleaseStringUTFChars(local_ip, s);
        
        conn.is_group_owner = is_go;
        
        g_wifi_direct_instance->on_connected(conn);
    }
}

JNIEXPORT void JNICALL
Java_com_teleportmobile_WifiDirectManager_nativeOnDisconnected(
    JNIEnv* env, jobject thiz) {
    
    if (g_wifi_direct_instance) {
        g_wifi_direct_instance->on_disconnected();
    }
}

} // extern "C"

} // namespace teleport

#endif // __ANDROID__
