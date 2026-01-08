package com.teleportmobile

import android.Manifest
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.net.NetworkInfo
import android.net.wifi.p2p.*
import android.os.Build
import android.os.Looper
import android.util.Log
import androidx.core.content.ContextCompat
import com.facebook.react.bridge.*
import com.facebook.react.modules.core.DeviceEventManagerModule
import java.net.InetAddress

/**
 * WiFi Direct / WiFi P2P manager for Android
 * 
 * Handles peer discovery, connection, and group formation for
 * direct device-to-device file transfer without a router.
 */
class WifiDirectManager(private val reactContext: ReactApplicationContext) : 
    ReactContextBaseJavaModule(reactContext) {
    
    companion object {
        const val NAME = "WifiDirectManager"
        private const val TAG = "WifiDirectManager"
        
        // Event names
        const val EVENT_PEER_FOUND = "WifiDirectPeerFound"
        const val EVENT_PEER_LOST = "WifiDirectPeerLost"
        const val EVENT_CONNECTED = "WifiDirectConnected"
        const val EVENT_DISCONNECTED = "WifiDirectDisconnected"
        const val EVENT_STATE_CHANGED = "WifiDirectStateChanged"
        const val EVENT_ERROR = "WifiDirectError"
    }
    
    private var wifiP2pManager: WifiP2pManager? = null
    private var channel: WifiP2pManager.Channel? = null
    private var receiver: BroadcastReceiver? = null
    private var isReceiverRegistered = false
    
    private val peers = mutableMapOf<String, WifiP2pDevice>()
    private var currentConnection: WifiP2pInfo? = null
    private var isDiscovering = false
    
    // Native callback references
    private external fun nativeOnPeerFound(mac: String, name: String, signal: Int)
    private external fun nativeOnPeerLost(mac: String)
    private external fun nativeOnConnected(
        peerMac: String, peerName: String, 
        goIp: String, localIp: String, isGo: Boolean
    )
    private external fun nativeOnDisconnected()
    
    override fun getName(): String = NAME
    
    init {
        try {
            System.loadLibrary("teleport_rn")
        } catch (e: UnsatisfiedLinkError) {
            Log.w(TAG, "Native library not loaded for WiFi Direct: ${e.message}")
        }
    }
    
    /**
     * Initialize WiFi P2P manager
     */
    @ReactMethod
    fun initialize(promise: Promise) {
        try {
            val activity = currentActivity
            if (activity == null) {
                promise.reject("NO_ACTIVITY", "Activity not available")
                return
            }
            
            wifiP2pManager = activity.getSystemService(Context.WIFI_P2P_SERVICE) as? WifiP2pManager
            if (wifiP2pManager == null) {
                promise.reject("NOT_SUPPORTED", "WiFi P2P not supported on this device")
                return
            }
            
            channel = wifiP2pManager?.initialize(activity, Looper.getMainLooper()) { 
                Log.i(TAG, "WiFi P2P channel disconnected")
                emitEvent(EVENT_STATE_CHANGED, "Disconnected")
            }
            
            registerReceiver()
            promise.resolve(true)
            
        } catch (e: Exception) {
            promise.reject("INIT_ERROR", e.message, e)
        }
    }
    
    /**
     * Check if WiFi Direct is available
     */
    @ReactMethod
    fun isAvailable(promise: Promise) {
        val available = wifiP2pManager != null && channel != null
        promise.resolve(available)
    }
    
    fun isAvailable(): Boolean {
        return wifiP2pManager != null && channel != null
    }
    
    /**
     * Start peer discovery
     */
    @ReactMethod
    fun startDiscovery(promise: Promise) {
        if (!checkPermissions()) {
            promise.reject("PERMISSION_DENIED", "Missing WiFi P2P permissions")
            return
        }
        
        val manager = wifiP2pManager
        val ch = channel
        if (manager == null || ch == null) {
            promise.reject("NOT_INITIALIZED", "WiFi P2P not initialized")
            return
        }
        
        try {
            manager.discoverPeers(ch, object : WifiP2pManager.ActionListener {
                override fun onSuccess() {
                    isDiscovering = true
                    emitEvent(EVENT_STATE_CHANGED, "Discovering")
                    promise.resolve(true)
                }
                
                override fun onFailure(reason: Int) {
                    val msg = getErrorMessage(reason)
                    Log.e(TAG, "Discovery failed: $msg")
                    promise.reject("DISCOVERY_FAILED", msg)
                }
            })
        } catch (e: SecurityException) {
            promise.reject("PERMISSION_DENIED", e.message, e)
        }
    }
    
    fun startDiscovery(): Boolean {
        if (!checkPermissions()) return false
        val manager = wifiP2pManager ?: return false
        val ch = channel ?: return false
        
        try {
            manager.discoverPeers(ch, object : WifiP2pManager.ActionListener {
                override fun onSuccess() {
                    isDiscovering = true
                }
                override fun onFailure(reason: Int) {
                    Log.e(TAG, "Discovery failed: ${getErrorMessage(reason)}")
                }
            })
            return true
        } catch (e: SecurityException) {
            return false
        }
    }
    
    /**
     * Stop peer discovery
     */
    @ReactMethod
    fun stopDiscovery(promise: Promise) {
        val manager = wifiP2pManager
        val ch = channel
        if (manager == null || ch == null) {
            promise.resolve(true)
            return
        }
        
        try {
            manager.stopPeerDiscovery(ch, object : WifiP2pManager.ActionListener {
                override fun onSuccess() {
                    isDiscovering = false
                    emitEvent(EVENT_STATE_CHANGED, "Idle")
                    promise.resolve(true)
                }
                override fun onFailure(reason: Int) {
                    promise.resolve(true) // Consider stopped anyway
                }
            })
        } catch (e: SecurityException) {
            promise.resolve(true)
        }
    }
    
    fun stopDiscovery() {
        val manager = wifiP2pManager ?: return
        val ch = channel ?: return
        try {
            manager.stopPeerDiscovery(ch, null)
            isDiscovering = false
        } catch (e: SecurityException) {
            // Ignore
        }
    }
    
    /**
     * Connect to a peer by MAC address
     */
    @ReactMethod
    fun connect(deviceAddress: String, promise: Promise) {
        if (!checkPermissions()) {
            promise.reject("PERMISSION_DENIED", "Missing WiFi P2P permissions")
            return
        }
        
        val manager = wifiP2pManager
        val ch = channel
        if (manager == null || ch == null) {
            promise.reject("NOT_INITIALIZED", "WiFi P2P not initialized")
            return
        }
        
        val config = WifiP2pConfig().apply {
            this.deviceAddress = deviceAddress
            // Let the framework decide group owner
            groupOwnerIntent = 0
        }
        
        try {
            manager.connect(ch, config, object : WifiP2pManager.ActionListener {
                override fun onSuccess() {
                    emitEvent(EVENT_STATE_CHANGED, "Connecting")
                    promise.resolve(true)
                }
                
                override fun onFailure(reason: Int) {
                    val msg = getErrorMessage(reason)
                    Log.e(TAG, "Connect failed: $msg")
                    promise.reject("CONNECT_FAILED", msg)
                }
            })
        } catch (e: SecurityException) {
            promise.reject("PERMISSION_DENIED", e.message, e)
        }
    }
    
    fun connect(deviceAddress: String): Boolean {
        if (!checkPermissions()) return false
        val manager = wifiP2pManager ?: return false
        val ch = channel ?: return false
        
        val config = WifiP2pConfig().apply {
            this.deviceAddress = deviceAddress
            groupOwnerIntent = 0
        }
        
        return try {
            manager.connect(ch, config, null)
            true
        } catch (e: SecurityException) {
            false
        }
    }
    
    /**
     * Disconnect from current peer
     */
    @ReactMethod
    fun disconnect(promise: Promise) {
        val manager = wifiP2pManager
        val ch = channel
        if (manager == null || ch == null) {
            promise.resolve(true)
            return
        }
        
        manager.removeGroup(ch, object : WifiP2pManager.ActionListener {
            override fun onSuccess() {
                currentConnection = null
                emitEvent(EVENT_STATE_CHANGED, "Idle")
                promise.resolve(true)
            }
            override fun onFailure(reason: Int) {
                promise.resolve(true) // Consider disconnected anyway
            }
        })
    }
    
    fun disconnect() {
        val manager = wifiP2pManager ?: return
        val ch = channel ?: return
        manager.removeGroup(ch, null)
        currentConnection = null
    }
    
    /**
     * Get list of discovered peers
     */
    @ReactMethod
    fun getPeers(promise: Promise) {
        val array = Arguments.createArray()
        synchronized(peers) {
            for ((_, device) in peers) {
                val map = Arguments.createMap()
                map.putString("mac", device.deviceAddress)
                map.putString("name", device.deviceName)
                map.putString("type", getDeviceType(device.primaryDeviceType))
                map.putInt("status", device.status)
                array.pushMap(map)
            }
        }
        promise.resolve(array)
    }
    
    /**
     * Get current connection info
     */
    @ReactMethod
    fun getConnectionInfo(promise: Promise) {
        val info = currentConnection
        if (info == null) {
            promise.resolve(null)
            return
        }
        
        val map = Arguments.createMap()
        map.putString("groupOwnerIp", info.groupOwnerAddress?.hostAddress ?: "")
        map.putBoolean("isGroupOwner", info.isGroupOwner)
        map.putBoolean("groupFormed", info.groupFormed)
        promise.resolve(map)
    }
    
    /**
     * Clean up resources
     */
    @ReactMethod
    fun destroy(promise: Promise) {
        try {
            unregisterReceiver()
            wifiP2pManager = null
            channel = null
            peers.clear()
            currentConnection = null
            promise.resolve(true)
        } catch (e: Exception) {
            promise.reject("DESTROY_ERROR", e.message, e)
        }
    }
    
    // =========================================================================
    // Private helpers
    // =========================================================================
    
    private fun registerReceiver() {
        if (isReceiverRegistered) return
        
        val intentFilter = IntentFilter().apply {
            addAction(WifiP2pManager.WIFI_P2P_STATE_CHANGED_ACTION)
            addAction(WifiP2pManager.WIFI_P2P_PEERS_CHANGED_ACTION)
            addAction(WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION)
            addAction(WifiP2pManager.WIFI_P2P_THIS_DEVICE_CHANGED_ACTION)
        }
        
        receiver = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                when (intent.action) {
                    WifiP2pManager.WIFI_P2P_STATE_CHANGED_ACTION -> {
                        val state = intent.getIntExtra(
                            WifiP2pManager.EXTRA_WIFI_STATE,
                            WifiP2pManager.WIFI_P2P_STATE_DISABLED
                        )
                        if (state == WifiP2pManager.WIFI_P2P_STATE_ENABLED) {
                            Log.i(TAG, "WiFi P2P enabled")
                        } else {
                            Log.w(TAG, "WiFi P2P disabled")
                            emitEvent(EVENT_STATE_CHANGED, "Disabled")
                        }
                    }
                    
                    WifiP2pManager.WIFI_P2P_PEERS_CHANGED_ACTION -> {
                        requestPeers()
                    }
                    
                    WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION -> {
                        val networkInfo = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                            intent.getParcelableExtra(
                                WifiP2pManager.EXTRA_NETWORK_INFO,
                                NetworkInfo::class.java
                            )
                        } else {
                            @Suppress("DEPRECATION")
                            intent.getParcelableExtra(WifiP2pManager.EXTRA_NETWORK_INFO)
                        }
                        
                        if (networkInfo?.isConnected == true) {
                            requestConnectionInfo()
                        } else {
                            currentConnection = null
                            emitEvent(EVENT_DISCONNECTED, "")
                            try {
                                nativeOnDisconnected()
                            } catch (e: Exception) {
                                Log.w(TAG, "Native callback failed: ${e.message}")
                            }
                        }
                    }
                    
                    WifiP2pManager.WIFI_P2P_THIS_DEVICE_CHANGED_ACTION -> {
                        // Our device status changed
                    }
                }
            }
        }
        
        currentActivity?.registerReceiver(receiver, intentFilter)
        isReceiverRegistered = true
    }
    
    private fun unregisterReceiver() {
        if (!isReceiverRegistered) return
        try {
            currentActivity?.unregisterReceiver(receiver)
        } catch (e: Exception) {
            Log.w(TAG, "Failed to unregister receiver: ${e.message}")
        }
        isReceiverRegistered = false
    }
    
    private fun requestPeers() {
        val manager = wifiP2pManager ?: return
        val ch = channel ?: return
        
        try {
            manager.requestPeers(ch) { peerList ->
                val newPeers = peerList.deviceList.associateBy { it.deviceAddress }
                val currentMacs = peers.keys.toSet()
                val newMacs = newPeers.keys
                
                // Find lost peers
                for (mac in currentMacs - newMacs) {
                    emitPeerLost(mac)
                    try {
                        nativeOnPeerLost(mac)
                    } catch (e: Exception) {
                        Log.w(TAG, "Native callback failed: ${e.message}")
                    }
                }
                
                // Find new peers
                for (mac in newMacs - currentMacs) {
                    val device = newPeers[mac] ?: continue
                    emitPeerFound(device)
                    try {
                        nativeOnPeerFound(mac, device.deviceName, 0)
                    } catch (e: Exception) {
                        Log.w(TAG, "Native callback failed: ${e.message}")
                    }
                }
                
                synchronized(peers) {
                    peers.clear()
                    peers.putAll(newPeers)
                }
            }
        } catch (e: SecurityException) {
            Log.e(TAG, "Permission denied for peer request: ${e.message}")
        }
    }
    
    private fun requestConnectionInfo() {
        val manager = wifiP2pManager ?: return
        val ch = channel ?: return
        
        manager.requestConnectionInfo(ch) { info ->
            currentConnection = info
            if (info.groupFormed) {
                val goIp = info.groupOwnerAddress?.hostAddress ?: ""
                val isGo = info.isGroupOwner
                
                emitConnected(info)
                
                try {
                    // Get the connected device info
                    val peerMac = peers.keys.firstOrNull() ?: ""
                    val peerName = peers.values.firstOrNull()?.deviceName ?: ""
                    val localIp = getLocalIpAddress()
                    nativeOnConnected(peerMac, peerName, goIp, localIp, isGo)
                } catch (e: Exception) {
                    Log.w(TAG, "Native callback failed: ${e.message}")
                }
            }
        }
    }
    
    private fun emitPeerFound(device: WifiP2pDevice) {
        val map = Arguments.createMap()
        map.putString("mac", device.deviceAddress)
        map.putString("name", device.deviceName)
        map.putString("type", getDeviceType(device.primaryDeviceType))
        emitEvent(EVENT_PEER_FOUND, map)
    }
    
    private fun emitPeerLost(mac: String) {
        val map = Arguments.createMap()
        map.putString("mac", mac)
        emitEvent(EVENT_PEER_LOST, map)
    }
    
    private fun emitConnected(info: WifiP2pInfo) {
        val map = Arguments.createMap()
        map.putString("groupOwnerIp", info.groupOwnerAddress?.hostAddress ?: "")
        map.putBoolean("isGroupOwner", info.isGroupOwner)
        emitEvent(EVENT_CONNECTED, map)
    }
    
    private fun emitEvent(eventName: String, data: Any) {
        try {
            reactContext
                .getJSModule(DeviceEventManagerModule.RCTDeviceEventEmitter::class.java)
                .emit(eventName, data)
        } catch (e: Exception) {
            Log.w(TAG, "Failed to emit event: ${e.message}")
        }
    }
    
    private fun checkPermissions(): Boolean {
        val context = reactContext.applicationContext
        
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            ContextCompat.checkSelfPermission(
                context, Manifest.permission.NEARBY_WIFI_DEVICES
            ) == PackageManager.PERMISSION_GRANTED
        } else {
            ContextCompat.checkSelfPermission(
                context, Manifest.permission.ACCESS_FINE_LOCATION
            ) == PackageManager.PERMISSION_GRANTED
        }
    }
    
    private fun getErrorMessage(reason: Int): String {
        return when (reason) {
            WifiP2pManager.ERROR -> "Internal error"
            WifiP2pManager.P2P_UNSUPPORTED -> "P2P not supported"
            WifiP2pManager.BUSY -> "Framework busy"
            WifiP2pManager.NO_SERVICE_REQUESTS -> "No service requests"
            else -> "Unknown error: $reason"
        }
    }
    
    private fun getDeviceType(type: String?): String {
        if (type == null) return "Unknown"
        // WiFi P2P device type format: "category-oui-subCategory"
        return when {
            type.startsWith("1-") -> "Computer"
            type.startsWith("2-") -> "Input Device"
            type.startsWith("3-") -> "Printer"
            type.startsWith("4-") -> "Camera"
            type.startsWith("5-") -> "Storage"
            type.startsWith("6-") -> "Network Infrastructure"
            type.startsWith("7-") -> "Display"
            type.startsWith("8-") -> "Multimedia"
            type.startsWith("9-") -> "Gaming"
            type.startsWith("10-") -> "Phone"
            type.startsWith("11-") -> "Audio"
            else -> "Unknown"
        }
    }
    
    private fun getLocalIpAddress(): String {
        try {
            val interfaces = java.net.NetworkInterface.getNetworkInterfaces()
            while (interfaces.hasMoreElements()) {
                val iface = interfaces.nextElement()
                if (iface.name.startsWith("p2p")) {
                    val addresses = iface.inetAddresses
                    while (addresses.hasMoreElements()) {
                        val addr = addresses.nextElement()
                        if (!addr.isLoopbackAddress && addr is java.net.Inet4Address) {
                            return addr.hostAddress ?: ""
                        }
                    }
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to get local IP: ${e.message}")
        }
        return ""
    }
    
    // Required for RN event emitter
    @ReactMethod
    fun addListener(eventName: String) {}
    
    @ReactMethod
    fun removeListeners(count: Int) {}
}
