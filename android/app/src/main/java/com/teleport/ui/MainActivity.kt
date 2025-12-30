package com.teleport.ui

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Upload
import androidx.compose.material.icons.filled.WifiTethering
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.teleport.HotspotManager
import com.teleport.TeleportEngine
import com.teleport.ui.screens.DeviceListScreen
import com.teleport.ui.screens.HotspotScreen
import com.teleport.ui.screens.ReceiveScreen
import com.teleport.ui.screens.SendScreen
import com.teleport.ui.theme.TeleportTheme

class MainActivity : ComponentActivity() {
    
    private lateinit var engine: TeleportEngine
    private lateinit var hotspotManager: HotspotManager
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        engine = TeleportEngine(this)
        engine.create()
        
        hotspotManager = HotspotManager(this)
        
        setContent {
            TeleportTheme {
                TeleportApp(engine, hotspotManager)
            }
        }
    }
    
    override fun onDestroy() {
        super.onDestroy()
        hotspotManager.stop()
        engine.destroy()
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TeleportApp(engine: TeleportEngine, hotspotManager: HotspotManager) {
    val navController = rememberNavController()
    var selectedTab by remember { mutableIntStateOf(0) }
    
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Teleport") },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.primaryContainer
                )
            )
        },
        bottomBar = {
            NavigationBar {
                NavigationBarItem(
                    icon = { Icon(Icons.Default.Search, "Discover") },
                    label = { Text("Discover") },
                    selected = selectedTab == 0,
                    onClick = {
                        selectedTab = 0
                        navController.navigate("devices") {
                            popUpTo("devices") { inclusive = true }
                        }
                    }
                )
                NavigationBarItem(
                    icon = { Icon(Icons.Default.Upload, "Send") },
                    label = { Text("Send") },
                    selected = selectedTab == 1,
                    onClick = {
                        selectedTab = 1
                        navController.navigate("send") { popUpTo("devices") }
                    }
                )
                NavigationBarItem(
                    icon = { Icon(Icons.Default.Download, "Receive") },
                    label = { Text("Receive") },
                    selected = selectedTab == 2,
                    onClick = {
                        selectedTab = 2
                        navController.navigate("receive") { popUpTo("devices") }
                    }
                )
                NavigationBarItem(
                    icon = { Icon(Icons.Default.WifiTethering, "Hotspot") },
                    label = { Text("Hotspot") },
                    selected = selectedTab == 3,
                    onClick = {
                        selectedTab = 3
                        navController.navigate("hotspot") { popUpTo("devices") }
                    }
                )
            }
        }
    ) { padding ->
        NavHost(
            navController = navController,
            startDestination = "devices",
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
        ) {
            composable("devices") { DeviceListScreen(engine = engine) }
            composable("send") { SendScreen(engine = engine) }
            composable("receive") { ReceiveScreen(engine = engine) }
            composable("hotspot") { HotspotScreen(hotspotManager = hotspotManager) }
        }
    }
}

