package com.teleportmobile

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.net.wifi.WifiManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import androidx.annotation.RequiresApi
import androidx.core.content.ContextCompat
import com.facebook.react.bridge.*
import com.facebook.react.modules.core.DeviceEventManagerModule

/**
 * Android Hotspot manager for Teleport
 * 
 * Uses the Local-only Hotspot API (Android 8+) to create a WiFi access point
 * that other devices can connect to for file transfer without internet.
 */
class HotspotManager(private val reactContext: ReactApplicationContext) : 
    ReactContextBaseJavaModule(reactContext) {
    
    companion object {
        const val NAME = "HotspotManager"
        private const val TAG = "HotspotManager"
        
        // Events
        const val EVENT_HOTSPOT_STARTED = "HotspotStarted"
        const val EVENT_HOTSPOT_STOPPED = "HotspotStopped"
        const val EVENT_HOTSPOT_FAILED = "HotspotFailed"
        const val EVENT_CLIENT_CONNECTED = "HotspotClientConnected"
        const val EVENT_CLIENT_DISCONNECTED = "HotspotClientDisconnected"
    }
    
    private var wifiManager: WifiManager? = null
    private var hotspotReservation: WifiManager.LocalOnlyHotspotReservation? = null
    private var isHotspotActive = false
    private var currentSsid: String? = null
    private var currentPassword: String? = null
    private var gatewayIp: String? = null
    
    private val mainHandler = Handler(Looper.getMainLooper())
    
    override fun getName(): String = NAME
    
    /**
     * Initialize the hotspot manager
     */
    @ReactMethod
    fun initialize(promise: Promise) {
        try {
            wifiManager = reactContext.applicationContext
                .getSystemService(Context.WIFI_SERVICE) as? WifiManager
            
            if (wifiManager == null) {
                promise.reject("NOT_SUPPORTED", "WiFi not available")
                return
            }
            
            promise.resolve(true)
        } catch (e: Exception) {
            promise.reject("INIT_ERROR", e.message, e)
        }
    }
    
    /**
     * Check if hotspot creation is supported
     */
    @ReactMethod
    fun isSupported(promise: Promise) {
        // Local-only hotspot requires Android 8.0+
        promise.resolve(Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
    }
    
    /**
     * Start a local-only hotspot
     */
    @ReactMethod
    fun startHotspot(promise: Promise) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
            promise.reject("NOT_SUPPORTED", "Requires Android 8.0 or higher")
            return
        }
        
        if (isHotspotActive) {
            promise.reject("ALREADY_ACTIVE", "Hotspot is already running")
            return
        }
        
        if (!checkPermissions()) {
            promise.reject("PERMISSION_DENIED", "Missing location permission")
            return
        }
        
        val wifi = wifiManager
        if (wifi == null) {
            promise.reject("NOT_INITIALIZED", "WiFi manager not initialized")
            return
        }
        
        try {
            startLocalOnlyHotspot(wifi, promise)
        } catch (e: SecurityException) {
            promise.reject("PERMISSION_DENIED", e.message, e)
        } catch (e: Exception) {
            promise.reject("START_FAILED", e.message, e)
        }
    }
    
    @RequiresApi(Build.VERSION_CODES.O)
    private fun startLocalOnlyHotspot(wifi: WifiManager, promise: Promise) {
        wifi.startLocalOnlyHotspot(object : WifiManager.LocalOnlyHotspotCallback() {
            override fun onStarted(reservation: WifiManager.LocalOnlyHotspotReservation) {
                hotspotReservation = reservation
                isHotspotActive = true
                
                val config = reservation.wifiConfiguration
                currentSsid = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    reservation.softApConfiguration?.ssid
                } else {
                    @Suppress("DEPRECATION")
                    config?.SSID
                }
                
                currentPassword = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    reservation.softApConfiguration?.passphrase
                } else {
                    @Suppress("DEPRECATION")
                    config?.preSharedKey
                }
                
                // Get gateway IP (typically 192.168.43.1 for Android hotspots)
                gatewayIp = getHotspotGatewayIp()
                
                Log.i(TAG, "Hotspot started: SSID=$currentSsid")
                
                val result = Arguments.createMap()
                result.putString("ssid", currentSsid)
                result.putString("password", currentPassword)
                result.putString("gatewayIp", gatewayIp)
                
                promise.resolve(result)
                emitEvent(EVENT_HOTSPOT_STARTED, result)
            }
            
            override fun onStopped() {
                Log.i(TAG, "Hotspot stopped")
                cleanup()
                emitEvent(EVENT_HOTSPOT_STOPPED, null)
            }
            
            override fun onFailed(reason: Int) {
                Log.e(TAG, "Hotspot failed: reason=$reason")
                cleanup()
                
                val msg = when (reason) {
                    ERROR_NO_CHANNEL -> "No available channel"
                    ERROR_GENERIC -> "Generic error"
                    ERROR_INCOMPATIBLE_MODE -> "Incompatible mode"
                    ERROR_TETHERING_DISALLOWED -> "Tethering disallowed"
                    else -> "Unknown error: $reason"
                }
                
                promise.reject("HOTSPOT_FAILED", msg)
                emitEvent(EVENT_HOTSPOT_FAILED, msg)
            }
        }, mainHandler)
    }
    
    /**
     * Stop the hotspot
     */
    @ReactMethod
    fun stopHotspot(promise: Promise) {
        if (!isHotspotActive) {
            promise.resolve(true)
            return
        }
        
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                hotspotReservation?.close()
            }
            cleanup()
            promise.resolve(true)
        } catch (e: Exception) {
            promise.reject("STOP_FAILED", e.message, e)
        }
    }
    
    /**
     * Get current hotspot info
     */
    @ReactMethod
    fun getHotspotInfo(promise: Promise) {
        if (!isHotspotActive) {
            promise.resolve(null)
            return
        }
        
        val info = Arguments.createMap()
        info.putString("ssid", currentSsid)
        info.putString("password", currentPassword)
        info.putString("gatewayIp", gatewayIp)
        info.putBoolean("isActive", isHotspotActive)
        promise.resolve(info)
    }
    
    /**
     * Check if hotspot is currently active
     */
    @ReactMethod
    fun isActive(promise: Promise) {
        promise.resolve(isHotspotActive)
    }
    
    /**
     * Clean up resources
     */
    @ReactMethod
    fun destroy(promise: Promise) {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                hotspotReservation?.close()
            }
            cleanup()
            promise.resolve(true)
        } catch (e: Exception) {
            promise.reject("DESTROY_ERROR", e.message, e)
        }
    }
    
    // =========================================================================
    // Private helpers
    // =========================================================================
    
    private fun cleanup() {
        hotspotReservation = null
        isHotspotActive = false
        currentSsid = null
        currentPassword = null
        gatewayIp = null
    }
    
    private fun checkPermissions(): Boolean {
        val context = reactContext.applicationContext
        
        // Location permission required for hotspot
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            ContextCompat.checkSelfPermission(
                context, Manifest.permission.ACCESS_FINE_LOCATION
            ) == PackageManager.PERMISSION_GRANTED
        } else {
            ContextCompat.checkSelfPermission(
                context, Manifest.permission.ACCESS_COARSE_LOCATION
            ) == PackageManager.PERMISSION_GRANTED
        }
    }
    
    private fun getHotspotGatewayIp(): String {
        // Android typically uses 192.168.43.1 for Local-only hotspot
        // Try to detect from network interfaces
        try {
            val interfaces = java.net.NetworkInterface.getNetworkInterfaces()
            while (interfaces.hasMoreElements()) {
                val iface = interfaces.nextElement()
                // Look for ap0, wlan0, or similar hotspot interfaces
                if (iface.name.startsWith("ap") || 
                    iface.name.startsWith("wlan") ||
                    iface.name.startsWith("swlan")) {
                    val addresses = iface.inetAddresses
                    while (addresses.hasMoreElements()) {
                        val addr = addresses.nextElement()
                        if (!addr.isLoopbackAddress && addr is java.net.Inet4Address) {
                            // Check if it's in the typical hotspot range
                            val ip = addr.hostAddress ?: continue
                            if (ip.startsWith("192.168.43.") || ip.startsWith("192.168.49.")) {
                                return ip
                            }
                        }
                    }
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "Failed to detect hotspot IP: ${e.message}")
        }
        
        // Fallback to common default
        return "192.168.43.1"
    }
    
    private fun emitEvent(eventName: String, data: Any?) {
        try {
            reactContext
                .getJSModule(DeviceEventManagerModule.RCTDeviceEventEmitter::class.java)
                .emit(eventName, data)
        } catch (e: Exception) {
            Log.w(TAG, "Failed to emit event: ${e.message}")
        }
    }
    
    // Required for RN event emitter
    @ReactMethod
    fun addListener(eventName: String) {}
    
    @ReactMethod
    fun removeListeners(count: Int) {}
}
