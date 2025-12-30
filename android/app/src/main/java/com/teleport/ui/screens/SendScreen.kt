package com.teleport.ui.screens

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.teleport.TeleportEngine
import com.teleport.model.Device

@Composable
fun SendScreen(engine: TeleportEngine) {
    val context = LocalContext.current
    val devices by engine.devices.collectAsState()
    val transferState by engine.transferState.collectAsState()
    val progress by engine.transferProgress.collectAsState()
    
    var selectedFiles by remember { mutableStateOf<List<Uri>>(emptyList()) }
    var selectedDevice by remember { mutableStateOf<Device?>(null) }
    
    val filePicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenMultipleDocuments()
    ) { uris ->
        selectedFiles = uris
    }
    
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp)
    ) {
        Text(
            text = "Send Files",
            style = MaterialTheme.typography.headlineMedium
        )
        
        Spacer(modifier = Modifier.height(24.dp))
        
        // File selection
        OutlinedCard(
            modifier = Modifier.fillMaxWidth(),
            onClick = { filePicker.launch(arrayOf("*/*")) }
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Icon(Icons.Default.AttachFile, "Select files")
                Spacer(modifier = Modifier.width(16.dp))
                Text(
                    text = if (selectedFiles.isEmpty()) 
                        "Tap to select files" 
                    else 
                        "${selectedFiles.size} file(s) selected"
                )
            }
        }
        
        Spacer(modifier = Modifier.height(16.dp))
        
        // Device selection
        Text(
            text = "Select destination",
            style = MaterialTheme.typography.titleMedium
        )
        
        Spacer(modifier = Modifier.height(8.dp))
        
        if (devices.isEmpty()) {
            Text(
                text = "No devices found. Go to Discover tab.",
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        } else {
            LazyColumn(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(devices, key = { it.id }) { device ->
                    FilterChip(
                        selected = selectedDevice?.id == device.id,
                        onClick = { selectedDevice = device },
                        label = { Text(device.displayName) },
                        leadingIcon = {
                            if (selectedDevice?.id == device.id) {
                                Icon(Icons.Default.Check, "Selected")
                            }
                        }
                    )
                }
            }
        }
        
        Spacer(modifier = Modifier.height(16.dp))
        
        // Progress
        if (transferState == TeleportEngine.TransferState.SENDING && progress != null) {
            Column {
                LinearProgressIndicator(
                    progress = { progress!!.percentage / 100f },
                    modifier = Modifier.fillMaxWidth()
                )
                Spacer(modifier = Modifier.height(8.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text("${progress!!.speedFormatted}")
                    Text("ETA: ${progress!!.eta}")
                }
            }
        }
        
        Spacer(modifier = Modifier.height(16.dp))
        
        // Send button
        Button(
            onClick = {
                selectedDevice?.let { device ->
                    val paths = selectedFiles.mapNotNull { uri ->
                        // Convert content URI to file path
                        context.contentResolver.openInputStream(uri)?.use {
                            val tempFile = java.io.File(context.cacheDir, uri.lastPathSegment ?: "file")
                            tempFile.outputStream().use { out -> it.copyTo(out) }
                            tempFile.absolutePath
                        }
                    }
                    if (paths.isNotEmpty()) {
                        engine.sendFiles(device, paths)
                    }
                }
            },
            modifier = Modifier.fillMaxWidth(),
            enabled = selectedFiles.isNotEmpty() && 
                      selectedDevice != null && 
                      transferState != TeleportEngine.TransferState.SENDING
        ) {
            Icon(Icons.Default.Send, "Send")
            Spacer(modifier = Modifier.width(8.dp))
            Text("Send Files")
        }
    }
}
