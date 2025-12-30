/**
 * @file TeleportJNI.cpp
 * @brief JNI bindings for Teleport core library
 */

#include <jni.h>
#include <android/log.h>
#include <string>
#include <vector>
#include <mutex>
#include <memory>

#include "teleport/teleport.h"
#include "utils/logger.hpp"

#define LOG_TAG "TeleportJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {
    // Global references for callbacks
    JavaVM* g_jvm = nullptr;
    
    struct CallbackContext {
        jobject callback_obj = nullptr;
        jmethodID on_device_method = nullptr;
        jmethodID on_device_lost_method = nullptr;
        jmethodID on_progress_method = nullptr;
        jmethodID on_complete_method = nullptr;
        jmethodID on_incoming_method = nullptr;
    };
    
    std::mutex g_callback_mutex;
    CallbackContext g_discovery_ctx;
    CallbackContext g_transfer_ctx;
    
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
    
    jstring stringToJstring(JNIEnv* env, const std::string& str) {
        return env->NewStringUTF(str.c_str());
    }
}

// Device discovery callback
void onDeviceDiscovered(const TeleportDevice* device, void* user_data) {
    std::lock_guard<std::mutex> lock(g_callback_mutex);
    JNIEnv* env = getEnv();
    if (!env || !g_discovery_ctx.callback_obj) return;
    
    // Create Java Device object
    jclass deviceClass = env->FindClass("com/teleport/model/Device");
    jmethodID ctor = env->GetMethodID(deviceClass, "<init>", 
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)V");
    
    jobject jdevice = env->NewObject(deviceClass, ctor,
        stringToJstring(env, device->id),
        stringToJstring(env, device->name),
        stringToJstring(env, device->os),
        stringToJstring(env, device->ip),
        device->port
    );
    
    env->CallVoidMethod(g_discovery_ctx.callback_obj, 
        g_discovery_ctx.on_device_method, jdevice);
    
    env->DeleteLocalRef(jdevice);
    env->DeleteLocalRef(deviceClass);
}

void onDeviceLost(const char* device_id, void* user_data) {
    std::lock_guard<std::mutex> lock(g_callback_mutex);
    JNIEnv* env = getEnv();
    if (!env || !g_discovery_ctx.callback_obj) return;
    
    env->CallVoidMethod(g_discovery_ctx.callback_obj,
        g_discovery_ctx.on_device_lost_method,
        stringToJstring(env, device_id));
}

void onProgress(const TeleportProgress* progress, void* user_data) {
    std::lock_guard<std::mutex> lock(g_callback_mutex);
    JNIEnv* env = getEnv();
    if (!env || !g_transfer_ctx.callback_obj) return;
    
    env->CallVoidMethod(g_transfer_ctx.callback_obj,
        g_transfer_ctx.on_progress_method,
        progress->total_bytes_transferred,
        progress->total_bytes_total,
        progress->speed_bytes_per_sec,
        progress->files_completed,
        progress->files_total);
}

void onComplete(TeleportError error, void* user_data) {
    std::lock_guard<std::mutex> lock(g_callback_mutex);
    JNIEnv* env = getEnv();
    if (!env || !g_transfer_ctx.callback_obj) return;
    
    env->CallVoidMethod(g_transfer_ctx.callback_obj,
        g_transfer_ctx.on_complete_method,
        static_cast<jint>(error));
}

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    LOGI("Teleport JNI loaded");
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved) {
    g_jvm = nullptr;
    LOGI("Teleport JNI unloaded");
}

JNIEXPORT jlong JNICALL
Java_com_teleport_TeleportEngine_nativeCreate(
    JNIEnv* env,
    jobject thiz,
    jstring device_name,
    jstring download_path
) {
    TeleportConfig config = {};
    
    std::string name = jstringToString(env, device_name);
    std::string path = jstringToString(env, download_path);
    
    config.device_name = name.c_str();
    config.download_path = path.c_str();
    config.control_port = 0;  // Auto-select
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
    
    LOGI("Engine created successfully");
    return reinterpret_cast<jlong>(engine);
}

JNIEXPORT void JNICALL
Java_com_teleport_TeleportEngine_nativeDestroy(
    JNIEnv* env,
    jobject thiz,
    jlong handle
) {
    if (handle) {
        TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
        teleport_destroy(engine);
        LOGI("Engine destroyed");
    }
}

JNIEXPORT jint JNICALL
Java_com_teleport_TeleportEngine_nativeStartDiscovery(
    JNIEnv* env,
    jobject thiz,
    jlong handle,
    jobject callback
) {
    if (!handle) return TELEPORT_ERROR_INVALID_ARGUMENT;
    
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    
    // Store callback reference
    {
        std::lock_guard<std::mutex> lock(g_callback_mutex);
        if (g_discovery_ctx.callback_obj) {
            env->DeleteGlobalRef(g_discovery_ctx.callback_obj);
        }
        g_discovery_ctx.callback_obj = env->NewGlobalRef(callback);
        
        jclass cls = env->GetObjectClass(callback);
        g_discovery_ctx.on_device_method = env->GetMethodID(cls, 
            "onDeviceFound", "(Lcom/teleport/model/Device;)V");
        g_discovery_ctx.on_device_lost_method = env->GetMethodID(cls,
            "onDeviceLost", "(Ljava/lang/String;)V");
        env->DeleteLocalRef(cls);
    }
    
    TeleportError err = teleport_start_discovery(engine, 
        onDeviceDiscovered, onDeviceLost, nullptr);
    
    if (err != TELEPORT_OK) {
        LOGE("Failed to start discovery: %s", teleport_error_string(err));
    }
    
    return err;
}

JNIEXPORT jint JNICALL
Java_com_teleport_TeleportEngine_nativeStopDiscovery(
    JNIEnv* env,
    jobject thiz,
    jlong handle
) {
    if (!handle) return TELEPORT_ERROR_INVALID_ARGUMENT;
    
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    
    {
        std::lock_guard<std::mutex> lock(g_callback_mutex);
        if (g_discovery_ctx.callback_obj) {
            env->DeleteGlobalRef(g_discovery_ctx.callback_obj);
            g_discovery_ctx.callback_obj = nullptr;
        }
    }
    
    return teleport_stop_discovery(engine);
}

JNIEXPORT jint JNICALL
Java_com_teleport_TeleportEngine_nativeSendFiles(
    JNIEnv* env,
    jobject thiz,
    jlong handle,
    jstring target_id,
    jstring target_name,
    jstring target_ip,
    jint target_port,
    jobjectArray file_paths,
    jobject callback
) {
    if (!handle) return TELEPORT_ERROR_INVALID_ARGUMENT;
    
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    
    // Build target device
    TeleportDevice target = {};
    std::string id = jstringToString(env, target_id);
    std::string name = jstringToString(env, target_name);
    std::string ip = jstringToString(env, target_ip);
    
    strncpy(target.id, id.c_str(), TELEPORT_UUID_SIZE - 1);
    strncpy(target.name, name.c_str(), TELEPORT_MAX_DEVICE_NAME - 1);
    strncpy(target.ip, ip.c_str(), sizeof(target.ip) - 1);
    target.port = static_cast<uint16_t>(target_port);
    
    // Get file paths
    int count = env->GetArrayLength(file_paths);
    std::vector<std::string> paths;
    std::vector<const char*> path_ptrs;
    
    for (int i = 0; i < count; i++) {
        jstring jpath = (jstring)env->GetObjectArrayElement(file_paths, i);
        paths.push_back(jstringToString(env, jpath));
        env->DeleteLocalRef(jpath);
    }
    
    for (const auto& p : paths) {
        path_ptrs.push_back(p.c_str());
    }
    
    // Store callback
    {
        std::lock_guard<std::mutex> lock(g_callback_mutex);
        if (g_transfer_ctx.callback_obj) {
            env->DeleteGlobalRef(g_transfer_ctx.callback_obj);
        }
        g_transfer_ctx.callback_obj = env->NewGlobalRef(callback);
        
        jclass cls = env->GetObjectClass(callback);
        g_transfer_ctx.on_progress_method = env->GetMethodID(cls,
            "onProgress", "(JJDII)V");
        g_transfer_ctx.on_complete_method = env->GetMethodID(cls,
            "onComplete", "(I)V");
        env->DeleteLocalRef(cls);
    }
    
    TeleportError err = teleport_send_files(engine, &target,
        path_ptrs.data(), path_ptrs.size(),
        onProgress, onComplete, nullptr, nullptr);
    
    if (err != TELEPORT_OK) {
        LOGE("Failed to send files: %s", teleport_error_string(err));
    }
    
    return err;
}

JNIEXPORT jint JNICALL
Java_com_teleport_TeleportEngine_nativeStartReceiving(
    JNIEnv* env,
    jobject thiz,
    jlong handle,
    jstring output_dir,
    jobject callback
) {
    if (!handle) return TELEPORT_ERROR_INVALID_ARGUMENT;
    
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    std::string dir = jstringToString(env, output_dir);
    
    // Store callback
    {
        std::lock_guard<std::mutex> lock(g_callback_mutex);
        if (g_transfer_ctx.callback_obj) {
            env->DeleteGlobalRef(g_transfer_ctx.callback_obj);
        }
        g_transfer_ctx.callback_obj = env->NewGlobalRef(callback);
        
        jclass cls = env->GetObjectClass(callback);
        g_transfer_ctx.on_progress_method = env->GetMethodID(cls,
            "onProgress", "(JJDII)V");
        g_transfer_ctx.on_complete_method = env->GetMethodID(cls,
            "onComplete", "(I)V");
        g_transfer_ctx.on_incoming_method = env->GetMethodID(cls,
            "onIncoming", "(Ljava/lang/String;J)Z");
        env->DeleteLocalRef(cls);
    }
    
    // Note: Simplified - full impl would handle incoming callback
    TeleportError err = teleport_start_receiving(engine, dir.c_str(),
        nullptr, onProgress, onComplete, nullptr);
    
    if (err != TELEPORT_OK) {
        LOGE("Failed to start receiving: %s", teleport_error_string(err));
    }
    
    return err;
}

JNIEXPORT jint JNICALL
Java_com_teleport_TeleportEngine_nativeStopReceiving(
    JNIEnv* env,
    jobject thiz,
    jlong handle
) {
    if (!handle) return TELEPORT_ERROR_INVALID_ARGUMENT;
    TeleportEngine* engine = reinterpret_cast<TeleportEngine*>(handle);
    return teleport_stop_receiving(engine);
}

} // extern "C"
