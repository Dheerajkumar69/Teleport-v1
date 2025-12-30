package com.teleport.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.teleport.TeleportEngine

@Composable
fun ReceiveScreen(engine: TeleportEngine) {
    val transferState by engine.transferState.collectAsState()
    val progress by engine.transferProgress.collectAsState()
    
    val isReceiving = transferState == TeleportEngine.TransferState.RECEIVING
    
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(
            text = "Receive Files",
            style = MaterialTheme.typography.headlineMedium
        )
        
        Spacer(modifier = Modifier.height(48.dp))
        
        // Status icon
        Icon(
            imageVector = when (transferState) {
                TeleportEngine.TransferState.RECEIVING -> Icons.Default.CloudDownload
                TeleportEngine.TransferState.COMPLETE -> Icons.Default.CheckCircle
                TeleportEngine.TransferState.ERROR -> Icons.Default.Error
                else -> Icons.Default.Download
            },
            contentDescription = "Status",
            modifier = Modifier.size(120.dp),
            tint = when (transferState) {
                TeleportEngine.TransferState.RECEIVING -> MaterialTheme.colorScheme.primary
                TeleportEngine.TransferState.COMPLETE -> MaterialTheme.colorScheme.tertiary
                TeleportEngine.TransferState.ERROR -> MaterialTheme.colorScheme.error
                else -> MaterialTheme.colorScheme.onSurfaceVariant
            }
        )
        
        Spacer(modifier = Modifier.height(24.dp))
        
        Text(
            text = when (transferState) {
                TeleportEngine.TransferState.RECEIVING -> "Receiving files..."
                TeleportEngine.TransferState.COMPLETE -> "Transfer complete!"
                TeleportEngine.TransferState.ERROR -> "Transfer failed"
                else -> "Ready to receive"
            },
            style = MaterialTheme.typography.titleLarge
        )
        
        Spacer(modifier = Modifier.height(16.dp))
        
        // Progress
        if (isReceiving && progress != null) {
            Column(
                modifier = Modifier.fillMaxWidth(),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                LinearProgressIndicator(
                    progress = { progress!!.percentage / 100f },
                    modifier = Modifier.fillMaxWidth()
                )
                
                Spacer(modifier = Modifier.height(16.dp))
                
                Text(
                    text = "${progress!!.filesCompleted}/${progress!!.filesTotal} files",
                    style = MaterialTheme.typography.bodyLarge
                )
                
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceEvenly
                ) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Text(progress!!.speedFormatted, style = MaterialTheme.typography.titleMedium)
                        Text("Speed", style = MaterialTheme.typography.bodySmall)
                    }
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Text(progress!!.eta, style = MaterialTheme.typography.titleMedium)
                        Text("ETA", style = MaterialTheme.typography.bodySmall)
                    }
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Text(
                            String.format("%.1f%%", progress!!.percentage),
                            style = MaterialTheme.typography.titleMedium
                        )
                        Text("Progress", style = MaterialTheme.typography.bodySmall)
                    }
                }
            }
        }
        
        Spacer(modifier = Modifier.weight(1f))
        
        // Toggle button
        Button(
            onClick = {
                if (isReceiving) {
                    engine.stopReceiving()
                } else {
                    engine.startReceiving()
                }
            },
            modifier = Modifier.fillMaxWidth(),
            colors = if (isReceiving) {
                ButtonDefaults.buttonColors(
                    containerColor = MaterialTheme.colorScheme.error
                )
            } else {
                ButtonDefaults.buttonColors()
            }
        ) {
            Icon(
                if (isReceiving) Icons.Default.Stop else Icons.Default.PlayArrow,
                "Toggle receive"
            )
            Spacer(modifier = Modifier.width(8.dp))
            Text(if (isReceiving) "Stop Receiving" else "Start Receiving")
        }
        
        Spacer(modifier = Modifier.height(8.dp))
        
        Text(
            text = "Files will be saved to Teleport folder",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}
