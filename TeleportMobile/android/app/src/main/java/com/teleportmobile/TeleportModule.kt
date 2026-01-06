package com.teleportmobile

import android.os.Handler
import android.os.Looper
import com.facebook.react.bridge.*
import com.facebook.react.modules.core.DeviceEventManagerModule

class TeleportModule(reactContext: ReactApplicationContext) : ReactContextBaseJavaModule(reactContext) {

    private var engineHandle: Long = 0
    private val mainHandler = Handler(Looper.getMainLooper())
    
    companion object {
        const val NAME = "TeleportModule"
        
        init {
            System.loadLibrary("teleport_rn")
        }
    }
    
    override fun getName(): String = NAME
    
    // Native JNI methods
    private external fun nativeInit(deviceName: String): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeStartDiscovery(handle: Long)
    private external fun nativeStopDiscovery(handle: Long)
    private external fun nativeGetDevices(handle: Long): String
    private external fun nativeSendFiles(handle: Long, targetId: String, filePaths: Array<String>)
    private external fun nativeStartReceiving(handle: Long, outputDir: String)
    private external fun nativeStopReceiving(handle: Long)
    
    @ReactMethod
    fun initialize(deviceName: String, promise: Promise) {
        try {
            engineHandle = nativeInit(deviceName)
            if (engineHandle != 0L) {
                promise.resolve(true)
            } else {
                promise.reject("INIT_ERROR", "Failed to initialize Teleport engine")
            }
        } catch (e: Exception) {
            promise.reject("INIT_ERROR", e.message, e)
        }
    }
    
    @ReactMethod
    fun destroy(promise: Promise) {
        try {
            if (engineHandle != 0L) {
                nativeDestroy(engineHandle)
                engineHandle = 0
            }
            promise.resolve(true)
        } catch (e: Exception) {
            promise.reject("DESTROY_ERROR", e.message, e)
        }
    }
    
    @ReactMethod
    fun startDiscovery(promise: Promise) {
        try {
            if (engineHandle == 0L) {
                promise.reject("NOT_INITIALIZED", "Engine not initialized")
                return
            }
            nativeStartDiscovery(engineHandle)
            promise.resolve(true)
        } catch (e: Exception) {
            promise.reject("DISCOVERY_ERROR", e.message, e)
        }
    }
    
    @ReactMethod
    fun stopDiscovery(promise: Promise) {
        try {
            if (engineHandle != 0L) {
                nativeStopDiscovery(engineHandle)
            }
            promise.resolve(true)
        } catch (e: Exception) {
            promise.reject("DISCOVERY_ERROR", e.message, e)
        }
    }
    
    @ReactMethod
    fun getDevices(promise: Promise) {
        try {
            if (engineHandle == 0L) {
                promise.reject("NOT_INITIALIZED", "Engine not initialized")
                return
            }
            val devicesJson = nativeGetDevices(engineHandle)
            promise.resolve(devicesJson)
        } catch (e: Exception) {
            promise.reject("DEVICES_ERROR", e.message, e)
        }
    }
    
    @ReactMethod
    fun sendFiles(targetId: String, filePaths: ReadableArray, promise: Promise) {
        try {
            if (engineHandle == 0L) {
                promise.reject("NOT_INITIALIZED", "Engine not initialized")
                return
            }
            val paths = Array(filePaths.size()) { i -> filePaths.getString(i) ?: "" }
            nativeSendFiles(engineHandle, targetId, paths)
            promise.resolve(true)
        } catch (e: Exception) {
            promise.reject("SEND_ERROR", e.message, e)
        }
    }
    
    @ReactMethod
    fun startReceiving(outputDir: String, promise: Promise) {
        try {
            if (engineHandle == 0L) {
                promise.reject("NOT_INITIALIZED", "Engine not initialized")
                return
            }
            nativeStartReceiving(engineHandle, outputDir)
            promise.resolve(true)
        } catch (e: Exception) {
            promise.reject("RECEIVE_ERROR", e.message, e)
        }
    }
    
    @ReactMethod
    fun stopReceiving(promise: Promise) {
        try {
            if (engineHandle != 0L) {
                nativeStopReceiving(engineHandle)
            }
            promise.resolve(true)
        } catch (e: Exception) {
            promise.reject("RECEIVE_ERROR", e.message, e)
        }
    }
    
    // Event emitter support
    @ReactMethod
    fun addListener(eventName: String) {
        // Required for RN event emitter
    }
    
    @ReactMethod
    fun removeListeners(count: Int) {
        // Required for RN event emitter
    }
    
    // Called from native to emit events
    fun emitEvent(eventName: String, data: String) {
        mainHandler.post {
            reactApplicationContext
                .getJSModule(DeviceEventManagerModule.RCTDeviceEventEmitter::class.java)
                .emit(eventName, data)
        }
    }
}
