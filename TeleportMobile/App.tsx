/**
 * TeleportMobile - Production-Ready P2P File Transfer  
 * Three-tab design: Send | Receive | History
 * With full settings persistence and transfer history
 */
import React, { useEffect, useState, useCallback, useRef, useMemo } from 'react';
import {
  SafeAreaView,
  StyleSheet,
  Text,
  View,
  TouchableOpacity,
  FlatList,
  Alert,
  PermissionsAndroid,
  Platform,
  Dimensions,
  Vibration,
  Animated,
  Easing,
  ScrollView,
  Modal,
  TextInput,
  Switch,
  AppState,
  AppStateStatus,
  RefreshControl,
} from 'react-native';
import DocumentPicker from 'react-native-document-picker';
import DotText from './src/components/DotText';
import teleportService, { TeleportDevice, FileReceivedInfo } from './src/TeleportService';
import {
  AppSettings,
  TransferRecord,
  loadSettings,
  saveSettings,
  loadHistory,
  addToHistory,
  clearHistory,
  generateTransferId,
  validateDownloadPath,
} from './src/SettingsStorage';
import ErrorBoundary, { reportCrash } from './src/ErrorBoundary';
import {
  withTimeout,
  withRetry,
  throttle,
  isOnline,
  onNetworkChange,
} from './src/NetworkUtils';

const { width } = Dimensions.get('window');

// Three-tab navigation with history
type Tab = 'send' | 'receive' | 'history';

// Haptic feedback helper with throttle to prevent spam
const rawHaptic = (type: 'light' | 'medium' | 'heavy' = 'light') => {
  try {
    if (Platform.OS === 'android') {
      Vibration.vibrate(type === 'light' ? 10 : type === 'medium' ? 20 : 30);
    }
  } catch (e) { }
};

// Throttled haptic - max once per 100ms to prevent vibration spam
const haptic = throttle(rawHaptic, 100);

// Format file size
const formatSize = (bytes: number): string => {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
  return `${(bytes / (1024 * 1024 * 1024)).toFixed(2)} GB`;
};

// Format timestamp
const formatTime = (timestamp: number): string => {
  const date = new Date(timestamp);
  const now = new Date();
  const diffMs = now.getTime() - date.getTime();
  const diffMins = Math.floor(diffMs / 60000);

  if (diffMins < 1) return 'just now';
  if (diffMins < 60) return `${diffMins}m ago`;
  if (diffMins < 1440) return `${Math.floor(diffMins / 60)}h ago`;
  return date.toLocaleDateString();
};

// Format transfer speed (bytes/second)
const formatSpeed = (bytesPerSecond: number): string => {
  if (bytesPerSecond < 1024) return `${bytesPerSecond.toFixed(0)} B/s`;
  if (bytesPerSecond < 1024 * 1024) return `${(bytesPerSecond / 1024).toFixed(1)} KB/s`;
  return `${(bytesPerSecond / (1024 * 1024)).toFixed(1)} MB/s`;
};

// Skeleton loader for loading states
const SkeletonLoader = ({ width = '100%', height = 20 }: { width?: string | number; height?: number }) => {
  const opacity = useRef(new Animated.Value(0.3)).current;

  useEffect(() => {
    Animated.loop(
      Animated.sequence([
        Animated.timing(opacity, { toValue: 0.7, duration: 800, useNativeDriver: true }),
        Animated.timing(opacity, { toValue: 0.3, duration: 800, useNativeDriver: true }),
      ])
    ).start();
  }, []);

  return (
    <Animated.View
      style={{
        width: typeof width === 'string' ? width as `${number}%` : width,
        height,
        backgroundColor: '#333',
        borderRadius: 4,
        opacity,
      }}
    />
  );
};

// Animated dot for loading states
const PulseDot = ({ delay = 0 }: { delay?: number }) => {
  const opacity = useRef(new Animated.Value(0.3)).current;

  useEffect(() => {
    const timeout = setTimeout(() => {
      Animated.loop(
        Animated.sequence([
          Animated.timing(opacity, { toValue: 1, duration: 600, useNativeDriver: true }),
          Animated.timing(opacity, { toValue: 0.3, duration: 600, useNativeDriver: true }),
        ])
      ).start();
    }, delay);
    return () => clearTimeout(timeout);
  }, []);

  return <Animated.View style={[styles.pulseDot, { opacity }]} />;
};

// Animated button component
const AnimatedButton = ({
  onPress,
  active,
  children,
  disabled,
}: {
  onPress: () => void;
  active?: boolean;
  children: React.ReactNode;
  disabled?: boolean;
}) => {
  const scale = useRef(new Animated.Value(1)).current;

  const handlePressIn = () => {
    Animated.spring(scale, { toValue: 0.96, useNativeDriver: true }).start();
    haptic('light');
  };

  const handlePressOut = () => {
    Animated.spring(scale, { toValue: 1, useNativeDriver: true }).start();
  };

  return (
    <Animated.View style={{ transform: [{ scale }] }}>
      <TouchableOpacity
        style={[
          styles.mainButton,
          active && styles.buttonActive,
          disabled && styles.buttonDisabled,
        ]}
        onPress={onPress}
        onPressIn={handlePressIn}
        onPressOut={handlePressOut}
        disabled={disabled}
        activeOpacity={0.8}>
        {children}
      </TouchableOpacity>
    </Animated.View>
  );
};

// Progress bar component with speed, size, and cancel
const ProgressBar = ({
  progress,
  fileName,
  speed = 0,
  totalSize = 0,
  onCancel,
}: {
  progress: number;
  fileName: string;
  speed?: number;
  totalSize?: number;
  onCancel?: () => void;
}) => {
  const widthAnim = useRef(new Animated.Value(0)).current;

  useEffect(() => {
    Animated.timing(widthAnim, {
      toValue: progress,
      duration: 200,
      useNativeDriver: false,
    }).start();
  }, [progress]);

  const progressWidth = widthAnim.interpolate({
    inputRange: [0, 100],
    outputRange: ['0%', '100%'],
  });

  return (
    <View style={styles.progressBarContainer}>
      <View style={styles.progressHeader}>
        <Text style={styles.progressFileName} numberOfLines={1}>{fileName}</Text>
        {onCancel && (
          <TouchableOpacity onPress={onCancel} style={styles.cancelButton}>
            <Text style={styles.cancelButtonText}>✕</Text>
          </TouchableOpacity>
        )}
      </View>
      <View style={styles.progressBarTrack}>
        <Animated.View style={[styles.progressBarFill, { width: progressWidth }]} />
      </View>
      <View style={styles.progressStats}>
        <Text style={styles.progressPercent}>{Math.round(progress)}%</Text>
        {totalSize > 0 && (
          <Text style={styles.progressSize}>{formatSize(totalSize)}</Text>
        )}
        {speed > 0 && (
          <Text style={styles.progressSpeed}>{formatSpeed(speed)}</Text>
        )}
      </View>
    </View>
  );
};

// Success celebration overlay - delightful completion state
const SuccessOverlay = ({ visible, onDone }: { visible: boolean; onDone: () => void }) => {
  const scale = useRef(new Animated.Value(0)).current;
  const opacity = useRef(new Animated.Value(0)).current;
  const ringScale = useRef(new Animated.Value(0)).current;

  useEffect(() => {
    if (visible) {
      // Celebration haptic pattern
      Vibration.vibrate([0, 30, 50, 30, 50, 50]);

      // Bouncy checkmark animation
      Animated.parallel([
        Animated.spring(scale, {
          toValue: 1,
          tension: 50,
          friction: 3,
          useNativeDriver: true,
        }),
        Animated.timing(opacity, {
          toValue: 1,
          duration: 200,
          useNativeDriver: true,
        }),
        // Expanding ring effect
        Animated.sequence([
          Animated.delay(100),
          Animated.timing(ringScale, {
            toValue: 3,
            duration: 600,
            easing: Easing.out(Easing.cubic),
            useNativeDriver: true,
          }),
        ]),
      ]).start();

      // Auto-dismiss after celebration
      const timer = setTimeout(() => {
        Animated.parallel([
          Animated.timing(opacity, { toValue: 0, duration: 300, useNativeDriver: true }),
          Animated.timing(scale, { toValue: 0.8, duration: 300, useNativeDriver: true }),
        ]).start(() => onDone());
      }, 1800);

      return () => clearTimeout(timer);
    } else {
      scale.setValue(0);
      opacity.setValue(0);
      ringScale.setValue(0);
    }
  }, [visible]);

  if (!visible) return null;

  return (
    <Animated.View style={[styles.successOverlay, { opacity }]}>
      {/* Expanding ring */}
      <Animated.View style={[
        styles.successRing,
        {
          transform: [{ scale: ringScale }], opacity: ringScale.interpolate({
            inputRange: [0, 2, 3],
            outputRange: [0.8, 0.3, 0],
          })
        }
      ]} />

      {/* Checkmark circle */}
      <Animated.View style={[styles.successCircle, { transform: [{ scale }] }]}>
        <Text style={styles.successCheck}>✓</Text>
      </Animated.View>

      <Animated.Text style={[styles.successText, { transform: [{ scale }] }]}>
        sent!
      </Animated.Text>
    </Animated.View>
  );
};

// Incoming Files Modal - shows when someone wants to send files
const IncomingFilesModal = ({
  visible,
  senderName,
  fileCount,
  totalSize,
  onAccept,
  onReject
}: {
  visible: boolean;
  senderName: string;
  fileCount: number;
  totalSize: number;
  onAccept: () => void;
  onReject: () => void;
}) => {
  const scale = useRef(new Animated.Value(0.8)).current;

  useEffect(() => {
    if (visible) {
      Vibration.vibrate([0, 100, 50, 100]);
      Animated.spring(scale, {
        toValue: 1,
        tension: 100,
        friction: 8,
        useNativeDriver: true,
      }).start();
    } else {
      scale.setValue(0.8);
    }
  }, [visible]);

  if (!visible) return null;

  const formatSize = (bytes: number) => {
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
    return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
  };

  return (
    <Modal visible={visible} animationType="fade" transparent>
      <View style={styles.incomingOverlay}>
        <Animated.View style={[styles.incomingCard, { transform: [{ scale }] }]}>
          <Text style={styles.incomingTitle}>incoming files</Text>

          <View style={styles.incomingSender}>
            <View style={styles.incomingDot} />
            <Text style={styles.incomingSenderName}>{senderName.toLowerCase()}</Text>
          </View>

          <Text style={styles.incomingInfo}>
            {fileCount} file{fileCount > 1 ? 's' : ''} • {formatSize(totalSize)}
          </Text>

          <View style={styles.incomingButtons}>
            <TouchableOpacity style={styles.rejectButton} onPress={onReject}>
              <Text style={styles.rejectText}>reject</Text>
            </TouchableOpacity>
            <TouchableOpacity style={styles.acceptButton} onPress={onAccept}>
              <Text style={styles.acceptText}>accept</Text>
            </TouchableOpacity>
          </View>
        </Animated.View>
      </View>
    </Modal>
  );
};

// Settings Modal - Fully Functional with Persistence
const SettingsModal = ({
  visible,
  onClose,
  settings,
  onSave,
}: {
  visible: boolean;
  onClose: () => void;
  settings: AppSettings;
  onSave: (settings: AppSettings) => void;
}) => {
  const [localSettings, setLocalSettings] = useState<AppSettings>(settings);
  const [isEditing, setIsEditing] = useState(false);

  useEffect(() => {
    if (visible) {
      setLocalSettings(settings);
      setIsEditing(false);
    }
  }, [visible, settings]);

  const handleSave = () => {
    onSave(localSettings);
    setIsEditing(false);
    onClose();
  };

  const updateField = <K extends keyof AppSettings>(key: K, value: AppSettings[K]) => {
    setLocalSettings(prev => ({ ...prev, [key]: value }));
    setIsEditing(true);
  };

  return (
    <Modal visible={visible} animationType="slide" transparent>
      <View style={styles.modalOverlay}>
        <View style={styles.modalContent}>
          <Text style={styles.modalTitle}>settings</Text>

          {/* Device Name - Editable */}
          <View style={styles.settingItem}>
            <Text style={styles.settingLabel}>device name</Text>
            <TextInput
              style={styles.settingInput}
              value={localSettings.deviceName}
              onChangeText={(text) => updateField('deviceName', text)}
              placeholder="Enter device name"
              placeholderTextColor="#444"
              maxLength={20}
              autoCapitalize="none"
              autoCorrect={false}
            />
          </View>

          {/* Download Path */}
          <View style={styles.settingItem}>
            <Text style={styles.settingLabel}>save location</Text>
            <Text style={styles.settingValue} numberOfLines={1}>
              {localSettings.downloadPath.split('/').pop() || 'Downloads'}
            </Text>
          </View>

          {/* Auto Accept Toggle */}
          <View style={styles.settingItemRow}>
            <Text style={styles.settingLabel}>auto accept files</Text>
            <Switch
              value={localSettings.autoAccept}
              onValueChange={(value) => updateField('autoAccept', value)}
              trackColor={{ false: '#333', true: '#4ade80' }}
              thumbColor={localSettings.autoAccept ? '#fff' : '#888'}
            />
          </View>

          {/* Vibration Toggle */}
          <View style={styles.settingItemRow}>
            <Text style={styles.settingLabel}>haptic feedback</Text>
            <Switch
              value={localSettings.vibrationEnabled}
              onValueChange={(value) => updateField('vibrationEnabled', value)}
              trackColor={{ false: '#333', true: '#4ade80' }}
              thumbColor={localSettings.vibrationEnabled ? '#fff' : '#888'}
            />
          </View>

          {/* Version Info */}
          <View style={styles.settingItem}>
            <Text style={styles.settingLabel}>version</Text>
            <Text style={styles.settingValue}>1.0.0</Text>
          </View>

          {/* Save/Close Buttons */}
          <View style={styles.settingsButtonRow}>
            {isEditing && (
              <TouchableOpacity
                style={[styles.modalCloseButton, styles.saveButton]}
                onPress={handleSave}
              >
                <Text style={styles.saveButtonText}>save</Text>
              </TouchableOpacity>
            )}
            <TouchableOpacity
              style={[styles.modalCloseButton, isEditing && styles.cancelButton]}
              onPress={onClose}
            >
              <Text style={styles.modalCloseText}>{isEditing ? 'cancel' : 'close'}</Text>
            </TouchableOpacity>
          </View>
        </View>
      </View>
    </Modal>
  );
};

const App = () => {
  const [activeTab, setActiveTab] = useState<Tab>('send');
  const [devices, setDevices] = useState<TeleportDevice[]>([]);
  const [isDiscovering, setIsDiscovering] = useState(false);
  const [isReceiving, setIsReceiving] = useState(false);
  const [isSending, setIsSending] = useState(false);
  const [selectedDevice, setSelectedDevice] = useState<TeleportDevice | null>(null);
  const [initialized, setInitialized] = useState(false);
  const [statusMessage, setStatusMessage] = useState('');
  const [progress, setProgress] = useState(0);
  const [currentFileName, setCurrentFileName] = useState('');
  const [showSettings, setShowSettings] = useState(false);
  const [showSuccess, setShowSuccess] = useState(false);

  // Settings state with persistence
  const [settings, setSettings] = useState<AppSettings>({
    deviceName: 'TeleportMobile',
    downloadPath: '/storage/emulated/0/Download',
    autoAccept: false,
    vibrationEnabled: true,
  });

  // Transfer history state
  const [transferHistory, setTransferHistory] = useState<TransferRecord[]>([]);

  // Transfer speed and size tracking
  const [transferSpeed, setTransferSpeed] = useState(0);
  const [currentFileSize, setCurrentFileSize] = useState(0);
  const lastBytesRef = useRef(0);
  const lastTimeRef = useRef(Date.now());

  // Pull-to-refresh state
  const [isRefreshing, setIsRefreshing] = useState(false);

  // Incoming files state  
  const [showIncoming, setShowIncoming] = useState(false);
  const [incomingSender, setIncomingSender] = useState('');
  const [incomingFileCount, setIncomingFileCount] = useState(0);
  const [incomingTotalSize, setIncomingTotalSize] = useState(0);

  // Tab indicator animation - 3 tabs now
  const tabIndicatorX = useRef(new Animated.Value(0)).current;
  const tabWidth = (width - 48) / 3;  // 3 tabs

  // Refs for accessing current state in async callbacks
  const currentFileNameRef = useRef(currentFileName);
  const selectedDeviceRef = useRef(selectedDevice);
  const settingsRef = useRef(settings);

  // Keep refs in sync with state
  useEffect(() => { currentFileNameRef.current = currentFileName; }, [currentFileName]);
  useEffect(() => { selectedDeviceRef.current = selectedDevice; }, [selectedDevice]);
  useEffect(() => { settingsRef.current = settings; }, [settings]);

  useEffect(() => {
    const index = ['send', 'receive', 'history'].indexOf(activeTab);
    Animated.spring(tabIndicatorX, {
      toValue: index * tabWidth,
      useNativeDriver: true,
      damping: 20,
      stiffness: 200,
    }).start();
  }, [activeTab]);

  // Load settings and history on mount - MUST complete before init
  const [settingsLoaded, setSettingsLoaded] = useState(false);
  const [permissionsGranted, setPermissionsGranted] = useState(false);

  useEffect(() => {
    const loadData = async () => {
      const [savedSettings, savedHistory] = await Promise.all([
        loadSettings(),
        loadHistory(),
      ]);
      setSettings(savedSettings);
      setTransferHistory(savedHistory);
      setSettingsLoaded(true);
    };
    loadData();
  }, []);

  // Haptic helper using ref to avoid dependency issues (memory leak fix)
  const doHaptic = useCallback((type: 'light' | 'medium' | 'heavy' = 'light') => {
    if (settingsRef.current.vibrationEnabled) {
      haptic(type);
    }
  }, []); // No dependencies - uses ref

  // Save settings handler
  const handleSaveSettings = useCallback(async (newSettings: AppSettings) => {
    setSettings(newSettings);
    await saveSettings(newSettings);
  }, []);

  // Initialize Teleport AFTER settings loaded, with proper permission handling
  useEffect(() => {
    if (!settingsLoaded) return; // Wait for settings to load first

    let mounted = true;

    const init = async () => {
      // Request permissions and track result
      if (Platform.OS === 'android') {
        try {
          const results = await PermissionsAndroid.requestMultiple([
            PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
            PermissionsAndroid.PERMISSIONS.READ_EXTERNAL_STORAGE,
            PermissionsAndroid.PERMISSIONS.WRITE_EXTERNAL_STORAGE,
          ]);

          const allGranted = Object.values(results).every(
            r => r === PermissionsAndroid.RESULTS.GRANTED
          );

          if (!allGranted) {
            Alert.alert(
              'Permissions Required',
              'Teleport needs location and storage permissions to discover devices and transfer files.',
              [{ text: 'OK' }]
            );
            setPermissionsGranted(false);
          } else {
            setPermissionsGranted(true);
          }
        } catch (err) {
          console.error('Permission error:', err);
          Alert.alert('Permission Error', 'Failed to request permissions');
          setPermissionsGranted(false);
          return;
        }
      } else {
        setPermissionsGranted(true);
      }

      // Use device name from settings (race condition fix)
      const deviceName = settingsRef.current.deviceName || 'TeleportMobile';
      const success = await teleportService.initialize(deviceName);

      if (mounted) {
        setInitialized(success);
        if (success) {
          doHaptic('medium');
        }
      }
    };
    init();

    // Set up real progress event listener with speed calculation
    const unsubProgress = teleportService.onProgress((progress) => {
      setProgress(progress.percent);
      if (progress.currentFile) {
        setCurrentFileName(progress.currentFile);
      }

      // Track file size from progress
      if (progress.totalBytes > 0) {
        setCurrentFileSize(progress.totalBytes);
      }

      // Calculate transfer speed (bytes per second)
      const now = Date.now();
      const timeDiff = (now - lastTimeRef.current) / 1000; // seconds
      if (timeDiff >= 0.5) { // Update speed every 500ms
        const bytesDiff = progress.bytesTransferred - lastBytesRef.current;
        const speed = bytesDiff / timeDiff;
        setTransferSpeed(speed > 0 ? speed : 0);
        lastBytesRef.current = progress.bytesTransferred;
        lastTimeRef.current = now;
      }
    });

    // Set up completion event listener - saves sent transfers to history
    const unsubComplete = teleportService.onComplete(async (errorCode) => {
      const success = errorCode === 0;
      if (success) {
        setStatusMessage('sent');
        setProgress(100);
        setIsSending(false);
        setShowSuccess(true);

        // Save to history when send completes
        if (currentFileNameRef.current && selectedDeviceRef.current) {
          const record: TransferRecord = {
            id: generateTransferId(),
            type: 'sent',
            fileName: currentFileNameRef.current,
            fileSize: 0, // Size not tracked in current implementation
            deviceName: selectedDeviceRef.current.name,
            timestamp: Date.now(),
            status: 'success',
          };
          const updated = await addToHistory(record);
          setTransferHistory(updated);
        }
      } else {
        setStatusMessage('failed');
        Alert.alert('transfer failed', 'the receiver may be offline or not accepting files');
        setIsSending(false);
        setProgress(0);
        setCurrentFileName('');
      }
    });

    // Set up incoming files listener - shows modal when files are arriving
    const unsubIncoming = teleportService.onIncomingFiles((info) => {
      setIncomingSender(info.senderName);
      setIncomingFileCount(info.fileCount);
      setIncomingTotalSize(info.totalSize);

      // Auto-accept if enabled
      if (settingsRef.current.autoAccept) {
        teleportService.acceptIncomingFiles();
        setStatusMessage('receiving');
      } else {
        setShowIncoming(true);
      }
    });

    // Set up file received listener - saves received files to history
    const unsubReceived = teleportService.onFileReceived(async (info) => {
      const record: TransferRecord = {
        id: generateTransferId(),
        type: 'received',
        fileName: info.fileName,
        fileSize: info.fileSize,
        deviceName: info.senderName,
        timestamp: info.timestamp || Date.now(),
        status: 'success',
      };
      const updated = await addToHistory(record);
      setTransferHistory(updated);
      doHaptic('medium');
    });

    return () => {
      mounted = false;
      unsubProgress();
      unsubComplete();
      unsubIncoming();
      unsubReceived();
      teleportService.destroy();
    };
  }, [settingsLoaded, doHaptic]); // Only re-run when settingsLoaded changes

  // Network state tracking
  const [isOnlineState, setIsOnlineState] = useState(true);

  useEffect(() => {
    // Check initial network state
    isOnline().then(setIsOnlineState);

    // Subscribe to network changes
    const unsubscribe = onNetworkChange((connected) => {
      setIsOnlineState(connected);
      if (!connected) {
        setStatusMessage('offline');
      } else if (statusMessage === 'offline') {
        setStatusMessage('');
      }
    });

    return unsubscribe;
  }, [statusMessage]);

  // App backgrounding - pause/resume operations
  const appState = useRef(AppState.currentState);

  useEffect(() => {
    const handleAppStateChange = (nextAppState: AppStateStatus) => {
      if (appState.current.match(/active/) && nextAppState.match(/inactive|background/)) {
        // App going to background - pause discovery if active
        if (isDiscovering) {
          teleportService.stopDiscovery();
          console.log('[App] Paused discovery due to backgrounding');
        }
      } else if (appState.current.match(/inactive|background/) && nextAppState === 'active') {
        // App coming to foreground - resume discovery if was active
        if (isDiscovering && initialized) {
          teleportService.startDiscovery();
          console.log('[App] Resumed discovery from background');
        }
      }
      appState.current = nextAppState;
    };

    const subscription = AppState.addEventListener('change', handleAppStateChange);
    return () => subscription?.remove();
  }, [isDiscovering, initialized]);

  // Native device discovery callbacks (replaces polling)
  useEffect(() => {
    if (!initialized) return;

    // Use native callbacks instead of polling
    const unsubDiscovered = teleportService.onDeviceDiscovered((device) => {
      setDevices(prev => {
        // Check if device already exists
        if (prev.some(d => d.id === device.id)) {
          return prev.map(d => d.id === device.id ? device : d);
        }
        doHaptic('light');
        return [...prev, device];
      });
    });

    const unsubLost = teleportService.onDeviceLost((deviceId) => {
      setDevices(prev => prev.filter(d => d.id !== deviceId));
    });

    return () => {
      unsubDiscovered();
      unsubLost();
    };
  }, [initialized, doHaptic]);

  const toggleDiscovery = useCallback(async () => {
    doHaptic('medium');

    // Check offline
    if (!isOnlineState) {
      Alert.alert('Offline', 'Cannot discover devices while offline');
      return;
    }

    if (isDiscovering) {
      await teleportService.stopDiscovery();
      setIsDiscovering(false);
      setStatusMessage('');
    } else {
      try {
        const success = await withTimeout(
          teleportService.startDiscovery(),
          30000,
          'Discovery timed out'
        );
        setIsDiscovering(success);
        setStatusMessage('scanning');
      } catch (error) {
        Alert.alert('Discovery Failed', error instanceof Error ? error.message : 'Unknown error');
        reportCrash(error instanceof Error ? error : new Error(String(error)), { action: 'toggleDiscovery' });
      }
    }
  }, [isDiscovering, isOnlineState, doHaptic]);

  const toggleReceiving = useCallback(async () => {
    doHaptic('medium');

    if (isReceiving) {
      await teleportService.stopReceiving();
      setIsReceiving(false);
      setStatusMessage('');
    } else {
      // Validate and use download path from settings (path injection prevention)
      const safePath = validateDownloadPath(settings.downloadPath);

      try {
        const success = await withTimeout(
          teleportService.startReceiving(safePath),
          30000,
          'Start receiving timed out'
        );
        setIsReceiving(success);
        if (success) {
          setStatusMessage('listening');
        }
      } catch (error) {
        Alert.alert('Receive Failed', error instanceof Error ? error.message : 'Unknown error');
        reportCrash(error instanceof Error ? error : new Error(String(error)), { action: 'toggleReceiving' });
      }
    }
  }, [isReceiving, settings.downloadPath, doHaptic]);

  const selectAndSendFiles = useCallback(async () => {
    if (!selectedDevice) return;
    doHaptic('medium');

    // Check offline before sending
    if (!isOnlineState) {
      Alert.alert('Offline', 'Cannot send files while offline');
      return;
    }

    try {
      const results = await DocumentPicker.pick({
        allowMultiSelection: true,
        type: [DocumentPicker.types.allFiles],
      });

      if (results && results.length > 0) {
        const filePaths = results.map(file => file.uri);
        const fileName = results[0].name || 'file';
        const fileSize = results[0].size || 0;

        setIsSending(true);
        setStatusMessage('sending');
        setProgress(0);
        setCurrentFileName(fileName);

        // Send with retry (3 attempts with exponential backoff)
        try {
          const started = await withRetry(
            () => withTimeout(
              teleportService.sendFiles(selectedDevice.id, filePaths),
              30000,
              'Send operation timed out'
            ),
            2, // 2 retries = 3 total attempts
            (attempt, error, delay) => {
              setStatusMessage(`retrying (${attempt}/3)...`);
              console.log(`[App] Send retry ${attempt}, error: ${error.message}, next delay: ${delay}ms`);
            }
          );

          if (!started) {
            // Transfer failed to start after retries - track in history
            setStatusMessage('failed');
            Alert.alert('Transfer Failed', 'Could not connect to device after multiple attempts');

            // Save failed transfer to history
            const failedRecord: TransferRecord = {
              id: generateTransferId(),
              type: 'sent',
              fileName,
              fileSize,
              deviceName: selectedDevice.name,
              timestamp: Date.now(),
              status: 'failed',
            };
            const updated = await addToHistory(failedRecord);
            setTransferHistory(updated);

            setIsSending(false);
            setProgress(0);
            setCurrentFileName('');
          }
        } catch (error) {
          // All retries failed
          setStatusMessage('failed');
          Alert.alert('Transfer Failed', error instanceof Error ? error.message : 'Unknown error');
          reportCrash(error instanceof Error ? error : new Error(String(error)), {
            action: 'sendFiles',
            deviceId: selectedDevice.id,
          });

          // Save failed transfer to history
          const failedRecord: TransferRecord = {
            id: generateTransferId(),
            type: 'sent',
            fileName,
            fileSize,
            deviceName: selectedDevice.name,
            timestamp: Date.now(),
            status: 'failed',
          };
          const updated = await addToHistory(failedRecord);
          setTransferHistory(updated);

          setIsSending(false);
          setProgress(0);
          setCurrentFileName('');
        }
      }
    } catch (err: any) {
      if (!DocumentPicker.isCancel(err)) {
        Alert.alert('Error', 'Could not select files');
        reportCrash(err instanceof Error ? err : new Error(String(err)), { action: 'documentPicker' });
      }
      setIsSending(false);
      setStatusMessage('');
      setProgress(0);
      setCurrentFileName('');
    }
  }, [selectedDevice, isOnlineState, doHaptic]);

  // Handle transfer cancellation
  const handleCancelTransfer = useCallback(async () => {
    doHaptic('medium');
    await teleportService.cancelTransfer();
    setIsSending(false);
    setProgress(0);
    setTransferSpeed(0);
    setCurrentFileSize(0);
    setCurrentFileName('');
    setStatusMessage('cancelled');

    // Track cancelled transfer in history if there was a file
    if (currentFileNameRef.current && selectedDeviceRef.current) {
      const cancelledRecord: TransferRecord = {
        id: generateTransferId(),
        type: 'sent',
        fileName: currentFileNameRef.current,
        fileSize: 0,
        deviceName: selectedDeviceRef.current.name,
        timestamp: Date.now(),
        status: 'failed',
      };
      const updated = await addToHistory(cancelledRecord);
      setTransferHistory(updated);
    }
  }, [doHaptic]);

  const handleTabChange = (tab: Tab) => {
    doHaptic('light');
    setActiveTab(tab);
    // Auto-start discovery when switching to send tab
    if (tab === 'send' && !isDiscovering && initialized) {
      teleportService.startDiscovery().then(success => {
        setIsDiscovering(success);
      });
    }
  };

  const handleDeviceSelect = (device: TeleportDevice) => {
    doHaptic('medium');
    setSelectedDevice(device);
    setActiveTab('send');
  };

  const renderDevice = ({ item }: { item: TeleportDevice }) => (
    <TouchableOpacity
      style={[
        styles.deviceItem,
        selectedDevice?.id === item.id && styles.deviceSelected,
      ]}
      onPress={() => {
        doHaptic('medium');
        setSelectedDevice(item);
      }}
      activeOpacity={0.7}>
      <View style={[styles.deviceDot, { backgroundColor: '#4ade80' }]} />
      <View style={styles.deviceContent}>
        <Text style={styles.deviceName}>{item.name.toLowerCase()}</Text>
        <Text style={styles.deviceInfo}>{item.os.toLowerCase()}</Text>
      </View>
    </TouchableOpacity>
  );

  const renderHistoryItem = ({ item }: { item: TransferRecord }) => (
    <View style={styles.historyItem}>
      <View style={[styles.historyDot, {
        backgroundColor: item.status === 'success' ? '#4ade80' : '#ef4444'
      }]} />
      <View style={styles.historyContent}>
        <Text style={styles.historyFileName} numberOfLines={1}>{item.fileName}</Text>
        <Text style={styles.historyMeta}>
          {item.type === 'sent' ? '↑' : '↓'} {item.deviceName.toLowerCase()} • {formatSize(item.fileSize)} • {formatTime(item.timestamp)}
        </Text>
      </View>
    </View>
  );

  // Tab content with fade animation
  const AnimatedTabContent = ({ children, visible }: { children: React.ReactNode; visible: boolean }) => {
    const opacity = useRef(new Animated.Value(0)).current;

    useEffect(() => {
      Animated.timing(opacity, {
        toValue: visible ? 1 : 0,
        duration: 200,
        useNativeDriver: true,
      }).start();
    }, [visible]);

    if (!visible) return null;

    return (
      <Animated.View style={[styles.tabContent, { opacity }]}>
        {children}
      </Animated.View>
    );
  };

  return (
    <SafeAreaView style={styles.container}>
      {/* Header */}
      <View style={styles.header}>
        <View style={styles.headerRow}>
          <DotText text="teleport" size={5} />
          <TouchableOpacity onPress={() => { doHaptic('light'); setShowSettings(true); }}>
            <Text style={styles.settingsIcon}>⚙</Text>
          </TouchableOpacity>
        </View>
        <Text style={styles.status}>
          {statusMessage || (initialized ? 'ready' : 'loading')}
        </Text>
      </View>

      {/* 3-tab bar */}
      <View style={styles.tabsContainer}>
        <Animated.View
          style={[
            styles.tabIndicator,
            { width: tabWidth, transform: [{ translateX: tabIndicatorX }] }
          ]}
        />
        {(['send', 'receive', 'history'] as Tab[]).map((tab) => (
          <TouchableOpacity
            key={tab}
            style={styles.tab}
            onPress={() => handleTabChange(tab)}
            activeOpacity={0.6}>
            <Text style={[styles.tabText, activeTab === tab && styles.tabTextActive]}>
              {tab}
            </Text>
          </TouchableOpacity>
        ))}
      </View>

      {/* Send Tab - combines discovery + send in one */}
      <AnimatedTabContent visible={activeTab === 'send'}>
        {selectedDevice ? (
          /* Device selected - show send UI */
          <>
            <View style={styles.targetInfo}>
              <Text style={styles.targetLabel}>sending to</Text>
              <DotText text={selectedDevice.name.toLowerCase().slice(0, 12)} size={5} />
            </View>

            {isSending ? (
              <ProgressBar
                progress={progress}
                fileName={currentFileName}
                speed={transferSpeed}
                totalSize={currentFileSize}
                onCancel={handleCancelTransfer}
              />
            ) : (
              <>
                <AnimatedButton onPress={selectAndSendFiles}>
                  <DotText text="select files" size={4} />
                </AnimatedButton>

                <TouchableOpacity
                  style={styles.clearButton}
                  onPress={() => {
                    doHaptic('light');
                    setSelectedDevice(null);
                  }}>
                  <Text style={styles.clearText}>change device</Text>
                </TouchableOpacity>
              </>
            )}
          </>
        ) : (
          /* No device selected - show device list */
          <>
            {devices.length === 0 ? (
              <View style={styles.emptyState}>
                {isDiscovering ? (
                  <View style={styles.scanningIndicator}>
                    <PulseDot delay={0} />
                    <PulseDot delay={200} />
                    <PulseDot delay={400} />
                  </View>
                ) : (
                  <TouchableOpacity onPress={toggleDiscovery}>
                    <Text style={styles.emptyText}>tap to scan for devices</Text>
                  </TouchableOpacity>
                )}
              </View>
            ) : (
              <>
                <Text style={styles.selectHint}>tap a device to send files</Text>
                <FlatList
                  data={devices}
                  renderItem={renderDevice}
                  keyExtractor={(item) => item.id}
                  style={styles.deviceList}
                  contentContainerStyle={styles.deviceListContent}
                  showsVerticalScrollIndicator={false}
                />
              </>
            )}
          </>
        )}
      </AnimatedTabContent>

      {/* Receive Tab - simple toggle */}
      <AnimatedTabContent visible={activeTab === 'receive'}>
        <AnimatedButton onPress={toggleReceiving} active={isReceiving}>
          <DotText text={isReceiving ? 'stop' : 'receive'} size={4} />
        </AnimatedButton>

        <View style={styles.receiveInfo}>
          {isReceiving ? (
            <View style={styles.listeningIndicator}>
              <PulseDot delay={0} />
              <Text style={styles.listeningText}>waiting for files</Text>
            </View>
          ) : (
            <Text style={styles.receiveText}>tap to accept files from others</Text>
          )}
        </View>
      </AnimatedTabContent>

      {/* History Tab - transfer records */}
      <AnimatedTabContent visible={activeTab === 'history'}>
        {transferHistory.length === 0 ? (
          <View style={styles.emptyState}>
            <Text style={styles.emptyText}>no transfers yet</Text>
          </View>
        ) : (
          <>
            <View style={styles.historyHeader}>
              <Text style={styles.historyCount}>{transferHistory.length} transfers</Text>
              <TouchableOpacity onPress={async () => {
                Alert.alert('Clear History', 'Delete all transfer records?', [
                  { text: 'Cancel', style: 'cancel' },
                  {
                    text: 'Clear', style: 'destructive', onPress: async () => {
                      await clearHistory();
                      setTransferHistory([]);
                    }
                  },
                ]);
              }}>
                <Text style={styles.clearHistoryText}>clear</Text>
              </TouchableOpacity>
            </View>
            <FlatList
              data={transferHistory}
              renderItem={renderHistoryItem}
              keyExtractor={(item) => item.id}
              style={styles.historyList}
              contentContainerStyle={styles.historyListContent}
              showsVerticalScrollIndicator={false}
              // Lazy loading optimizations
              initialNumToRender={10}
              maxToRenderPerBatch={10}
              windowSize={5}
              removeClippedSubviews={true}
              getItemLayout={(_, index) => ({
                length: 60, // Approximate height of each item
                offset: 60 * index,
                index,
              })}
            />
          </>
        )}
      </AnimatedTabContent>

      {/* Settings Modal */}
      <SettingsModal
        visible={showSettings}
        onClose={() => setShowSettings(false)}
        settings={settings}
        onSave={handleSaveSettings}
      />

      {/* Success Celebration Overlay */}
      <SuccessOverlay
        visible={showSuccess}
        onDone={() => {
          setShowSuccess(false);
          setStatusMessage('');
          setProgress(0);
          setCurrentFileName('');
          setSelectedDevice(null);
        }}
      />

      {/* Incoming Files Request Modal */}
      <IncomingFilesModal
        visible={showIncoming}
        senderName={incomingSender}
        fileCount={incomingFileCount}
        totalSize={incomingTotalSize}
        onAccept={async () => {
          setShowIncoming(false);
          await teleportService.acceptIncomingFiles();
          setStatusMessage('receiving');
        }}
        onReject={async () => {
          setShowIncoming(false);
          await teleportService.rejectIncomingFiles();
          setStatusMessage('');
        }}
      />
    </SafeAreaView>
  );
};

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#000',
  },
  header: {
    paddingHorizontal: 24,
    paddingTop: 50,
    paddingBottom: 20,
  },
  headerRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  settingsIcon: {
    fontSize: 20,
    color: '#444',
  },
  status: {
    fontSize: 12,
    color: '#444',
    marginTop: 8,
    letterSpacing: 2,
    textTransform: 'lowercase',
  },
  tabsContainer: {
    flexDirection: 'row',
    marginHorizontal: 24,
    marginBottom: 20,
    position: 'relative',
  },
  tabIndicator: {
    position: 'absolute',
    bottom: 0,
    height: 1,
    backgroundColor: '#fff',
  },
  tab: {
    flex: 1,
    paddingVertical: 10,
    alignItems: 'center',
  },
  tabText: {
    color: '#333',
    fontSize: 12,
    letterSpacing: 1,
    textTransform: 'lowercase',
  },
  tabTextActive: {
    color: '#fff',
  },
  tabContent: {
    flex: 1,
    paddingHorizontal: 24,
  },
  mainButton: {
    backgroundColor: '#0a0a0a',
    paddingVertical: 20,
    borderRadius: 2,
    alignItems: 'center',
    justifyContent: 'center',
    borderWidth: 1,
    borderColor: '#1a1a1a',
  },
  buttonActive: {
    borderColor: '#333',
  },
  buttonDisabled: {
    opacity: 0.5,
  },
  deviceList: {
    marginTop: 20,
  },
  deviceListContent: {
    paddingBottom: 24,
  },
  deviceItem: {
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: '#050505',
    paddingVertical: 16,
    paddingHorizontal: 16,
    borderRadius: 2,
    borderWidth: 1,
    borderColor: '#111',
    marginBottom: 10,
  },
  deviceSelected: {
    borderColor: '#333',
  },
  deviceDot: {
    width: 8,
    height: 8,
    borderRadius: 4,
    marginRight: 14,
  },
  deviceContent: {
    flex: 1,
  },
  deviceName: {
    color: '#fff',
    fontSize: 15,
    letterSpacing: 0.5,
  },
  deviceInfo: {
    color: '#444',
    fontSize: 11,
    marginTop: 3,
    letterSpacing: 0.5,
  },
  emptyState: {
    flex: 1,
    justifyContent: 'center',
    alignItems: 'center',
  },
  emptyText: {
    color: '#222',
    fontSize: 13,
    letterSpacing: 1,
  },
  selectHint: {
    color: '#333',
    fontSize: 12,
    letterSpacing: 1,
    marginBottom: 12,
  },
  scanningIndicator: {
    flexDirection: 'row',
    gap: 8,
  },
  pulseDot: {
    width: 8,
    height: 8,
    borderRadius: 4,
    backgroundColor: '#fff',
    marginHorizontal: 4,
  },
  targetInfo: {
    marginBottom: 24,
  },
  targetLabel: {
    color: '#444',
    fontSize: 11,
    letterSpacing: 3,
    marginBottom: 6,
    textTransform: 'lowercase',
  },
  clearButton: {
    marginTop: 20,
    alignItems: 'center',
    paddingVertical: 14,
  },
  clearText: {
    color: '#333',
    fontSize: 13,
    letterSpacing: 2,
  },
  progressBarContainer: {
    paddingVertical: 20,
  },
  progressFileName: {
    color: '#666',
    fontSize: 12,
    letterSpacing: 0.5,
    marginBottom: 12,
  },
  progressBarTrack: {
    height: 2,
    backgroundColor: '#1a1a1a',
    borderRadius: 1,
    overflow: 'hidden',
  },
  progressBarFill: {
    height: '100%',
    backgroundColor: '#fff',
  },
  progressPercent: {
    color: '#fff',
    fontSize: 18,
    fontWeight: '200',
    letterSpacing: 2,
  },
  progressHeader: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 8,
  },
  progressStats: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginTop: 8,
  },
  progressSize: {
    color: '#666',
    fontSize: 12,
    letterSpacing: 1,
  },
  progressSpeed: {
    color: '#4ade80',
    fontSize: 12,
    letterSpacing: 1,
    fontWeight: '500',
  },
  cancelButtonText: {
    color: '#ef4444',
    fontSize: 16,
    fontWeight: '600',
  },
  receiveInfo: {
    flex: 1,
    justifyContent: 'center',
    alignItems: 'center',
  },
  receiveText: {
    color: '#222',
    fontSize: 13,
    letterSpacing: 1,
  },
  listeningIndicator: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 12,
  },
  listeningText: {
    color: '#444',
    fontSize: 13,
    letterSpacing: 1,
  },
  historyHeader: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 16,
  },
  historyCount: {
    color: '#666',
    fontSize: 12,
    letterSpacing: 1,
  },
  clearHistoryText: {
    color: '#ef4444',
    fontSize: 12,
    letterSpacing: 1,
  },
  historyList: {
    flex: 1,
  },
  historyListContent: {
    paddingBottom: 24,
  },
  historyItem: {
    flexDirection: 'row',
    alignItems: 'center',
    paddingVertical: 14,
    borderBottomWidth: 1,
    borderBottomColor: '#111',
  },
  historyDot: {
    width: 6,
    height: 6,
    borderRadius: 3,
    marginRight: 14,
  },
  historyContent: {
    flex: 1,
  },
  historyFileName: {
    color: '#fff',
    fontSize: 14,
    letterSpacing: 0.5,
  },
  historyMeta: {
    color: '#444',
    fontSize: 11,
    marginTop: 3,
    letterSpacing: 0.5,
  },
  modalOverlay: {
    flex: 1,
    backgroundColor: 'rgba(0,0,0,0.9)',
    justifyContent: 'flex-end',
  },
  modalContent: {
    backgroundColor: '#0a0a0a',
    borderTopLeftRadius: 4,
    borderTopRightRadius: 4,
    padding: 24,
    paddingBottom: 40,
  },
  modalTitle: {
    color: '#fff',
    fontSize: 18,
    letterSpacing: 2,
    marginBottom: 24,
  },
  settingItem: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    paddingVertical: 14,
    borderBottomWidth: 1,
    borderBottomColor: '#1a1a1a',
  },
  settingLabel: {
    color: '#666',
    fontSize: 13,
    letterSpacing: 1,
  },
  settingValue: {
    color: '#fff',
    fontSize: 13,
    letterSpacing: 0.5,
    maxWidth: '50%',
  },
  settingInput: {
    color: '#fff',
    fontSize: 13,
    letterSpacing: 0.5,
    textAlign: 'right',
    minWidth: 120,
    paddingVertical: 4,
    paddingHorizontal: 8,
    backgroundColor: '#1a1a1a',
    borderRadius: 4,
  },
  settingItemRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    paddingVertical: 14,
    borderBottomWidth: 1,
    borderBottomColor: '#1a1a1a',
  },
  settingsButtonRow: {
    flexDirection: 'row',
    gap: 12,
    marginTop: 24,
  },
  saveButton: {
    flex: 1,
    backgroundColor: '#4ade80',
  },
  saveButtonText: {
    color: '#000',
    fontSize: 14,
    letterSpacing: 2,
    fontWeight: '600',
  },
  cancelButton: {
    flex: 1,
    backgroundColor: '#333',
  },
  modalCloseButton: {
    flex: 1,
    alignItems: 'center',
    paddingVertical: 16,
    backgroundColor: '#111',
    borderRadius: 2,
  },
  modalCloseText: {
    color: '#fff',
    fontSize: 14,
    letterSpacing: 2,
  },
  // Success celebration overlay styles
  successOverlay: {
    ...StyleSheet.absoluteFillObject,
    backgroundColor: 'rgba(0, 0, 0, 0.95)',
    justifyContent: 'center',
    alignItems: 'center',
    zIndex: 1000,
  },
  successRing: {
    position: 'absolute',
    width: 80,
    height: 80,
    borderRadius: 40,
    borderWidth: 2,
    borderColor: '#4ade80',
  },
  successCircle: {
    width: 80,
    height: 80,
    borderRadius: 40,
    backgroundColor: '#4ade80',
    justifyContent: 'center',
    alignItems: 'center',
    marginBottom: 24,
  },
  successCheck: {
    fontSize: 40,
    color: '#000',
    fontWeight: '200',
  },
  successText: {
    fontSize: 24,
    color: '#fff',
    letterSpacing: 4,
    fontWeight: '200',
  },
  // Incoming files modal styles
  incomingOverlay: {
    flex: 1,
    backgroundColor: 'rgba(0, 0, 0, 0.9)',
    justifyContent: 'center',
    alignItems: 'center',
    padding: 24,
  },
  incomingCard: {
    backgroundColor: '#0a0a0a',
    borderRadius: 4,
    padding: 28,
    width: '100%',
    maxWidth: 320,
    borderWidth: 1,
    borderColor: '#1a1a1a',
  },
  incomingTitle: {
    color: '#fff',
    fontSize: 18,
    letterSpacing: 2,
    marginBottom: 20,
  },
  incomingSender: {
    flexDirection: 'row',
    alignItems: 'center',
    marginBottom: 8,
  },
  incomingDot: {
    width: 8,
    height: 8,
    borderRadius: 4,
    backgroundColor: '#4ade80',
    marginRight: 10,
  },
  incomingSenderName: {
    color: '#fff',
    fontSize: 16,
    letterSpacing: 0.5,
  },
  incomingInfo: {
    color: '#666',
    fontSize: 13,
    letterSpacing: 0.5,
    marginBottom: 24,
    marginLeft: 18,
  },
  incomingButtons: {
    flexDirection: 'row',
    gap: 12,
  },
  rejectButton: {
    flex: 1,
    backgroundColor: '#1a1a1a',
    paddingVertical: 14,
    borderRadius: 2,
    alignItems: 'center',
  },
  rejectText: {
    color: '#888',
    fontSize: 14,
    letterSpacing: 1,
  },
  acceptButton: {
    flex: 1,
    backgroundColor: '#4ade80',
    paddingVertical: 14,
    borderRadius: 2,
    alignItems: 'center',
  },
  acceptText: {
    color: '#000',
    fontSize: 14,
    letterSpacing: 1,
    fontWeight: '600',
  },
});

// Wrap App with ErrorBoundary for global error catching
const SafeApp = () => (
  <ErrorBoundary
    onError={(error, errorInfo) => {
      reportCrash(error, { componentStack: errorInfo.componentStack });
    }}
  >
    <App />
  </ErrorBoundary>
);

export default SafeApp;


