package com.teleportmobile

import android.os.Build
import com.facebook.react.bridge.*
import com.facebook.react.modules.core.DeviceEventManagerModule
import java.io.File

class TeleportModule(reactContext: ReactApplicationContext) : ReactContextBaseJavaModule(reactContext) {

    companion object {
        init {
            System.loadLibrary("teleport_rn")
        }
    }

    private var engineHandle: Long = 0

    override fun getName(): String = "TeleportModule"

    override fun initialize() {
        super.initialize()
        // Create engine on initialize
        val deviceName = "${Build.MANUFACTURER} ${Build.MODEL}"
        val downloadPath = File(reactApplicationContext.getExternalFilesDir(null), "Teleport").apply {
            mkdirs()
        }.absolutePath
        engineHandle = nativeCreate(deviceName, downloadPath)
    }

    override fun invalidate() {
        if (engineHandle != 0L) {
            nativeDestroy(engineHandle)
            engineHandle = 0
        }
        super.invalidate()
    }

    // === Discovery APIs ===
    
    @ReactMethod
    fun startDiscovery(promise: Promise) {
        if (engineHandle == 0L) {
            promise.reject("ENGINE_ERROR", "Engine not initialized")
            return
        }
        val result = nativeStartDiscovery(engineHandle)
        if (result == 0) {
            promise.resolve(true)
        } else {
            promise.reject("DISCOVERY_ERROR", "Failed to start discovery: $result")
        }
    }

    @ReactMethod
    fun stopDiscovery(promise: Promise) {
        if (engineHandle == 0L) {
            promise.reject("ENGINE_ERROR", "Engine not initialized")
            return
        }
        nativeStopDiscovery(engineHandle)
        promise.resolve(true)
    }

    @ReactMethod
    fun getDevices(promise: Promise) {
        if (engineHandle == 0L) {
            promise.reject("ENGINE_ERROR", "Engine not initialized")
            return
        }
        val devicesJson = nativeGetDevices(engineHandle)
        promise.resolve(devicesJson)
    }

    // === Send APIs ===
    
    @ReactMethod
    fun sendFiles(deviceId: String, filePaths: ReadableArray, promise: Promise) {
        if (engineHandle == 0L) {
            promise.reject("ENGINE_ERROR", "Engine not initialized")
            return
        }
        val paths = Array(filePaths.size()) { filePaths.getString(it) ?: "" }
        val result = nativeSendFiles(engineHandle, deviceId, paths)
        if (result == 0) {
            promise.resolve(true)
        } else {
            promise.reject("SEND_ERROR", "Failed to send files: $result")
        }
    }

    // === Receive APIs ===
    
    @ReactMethod
    fun startReceiving(promise: Promise) {
        if (engineHandle == 0L) {
            promise.reject("ENGINE_ERROR", "Engine not initialized")
            return
        }
        val downloadPath = File(reactApplicationContext.getExternalFilesDir(null), "Teleport").absolutePath
        val result = nativeStartReceiving(engineHandle, downloadPath)
        if (result == 0) {
            promise.resolve(true)
        } else {
            promise.reject("RECEIVE_ERROR", "Failed to start receiving: $result")
        }
    }

    @ReactMethod
    fun stopReceiving(promise: Promise) {
        if (engineHandle == 0L) {
            promise.reject("ENGINE_ERROR", "Engine not initialized")
            return
        }
        nativeStopReceiving(engineHandle)
        promise.resolve(true)
    }

    // === Event Emitters ===
    
    private fun sendEvent(eventName: String, params: WritableMap?) {
        reactApplicationContext
            .getJSModule(DeviceEventManagerModule.RCTDeviceEventEmitter::class.java)
            .emit(eventName, params)
    }

    // Called from JNI when device found
    fun onDeviceFound(id: String, name: String, os: String, ip: String, port: Int) {
        val params = Arguments.createMap().apply {
            putString("id", id)
            putString("name", name)
            putString("os", os)
            putString("ip", ip)
            putInt("port", port)
        }
        sendEvent("onDeviceFound", params)
    }

    // Called from JNI when device lost
    fun onDeviceLost(deviceId: String) {
        val params = Arguments.createMap().apply {
            putString("id", deviceId)
        }
        sendEvent("onDeviceLost", params)
    }

    // Called from JNI for transfer progress
    fun onTransferProgress(bytesTransferred: Long, bytesTotal: Long, speedBps: Double, filesCompleted: Int, filesTotal: Int) {
        val params = Arguments.createMap().apply {
            putDouble("bytesTransferred", bytesTransferred.toDouble())
            putDouble("bytesTotal", bytesTotal.toDouble())
            putDouble("speedBps", speedBps)
            putInt("filesCompleted", filesCompleted)
            putInt("filesTotal", filesTotal)
        }
        sendEvent("onTransferProgress", params)
    }

    // Called from JNI when transfer complete
    fun onTransferComplete(errorCode: Int) {
        val params = Arguments.createMap().apply {
            putInt("errorCode", errorCode)
            putBoolean("success", errorCode == 0)
        }
        sendEvent("onTransferComplete", params)
    }

    // Native methods
    private external fun nativeCreate(deviceName: String, downloadPath: String): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeStartDiscovery(handle: Long): Int
    private external fun nativeStopDiscovery(handle: Long): Int
    private external fun nativeGetDevices(handle: Long): String
    private external fun nativeSendFiles(handle: Long, deviceId: String, filePaths: Array<String>): Int
    private external fun nativeStartReceiving(handle: Long, outputDir: String): Int
    private external fun nativeStopReceiving(handle: Long): Int
}
