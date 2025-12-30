package com.teleport.ui.screens

import android.Manifest
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import com.google.accompanist.permissions.ExperimentalPermissionsApi
import com.google.accompanist.permissions.isGranted
import com.google.accompanist.permissions.rememberPermissionState
import com.teleport.HotspotManager

@OptIn(ExperimentalPermissionsApi::class)
@Composable
fun HotspotScreen(hotspotManager: HotspotManager) {
    val context = LocalContext.current
    val state by hotspotManager.state.collectAsState()
    val info by hotspotManager.info.collectAsState()
    
    // Location permission required for hotspot on Android
    val locationPermission = rememberPermissionState(Manifest.permission.ACCESS_FINE_LOCATION)
    
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(
            text = "Hotspot Mode",
            style = MaterialTheme.typography.headlineMedium
        )
        
        Spacer(modifier = Modifier.height(16.dp))
        
        Text(
            text = "Create a direct connection without Wi-Fi",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        
        Spacer(modifier = Modifier.height(32.dp))
        
        // Status icon
        Icon(
            imageVector = when (state) {
                HotspotManager.HotspotState.ACTIVE -> Icons.Default.WifiTethering
                HotspotManager.HotspotState.STARTING -> Icons.Default.Sync
                HotspotManager.HotspotState.FAILED -> Icons.Default.WifiTetheringError
                else -> Icons.Default.WifiTetheringOff
            },
            contentDescription = "Hotspot status",
            modifier = Modifier.size(100.dp),
            tint = when (state) {
                HotspotManager.HotspotState.ACTIVE -> MaterialTheme.colorScheme.primary
                HotspotManager.HotspotState.FAILED -> MaterialTheme.colorScheme.error
                else -> MaterialTheme.colorScheme.onSurfaceVariant
            }
        )
        
        Spacer(modifier = Modifier.height(24.dp))
        
        // Connection details when active
        if (state == HotspotManager.HotspotState.ACTIVE && info != null) {
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.primaryContainer
                )
            ) {
                Column(
                    modifier = Modifier.padding(16.dp)
                ) {
                    Text(
                        text = "Connect to this network:",
                        style = MaterialTheme.typography.titleMedium
                    )
                    
                    Spacer(modifier = Modifier.height(16.dp))
                    
                    // SSID
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text("Network Name", style = MaterialTheme.typography.labelSmall)
                            Text(
                                text = info!!.ssid,
                                style = MaterialTheme.typography.titleLarge,
                                fontFamily = FontFamily.Monospace
                            )
                        }
                        IconButton(onClick = {
                            copyToClipboard(context, info!!.ssid)
                        }) {
                            Icon(Icons.Default.ContentCopy, "Copy")
                        }
                    }
                    
                    Spacer(modifier = Modifier.height(12.dp))
                    
                    // Password
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text("Password", style = MaterialTheme.typography.labelSmall)
                            Text(
                                text = info!!.password.ifEmpty { "(no password)" },
                                style = MaterialTheme.typography.titleLarge,
                                fontFamily = FontFamily.Monospace
                            )
                        }
                        if (info!!.password.isNotEmpty()) {
                            IconButton(onClick = {
                                copyToClipboard(context, info!!.password)
                            }) {
                                Icon(Icons.Default.ContentCopy, "Copy")
                            }
                        }
                    }
                    
                    Spacer(modifier = Modifier.height(12.dp))
                    
                    // Gateway IP
                    Text("Gateway IP", style = MaterialTheme.typography.labelSmall)
                    Text(
                        text = info!!.gatewayIp,
                        style = MaterialTheme.typography.bodyLarge,
                        fontFamily = FontFamily.Monospace
                    )
                }
            }
            
            Spacer(modifier = Modifier.height(16.dp))
            
            Text(
                text = "Other devices can now connect to this network\nand transfer files directly",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        
        Spacer(modifier = Modifier.weight(1f))
        
        // Toggle button
        if (!locationPermission.status.isGranted) {
            Button(
                onClick = { locationPermission.launchPermissionRequest() },
                modifier = Modifier.fillMaxWidth()
            ) {
                Icon(Icons.Default.LocationOn, "Permission")
                Spacer(modifier = Modifier.width(8.dp))
                Text("Grant Location Permission")
            }
            
            Text(
                text = "Location permission is required for hotspot",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        } else {
            Button(
                onClick = {
                    if (state == HotspotManager.HotspotState.ACTIVE) {
                        hotspotManager.stop()
                    } else {
                        hotspotManager.start()
                    }
                },
                modifier = Modifier.fillMaxWidth(),
                colors = if (state == HotspotManager.HotspotState.ACTIVE) {
                    ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.error
                    )
                } else {
                    ButtonDefaults.buttonColors()
                },
                enabled = state != HotspotManager.HotspotState.STARTING
            ) {
                Icon(
                    if (state == HotspotManager.HotspotState.ACTIVE) 
                        Icons.Default.Stop else Icons.Default.WifiTethering,
                    "Toggle"
                )
                Spacer(modifier = Modifier.width(8.dp))
                Text(
                    when (state) {
                        HotspotManager.HotspotState.ACTIVE -> "Stop Hotspot"
                        HotspotManager.HotspotState.STARTING -> "Starting..."
                        else -> "Start Hotspot"
                    }
                )
            }
        }
    }
}

private fun copyToClipboard(context: Context, text: String) {
    val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
    clipboard.setPrimaryClip(ClipData.newPlainText("Teleport", text))
}
