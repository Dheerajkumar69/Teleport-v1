/**
 * TeleportMobile - Ultra-Simple P2P File Transfer  
 * Two-tab design: Send | Receive
 */
import React, { useEffect, useState, useCallback, useRef } from 'react';
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
} from 'react-native';
import DocumentPicker from 'react-native-document-picker';
import DotText from './src/components/DotText';
import teleportService, { TeleportDevice } from './src/TeleportService';

const { width } = Dimensions.get('window');

// Simple 2-tab navigation
type Tab = 'send' | 'receive';

interface TransferRecord {
  id: string;
  type: 'sent' | 'received';
  fileName: string;
  fileSize: number;
  deviceName: string;
  timestamp: number;
  status: 'success' | 'failed';
}

// Haptic feedback helper
const haptic = (type: 'light' | 'medium' | 'heavy' = 'light') => {
  try {
    if (Platform.OS === 'android') {
      Vibration.vibrate(type === 'light' ? 10 : type === 'medium' ? 20 : 30);
    }
  } catch (e) { }
};

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

// Progress bar component
const ProgressBar = ({ progress, fileName }: { progress: number; fileName: string }) => {
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
      <Text style={styles.progressFileName} numberOfLines={1}>{fileName}</Text>
      <View style={styles.progressBarTrack}>
        <Animated.View style={[styles.progressBarFill, { width: progressWidth }]} />
      </View>
      <Text style={styles.progressPercent}>{Math.round(progress)}%</Text>
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

// Settings Modal
const SettingsModal = ({ visible, onClose }: { visible: boolean; onClose: () => void }) => {
  return (
    <Modal visible={visible} animationType="slide" transparent>
      <View style={styles.modalOverlay}>
        <View style={styles.modalContent}>
          <Text style={styles.modalTitle}>settings</Text>

          <View style={styles.settingItem}>
            <Text style={styles.settingLabel}>device name</Text>
            <Text style={styles.settingValue}>teleportmobile</Text>
          </View>

          <View style={styles.settingItem}>
            <Text style={styles.settingLabel}>save location</Text>
            <Text style={styles.settingValue}>downloads</Text>
          </View>

          <View style={styles.settingItem}>
            <Text style={styles.settingLabel}>auto accept</Text>
            <Text style={styles.settingValue}>off</Text>
          </View>

          <View style={styles.settingItem}>
            <Text style={styles.settingLabel}>version</Text>
            <Text style={styles.settingValue}>1.0.0</Text>
          </View>

          <TouchableOpacity style={styles.modalCloseButton} onPress={onClose}>
            <Text style={styles.modalCloseText}>close</Text>
          </TouchableOpacity>
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

  // Incoming files state  
  const [showIncoming, setShowIncoming] = useState(false);
  const [incomingSender, setIncomingSender] = useState('');
  const [incomingFileCount, setIncomingFileCount] = useState(0);
  const [incomingTotalSize, setIncomingTotalSize] = useState(0);

  // Tab indicator animation - 2 tabs now
  const tabIndicatorX = useRef(new Animated.Value(0)).current;
  const tabWidth = (width - 48) / 2;  // 2 tabs

  useEffect(() => {
    const index = ['send', 'receive'].indexOf(activeTab);
    Animated.spring(tabIndicatorX, {
      toValue: index * tabWidth,
      useNativeDriver: true,
      damping: 20,
      stiffness: 200,
    }).start();
  }, [activeTab]);

  // Initialize Teleport on mount and set up event listeners
  useEffect(() => {
    const init = async () => {
      if (Platform.OS === 'android') {
        try {
          await PermissionsAndroid.requestMultiple([
            PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
            PermissionsAndroid.PERMISSIONS.READ_EXTERNAL_STORAGE,
            PermissionsAndroid.PERMISSIONS.WRITE_EXTERNAL_STORAGE,
          ]);
        } catch (err) {
          console.warn('Permission error:', err);
        }
      }

      const success = await teleportService.initialize('TeleportMobile');
      setInitialized(success);
      if (success) {
        haptic('medium');
      }
    };
    init();

    // Set up real progress event listener
    const unsubProgress = teleportService.onProgress((progress) => {
      setProgress(progress.percent);
      if (progress.currentFile) {
        setCurrentFileName(progress.currentFile);
      }
    });

    // Set up completion event listener
    const unsubComplete = teleportService.onComplete((errorCode) => {
      const success = errorCode === 0;
      if (success) {
        setStatusMessage('sent');
        setProgress(100);
        setIsSending(false);
        setShowSuccess(true);
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
      setShowIncoming(true);
    });

    return () => {
      unsubProgress();
      unsubComplete();
      unsubIncoming();
      teleportService.destroy();
    };
  }, []);

  // Refresh devices while discovering
  useEffect(() => {
    if (!isDiscovering || !initialized) return;

    const interval = setInterval(async () => {
      const newDevices = await teleportService.getDevices();
      if (newDevices.length !== devices.length) {
        haptic('light');
      }
      setDevices(newDevices);
    }, 1000);

    return () => clearInterval(interval);
  }, [isDiscovering, initialized, devices.length]);

  const toggleDiscovery = useCallback(async () => {
    haptic('medium');
    if (isDiscovering) {
      await teleportService.stopDiscovery();
      setIsDiscovering(false);
      setStatusMessage('');
    } else {
      const success = await teleportService.startDiscovery();
      setIsDiscovering(success);
      setStatusMessage('scanning');
    }
  }, [isDiscovering]);

  const toggleReceiving = useCallback(async () => {
    haptic('medium');
    if (isReceiving) {
      await teleportService.stopReceiving();
      setIsReceiving(false);
      setStatusMessage('');
    } else {
      const success = await teleportService.startReceiving('/storage/emulated/0/Download');
      setIsReceiving(success);
      if (success) {
        setStatusMessage('listening');
      }
    }
  }, [isReceiving]);

  const selectAndSendFiles = useCallback(async () => {
    if (!selectedDevice) return;
    haptic('medium');

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

        // Real file transfer - progress and completion come from native events
        const started = await teleportService.sendFiles(selectedDevice.id, filePaths);

        if (!started) {
          // Transfer failed to start
          setStatusMessage('failed');
          Alert.alert('transfer failed', 'could not connect to device');
          setIsSending(false);
          setProgress(0);
          setCurrentFileName('');
        }

        // If started successfully, wait for onComplete event to handle the rest
        // Add to history when transfer completes (in onComplete handler)
      }
    } catch (err: any) {
      if (!DocumentPicker.isCancel(err)) {
        Alert.alert('error', 'could not select files');
      }
      setIsSending(false);
      setStatusMessage('');
      setProgress(0);
      setCurrentFileName('');
    }
  }, [selectedDevice]);

  const handleTabChange = (tab: Tab) => {
    haptic('light');
    setActiveTab(tab);
    // Auto-start discovery when switching to send tab
    if (tab === 'send' && !isDiscovering && initialized) {
      teleportService.startDiscovery().then(success => {
        setIsDiscovering(success);
      });
    }
  };

  const handleDeviceSelect = (device: TeleportDevice) => {
    haptic('medium');
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
        haptic('medium');
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
          <TouchableOpacity onPress={() => { haptic('light'); setShowSettings(true); }}>
            <Text style={styles.settingsIcon}>⚙</Text>
          </TouchableOpacity>
        </View>
        <Text style={styles.status}>
          {statusMessage || (initialized ? 'ready' : 'loading')}
        </Text>
      </View>

      {/* Simple 2-tab bar */}
      <View style={styles.tabsContainer}>
        <Animated.View
          style={[
            styles.tabIndicator,
            { width: tabWidth, transform: [{ translateX: tabIndicatorX }] }
          ]}
        />
        {(['send', 'receive'] as Tab[]).map((tab) => (
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
              <ProgressBar progress={progress} fileName={currentFileName} />
            ) : (
              <>
                <AnimatedButton onPress={selectAndSendFiles}>
                  <DotText text="select files" size={4} />
                </AnimatedButton>

                <TouchableOpacity
                  style={styles.clearButton}
                  onPress={() => {
                    haptic('light');
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

      {/* Settings Modal */}
      <SettingsModal visible={showSettings} onClose={() => setShowSettings(false)} />

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
    fontSize: 24,
    fontWeight: '200',
    letterSpacing: 2,
    marginTop: 16,
    textAlign: 'center',
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
  },
  modalCloseButton: {
    marginTop: 24,
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

export default App;


