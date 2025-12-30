package com.teleport.model

/**
 * Transfer progress state
 */
data class TransferProgress(
    val bytesTransferred: Long,
    val bytesTotal: Long,
    val speedBps: Double,
    val filesCompleted: Int,
    val filesTotal: Int
) {
    val percentage: Float
        get() = if (bytesTotal > 0) (bytesTransferred.toFloat() / bytesTotal) * 100f else 0f
    
    val speedFormatted: String
        get() {
            return when {
                speedBps >= 1_000_000_000 -> String.format("%.1f GB/s", speedBps / 1_000_000_000)
                speedBps >= 1_000_000 -> String.format("%.1f MB/s", speedBps / 1_000_000)
                speedBps >= 1_000 -> String.format("%.1f KB/s", speedBps / 1_000)
                else -> String.format("%.0f B/s", speedBps)
            }
        }
    
    val eta: String
        get() {
            if (speedBps <= 0) return "--"
            val remaining = bytesTotal - bytesTransferred
            val seconds = (remaining / speedBps).toLong()
            return when {
                seconds < 60 -> "${seconds}s"
                seconds < 3600 -> "${seconds / 60}m ${seconds % 60}s"
                else -> "${seconds / 3600}h ${(seconds % 3600) / 60}m"
            }
        }
}
