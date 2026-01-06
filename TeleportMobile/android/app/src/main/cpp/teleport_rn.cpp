/**
 * JNI bridge between Kotlin and C++ Teleport core
 */
#include <jni.h>
#include <string>
#include <cstring>
#include <android/log.h>
#include <nlohmann/json.hpp>

#include "teleport/teleport.h"

#define LOG_TAG "TeleportRN"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using json = nlohmann::json;

// Global references for callbacks
static JavaVM* g_jvm = nullptr;
static jobject g_module_instance = nullptr;

// Helper to get JNIEnv in callback threads
static JNIEnv* getEnv() {
    JNIEnv* env = nullptr;
    if (g_jvm) {
        int status = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
        if (status == JNI_EDETACHED) {
            g_jvm->AttachCurrentThread(&env, nullptr);
        }
    }
    return env;
}

// Callbacks from C++ core
void onDeviceDiscovered(const TeleportDevice* device, void*) {
    LOGI("Device discovered: %s (%s)", device->name, device->id);
    // TODO: Emit event to JS
}

void onDeviceLost(const char* device_id, void*) {
    LOGI("Device lost: %s", device_id);
    // TODO: Emit event to JS
}

void onProgress(const TeleportProgress* progress, void*) {
    LOGI("Progress: %llu / %llu", 
         (unsigned long long)progress->total_bytes_transferred, 
         (unsigned long long)progress->total_bytes_total);
    // TODO: Emit event to JS
}

void onComplete(TeleportError error, void*) {
    LOGI("Transfer complete: %d", error);
    // TODO: Emit event to JS
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
    config.discovery_interval_ms = 0;  // Default
    config.device_ttl_ms = 0;  // Default
    config.download_path = nullptr;  // Default
    
    TeleportEngine* engine = nullptr;
    TeleportError err = teleport_create(&config, &engine);
    
    env->ReleaseStringUTFChars(device_name, name);
    
    if (err != TELEPORT_OK) {
        LOGE("Failed to create Teleport engine: %d", err);
        return 0;
    }
    
    // Store global reference to module instance
    if (g_module_instance != nullptr) {
        env->DeleteGlobalRef(g_module_instance);
    }
    g_module_instance = env->NewGlobalRef(thiz);
    
    LOGI("Teleport engine created: %p", (void*)engine);
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
    
    if (g_module_instance != nullptr) {
        env->DeleteGlobalRef(g_module_instance);
        g_module_instance = nullptr;
    }
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

JNIEXPORT void JNICALL
Java_com_teleportmobile_TeleportModule_nativeSendFiles(
    JNIEnv* env, jobject, jlong handle, jstring target_id, jobjectArray file_paths
) {
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    if (!engine) return;
    
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
        return;
    }
    
    // Get file paths
    int path_count = env->GetArrayLength(file_paths);
    const char** paths = new const char*[path_count];
    jstring* jstrings = new jstring[path_count];
    
    for (int i = 0; i < path_count; i++) {
        jstrings[i] = (jstring)env->GetObjectArrayElement(file_paths, i);
        paths[i] = env->GetStringUTFChars(jstrings[i], nullptr);
    }
    
    LOGI("Sending %d files to %s", path_count, target_device->name);
    
    TeleportTransfer* transfer = nullptr;
    teleport_send_files(engine, target_device, paths, static_cast<size_t>(path_count), 
                        onProgress, onComplete, nullptr, &transfer);
    
    // Clean up
    for (int i = 0; i < path_count; i++) {
        env->ReleaseStringUTFChars(jstrings[i], paths[i]);
    }
    delete[] paths;
    delete[] jstrings;
    env->ReleaseStringUTFChars(target_id, target);
}

JNIEXPORT void JNICALL
Java_com_teleportmobile_TeleportModule_nativeStartReceiving(
    JNIEnv* env, jobject, jlong handle, jstring output_dir
) {
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    if (!engine) return;
    
    const char* dir = env->GetStringUTFChars(output_dir, nullptr);
    LOGI("Starting receiver with output dir: %s", dir);
    
    teleport_start_receiving(engine, dir, nullptr, onProgress, onComplete, nullptr);
    
    env->ReleaseStringUTFChars(output_dir, dir);
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

} // extern "C"
