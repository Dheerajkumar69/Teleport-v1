package com.teleport

import android.content.Context
import android.os.Build
import com.teleport.model.Device
import com.teleport.model.TransferProgress
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.io.File

/**
 * Kotlin wrapper for Teleport native engine
 */
class TeleportEngine(context: Context) {
    
    companion object {
        init {
            System.loadLibrary("teleport_jni")
        }
    }
    
    private var nativeHandle: Long = 0
    private val appContext = context.applicationContext
    
    // State flows for reactive UI
    private val _devices = MutableStateFlow<List<Device>>(emptyList())
    val devices: StateFlow<List<Device>> = _devices.asStateFlow()
    
    private val _isDiscovering = MutableStateFlow(false)
    val isDiscovering: StateFlow<Boolean> = _isDiscovering.asStateFlow()
    
    private val _transferProgress = MutableStateFlow<TransferProgress?>(null)
    val transferProgress: StateFlow<TransferProgress?> = _transferProgress.asStateFlow()
    
    private val _transferState = MutableStateFlow(TransferState.IDLE)
    val transferState: StateFlow<TransferState> = _transferState.asStateFlow()
    
    enum class TransferState {
        IDLE, SENDING, RECEIVING, COMPLETE, ERROR
    }
    
    // Callback for discovery
    private val discoveryCallback = object : DiscoveryCallback {
        override fun onDeviceFound(device: Device) {
            val current = _devices.value.toMutableList()
            val existing = current.indexOfFirst { it.id == device.id }
            if (existing >= 0) {
                current[existing] = device
            } else {
                current.add(device)
            }
            _devices.value = current
        }
        
        override fun onDeviceLost(deviceId: String) {
            _devices.value = _devices.value.filter { it.id != deviceId }
        }
    }
    
    // Callback for transfers
    private val transferCallback = object : TransferCallback {
        override fun onProgress(
            bytesTransferred: Long,
            bytesTotal: Long,
            speedBps: Double,
            filesCompleted: Int,
            filesTotal: Int
        ) {
            _transferProgress.value = TransferProgress(
                bytesTransferred, bytesTotal, speedBps, filesCompleted, filesTotal
            )
        }
        
        override fun onComplete(errorCode: Int) {
            _transferState.value = if (errorCode == 0) {
                TransferState.COMPLETE
            } else {
                TransferState.ERROR
            }
            _transferProgress.value = null
        }
        
        override fun onIncoming(senderName: String, totalSize: Long): Boolean {
            // Auto-accept for now - could show dialog in production
            return true
        }
    }
    
    fun create(): Boolean {
        val deviceName = "${Build.MANUFACTURER} ${Build.MODEL}"
        val downloadPath = File(appContext.getExternalFilesDir(null), "Teleport").apply {
            mkdirs()
        }.absolutePath
        
        nativeHandle = nativeCreate(deviceName, downloadPath)
        return nativeHandle != 0L
    }
    
    fun destroy() {
        if (nativeHandle != 0L) {
            nativeDestroy(nativeHandle)
            nativeHandle = 0
        }
    }
    
    fun startDiscovery(): Int {
        if (nativeHandle == 0L) return -1
        _isDiscovering.value = true
        _devices.value = emptyList()
        return nativeStartDiscovery(nativeHandle, discoveryCallback)
    }
    
    fun stopDiscovery(): Int {
        if (nativeHandle == 0L) return -1
        _isDiscovering.value = false
        return nativeStopDiscovery(nativeHandle)
    }
    
    fun sendFiles(target: Device, filePaths: List<String>): Int {
        if (nativeHandle == 0L) return -1
        _transferState.value = TransferState.SENDING
        return nativeSendFiles(
            nativeHandle,
            target.id,
            target.name,
            target.ip,
            target.port,
            filePaths.toTypedArray(),
            transferCallback
        )
    }
    
    fun startReceiving(): Int {
        if (nativeHandle == 0L) return -1
        _transferState.value = TransferState.RECEIVING
        val downloadPath = File(appContext.getExternalFilesDir(null), "Teleport").absolutePath
        return nativeStartReceiving(nativeHandle, downloadPath, transferCallback)
    }
    
    fun stopReceiving(): Int {
        if (nativeHandle == 0L) return -1
        _transferState.value = TransferState.IDLE
        return nativeStopReceiving(nativeHandle)
    }
    
    // Native methods
    private external fun nativeCreate(deviceName: String, downloadPath: String): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeStartDiscovery(handle: Long, callback: DiscoveryCallback): Int
    private external fun nativeStopDiscovery(handle: Long): Int
    private external fun nativeSendFiles(
        handle: Long,
        targetId: String,
        targetName: String,
        targetIp: String,
        targetPort: Int,
        filePaths: Array<String>,
        callback: TransferCallback
    ): Int
    private external fun nativeStartReceiving(
        handle: Long,
        outputDir: String,
        callback: TransferCallback
    ): Int
    private external fun nativeStopReceiving(handle: Long): Int
    
    // Callback interfaces
    interface DiscoveryCallback {
        fun onDeviceFound(device: Device)
        fun onDeviceLost(deviceId: String)
    }
    
    interface TransferCallback {
        fun onProgress(
            bytesTransferred: Long,
            bytesTotal: Long,
            speedBps: Double,
            filesCompleted: Int,
            filesTotal: Int
        )
        fun onComplete(errorCode: Int)
        fun onIncoming(senderName: String, totalSize: Long): Boolean
    }
}
