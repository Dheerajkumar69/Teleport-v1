/**
 * Teleport Mobile - React Native App
 * Cross-platform file transfer with beautiful UI
 */

import React, { useEffect, useState, useCallback } from 'react';
import {
  View,
  Text,
  StyleSheet,
  TouchableOpacity,
  FlatList,
  Switch,
  ActivityIndicator,
  Alert,
  StatusBar,
} from 'react-native';
import { NavigationContainer, DarkTheme } from '@react-navigation/native';
import { createBottomTabNavigator } from '@react-navigation/bottom-tabs';
import { SafeAreaProvider, SafeAreaView } from 'react-native-safe-area-context';
import teleport, { Device, TransferProgress } from './src/TeleportService';

const Tab = createBottomTabNavigator();

// Colors
const colors = {
  background: '#121218',
  surface: '#1E1E2E',
  surfaceLight: '#2A2A3E',
  primary: '#8B5CF6',
  primaryLight: '#A78BFA',
  success: '#10B981',
  error: '#EF4444',
  text: '#F9FAFB',
  textSecondary: '#9CA3AF',
  border: '#374151',
};

// ============= DISCOVER SCREEN =============
function DiscoverScreen() {
  const [devices, setDevices] = useState<Device[]>([]);
  const [isDiscovering, setIsDiscovering] = useState(false);

  useEffect(() => {
    startDiscovery();

    const unsubDevice = teleport.onDeviceFound((device) => {
      setDevices(prev => {
        const exists = prev.find(d => d.id === device.id);
        if (exists) {
          return prev.map(d => d.id === device.id ? device : d);
        }
        return [...prev, device];
      });
    });

    const unsubLost = teleport.onDeviceLost(({ id }) => {
      setDevices(prev => prev.filter(d => d.id !== id));
    });

    return () => {
      unsubDevice();
      unsubLost();
      teleport.stopDiscovery();
    };
  }, []);

  const startDiscovery = async () => {
    setIsDiscovering(true);
    try {
      await teleport.startDiscovery();
      // Fetch initial devices
      const initial = await teleport.getDevices();
      setDevices(initial);
    } catch (e) {
      console.error('Discovery error:', e);
    }
  };

  const refreshDevices = async () => {
    setDevices([]);
    await teleport.stopDiscovery();
    await startDiscovery();
  };

  const renderDevice = ({ item }: { item: Device }) => (
    <View style={styles.deviceCard}>
      <View style={styles.deviceIcon}>
        <Text style={styles.deviceIconText}>
          {item.os.includes('Android') ? '📱' : '💻'}
        </Text>
      </View>
      <View style={styles.deviceInfo}>
        <Text style={styles.deviceName}>{item.name}</Text>
        <Text style={styles.deviceMeta}>{item.os} • {item.ip}</Text>
      </View>
    </View>
  );

  return (
    <SafeAreaView style={styles.screen}>
      <View style={styles.header}>
        <Text style={styles.headerTitle}>Nearby Devices</Text>
        <TouchableOpacity onPress={refreshDevices} style={styles.refreshBtn}>
          <Text style={styles.refreshText}>↻</Text>
        </TouchableOpacity>
      </View>

      {isDiscovering && devices.length === 0 ? (
        <View style={styles.emptyState}>
          <ActivityIndicator size="large" color={colors.primary} />
          <Text style={styles.emptyText}>Searching for devices...</Text>
        </View>
      ) : (
        <FlatList
          data={devices}
          keyExtractor={item => item.id}
          renderItem={renderDevice}
          contentContainerStyle={styles.list}
          ListEmptyComponent={
            <View style={styles.emptyState}>
              <Text style={styles.emptyEmoji}>📡</Text>
              <Text style={styles.emptyText}>No devices found</Text>
              <Text style={styles.emptySubtext}>Make sure other devices are running Teleport</Text>
            </View>
          }
        />
      )}
    </SafeAreaView>
  );
}

// ============= SEND SCREEN =============
function SendScreen() {
  const [devices, setDevices] = useState<Device[]>([]);
  const [selectedDevice, setSelectedDevice] = useState<Device | null>(null);
  const [selectedFiles, setSelectedFiles] = useState<string[]>([]);
  const [progress, setProgress] = useState<TransferProgress | null>(null);
  const [isSending, setIsSending] = useState(false);

  useEffect(() => {
    loadDevices();

    const unsubProgress = teleport.onTransferProgress(setProgress);
    const unsubComplete = teleport.onTransferComplete(({ success }) => {
      setIsSending(false);
      setProgress(null);
      Alert.alert(
        success ? 'Success!' : 'Error',
        success ? 'Files sent successfully' : 'Transfer failed'
      );
    });

    return () => {
      unsubProgress();
      unsubComplete();
    };
  }, []);

  const loadDevices = async () => {
    const devs = await teleport.getDevices();
    setDevices(devs);
  };

  const pickFiles = async () => {
    try {
      const DocumentPicker = require('react-native-document-picker').default;
      const results = await DocumentPicker.pick({
        type: [DocumentPicker.types.allFiles],
        allowMultiSelection: true,
      });
      setSelectedFiles(results.map((r: any) => r.uri));
    } catch (e: any) {
      if (!e.message?.includes('canceled')) {
        console.error('File pick error:', e);
      }
    }
  };

  const sendFiles = async () => {
    if (!selectedDevice || selectedFiles.length === 0) return;

    setIsSending(true);
    try {
      await teleport.sendFiles(selectedDevice.id, selectedFiles);
    } catch (e) {
      setIsSending(false);
      Alert.alert('Error', 'Failed to send files');
    }
  };

  return (
    <SafeAreaView style={styles.screen}>
      <View style={styles.header}>
        <Text style={styles.headerTitle}>Send Files</Text>
      </View>

      <TouchableOpacity style={styles.filePickerBtn} onPress={pickFiles}>
        <Text style={styles.filePickerIcon}>📁</Text>
        <Text style={styles.filePickerText}>
          {selectedFiles.length > 0
            ? `${selectedFiles.length} file(s) selected`
            : 'Tap to select files'}
        </Text>
      </TouchableOpacity>

      <Text style={styles.sectionTitle}>Select Destination</Text>
      <FlatList
        data={devices}
        keyExtractor={item => item.id}
        renderItem={({ item }) => (
          <TouchableOpacity
            style={[
              styles.deviceChip,
              selectedDevice?.id === item.id && styles.deviceChipSelected
            ]}
            onPress={() => setSelectedDevice(item)}
          >
            <Text style={styles.deviceChipText}>{item.name}</Text>
          </TouchableOpacity>
        )}
        horizontal
        showsHorizontalScrollIndicator={false}
        contentContainerStyle={{ paddingHorizontal: 16 }}
      />

      {progress && (
        <View style={styles.progressContainer}>
          <View style={styles.progressBar}>
            <View style={[styles.progressFill, { width: `${(progress.bytesTransferred / progress.bytesTotal) * 100}%` }]} />
          </View>
          <Text style={styles.progressText}>
            {((progress.bytesTransferred / progress.bytesTotal) * 100).toFixed(0)}% • {formatBytes(progress.speedBps)}/s
          </Text>
        </View>
      )}

      <TouchableOpacity
        style={[styles.sendBtn, (!selectedDevice || selectedFiles.length === 0) && styles.sendBtnDisabled]}
        onPress={sendFiles}
        disabled={!selectedDevice || selectedFiles.length === 0 || isSending}
      >
        {isSending ? (
          <ActivityIndicator color="#fff" />
        ) : (
          <Text style={styles.sendBtnText}>Send Files</Text>
        )}
      </TouchableOpacity>
    </SafeAreaView>
  );
}

// ============= RECEIVE SCREEN =============
function ReceiveScreen() {
  const [isReceiving, setIsReceiving] = useState(false);
  const [progress, setProgress] = useState<TransferProgress | null>(null);

  useEffect(() => {
    const unsubProgress = teleport.onTransferProgress(setProgress);
    const unsubComplete = teleport.onTransferComplete(({ success }) => {
      setProgress(null);
      if (success) {
        Alert.alert('Success!', 'Files received successfully');
      }
    });

    return () => {
      unsubProgress();
      unsubComplete();
      teleport.stopReceiving();
    };
  }, []);

  const toggleReceiving = async (value: boolean) => {
    setIsReceiving(value);
    if (value) {
      await teleport.startReceiving();
    } else {
      await teleport.stopReceiving();
    }
  };

  return (
    <SafeAreaView style={styles.screen}>
      <View style={styles.header}>
        <Text style={styles.headerTitle}>Receive Files</Text>
      </View>

      <View style={styles.receiveCard}>
        <View style={styles.receiveInfo}>
          <Text style={styles.receiveTitle}>Enable Receiving</Text>
          <Text style={styles.receiveSubtitle}>
            {isReceiving ? 'Waiting for incoming files...' : 'Turn on to receive files'}
          </Text>
        </View>
        <Switch
          value={isReceiving}
          onValueChange={toggleReceiving}
          trackColor={{ false: colors.border, true: colors.primary }}
          thumbColor="#fff"
        />
      </View>

      {isReceiving && progress && (
        <View style={styles.transferCard}>
          <Text style={styles.transferTitle}>Receiving...</Text>
          <View style={styles.progressBar}>
            <View style={[styles.progressFill, { width: `${(progress.bytesTransferred / progress.bytesTotal) * 100}%` }]} />
          </View>
          <Text style={styles.progressText}>
            {progress.filesCompleted}/{progress.filesTotal} files • {formatBytes(progress.speedBps)}/s
          </Text>
        </View>
      )}

      {isReceiving && !progress && (
        <View style={styles.waitingCard}>
          <ActivityIndicator size="large" color={colors.primary} />
          <Text style={styles.waitingText}>Ready to receive</Text>
          <Text style={styles.waitingSubtext}>Send files from another device to this phone</Text>
        </View>
      )}
    </SafeAreaView>
  );
}

// ============= HELPER =============
function formatBytes(bytes: number): string {
  if (bytes < 1024) return bytes + ' B';
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
  if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
  return (bytes / (1024 * 1024 * 1024)).toFixed(1) + ' GB';
}

// ============= APP =============
export default function App() {
  return (
    <SafeAreaProvider>
      <StatusBar barStyle="light-content" backgroundColor={colors.background} />
      <NavigationContainer theme={DarkTheme}>
        <Tab.Navigator
          screenOptions={{
            headerShown: false,
            tabBarStyle: {
              backgroundColor: colors.surface,
              borderTopColor: colors.border,
              height: 60,
              paddingBottom: 8,
            },
            tabBarActiveTintColor: colors.primary,
            tabBarInactiveTintColor: colors.textSecondary,
          }}
        >
          <Tab.Screen
            name="Discover"
            component={DiscoverScreen}
            options={{ tabBarIcon: ({ color }) => <Text style={{ fontSize: 24 }}>📡</Text> }}
          />
          <Tab.Screen
            name="Send"
            component={SendScreen}
            options={{ tabBarIcon: ({ color }) => <Text style={{ fontSize: 24 }}>📤</Text> }}
          />
          <Tab.Screen
            name="Receive"
            component={ReceiveScreen}
            options={{ tabBarIcon: ({ color }) => <Text style={{ fontSize: 24 }}>📥</Text> }}
          />
        </Tab.Navigator>
      </NavigationContainer>
    </SafeAreaProvider>
  );
}

// ============= STYLES =============
const styles = StyleSheet.create({
  screen: {
    flex: 1,
    backgroundColor: colors.background,
  },
  header: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    padding: 16,
    paddingTop: 8,
  },
  headerTitle: {
    fontSize: 28,
    fontWeight: '700',
    color: colors.text,
  },
  refreshBtn: {
    padding: 8,
  },
  refreshText: {
    fontSize: 24,
    color: colors.primary,
  },
  list: {
    padding: 16,
  },
  deviceCard: {
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: colors.surface,
    borderRadius: 16,
    padding: 16,
    marginBottom: 12,
  },
  deviceIcon: {
    width: 48,
    height: 48,
    borderRadius: 24,
    backgroundColor: colors.surfaceLight,
    justifyContent: 'center',
    alignItems: 'center',
  },
  deviceIconText: {
    fontSize: 24,
  },
  deviceInfo: {
    marginLeft: 16,
    flex: 1,
  },
  deviceName: {
    fontSize: 18,
    fontWeight: '600',
    color: colors.text,
  },
  deviceMeta: {
    fontSize: 14,
    color: colors.textSecondary,
    marginTop: 4,
  },
  emptyState: {
    flex: 1,
    justifyContent: 'center',
    alignItems: 'center',
    paddingBottom: 100,
  },
  emptyEmoji: {
    fontSize: 64,
    marginBottom: 16,
  },
  emptyText: {
    fontSize: 18,
    color: colors.text,
    marginTop: 16,
  },
  emptySubtext: {
    fontSize: 14,
    color: colors.textSecondary,
    marginTop: 8,
  },
  filePickerBtn: {
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: colors.surface,
    borderRadius: 16,
    padding: 20,
    margin: 16,
    borderWidth: 2,
    borderColor: colors.border,
    borderStyle: 'dashed',
  },
  filePickerIcon: {
    fontSize: 32,
    marginRight: 16,
  },
  filePickerText: {
    fontSize: 16,
    color: colors.text,
  },
  sectionTitle: {
    fontSize: 16,
    fontWeight: '600',
    color: colors.textSecondary,
    marginLeft: 16,
    marginTop: 16,
    marginBottom: 12,
  },
  deviceChip: {
    backgroundColor: colors.surface,
    borderRadius: 20,
    paddingHorizontal: 16,
    paddingVertical: 10,
    marginRight: 8,
    borderWidth: 1,
    borderColor: colors.border,
  },
  deviceChipSelected: {
    backgroundColor: colors.primary,
    borderColor: colors.primary,
  },
  deviceChipText: {
    color: colors.text,
    fontSize: 14,
  },
  progressContainer: {
    margin: 16,
    marginTop: 24,
  },
  progressBar: {
    height: 8,
    backgroundColor: colors.surfaceLight,
    borderRadius: 4,
    overflow: 'hidden',
  },
  progressFill: {
    height: '100%',
    backgroundColor: colors.primary,
    borderRadius: 4,
  },
  progressText: {
    color: colors.textSecondary,
    fontSize: 14,
    marginTop: 8,
    textAlign: 'center',
  },
  sendBtn: {
    backgroundColor: colors.primary,
    borderRadius: 16,
    padding: 18,
    margin: 16,
    alignItems: 'center',
  },
  sendBtnDisabled: {
    opacity: 0.5,
  },
  sendBtnText: {
    color: '#fff',
    fontSize: 18,
    fontWeight: '600',
  },
  receiveCard: {
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: colors.surface,
    borderRadius: 16,
    padding: 20,
    margin: 16,
  },
  receiveInfo: {
    flex: 1,
  },
  receiveTitle: {
    fontSize: 18,
    fontWeight: '600',
    color: colors.text,
  },
  receiveSubtitle: {
    fontSize: 14,
    color: colors.textSecondary,
    marginTop: 4,
  },
  transferCard: {
    backgroundColor: colors.surface,
    borderRadius: 16,
    padding: 20,
    margin: 16,
  },
  transferTitle: {
    fontSize: 18,
    fontWeight: '600',
    color: colors.text,
    marginBottom: 12,
  },
  waitingCard: {
    alignItems: 'center',
    backgroundColor: colors.surface,
    borderRadius: 16,
    padding: 32,
    margin: 16,
  },
  waitingText: {
    fontSize: 18,
    fontWeight: '600',
    color: colors.text,
    marginTop: 16,
  },
  waitingSubtext: {
    fontSize: 14,
    color: colors.textSecondary,
    marginTop: 8,
    textAlign: 'center',
  },
});
