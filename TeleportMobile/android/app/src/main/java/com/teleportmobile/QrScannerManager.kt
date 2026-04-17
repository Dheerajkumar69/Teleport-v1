package com.teleportmobile

import android.content.Context
import android.util.Log
import android.view.ViewGroup
import androidx.camera.core.*
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.core.content.ContextCompat
import androidx.lifecycle.LifecycleOwner
import com.facebook.react.bridge.*
import com.google.mlkit.vision.barcode.BarcodeScanning
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.common.InputImage
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

/**
 * QR Code Scanner using CameraX and ML Kit
 * Scans Teleport QR pairing codes and connects to the device
 */
@ExperimentalGetImage
class QrScannerManager(
    private val reactContext: ReactApplicationContext
) : ReactContextBaseJavaModule(reactContext) {

    private var cameraExecutor: ExecutorService? = null
    private var cameraProvider: ProcessCameraProvider? = null
    private var preview: Preview? = null
    private var imageAnalyzer: ImageAnalysis? = null
    
    private var onQrCodeScanned: ((String) -> Unit)? = null
    private var isScanning = false

    override fun getName(): String = "QrScannerManager"

    @ReactMethod
    fun startScanning(promise: Promise) {
        val activity = currentActivity
        if (activity == null) {
            promise.reject("NO_ACTIVITY", "No current activity")
            return
        }

        try {
            cameraExecutor = Executors.newSingleThreadExecutor()
            
            val cameraProviderFuture = ProcessCameraProvider.getInstance(activity)
            cameraProviderFuture.addListener({
                cameraProvider = cameraProviderFuture.get()
                bindCameraUseCases(activity as LifecycleOwner)
                isScanning = true
                promise.resolve(true)
            }, ContextCompat.getMainExecutor(activity))
            
        } catch (e: Exception) {
            Log.e(TAG, "Failed to start scanning", e)
            promise.reject("CAMERA_ERROR", e.message)
        }
    }

    @ReactMethod
    fun stopScanning(promise: Promise) {
        try {
            isScanning = false
            cameraProvider?.unbindAll()
            cameraExecutor?.shutdown()
            cameraExecutor = null
            promise.resolve(true)
        } catch (e: Exception) {
            promise.reject("STOP_ERROR", e.message)
        }
    }

    private fun bindCameraUseCases(lifecycleOwner: LifecycleOwner) {
        val cameraProvider = cameraProvider ?: return

        // Unbind previous use cases
        cameraProvider.unbindAll()

        // Preview
        preview = Preview.Builder()
            .setTargetRotation(android.view.Surface.ROTATION_0)
            .build()

        // Image analysis for QR detection
        imageAnalyzer = ImageAnalysis.Builder()
            .setTargetRotation(android.view.Surface.ROTATION_0)
            .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
            .build()
            .also { analysis ->
                analysis.setAnalyzer(cameraExecutor!!) { imageProxy ->
                    processImage(imageProxy)
                }
            }

        // Select back camera
        val cameraSelector = CameraSelector.DEFAULT_BACK_CAMERA

        try {
            cameraProvider.bindToLifecycle(
                lifecycleOwner,
                cameraSelector,
                preview,
                imageAnalyzer
            )
        } catch (e: Exception) {
            Log.e(TAG, "Camera binding failed", e)
        }
    }

    private fun processImage(imageProxy: ImageProxy) {
        if (!isScanning) {
            imageProxy.close()
            return
        }

        val mediaImage = imageProxy.image
        if (mediaImage != null) {
            val image = InputImage.fromMediaImage(
                mediaImage,
                imageProxy.imageInfo.rotationDegrees
            )

            val scanner = BarcodeScanning.getClient()
            
            scanner.process(image)
                .addOnSuccessListener { barcodes ->
                    for (barcode in barcodes) {
                        if (barcode.valueType == Barcode.TYPE_TEXT) {
                            val rawValue = barcode.rawValue
                            if (rawValue != null && rawValue.contains("teleport")) {
                                // Found a Teleport QR code
                                Log.i(TAG, "Scanned Teleport QR: ${rawValue.take(50)}...")
                                emitQrCode(rawValue)
                                
                                // Pause scanning briefly to avoid duplicates
                                isScanning = false
                                android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                                    isScanning = true
                                }, 2000)
                            }
                        }
                    }
                }
                .addOnFailureListener { e ->
                    Log.e(TAG, "Barcode scanning failed", e)
                }
                .addOnCompleteListener {
                    imageProxy.close()
                }
        } else {
            imageProxy.close()
        }
    }

    private fun emitQrCode(qrData: String) {
        reactContext
            .getJSModule(com.facebook.react.modules.core.DeviceEventManagerModule.RCTDeviceEventEmitter::class.java)
            .emit("onQrCodeScanned", qrData)
    }

    @ReactMethod
    fun connectViaQr(qrData: String, promise: Promise) {
        // QR pairing via this path is superseded by WebRTC signaling.
        // Kept as a no-op stub to avoid breaking the JS API.
        promise.reject("NOT_SUPPORTED", "QR pairing not available in this build")
    }

    // Native QR validation - implemented in teleport_rn.cpp
    private external fun nativeValidateQrPairing(qrData: String): String

    companion object {
        private const val TAG = "QrScannerManager"
        
        init {
            System.loadLibrary("teleport_rn")
        }
    }
}
