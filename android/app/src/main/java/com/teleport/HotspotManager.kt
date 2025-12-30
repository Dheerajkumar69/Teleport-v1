package com.teleport

import android.content.Context
import android.net.ConnectivityManager
import android.net.wifi.WifiManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Manages Wi-Fi hotspot for direct device-to-device transfers
 * 
 * Uses LocalOnlyHotspot API (Android 8.0+) which creates a hotspot
 * that only allows local network communication (no internet sharing).
 */
class HotspotManager(context: Context) {
    
    private val appContext = context.applicationContext
    private val wifiManager = appContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
    private val connectivityManager = appContext.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    
    private var hotspotReservation: WifiManager.LocalOnlyHotspotReservation? = null
    
    private val _state = MutableStateFlow(HotspotState.IDLE)
    val state: StateFlow<HotspotState> = _state.asStateFlow()
    
    private val _info = MutableStateFlow<HotspotInfo?>(null)
    val info: StateFlow<HotspotInfo?> = _info.asStateFlow()
    
    enum class HotspotState {
        IDLE,
        STARTING,
        ACTIVE,
        FAILED
    }
    
    data class HotspotInfo(
        val ssid: String,
        val password: String,
        val gatewayIp: String
    )
    
    /**
     * Start a local-only hotspot
     * Requires: android.permission.CHANGE_WIFI_STATE
     */
    fun start(callback: HotspotCallback? = null) {
        if (_state.value == HotspotState.ACTIVE) {
            callback?.onStarted(_info.value!!)
            return
        }
        
        _state.value = HotspotState.STARTING
        
        try {
            wifiManager.startLocalOnlyHotspot(object : WifiManager.LocalOnlyHotspotCallback() {
                override fun onStarted(reservation: WifiManager.LocalOnlyHotspotReservation) {
                    hotspotReservation = reservation
                    
                    val config = reservation.wifiConfiguration
                        ?: reservation.softApConfiguration?.let { softAp ->
                            // Android 11+ uses SoftApConfiguration
                            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                                HotspotInfo(
                                    ssid = softAp.ssid ?: "Teleport",
                                    password = softAp.passphrase ?: "",
                                    gatewayIp = getGatewayIp()
                                )
                            } else null
                        }?.let { return@let it }
                        ?: run {
                            // Fallback for older Android versions
                            HotspotInfo(
                                ssid = "Teleport-Hotspot",
                                password = "",
                                gatewayIp = getGatewayIp()
                            )
                        }
                    
                    val info = if (config is HotspotInfo) {
                        config
                    } else {
                        @Suppress("DEPRECATION")
                        val wifiConfig = reservation.wifiConfiguration
                        HotspotInfo(
                            ssid = wifiConfig?.SSID ?: "Teleport",
                            password = wifiConfig?.preSharedKey ?: "",
                            gatewayIp = getGatewayIp()
                        )
                    }
                    
                    _info.value = info
                    _state.value = HotspotState.ACTIVE
                    callback?.onStarted(info)
                }
                
                override fun onStopped() {
                    hotspotReservation = null
                    _info.value = null
                    _state.value = HotspotState.IDLE
                    callback?.onStopped()
                }
                
                override fun onFailed(reason: Int) {
                    _state.value = HotspotState.FAILED
                    callback?.onFailed(reason)
                }
            }, Handler(Looper.getMainLooper()))
            
        } catch (e: SecurityException) {
            _state.value = HotspotState.FAILED
            callback?.onFailed(-1)
        }
    }
    
    /**
     * Stop the hotspot
     */
    fun stop() {
        hotspotReservation?.close()
        hotspotReservation = null
        _info.value = null
        _state.value = HotspotState.IDLE
    }
    
    /**
     * Check if we're connected to a hotspot (as a client)
     * Returns gateway IP if connected, null otherwise
     */
    fun detectHotspotGateway(): String? {
        val gateway = getGatewayIp()
        // Common hotspot IP ranges
        return if (gateway.startsWith("192.168.43.") ||  // Android hotspot
                   gateway.startsWith("192.168.137.") || // Windows hotspot
                   gateway.startsWith("172.20.10.")) {   // iOS hotspot
            gateway
        } else {
            null
        }
    }
    
    private fun getGatewayIp(): String {
        return try {
            val dhcp = wifiManager.dhcpInfo
            val gateway = dhcp.gateway
            if (gateway != 0) {
                intToIp(gateway)
            } else {
                // If we're the hotspot host, use typical gateway
                "192.168.43.1"
            }
        } catch (e: Exception) {
            "192.168.43.1"
        }
    }
    
    private fun intToIp(ip: Int): String {
        return "${ip and 0xFF}.${(ip shr 8) and 0xFF}.${(ip shr 16) and 0xFF}.${(ip shr 24) and 0xFF}"
    }
    
    interface HotspotCallback {
        fun onStarted(info: HotspotInfo)
        fun onStopped()
        fun onFailed(reason: Int)
    }
}
