package com.teleportmobile

import android.os.Handler
import android.os.Looper
import android.util.Log
import com.facebook.react.bridge.*
import com.facebook.react.modules.core.DeviceEventManagerModule

class TeleportModule(reactContext: ReactApplicationContext) : ReactContextBaseJavaModule(reactContext) {

    private var engineHandle: Long = 0
    private val mainHandler = Handler(Looper.getMainLooper())
    
    companion object {
        const val NAME = "TeleportModule"
        private const val TAG = "TeleportModule"
        
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
    private external fun nativeSendFiles(handle: Long, targetId: String, filePaths: Array<String>): Boolean
    private external fun nativeStartReceiving(handle: Long, outputDir: String): Boolean
    private external fun nativeStopReceiving(handle: Long)
    
    // Emit methods called from native code (JNI)
    fun emitDeviceDiscovered(deviceJson: String) {
        Log.d(TAG, "emitDeviceDiscovered: $deviceJson")
        sendEvent("onDeviceDiscovered", deviceJson)
    }
    
    fun emitDeviceLost(deviceId: String) {
        Log.d(TAG, "emitDeviceLost: $deviceId")
        sendEvent("onDeviceLost", deviceId)
    }
    
    fun emitProgress(progressJson: String) {
        Log.d(TAG, "emitProgress: $progressJson")
        sendEvent("onProgress", progressJson)
    }
    
    fun emitComplete(errorCode: Int) {
        Log.d(TAG, "emitComplete: errorCode=$errorCode")
        sendEvent("onComplete", errorCode.toString())
    }
    
    private fun sendEvent(eventName: String, data: String) {
        mainHandler.post {
            try {
                reactApplicationContext
                    .getJSModule(DeviceEventManagerModule.RCTDeviceEventEmitter::class.java)
                    .emit(eventName, data)
            } catch (e: Exception) {
                Log.e(TAG, "Failed to emit event $eventName: ${e.message}")
            }
        }
    }
    
    @ReactMethod
    fun initialize(deviceName: String, promise: Promise) {
        try {
            Log.d(TAG, "Initializing with device name: $deviceName")
            engineHandle = nativeInit(deviceName)
            if (engineHandle != 0L) {
                Log.d(TAG, "Engine initialized successfully: $engineHandle")
                promise.resolve(true)
            } else {
                promise.reject("INIT_ERROR", "Failed to initialize Teleport engine")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Init error: ${e.message}", e)
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
            Log.d(TAG, "Sending ${paths.size} files to $targetId")
            val success = nativeSendFiles(engineHandle, targetId, paths)
            if (success) {
                promise.resolve(true)
            } else {
                promise.reject("SEND_ERROR", "Failed to start file transfer - target may not be receiving")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Send error: ${e.message}", e)
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
            Log.d(TAG, "Starting receiving to: $outputDir")
            val success = nativeStartReceiving(engineHandle, outputDir)
            if (success) {
                promise.resolve(true)
            } else {
                promise.reject("RECEIVE_ERROR", "Failed to start receiving")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Receive error: ${e.message}", e)
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
    
    // ============================================================================
    // QR Code Pairing
    // ============================================================================
    
    private external fun nativeGenerateQrPairing(handle: Long, expirySeconds: Int): String
    private external fun nativeConnectViaQr(handle: Long, qrData: String): Boolean
    private external fun nativeValidateQrPairing(qrData: String): String
    
    @ReactMethod
    fun generateQrPairing(expirySeconds: Int, promise: Promise) {
        try {
            if (engineHandle == 0L) {
                promise.reject("NOT_INITIALIZED", "Engine not initialized")
                return
            }
            val result = nativeGenerateQrPairing(engineHandle, expirySeconds)
            if (result.isNotEmpty()) {
                promise.resolve(result)
            } else {
                promise.reject("QR_ERROR", "Failed to generate QR pairing")
            }
        } catch (e: Exception) {
            promise.reject("QR_ERROR", e.message, e)
        }
    }
    
    @ReactMethod
    fun connectViaQr(qrData: String, promise: Promise) {
        try {
            if (engineHandle == 0L) {
                promise.reject("NOT_INITIALIZED", "Engine not initialized")
                return
            }
            val success = nativeConnectViaQr(engineHandle, qrData)
            if (success) {
                promise.resolve(true)
            } else {
                promise.reject("QR_ERROR", "Failed to connect via QR")
            }
        } catch (e: Exception) {
            promise.reject("QR_ERROR", e.message, e)
        }
    }
    
    @ReactMethod
    fun validateQrPairing(qrData: String, promise: Promise) {
        try {
            val result = nativeValidateQrPairing(qrData)
            promise.resolve(result)
        } catch (e: Exception) {
            promise.reject("QR_ERROR", e.message, e)
        }
    }
    
    // ============================================================================
    // Hotspot Mode
    // ============================================================================
    
    private external fun nativeIsHotspotSupported(): Boolean
    private external fun nativeCreateHotspot(handle: Long): String
    private external fun nativeDestroyHotspot(handle: Long): Boolean
    private external fun nativeGetHotspotInfo(handle: Long): String
    private external fun nativeDetectHotspot(): String
    
    @ReactMethod
    fun isHotspotSupported(promise: Promise) {
        try {
            promise.resolve(nativeIsHotspotSupported())
        } catch (e: Exception) {
            promise.resolve(false)
        }
    }
    
    @ReactMethod
    fun createHotspot(promise: Promise) {
        try {
            if (engineHandle == 0L) {
                promise.reject("NOT_INITIALIZED", "Engine not initialized")
                return
            }
            val result = nativeCreateHotspot(engineHandle)
            if (result.isNotEmpty()) {
                promise.resolve(result)
            } else {
                promise.reject("HOTSPOT_ERROR", "Failed to create hotspot")
            }
        } catch (e: Exception) {
            promise.reject("HOTSPOT_ERROR", e.message, e)
        }
    }
    
    @ReactMethod
    fun destroyHotspot(promise: Promise) {
        try {
            if (engineHandle == 0L) {
                promise.resolve(false)
                return
            }
            val success = nativeDestroyHotspot(engineHandle)
            promise.resolve(success)
        } catch (e: Exception) {
            promise.reject("HOTSPOT_ERROR", e.message, e)
        }
    }
    
    @ReactMethod
    fun getHotspotInfo(promise: Promise) {
        try {
            if (engineHandle == 0L) {
                promise.resolve("")
                return
            }
            promise.resolve(nativeGetHotspotInfo(engineHandle))
        } catch (e: Exception) {
            promise.reject("HOTSPOT_ERROR", e.message, e)
        }
    }
    
    @ReactMethod
    fun detectHotspot(promise: Promise) {
        try {
            promise.resolve(nativeDetectHotspot())
        } catch (e: Exception) {
            promise.resolve("")
        }
    }
    
    // ============================================================================
    // WiFi Direct
    // ============================================================================
    
    private external fun nativeIsWifiDirectSupported(): Boolean
    private external fun nativeWifiDirectDisconnect(handle: Long): Boolean
    
    @ReactMethod
    fun isWifiDirectSupported(promise: Promise) {
        try {
            promise.resolve(nativeIsWifiDirectSupported())
        } catch (e: Exception) {
            promise.resolve(false)
        }
    }
    
    @ReactMethod
    fun wifiDirectDisconnect(promise: Promise) {
        try {
            if (engineHandle == 0L) {
                promise.resolve(false)
                return
            }
            val success = nativeWifiDirectDisconnect(engineHandle)
            promise.resolve(success)
        } catch (e: Exception) {
            promise.reject("WIFI_DIRECT_ERROR", e.message, e)
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
}

