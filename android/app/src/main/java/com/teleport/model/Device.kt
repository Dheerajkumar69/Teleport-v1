package com.teleport.model

/**
 * Represents a discovered device on the network
 */
data class Device(
    val id: String,
    val name: String,
    val os: String,
    val ip: String,
    val port: Int
) {
    val displayName: String
        get() = "$name ($os)"
    
    val address: String
        get() = "$ip:$port"
}
