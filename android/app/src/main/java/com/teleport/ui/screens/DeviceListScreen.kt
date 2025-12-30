package com.teleport.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Computer
import androidx.compose.material.icons.filled.PhoneAndroid
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.teleport.TeleportEngine
import com.teleport.model.Device

@Composable
fun DeviceListScreen(engine: TeleportEngine) {
    val devices by engine.devices.collectAsState()
    val isDiscovering by engine.isDiscovering.collectAsState()
    
    LaunchedEffect(Unit) {
        engine.startDiscovery()
    }
    
    DisposableEffect(Unit) {
        onDispose {
            engine.stopDiscovery()
        }
    }
    
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = "Nearby Devices",
                style = MaterialTheme.typography.headlineMedium
            )
            
            IconButton(
                onClick = {
                    engine.stopDiscovery()
                    engine.startDiscovery()
                }
            ) {
                Icon(Icons.Default.Refresh, "Refresh")
            }
        }
        
        Spacer(modifier = Modifier.height(8.dp))
        
        if (isDiscovering) {
            LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
        }
        
        Spacer(modifier = Modifier.height(16.dp))
        
        if (devices.isEmpty()) {
            Box(
                modifier = Modifier.fillMaxSize(),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = "Searching for devices...\nMake sure other devices are running Teleport",
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        } else {
            LazyColumn(
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(devices, key = { it.id }) { device ->
                    DeviceCard(device = device)
                }
            }
        }
    }
}

@Composable
fun DeviceCard(device: Device) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant
        )
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = if (device.os.contains("Android", ignoreCase = true)) 
                    Icons.Default.PhoneAndroid else Icons.Default.Computer,
                contentDescription = device.os,
                modifier = Modifier.size(40.dp),
                tint = MaterialTheme.colorScheme.primary
            )
            
            Spacer(modifier = Modifier.width(16.dp))
            
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = device.name,
                    style = MaterialTheme.typography.titleMedium
                )
                Text(
                    text = "${device.os} • ${device.address}",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}
