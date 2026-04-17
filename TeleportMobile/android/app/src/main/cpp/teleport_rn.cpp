/**
 * JNI bridge between Kotlin and C++ Teleport core
 * Implements real file transfer with progress callbacks to React Native
 */
#include <jni.h>
#include <string>
#include <cstring>
#include <android/log.h>
#include <nlohmann/json.hpp>
#include <mutex>

#include "teleport/teleport.h"

#define LOG_TAG "TeleportRN"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using json = nlohmann::json;

// Global references for callbacks (file-private)
static jobject g_module_instance = nullptr;
static jmethodID g_emit_device_discovered = nullptr;
static jmethodID g_emit_device_lost = nullptr;
static jmethodID g_emit_progress = nullptr;
static jmethodID g_emit_complete = nullptr;
static std::mutex g_mutex;

// These must be in the teleport namespace so wifi_direct_android.cpp can find them
// via its `extern teleport::g_jvm` and `extern teleport::g_wifi_direct_manager` declarations.
namespace teleport {
    JavaVM* g_jvm = nullptr;
    jobject g_wifi_direct_manager = nullptr;
} // namespace teleport

// Local alias for convenience in this file
static JavaVM*& g_jvm = teleport::g_jvm;


// Helper to get JNIEnv in callback threads
static JNIEnv* getEnv(bool* needsDetach) {
    *needsDetach = false;
    JNIEnv* env = nullptr;
    if (g_jvm) {
        int status = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
        if (status == JNI_EDETACHED) {
            if (g_jvm->AttachCurrentThread(&env, nullptr) == 0) {
                *needsDetach = true;
            }
        }
    }
    return env;
}

static void detachIfNeeded(bool needsDetach) {
    if (needsDetach && g_jvm) {
        g_jvm->DetachCurrentThread();
    }
}

// Callbacks from C++ core - emit to Kotlin/React Native
void onDeviceDiscovered(const TeleportDevice* device, void*) {
    LOGI("Device discovered: %s (%s) at %s:%d", device->name, device->id, device->ip, device->port);
    
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_module_instance || !g_emit_device_discovered) return;
    
    bool needsDetach = false;
    JNIEnv* env = getEnv(&needsDetach);
    if (!env) return;
    
    json device_obj = {
        {"id", device->id},
        {"name", device->name},
        {"ip", device->ip},
        {"port", device->port},
        {"os", device->os}
    };
    
    jstring deviceJson = env->NewStringUTF(device_obj.dump().c_str());
    env->CallVoidMethod(g_module_instance, g_emit_device_discovered, deviceJson);
    env->DeleteLocalRef(deviceJson);
    
    detachIfNeeded(needsDetach);
}

void onDeviceLost(const char* device_id, void*) {
    LOGI("Device lost: %s", device_id);
    
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_module_instance || !g_emit_device_lost) return;
    
    bool needsDetach = false;
    JNIEnv* env = getEnv(&needsDetach);
    if (!env) return;
    
    jstring deviceId = env->NewStringUTF(device_id);
    env->CallVoidMethod(g_module_instance, g_emit_device_lost, deviceId);
    env->DeleteLocalRef(deviceId);
    
    detachIfNeeded(needsDetach);
}

void onProgress(const TeleportProgress* progress, void*) {
    double percent = 0;
    if (progress->total_bytes_total > 0) {
        percent = (double)progress->total_bytes_transferred / (double)progress->total_bytes_total * 100.0;
    }
    
    LOGI("Progress: %.1f%% (%llu / %llu bytes)", 
         percent,
         (unsigned long long)progress->total_bytes_transferred, 
         (unsigned long long)progress->total_bytes_total);
    
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_module_instance || !g_emit_progress) return;
    
    bool needsDetach = false;
    JNIEnv* env = getEnv(&needsDetach);
    if (!env) return;
    
    json progress_obj = {
        {"bytesTransferred", progress->total_bytes_transferred},
        {"totalBytes", progress->total_bytes_total},
        {"percent", percent},
        {"currentFile", progress->file_name ? progress->file_name : ""},
        {"filesCompleted", progress->files_completed},
        {"filesTotal", progress->files_total}
    };
    
    jstring progressJson = env->NewStringUTF(progress_obj.dump().c_str());
    env->CallVoidMethod(g_module_instance, g_emit_progress, progressJson);
    env->DeleteLocalRef(progressJson);
    
    detachIfNeeded(needsDetach);
}

void onComplete(TeleportError error, void*) {
    LOGI("Transfer complete: %d (%s)", error, error == TELEPORT_OK ? "success" : "failed");
    
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_module_instance || !g_emit_complete) return;
    
    bool needsDetach = false;
    JNIEnv* env = getEnv(&needsDetach);
    if (!env) return;
    
    env->CallVoidMethod(g_module_instance, g_emit_complete, (jint)error);
    
    detachIfNeeded(needsDetach);
}

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    g_jvm = vm;
    LOGI("JNI_OnLoad: Teleport native module loaded");
    return JNI_VERSION_1_6;
}

JNIEXPORT jlong JNICALL
Java_com_teleportmobile_TeleportModule_nativeInit(
    JNIEnv* env, jobject thiz, jstring device_name
) {
    const char* name = env->GetStringUTFChars(device_name, nullptr);
    LOGI("Initializing Teleport engine with device name: %s", name);
    
    TeleportConfig config = {};
    config.device_name = name;
    config.control_port = 0;  // Auto
    config.chunk_size = 0;    // Default
    config.parallel_streams = 0;  // Default
    config.discovery_interval_ms = 1000;  // 1 second
    config.device_ttl_ms = 5000;  // 5 seconds
    config.download_path = nullptr;
    
    TeleportEngine* engine = nullptr;
    TeleportError err = teleport_create(&config, &engine);
    
    env->ReleaseStringUTFChars(device_name, name);
    
    if (err != TELEPORT_OK) {
        LOGE("Failed to create Teleport engine: %d", err);
        return 0;
    }
    
    // Store global reference to module instance and cache method IDs
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (g_module_instance != nullptr) {
        env->DeleteGlobalRef(g_module_instance);
    }
    g_module_instance = env->NewGlobalRef(thiz);
    
    // Cache method IDs for callbacks
    jclass moduleClass = env->GetObjectClass(thiz);
    g_emit_device_discovered = env->GetMethodID(moduleClass, "emitDeviceDiscovered", "(Ljava/lang/String;)V");
    g_emit_device_lost = env->GetMethodID(moduleClass, "emitDeviceLost", "(Ljava/lang/String;)V");
    g_emit_progress = env->GetMethodID(moduleClass, "emitProgress", "(Ljava/lang/String;)V");
    g_emit_complete = env->GetMethodID(moduleClass, "emitComplete", "(I)V");
    
    LOGI("Teleport engine created: %p, callbacks registered", (void*)engine);
    return reinterpret_cast<jlong>(engine);
}

JNIEXPORT void JNICALL
Java_com_teleportmobile_TeleportModule_nativeDestroy(
    JNIEnv* env, jobject, jlong handle
) {
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    if (engine) {
        LOGI("Destroying Teleport engine: %p", (void*)engine);
        teleport_destroy(engine);
    }
    
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_module_instance != nullptr) {
        env->DeleteGlobalRef(g_module_instance);
        g_module_instance = nullptr;
    }
    g_emit_device_discovered = nullptr;
    g_emit_device_lost = nullptr;
    g_emit_progress = nullptr;
    g_emit_complete = nullptr;
}

JNIEXPORT void JNICALL
Java_com_teleportmobile_TeleportModule_nativeStartDiscovery(
    JNIEnv*, jobject, jlong handle
) {
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    if (engine) {
        LOGI("Starting discovery");
        teleport_start_discovery(engine, onDeviceDiscovered, onDeviceLost, nullptr);
    }
}

JNIEXPORT void JNICALL
Java_com_teleportmobile_TeleportModule_nativeStopDiscovery(
    JNIEnv*, jobject, jlong handle
) {
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    if (engine) {
        LOGI("Stopping discovery");
        teleport_stop_discovery(engine);
    }
}

JNIEXPORT jstring JNICALL
Java_com_teleportmobile_TeleportModule_nativeGetDevices(
    JNIEnv* env, jobject, jlong handle
) {
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    if (!engine) {
        return env->NewStringUTF("[]");
    }
    
    TeleportDevice devices[32];
    size_t count = 0;
    TeleportError err = teleport_get_devices(engine, devices, 32, &count);
    
    if (err != TELEPORT_OK) {
        LOGE("Failed to get devices: %d", err);
        return env->NewStringUTF("[]");
    }
    
    json device_array = json::array();
    for (size_t i = 0; i < count; i++) {
        json device_obj = {
            {"id", devices[i].id},
            {"name", devices[i].name},
            {"ip", devices[i].ip},
            {"port", devices[i].port},
            {"os", devices[i].os}
        };
        device_array.push_back(device_obj);
    }
    
    std::string json_str = device_array.dump();
    return env->NewStringUTF(json_str.c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_teleportmobile_TeleportModule_nativeSendFiles(
    JNIEnv* env, jobject, jlong handle, jstring target_id, jobjectArray file_paths
) {
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    if (!engine) return JNI_FALSE;
    
    const char* target = env->GetStringUTFChars(target_id, nullptr);
    
    // Find target device
    TeleportDevice devices[32];
    size_t count = 0;
    teleport_get_devices(engine, devices, 32, &count);
    
    const TeleportDevice* target_device = nullptr;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(devices[i].id, target) == 0) {
            target_device = &devices[i];
            break;
        }
    }
    
    if (!target_device) {
        LOGE("Target device not found: %s", target);
        env->ReleaseStringUTFChars(target_id, target);
        return JNI_FALSE;
    }
    
    // Get file paths
    int path_count = env->GetArrayLength(file_paths);
    const char** paths = new const char*[path_count];
    jstring* jstrings = new jstring[path_count];
    
    for (int i = 0; i < path_count; i++) {
        jstrings[i] = (jstring)env->GetObjectArrayElement(file_paths, i);
        paths[i] = env->GetStringUTFChars(jstrings[i], nullptr);
        LOGI("File %d: %s", i, paths[i]);
    }
    
    LOGI("Sending %d files to %s (%s:%d)", path_count, target_device->name, 
         target_device->ip, target_device->port);
    
    TeleportTransfer* transfer = nullptr;
    TeleportError err = teleport_send_files(engine, target_device, paths, 
                                            static_cast<size_t>(path_count), 
                                            onProgress, onComplete, nullptr, &transfer);
    
    // Clean up
    for (int i = 0; i < path_count; i++) {
        env->ReleaseStringUTFChars(jstrings[i], paths[i]);
    }
    delete[] paths;
    delete[] jstrings;
    env->ReleaseStringUTFChars(target_id, target);
    
    if (err != TELEPORT_OK) {
        LOGE("Failed to start file transfer: %d", err);
        return JNI_FALSE;
    }
    
    LOGI("File transfer started successfully");
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_teleportmobile_TeleportModule_nativeStartReceiving(
    JNIEnv* env, jobject, jlong handle, jstring output_dir
) {
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    if (!engine) return JNI_FALSE;
    
    const char* dir = env->GetStringUTFChars(output_dir, nullptr);
    LOGI("Starting receiver with output dir: %s", dir);
    
    TeleportError err = teleport_start_receiving(engine, dir, nullptr, onProgress, onComplete, nullptr);
    
    env->ReleaseStringUTFChars(output_dir, dir);
    
    if (err != TELEPORT_OK) {
        LOGE("Failed to start receiving: %d", err);
        return JNI_FALSE;
    }
    
    LOGI("Receiver started successfully");
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_teleportmobile_TeleportModule_nativeStopReceiving(
    JNIEnv*, jobject, jlong handle
) {
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    if (engine) {
        LOGI("Stopping receiver");
        teleport_stop_receiving(engine);
    }
}

// ============================================================================
// QR Code Pairing Functions
// ============================================================================

JNIEXPORT jstring JNICALL
Java_com_teleportmobile_TeleportModule_nativeGenerateQrPairing(
    JNIEnv* env, jobject, jlong handle, jint expiry_seconds
) {
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    if (!engine) return env->NewStringUTF("");
    
    TeleportQrPairingInfo info = {};
    uint8_t qr_data[65536];  // 64KB for QR bitmap
    size_t qr_size = sizeof(qr_data);
    
    TeleportError err = teleport_generate_qr_pairing(engine, &info, qr_data, &qr_size, expiry_seconds);
    
    if (err != TELEPORT_OK) {
        LOGE("Failed to generate QR pairing: %d", err);
        return env->NewStringUTF("");
    }
    
    // Return pairing info as JSON
    json result = {
        {"ip", info.ip},
        {"port", info.port},
        {"sessionToken", info.session_token},
        {"deviceName", info.device_name},
        {"expiresAt", info.expires_at_ms}
    };
    
    LOGI("Generated QR pairing for %s:%d", info.ip, info.port);
    return env->NewStringUTF(result.dump().c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_teleportmobile_TeleportModule_nativeConnectViaQr(
    JNIEnv* env, jobject, jlong handle, jstring qr_data
) {
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    if (!engine) return JNI_FALSE;
    
    const char* data = env->GetStringUTFChars(qr_data, nullptr);
    LOGI("Connecting via QR: %s", data);
    
    TeleportError err = teleport_connect_via_qr(engine, data);
    
    env->ReleaseStringUTFChars(qr_data, data);
    
    if (err != TELEPORT_OK) {
        LOGE("Failed to connect via QR: %d", err);
        return JNI_FALSE;
    }
    
    LOGI("Successfully connected via QR");
    return JNI_TRUE;
}

JNIEXPORT jstring JNICALL
Java_com_teleportmobile_TeleportModule_nativeValidateQrPairing(
    JNIEnv* env, jobject, jstring qr_data
) {
    const char* data = env->GetStringUTFChars(qr_data, nullptr);
    
    TeleportQrPairingInfo info = {};
    TeleportError err = teleport_validate_qr_pairing(data, &info);
    
    env->ReleaseStringUTFChars(qr_data, data);
    
    if (err != TELEPORT_OK) {
        return env->NewStringUTF("");
    }
    
    json result = {
        {"ip", info.ip},
        {"port", info.port},
        {"sessionToken", info.session_token},
        {"deviceName", info.device_name},
        {"expiresAt", info.expires_at_ms},
        {"valid", true}
    };
    
    return env->NewStringUTF(result.dump().c_str());
}

// ============================================================================
// Hotspot Mode Functions
// ============================================================================

JNIEXPORT jboolean JNICALL
Java_com_teleportmobile_TeleportModule_nativeIsHotspotSupported(
    JNIEnv*, jobject
) {
    return teleport_hotspot_is_supported() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_teleportmobile_TeleportModule_nativeCreateHotspot(
    JNIEnv* env, jobject, jlong handle
) {
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    if (!engine) return env->NewStringUTF("");
    
    TeleportHotspotInfo info = {};
    TeleportError err = teleport_create_hotspot(engine, &info);
    
    if (err != TELEPORT_OK) {
        LOGE("Failed to create hotspot: %d", err);
        return env->NewStringUTF("");
    }
    
    json result = {
        {"ssid", info.ssid},
        {"password", info.password},
        {"gatewayIp", info.gateway_ip},
        {"controlPort", info.control_port},
        {"isActive", info.is_active != 0},
        {"clientCount", info.client_count}
    };
    
    LOGI("Created hotspot: %s", info.ssid);
    return env->NewStringUTF(result.dump().c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_teleportmobile_TeleportModule_nativeDestroyHotspot(
    JNIEnv*, jobject, jlong handle
) {
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    if (!engine) return JNI_FALSE;
    
    TeleportError err = teleport_destroy_hotspot(engine);
    return err == TELEPORT_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_teleportmobile_TeleportModule_nativeGetHotspotInfo(
    JNIEnv* env, jobject, jlong handle
) {
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    if (!engine) return env->NewStringUTF("");
    
    TeleportHotspotInfo info = {};
    TeleportError err = teleport_get_hotspot_info(engine, &info);
    
    if (err != TELEPORT_OK) {
        return env->NewStringUTF("");
    }
    
    json result = {
        {"ssid", info.ssid},
        {"password", info.password},
        {"gatewayIp", info.gateway_ip},
        {"controlPort", info.control_port},
        {"isActive", info.is_active != 0},
        {"clientCount", info.client_count}
    };
    
    return env->NewStringUTF(result.dump().c_str());
}

JNIEXPORT jstring JNICALL
Java_com_teleportmobile_TeleportModule_nativeDetectHotspot(
    JNIEnv* env, jobject
) {
    char gateway_ip[46] = {0};
    TeleportError err = teleport_detect_hotspot(gateway_ip, sizeof(gateway_ip));
    
    if (err != TELEPORT_OK) {
        return env->NewStringUTF("");
    }
    
    return env->NewStringUTF(gateway_ip);
}

// ============================================================================
// WiFi Direct Functions  
// ============================================================================

JNIEXPORT jboolean JNICALL
Java_com_teleportmobile_TeleportModule_nativeIsWifiDirectSupported(
    JNIEnv*, jobject
) {
    return teleport_wifi_direct_is_supported() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_teleportmobile_TeleportModule_nativeWifiDirectDisconnect(
    JNIEnv*, jobject, jlong handle
) {
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    if (!engine) return JNI_FALSE;
    
    TeleportError err = teleport_wifi_direct_disconnect(engine);
    return err == TELEPORT_OK ? JNI_TRUE : JNI_FALSE;
}

} // extern "C"

