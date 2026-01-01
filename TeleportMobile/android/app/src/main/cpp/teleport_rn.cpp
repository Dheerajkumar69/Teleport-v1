/**
 * @file teleport_rn.cpp
 * @brief JNI bridge for React Native TeleportModule
 */

#include <jni.h>
#include <android/log.h>
#include <string>
#include <vector>
#include <mutex>

#include "teleport/teleport.h"

#define LOG_TAG "TeleportRN"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {
    JavaVM* g_jvm = nullptr;
    jobject g_module = nullptr;
    std::mutex g_mutex;
    
    JNIEnv* getEnv() {
        JNIEnv* env = nullptr;
        if (g_jvm) {
            g_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
            if (!env) {
                g_jvm->AttachCurrentThread(&env, nullptr);
            }
        }
        return env;
    }
    
    std::string jstringToString(JNIEnv* env, jstring jstr) {
        if (!jstr) return "";
        const char* chars = env->GetStringUTFChars(jstr, nullptr);
        std::string result(chars);
        env->ReleaseStringUTFChars(jstr, chars);
        return result;
    }
}

// Discovery callbacks
void onDeviceDiscovered(const TeleportDevice* device, void* user_data) {
    std::lock_guard<std::mutex> lock(g_mutex);
    JNIEnv* env = getEnv();
    if (!env || !g_module) return;
    
    jclass cls = env->GetObjectClass(g_module);
    jmethodID method = env->GetMethodID(cls, "onDeviceFound",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)V");
    
    jstring id = env->NewStringUTF(device->id);
    jstring name = env->NewStringUTF(device->name);
    jstring os = env->NewStringUTF(device->os);
    jstring ip = env->NewStringUTF(device->ip);
    
    env->CallVoidMethod(g_module, method, id, name, os, ip, (jint)device->port);
    
    env->DeleteLocalRef(id);
    env->DeleteLocalRef(name);
    env->DeleteLocalRef(os);
    env->DeleteLocalRef(ip);
    env->DeleteLocalRef(cls);
}

void onDeviceLost(const char* device_id, void* user_data) {
    std::lock_guard<std::mutex> lock(g_mutex);
    JNIEnv* env = getEnv();
    if (!env || !g_module) return;
    
    jclass cls = env->GetObjectClass(g_module);
    jmethodID method = env->GetMethodID(cls, "onDeviceLost", "(Ljava/lang/String;)V");
    jstring id = env->NewStringUTF(device_id);
    env->CallVoidMethod(g_module, method, id);
    env->DeleteLocalRef(id);
    env->DeleteLocalRef(cls);
}

void onProgress(const TeleportProgress* progress, void* user_data) {
    std::lock_guard<std::mutex> lock(g_mutex);
    JNIEnv* env = getEnv();
    if (!env || !g_module) return;
    
    jclass cls = env->GetObjectClass(g_module);
    jmethodID method = env->GetMethodID(cls, "onTransferProgress", "(JJDII)V");
    env->CallVoidMethod(g_module, method,
        (jlong)progress->total_bytes_transferred,
        (jlong)progress->total_bytes_total,
        (jdouble)progress->speed_bytes_per_sec,
        (jint)progress->files_completed,
        (jint)progress->files_total);
    env->DeleteLocalRef(cls);
}

void onComplete(TeleportError error, void* user_data) {
    std::lock_guard<std::mutex> lock(g_mutex);
    JNIEnv* env = getEnv();
    if (!env || !g_module) return;
    
    jclass cls = env->GetObjectClass(g_module);
    jmethodID method = env->GetMethodID(cls, "onTransferComplete", "(I)V");
    env->CallVoidMethod(g_module, method, (jint)error);
    env->DeleteLocalRef(cls);
}

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    LOGI("Teleport RN JNI loaded");
    return JNI_VERSION_1_6;
}

JNIEXPORT jlong JNICALL
Java_com_teleportmobile_TeleportModule_nativeCreate(
    JNIEnv* env, jobject thiz, jstring device_name, jstring download_path
) {
    // Store module reference for callbacks
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_module) {
            env->DeleteGlobalRef(g_module);
        }
        g_module = env->NewGlobalRef(thiz);
    }
    
    TeleportConfig config = {};
    std::string name = jstringToString(env, device_name);
    std::string path = jstringToString(env, download_path);
    
    config.device_name = name.c_str();
    config.download_path = path.c_str();
    config.control_port = 0;
    config.chunk_size = TELEPORT_CHUNK_SIZE;
    config.parallel_streams = TELEPORT_PARALLEL_STREAMS;
    config.discovery_interval_ms = TELEPORT_DISCOVERY_INTERVAL;
    config.device_ttl_ms = TELEPORT_DEVICE_TTL;
    
    TeleportEngine* engine = nullptr;
    TeleportError err = teleport_create(&config, &engine);
    
    if (err != TELEPORT_OK) {
        LOGE("Failed to create engine: %s", teleport_error_string(err));
        return 0;
    }
    
    LOGI("Teleport engine created");
    return reinterpret_cast<jlong>(engine);
}

JNIEXPORT void JNICALL
Java_com_teleportmobile_TeleportModule_nativeDestroy(
    JNIEnv* env, jobject thiz, jlong handle
) {
    if (handle) {
        TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
        teleport_destroy(engine);
        LOGI("Teleport engine destroyed");
    }
    
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_module) {
        env->DeleteGlobalRef(g_module);
        g_module = nullptr;
    }
}

JNIEXPORT jint JNICALL
Java_com_teleportmobile_TeleportModule_nativeStartDiscovery(
    JNIEnv* env, jobject thiz, jlong handle
) {
    if (!handle) return -1;
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    return teleport_start_discovery(engine, onDeviceDiscovered, onDeviceLost, nullptr);
}

JNIEXPORT jint JNICALL
Java_com_teleportmobile_TeleportModule_nativeStopDiscovery(
    JNIEnv* env, jobject thiz, jlong handle
) {
    if (!handle) return -1;
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    return teleport_stop_discovery(engine);
}

JNIEXPORT jstring JNICALL
Java_com_teleportmobile_TeleportModule_nativeGetDevices(
    JNIEnv* env, jobject thiz, jlong handle
) {
    if (!handle) return env->NewStringUTF("[]");
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    
    TeleportDevice devices[32];
    size_t count = 32;
    TeleportError err = teleport_get_devices(engine, devices, &count);
    
    if (err != TELEPORT_OK || count == 0) {
        return env->NewStringUTF("[]");
    }
    
    // Build JSON array
    std::string json = "[";
    for (size_t i = 0; i < count; i++) {
        if (i > 0) json += ",";
        json += "{";
        json += "\"id\":\"" + std::string(devices[i].id) + "\",";
        json += "\"name\":\"" + std::string(devices[i].name) + "\",";
        json += "\"os\":\"" + std::string(devices[i].os) + "\",";
        json += "\"ip\":\"" + std::string(devices[i].ip) + "\",";
        json += "\"port\":" + std::to_string(devices[i].port);
        json += "}";
    }
    json += "]";
    
    return env->NewStringUTF(json.c_str());
}

JNIEXPORT jint JNICALL
Java_com_teleportmobile_TeleportModule_nativeSendFiles(
    JNIEnv* env, jobject thiz, jlong handle, jstring target_id, jobjectArray file_paths
) {
    if (!handle) return -1;
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    
    std::string deviceId = jstringToString(env, target_id);
    
    // Get device by ID
    TeleportDevice devices[32];
    size_t count = 32;
    teleport_get_devices(engine, devices, &count);
    
    TeleportDevice* target = nullptr;
    for (size_t i = 0; i < count; i++) {
        if (std::string(devices[i].id) == deviceId) {
            target = &devices[i];
            break;
        }
    }
    
    if (!target) {
        LOGE("Device not found: %s", deviceId.c_str());
        return -1;
    }
    
    // Get file paths
    int pathCount = env->GetArrayLength(file_paths);
    std::vector<std::string> paths;
    std::vector<const char*> pathPtrs;
    
    for (int i = 0; i < pathCount; i++) {
        jstring jpath = (jstring)env->GetObjectArrayElement(file_paths, i);
        paths.push_back(jstringToString(env, jpath));
        env->DeleteLocalRef(jpath);
    }
    
    for (const auto& p : paths) {
        pathPtrs.push_back(p.c_str());
    }
    
    return teleport_send_files(engine, target, pathPtrs.data(), pathPtrs.size(),
        onProgress, onComplete, nullptr, nullptr);
}

JNIEXPORT jint JNICALL
Java_com_teleportmobile_TeleportModule_nativeStartReceiving(
    JNIEnv* env, jobject thiz, jlong handle, jstring output_dir
) {
    if (!handle) return -1;
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    std::string dir = jstringToString(env, output_dir);
    return teleport_start_receiving(engine, dir.c_str(), nullptr, onProgress, onComplete, nullptr);
}

JNIEXPORT jint JNICALL
Java_com_teleportmobile_TeleportModule_nativeStopReceiving(
    JNIEnv* env, jobject thiz, jlong handle
) {
    if (!handle) return -1;
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    return teleport_stop_receiving(engine);
}

} // extern "C"
