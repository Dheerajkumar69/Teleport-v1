/**
 * TeleportMobile — All Offline Transfer Modes
 * =====================================================
 * Tabs: Send | Receive | History
 * Transfer Modes: LAN | WiFi Direct | Hotspot | QR Code | WebRTC
 */
import React, {
  useEffect, useState, useCallback, useRef
} from 'react';
import {
  SafeAreaView, StyleSheet, Text, View, TouchableOpacity,
  FlatList, Alert, PermissionsAndroid, Platform, Dimensions,
  Vibration, Animated, Easing, ScrollView, Modal, TextInput,
  Switch, AppState, AppStateStatus, NativeModules,
  NativeEventEmitter,
} from 'react-native';
import DocumentPicker from 'react-native-document-picker';
import DotText from './src/components/DotText';
import teleportService, { TeleportDevice, FileReceivedInfo } from './src/TeleportService';
import WebRTCService from './src/WebRTCService';
// SignalingClient USB mode hook — reserved for future adb-forward usage
import type { PeerInfo, FileInfo } from './src/SignalingClient';
import {
  AppSettings, TransferRecord, loadSettings, saveSettings,
  loadHistory, addToHistory, clearHistory, generateTransferId,
  validateDownloadPath,
} from './src/SettingsStorage';
import ErrorBoundary, { reportCrash } from './src/ErrorBoundary';
import {
  withTimeout, withRetry, throttle, isOnline, onNetworkChange,
} from './src/NetworkUtils';

const { width } = Dimensions.get('window');

// ============================================================================
// TYPES
// ============================================================================

type Tab = 'send' | 'receive' | 'history';

/** All supported transfer modes */
type TransferMode = 'lan' | 'wifidirect' | 'hotspot' | 'qr' | 'webrtc';

interface WifiDirectPeer {
  mac: string;
  name: string;
  type: string;
  status: number;
}

interface WifiDirectConnectionInfo {
  groupOwnerIp: string;
  isGroupOwner: boolean;
  groupFormed: boolean;
}

interface HotspotInfo {
  ssid: string | null;
  password: string | null;
  gatewayIp: string | null;
  isActive?: boolean;
}

// ============================================================================
// HELPERS
// ============================================================================

const rawHaptic = (type: 'light' | 'medium' | 'heavy' = 'light') => {
  try {
    if (Platform.OS === 'android') {
      Vibration.vibrate(type === 'light' ? 10 : type === 'medium' ? 20 : 30);
    }
  } catch (_) {}
};
const haptic = throttle(rawHaptic, 100);

const formatSize = (bytes: number): string => {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
  return `${(bytes / (1024 * 1024 * 1024)).toFixed(2)} GB`;
};

const formatTime = (timestamp: number): string => {
  const date = new Date(timestamp);
  const now = new Date();
  const diffMins = Math.floor((now.getTime() - date.getTime()) / 60000);
  if (diffMins < 1) return 'just now';
  if (diffMins < 60) return `${diffMins}m ago`;
  if (diffMins < 1440) return `${Math.floor(diffMins / 60)}h ago`;
  return date.toLocaleDateString();
};

const formatSpeed = (bps: number): string => {
  if (bps < 1024) return `${bps.toFixed(0)} B/s`;
  if (bps < 1024 * 1024) return `${(bps / 1024).toFixed(1)} KB/s`;
  return `${(bps / (1024 * 1024)).toFixed(1)} MB/s`;
};

// ============================================================================
// SMALL COMPONENTS
// ============================================================================

const SkeletonLoader = ({ width: w = '100%', height = 20 }: { width?: string | number; height?: number }) => {
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
      style={{ width: w as any, height, backgroundColor: '#1a1a1a', borderRadius: 4, opacity }}
    />
  );
};

const PulseDot = ({ delay = 0, color = '#fff' }: { delay?: number; color?: string }) => {
  const opacity = useRef(new Animated.Value(0.3)).current;
  useEffect(() => {
    const t = setTimeout(() => {
      Animated.loop(
        Animated.sequence([
          Animated.timing(opacity, { toValue: 1, duration: 600, useNativeDriver: true }),
          Animated.timing(opacity, { toValue: 0.3, duration: 600, useNativeDriver: true }),
        ])
      ).start();
    }, delay);
    return () => clearTimeout(t);
  }, []);
  return <Animated.View style={[styles.pulseDot, { opacity, backgroundColor: color }]} />;
};

const AnimatedButton = ({
  onPress, active, children, disabled, color,
}: {
  onPress: () => void;
  active?: boolean;
  children: React.ReactNode;
  disabled?: boolean;
  color?: string;
}) => {
  const scale = useRef(new Animated.Value(1)).current;
  return (
    <Animated.View style={{ transform: [{ scale }] }}>
      <TouchableOpacity
        style={[
          styles.mainButton,
          active && styles.buttonActive,
          disabled && styles.buttonDisabled,
          color ? { borderColor: color + '44' } : null,
        ]}
        onPress={() => {
          Animated.spring(scale, { toValue: 0.96, useNativeDriver: true }).start(() =>
            Animated.spring(scale, { toValue: 1, useNativeDriver: true }).start()
          );
          haptic('light');
          onPress();
        }}
        disabled={disabled}
        activeOpacity={0.8}>
        {children}
      </TouchableOpacity>
    </Animated.View>
  );
};

const ProgressBar = ({
  progress, fileName, speed = 0, totalSize = 0, onCancel,
}: {
  progress: number;
  fileName: string;
  speed?: number;
  totalSize?: number;
  onCancel?: () => void;
}) => {
  const widthAnim = useRef(new Animated.Value(0)).current;
  useEffect(() => {
    Animated.timing(widthAnim, { toValue: progress, duration: 200, useNativeDriver: false }).start();
  }, [progress]);
  const progressWidth = widthAnim.interpolate({ inputRange: [0, 100], outputRange: ['0%', '100%'] });
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
        {totalSize > 0 && <Text style={styles.progressSize}>{formatSize(totalSize)}</Text>}
        {speed > 0 && <Text style={styles.progressSpeed}>{formatSpeed(speed)}</Text>}
      </View>
    </View>
  );
};

const SuccessOverlay = ({ visible, onDone }: { visible: boolean; onDone: () => void }) => {
  const scale = useRef(new Animated.Value(0)).current;
  const opacity = useRef(new Animated.Value(0)).current;
  const ringScale = useRef(new Animated.Value(0)).current;
  useEffect(() => {
    if (visible) {
      Vibration.vibrate([0, 30, 50, 30, 50, 50]);
      Animated.parallel([
        Animated.spring(scale, { toValue: 1, tension: 50, friction: 3, useNativeDriver: true }),
        Animated.timing(opacity, { toValue: 1, duration: 200, useNativeDriver: true }),
        Animated.sequence([
          Animated.delay(100),
          Animated.timing(ringScale, { toValue: 3, duration: 600, easing: Easing.out(Easing.cubic), useNativeDriver: true }),
        ]),
      ]).start();
      const t = setTimeout(() => {
        Animated.parallel([
          Animated.timing(opacity, { toValue: 0, duration: 300, useNativeDriver: true }),
          Animated.timing(scale, { toValue: 0.8, duration: 300, useNativeDriver: true }),
        ]).start(() => onDone());
      }, 1800);
      return () => clearTimeout(t);
    } else {
      scale.setValue(0); opacity.setValue(0); ringScale.setValue(0);
    }
  }, [visible]);
  if (!visible) return null;
  return (
    <Animated.View style={[styles.successOverlay, { opacity }]}>
      <Animated.View style={[styles.successRing, {
        transform: [{ scale: ringScale }],
        opacity: ringScale.interpolate({ inputRange: [0, 2, 3], outputRange: [0.8, 0.3, 0] }),
      }]} />
      <Animated.View style={[styles.successCircle, { transform: [{ scale }] }]}>
        <Text style={styles.successCheck}>✓</Text>
      </Animated.View>
      <Animated.Text style={[styles.successText, { transform: [{ scale }] }]}>sent!</Animated.Text>
    </Animated.View>
  );
};

const IncomingFilesModal = ({
  visible, senderName, fileCount, totalSize, onAccept, onReject, mode,
}: {
  visible: boolean;
  senderName: string;
  fileCount: number;
  totalSize: number;
  onAccept: () => void;
  onReject: () => void;
  mode?: string;
}) => {
  const scale = useRef(new Animated.Value(0.8)).current;
  useEffect(() => {
    if (visible) {
      Vibration.vibrate([0, 100, 50, 100]);
      Animated.spring(scale, { toValue: 1, tension: 100, friction: 8, useNativeDriver: true }).start();
    } else { scale.setValue(0.8); }
  }, [visible]);
  if (!visible) return null;
  return (
    <Modal visible={visible} animationType="fade" transparent>
      <View style={styles.incomingOverlay}>
        <Animated.View style={[styles.incomingCard, { transform: [{ scale }] }]}>
          <Text style={styles.incomingTitle}>incoming files</Text>
          {mode && <Text style={styles.incomingMode}>via {mode}</Text>}
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

// ============================================================================
// TRANSFER MODE BADGE — colored pill showing current mode
// ============================================================================

const MODE_CONFIG: Record<TransferMode, { label: string; color: string; icon: string; desc: string }> = {
  lan:        { label: 'LAN',        color: '#4ade80', icon: '📡', desc: 'same wifi network' },
  wifidirect: { label: 'WiFi Direct', color: '#60a5fa', icon: '⚡', desc: 'p2p no router needed' },
  hotspot:    { label: 'Hotspot',    color: '#fb923c', icon: '🔥', desc: 'create or join ap' },
  qr:         { label: 'QR Code',    color: '#a78bfa', icon: '▦',  desc: 'scan to connect' },
  webrtc:     { label: 'WebRTC',     color: '#f472b6', icon: '🌐', desc: 'internet relay' },
};

const ModePicker = ({
  current, onChange,
}: {
  current: TransferMode;
  onChange: (m: TransferMode) => void;
}) => (
  <ScrollView
    horizontal
    showsHorizontalScrollIndicator={false}
    style={styles.modePickerScroll}
    contentContainerStyle={styles.modePickerContent}>
    {(Object.keys(MODE_CONFIG) as TransferMode[]).map(mode => {
      const cfg = MODE_CONFIG[mode]!;
      const active = mode === current;
      return (
        <TouchableOpacity
          key={mode}
          style={[styles.modeChip, active && { borderColor: cfg.color, backgroundColor: cfg.color + '18' }]}
          onPress={() => { haptic('light'); onChange(mode); }}
          activeOpacity={0.7}>
          <Text style={styles.modeChipIcon}>{cfg.icon}</Text>
          <Text style={[styles.modeChipLabel, active && { color: cfg.color }]}>{cfg.label}</Text>
        </TouchableOpacity>
      );
    })}
  </ScrollView>
);

// ============================================================================
// QR CODE MODAL (show generated QR info for scanning)
// ============================================================================

const QrCodeModal = ({
  visible, onClose, settings,
}: {
  visible: boolean;
  onClose: () => void;
  settings: AppSettings;
}) => {
  const [qrInfo, setQrInfo] = useState<{ip: string; port: number; deviceName: string} | null>(null);
  const [loading, setLoading] = useState(false);
  const [manualIp, setManualIp] = useState('');
  const [manualPort, setManualPort] = useState('9876');
  const [connectMode, setConnectMode] = useState<'show' | 'enter'>('show');

  useEffect(() => {
    if (visible) {
      setLoading(true);
      // Try to get our IP for display
      teleportService.generateQrPairing(300).then(info => {
        if (info) {
          setQrInfo({ ip: info.ip, port: info.port, deviceName: info.deviceName });
        } else {
          // Fallback - show manual entry
          setConnectMode('enter');
        }
        setLoading(false);
      }).catch(() => {
        setConnectMode('enter');
        setLoading(false);
      });
    }
  }, [visible]);

  if (!visible) return null;

  return (
    <Modal visible={visible} animationType="slide" transparent>
      <View style={styles.modalOverlay}>
        <View style={[styles.modalContent, { paddingBottom: 48 }]}>

          <View style={styles.qrModeToggle}>
            <TouchableOpacity
              style={[styles.qrModeBtn, connectMode === 'show' && styles.qrModeBtnActive]}
              onPress={() => setConnectMode('show')}>
              <Text style={[styles.qrModeBtnText, connectMode === 'show' && { color: '#a78bfa' }]}>show code</Text>
            </TouchableOpacity>
            <TouchableOpacity
              style={[styles.qrModeBtn, connectMode === 'enter' && styles.qrModeBtnActive]}
              onPress={() => setConnectMode('enter')}>
              <Text style={[styles.qrModeBtnText, connectMode === 'enter' && { color: '#a78bfa' }]}>enter ip</Text>
            </TouchableOpacity>
          </View>

          {connectMode === 'show' ? (
            <>
              <Text style={styles.modalTitle}>your connection info</Text>
              {loading ? (
                <View style={{ gap: 12, marginVertical: 24 }}>
                  <SkeletonLoader height={16} />
                  <SkeletonLoader width="60%" height={16} />
                </View>
              ) : qrInfo ? (
                <>
                  <View style={styles.qrInfoBox}>
                    <Text style={styles.qrInfoLabel}>device</Text>
                    <Text style={styles.qrInfoValue}>{qrInfo.deviceName.toLowerCase()}</Text>
                  </View>
                  <View style={styles.qrInfoBox}>
                    <Text style={styles.qrInfoLabel}>ip address</Text>
                    <TouchableOpacity onPress={() => {
                      // Clipboard.setString removed in RN 0.76 — show alert with IP to copy manually
                      Alert.alert('IP Address', qrInfo.ip + '\n\nLong press to copy');
                    }}>
                      <Text style={[styles.qrInfoValue, { color: '#a78bfa' }]}>{qrInfo.ip}</Text>
                    </TouchableOpacity>
                  </View>
                  <View style={styles.qrInfoBox}>
                    <Text style={styles.qrInfoLabel}>port</Text>
                    <Text style={styles.qrInfoValue}>{qrInfo.port}</Text>
                  </View>
                  <Text style={styles.qrHint}>
                    share this ip & port with the other device, then they can connect using "enter ip" mode.
                  </Text>
                </>
              ) : (
                <Text style={styles.qrHint}>
                  Could not get network info. Switch to "enter ip" and type the sender's IP address manually.
                </Text>
              )}
            </>
          ) : (
            <>
              <Text style={styles.modalTitle}>connect by ip</Text>
              <Text style={styles.qrHint}>enter the ip address shown on the other device</Text>
              <View style={styles.settingItem}>
                <Text style={styles.settingLabel}>ip address</Text>
                <TextInput
                  style={styles.settingInput}
                  value={manualIp}
                  onChangeText={setManualIp}
                  placeholder="192.168.x.x"
                  placeholderTextColor="#333"
                  keyboardType="numeric"
                  autoComplete="off"
                />
              </View>
              <View style={styles.settingItem}>
                <Text style={styles.settingLabel}>port</Text>
                <TextInput
                  style={styles.settingInput}
                  value={manualPort}
                  onChangeText={setManualPort}
                  placeholder="9876"
                  placeholderTextColor="#333"
                  keyboardType="number-pad"
                />
              </View>
              <TouchableOpacity
                style={[styles.mainButton, { borderColor: '#a78bfa44', marginTop: 16 }]}
                onPress={async () => {
                  if (!manualIp.trim()) {
                    Alert.alert('Missing IP', 'Please enter the device IP address');
                    return;
                  }
                  haptic('medium');
                  // Build qr data string and connect
                  const qrData = JSON.stringify({
                    ip: manualIp.trim(),
                    port: parseInt(manualPort, 10) || 9876,
                    deviceName: 'Remote',
                    sessionToken: '',
                  });
                  const ok = await teleportService.connectViaQr(qrData);
                  if (ok) {
                    Alert.alert('Connected', `Connected to ${manualIp}`);
                    onClose();
                  } else {
                    Alert.alert('Connection Failed', 'Could not connect to ' + manualIp);
                  }
                }}>
                <Text style={[styles.tabText, { color: '#a78bfa' }]}>connect</Text>
              </TouchableOpacity>
            </>
          )}

          <TouchableOpacity style={[styles.modalCloseButton, { marginTop: 20 }]} onPress={onClose}>
            <Text style={styles.modalCloseText}>close</Text>
          </TouchableOpacity>
        </View>
      </View>
    </Modal>
  );
};

// ============================================================================
// HOTSPOT MODE VIEW
// ============================================================================

const HotspotModeView = ({
  onDeviceReady,
}: {
  onDeviceReady: (ip: string, port: number, name: string) => void;
}) => {
  const [hotspotState, setHotspotState] = useState<'idle' | 'starting' | 'active' | 'error'>('idle');
  const [hotspotInfo, setHotspotInfo] = useState<HotspotInfo | null>(null);
  const [joinIp, setJoinIp] = useState('192.168.43.1'); // Default Android hotspot gateway

  const { HotspotManager } = NativeModules;

  const startHotspot = async () => {
    if (!HotspotManager) {
      Alert.alert('Not Available', 'Hotspot manager is not available on this device');
      return;
    }
    setHotspotState('starting');
    haptic('medium');
    try {
      const result = await HotspotManager.startHotspot();
      setHotspotInfo(result);
      setHotspotState('active');
    } catch (e: any) {
      Alert.alert('Hotspot Failed', e?.message || 'Could not create hotspot');
      setHotspotState('error');
    }
  };

  const stopHotspot = async () => {
    if (!HotspotManager) return;
    try {
      await HotspotManager.stopHotspot();
    } catch (_) {}
    setHotspotState('idle');
    setHotspotInfo(null);
  };

  return (
    <ScrollView showsVerticalScrollIndicator={false}>
      {hotspotState === 'idle' || hotspotState === 'starting' ? (
        <>
          {/* Create hotspot section */}
          <Text style={styles.sectionLabel}>create hotspot</Text>
          <AnimatedButton
            onPress={startHotspot}
            disabled={hotspotState === 'starting'}
            color="#fb923c">
            <Text style={[styles.tabText, { color: hotspotState === 'starting' ? '#888' : '#fb923c' }]}>
              {hotspotState === 'starting' ? 'starting...' : 'start hotspot'}
            </Text>
          </AnimatedButton>
          <Text style={styles.modeDesc}>
            Creates a local WiFi network. The other device connects to it, then you can transfer files.
          </Text>

          {/* Spacer */}
          <View style={{ height: 24, borderBottomWidth: 1, borderBottomColor: '#111', marginBottom: 24 }} />

          {/* Join hotspot section */}
          <Text style={styles.sectionLabel}>join hotspot</Text>
          <Text style={styles.modeDesc}>Connect your phone to the other device's hotspot SSID first, then enter the gateway IP below.</Text>
          <View style={styles.settingItem}>
            <Text style={styles.settingLabel}>gateway ip</Text>
            <TextInput
              style={styles.settingInput}
              value={joinIp}
              onChangeText={setJoinIp}
              placeholder="192.168.43.1"
              placeholderTextColor="#333"
              keyboardType="numeric"
            />
          </View>
          <AnimatedButton
            onPress={() => {
              haptic('medium');
              onDeviceReady(joinIp, 9876, 'Hotspot Host');
            }}
            color="#fb923c">
            <Text style={[styles.tabText, { color: '#fb923c' }]}>connect to host</Text>
          </AnimatedButton>
        </>
      ) : hotspotState === 'active' && hotspotInfo ? (
        <>
          <View style={styles.hotspotCard}>
            <View style={styles.hotspotActiveRow}>
              <PulseDot color="#fb923c" />
              <Text style={[styles.sectionLabel, { color: '#fb923c', marginBottom: 0, marginLeft: 10 }]}>hotspot active</Text>
            </View>
            <View style={{ height: 16 }} />
            <View style={styles.hotspotRow}>
              <Text style={styles.hotspotLabel}>network name</Text>
              <Text style={[styles.hotspotValue, { color: '#fb923c' }]}>{hotspotInfo.ssid || 'Teleport-XXXX'}</Text>
            </View>
            {hotspotInfo.password && (
              <View style={styles.hotspotRow}>
                <Text style={styles.hotspotLabel}>password</Text>
                <Text style={styles.hotspotValue}>{hotspotInfo.password}</Text>
              </View>
            )}
            <View style={styles.hotspotRow}>
              <Text style={styles.hotspotLabel}>your ip</Text>
              <Text style={styles.hotspotValue}>{hotspotInfo.gatewayIp || '192.168.43.1'}</Text>
            </View>
            <Text style={[styles.modeDesc, { marginTop: 16 }]}>
              Have the other device connect to this WiFi network, then switch to LAN mode.
            </Text>
          </View>
          <AnimatedButton onPress={stopHotspot} color="#ef4444">
            <Text style={[styles.tabText, { color: '#ef4444' }]}>stop hotspot</Text>
          </AnimatedButton>
        </>
      ) : (
        <>
          <Text style={{ color: '#ef4444', letterSpacing: 1, textAlign: 'center', marginVertical: 20 }}>
            hotspot failed — check permissions
          </Text>
          <AnimatedButton onPress={() => setHotspotState('idle')} color="#888">
            <Text style={[styles.tabText, { color: '#888' }]}>try again</Text>
          </AnimatedButton>
        </>
      )}
    </ScrollView>
  );
};

// ============================================================================
// WIFI DIRECT MODE VIEW
// ============================================================================

const WiFiDirectModeView = ({
  onDeviceSelected,
}: {
  onDeviceSelected: (ip: string, port: number, name: string) => void;
}) => {
  const [wdPeers, setWdPeers] = useState<WifiDirectPeer[]>([]);
  const [wdState, setWdState] = useState<'idle' | 'discovering' | 'connecting' | 'connected'>('idle');
  const [connInfo, setConnInfo] = useState<WifiDirectConnectionInfo | null>(null);
  const subscriptions = useRef<any[]>([]);

  const { WifiDirectManager } = NativeModules;

  useEffect(() => {
    if (!WifiDirectManager) return;

    // Initialize
    WifiDirectManager.initialize?.().catch((_: any) => {});

    const emitter = new NativeEventEmitter(WifiDirectManager);

    subscriptions.current.push(
      emitter.addListener('WifiDirectPeerFound', (peer: WifiDirectPeer) => {
        setWdPeers(prev => {
          const exists = prev.some(p => p.mac === peer.mac);
          return exists ? prev.map(p => p.mac === peer.mac ? peer : p) : [...prev, peer];
        });
      }),
      emitter.addListener('WifiDirectPeerLost', (data: { mac: string }) => {
        setWdPeers(prev => prev.filter(p => p.mac !== data.mac));
      }),
      emitter.addListener('WifiDirectConnected', (info: WifiDirectConnectionInfo) => {
        setConnInfo(info);
        setWdState('connected');
        // If we're not the group owner, connect to group owner's IP
        if (!info.isGroupOwner && info.groupOwnerIp) {
          onDeviceSelected(info.groupOwnerIp, 9876, 'WiFi Direct Peer');
        }
      }),
      emitter.addListener('WifiDirectDisconnected', () => {
        setConnInfo(null);
        setWdState('idle');
      }),
    );

    return () => {
      subscriptions.current.forEach(s => s.remove());
      subscriptions.current = [];
      WifiDirectManager.stopDiscovery?.().catch((_: any) => {});
    };
  }, []);

  const startDiscovery = async () => {
    if (!WifiDirectManager) {
      Alert.alert('Not Available', 'WiFi Direct is not available on this device');
      return;
    }
    haptic('medium');
    try {
      await WifiDirectManager.startDiscovery();
      setWdState('discovering');
    } catch (e: any) {
      Alert.alert('Discovery Failed', e?.message || 'Could not start WiFi Direct discovery');
    }
  };

  const connectToPeer = async (peer: WifiDirectPeer) => {
    if (!WifiDirectManager) return;
    haptic('medium');
    setWdState('connecting');
    try {
      await WifiDirectManager.connect(peer.mac);
      // Connection result comes via WifiDirectConnected event
    } catch (e: any) {
      Alert.alert('Connect Failed', e?.message || 'Could not connect to peer');
      setWdState('discovering');
    }
  };

  return (
    <View style={{ flex: 1 }}>
      {wdState === 'idle' && (
        <AnimatedButton onPress={startDiscovery} color="#60a5fa">
          <Text style={[styles.tabText, { color: '#60a5fa' }]}>scan for devices</Text>
        </AnimatedButton>
      )}
      {wdState === 'discovering' && (
        <>
          <View style={[styles.scanningIndicator, { marginBottom: 16 }]}>
            <PulseDot color="#60a5fa" delay={0} />
            <PulseDot color="#60a5fa" delay={200} />
            <PulseDot color="#60a5fa" delay={400} />
            <Text style={[styles.tabText, { color: '#60a5fa', marginLeft: 12 }]}>scanning…</Text>
          </View>
          <AnimatedButton onPress={async () => {
            WifiDirectManager?.stopDiscovery?.().catch((_: any) => {});
            setWdState('idle');
          }} color="#888">
            <Text style={[styles.tabText, { color: '#888' }]}>stop</Text>
          </AnimatedButton>
        </>
      )}
      {wdState === 'connecting' && (
        <View style={styles.scanningIndicator}>
          <PulseDot color="#60a5fa" delay={0} />
          <Text style={[styles.tabText, { color: '#60a5fa', marginLeft: 12 }]}>connecting…</Text>
        </View>
      )}
      {wdState === 'connected' && connInfo && (
        <View style={styles.hotspotCard}>
          <Text style={[styles.sectionLabel, { color: '#60a5fa' }]}>wifi direct connected</Text>
          <Text style={styles.modeDesc}>
            {connInfo.isGroupOwner
              ? `You are group owner at ${connInfo.groupOwnerIp || 'unknown IP'}`
              : `Peer is group owner at ${connInfo.groupOwnerIp || 'unknown IP'}`}
          </Text>
          {connInfo.isGroupOwner && (
            <Text style={[styles.modeDesc, { color: '#4ade80' }]}>
              The other device will connect to you automatically once they join.
            </Text>
          )}
          <AnimatedButton onPress={async () => {
            await WifiDirectManager?.disconnect?.();
            setWdState('idle');
            setConnInfo(null);
          }} color="#ef4444">
            <Text style={[styles.tabText, { color: '#ef4444' }]}>disconnect</Text>
          </AnimatedButton>
        </View>
      )}

      {/* Peer list */}
      {wdPeers.length > 0 && (wdState === 'discovering' || wdState === 'idle') && (
        <ScrollView style={{ marginTop: 16 }} showsVerticalScrollIndicator={false}>
          <Text style={styles.sectionLabel}>nearby devices</Text>
          {wdPeers.map(peer => (
            <TouchableOpacity
              key={peer.mac}
              style={[styles.deviceItem, { borderLeftWidth: 2, borderLeftColor: '#60a5fa' }]}
              onPress={() => connectToPeer(peer)}
              activeOpacity={0.7}>
              <View style={[styles.deviceDot, { backgroundColor: '#60a5fa' }]} />
              <View style={styles.deviceContent}>
                <Text style={styles.deviceName}>{peer.name.toLowerCase()}</Text>
                <Text style={styles.deviceInfo}>{peer.type.toLowerCase()} · wifi direct</Text>
              </View>
            </TouchableOpacity>
          ))}
        </ScrollView>
      )}

      {wdPeers.length === 0 && wdState === 'discovering' && (
        <View style={styles.emptyState}>
          <Text style={styles.emptyText}>looking for wifi direct peers…</Text>
          <Text style={[styles.modeDesc, { marginTop: 8, textAlign: 'center' }]}>
            Make sure both devices have WiFi Direct enabled
          </Text>
        </View>
      )}
    </View>
  );
};

// ============================================================================
// SETTINGS MODAL
// ============================================================================

const SettingsModal = ({
  visible, onClose, settings, onSave,
}: {
  visible: boolean;
  onClose: () => void;
  settings: AppSettings;
  onSave: (s: AppSettings) => void;
}) => {
  const [local, setLocal] = useState<AppSettings>(settings);
  const [editing, setEditing] = useState(false);
  useEffect(() => { if (visible) { setLocal(settings); setEditing(false); } }, [visible, settings]);
  const update = <K extends keyof AppSettings>(key: K, value: AppSettings[K]) => {
    setLocal(prev => ({ ...prev, [key]: value }));
    setEditing(true);
  };
  return (
    <Modal visible={visible} animationType="slide" transparent>
      <View style={styles.modalOverlay}>
        <View style={styles.modalContent}>
          <Text style={styles.modalTitle}>settings</Text>
          <View style={styles.settingItem}>
            <Text style={styles.settingLabel}>device name</Text>
            <TextInput
              style={styles.settingInput}
              value={local.deviceName}
              onChangeText={t => update('deviceName', t)}
              placeholder="Enter device name"
              placeholderTextColor="#333"
              maxLength={20}
              autoCapitalize="none"
              autoCorrect={false}
            />
          </View>
          <View style={styles.settingItem}>
            <Text style={styles.settingLabel}>save location</Text>
            <Text style={styles.settingValue} numberOfLines={1}>
              {local.downloadPath.split('/').pop() || 'Downloads'}
            </Text>
          </View>
          <View style={styles.settingItemRow}>
            <Text style={styles.settingLabel}>auto accept files</Text>
            <Switch
              value={local.autoAccept}
              onValueChange={v => update('autoAccept', v)}
              trackColor={{ false: '#222', true: '#4ade80' }}
              thumbColor={local.autoAccept ? '#fff' : '#555'}
            />
          </View>
          <View style={styles.settingItemRow}>
            <Text style={styles.settingLabel}>haptic feedback</Text>
            <Switch
              value={local.vibrationEnabled}
              onValueChange={v => update('vibrationEnabled', v)}
              trackColor={{ false: '#222', true: '#4ade80' }}
              thumbColor={local.vibrationEnabled ? '#fff' : '#555'}
            />
          </View>
          <View style={styles.settingItem}>
            <Text style={styles.settingLabel}>version</Text>
            <Text style={styles.settingValue}>1.1.0</Text>
          </View>
          <View style={styles.settingsButtonRow}>
            {editing && (
              <TouchableOpacity
                style={[styles.modalCloseButton, styles.saveButton]}
                onPress={() => { onSave(local); setEditing(false); onClose(); }}>
                <Text style={styles.saveButtonText}>save</Text>
              </TouchableOpacity>
            )}
            <TouchableOpacity
              style={[styles.modalCloseButton, editing && styles.cancelButton2]}
              onPress={onClose}>
              <Text style={styles.modalCloseText}>{editing ? 'cancel' : 'close'}</Text>
            </TouchableOpacity>
          </View>
        </View>
      </View>
    </Modal>
  );
};

// ============================================================================
// MAIN APP
// ============================================================================

const App = () => {
  // ── Core State ──────────────────────────────────────────────────────────
  const [activeTab, setActiveTab] = useState<Tab>('send');
  const [transferMode, setTransferMode] = useState<TransferMode>('lan');
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
  const [showQr, setShowQr] = useState(false);

  // ── Settings & History ───────────────────────────────────────────────────
  const [settings, setSettings] = useState<AppSettings>({
    deviceName: 'TeleportMobile',
    downloadPath: '/storage/emulated/0/Download',
    autoAccept: false,
    vibrationEnabled: true,
  });
  const [transferHistory, setTransferHistory] = useState<TransferRecord[]>([]);
  const [settingsLoaded, setSettingsLoaded] = useState(false);
  const [permissionsGranted, setPermissionsGranted] = useState(false);

  // ── WebRTC ────────────────────────────────────────────────────────────────
  const webRTCRef = useRef<WebRTCService | null>(null);
  const [webPeers, setWebPeers] = useState<PeerInfo[]>([]);
  const [webRtcConnected, setWebRtcConnected] = useState(false);
  const [webProgress, setWebProgress] = useState(0);
  const [webProgressFile, setWebProgressFile] = useState('');
  const [webIsSending, setWebIsSending] = useState(false);
  const [selectedWebPeer, setSelectedWebPeer] = useState<PeerInfo | null>(null);
  const [showWebIncoming, setShowWebIncoming] = useState(false);
  const [webIncomingFrom, setWebIncomingFrom] = useState('');
  const [webIncomingName, setWebIncomingName] = useState('');
  const [webIncomingFiles, setWebIncomingFiles] = useState<FileInfo[]>([]);

  // ── Speed tracking ────────────────────────────────────────────────────────
  const [transferSpeed, setTransferSpeed] = useState(0);
  const [currentFileSize, setCurrentFileSize] = useState(0);
  const lastBytesRef = useRef(0);
  const lastTimeRef = useRef(Date.now());

  // ── Incoming state ────────────────────────────────────────────────────────
  const [showIncoming, setShowIncoming] = useState(false);
  const [incomingSender, setIncomingSender] = useState('');
  const [incomingFileCount, setIncomingFileCount] = useState(0);
  const [incomingTotalSize, setIncomingTotalSize] = useState(0);
  const [isOnlineState, setIsOnlineState] = useState(true);
  const [isRefreshing, setIsRefreshing] = useState(false);

  // ── Tab animation ─────────────────────────────────────────────────────────
  const tabIndicatorX = useRef(new Animated.Value(0)).current;
  const tabWidth = (width - 48) / 3;

  // ── Refs ──────────────────────────────────────────────────────────────────
  const currentFileNameRef = useRef(currentFileName);
  const selectedDeviceRef = useRef(selectedDevice);
  const settingsRef = useRef(settings);

  useEffect(() => { currentFileNameRef.current = currentFileName; }, [currentFileName]);
  useEffect(() => { selectedDeviceRef.current = selectedDevice; }, [selectedDevice]);
  useEffect(() => { settingsRef.current = settings; }, [settings]);

  // Tab indicator animation
  useEffect(() => {
    const index = ['send', 'receive', 'history'].indexOf(activeTab);
    Animated.spring(tabIndicatorX, {
      toValue: index * tabWidth,
      useNativeDriver: true,
      damping: 20,
      stiffness: 200,
    }).start();
  }, [activeTab]);

  // ── Haptic helper ──────────────────────────────────────────────────────────
  const doHaptic = useCallback((type: 'light' | 'medium' | 'heavy' = 'light') => {
    if (settingsRef.current.vibrationEnabled) haptic(type);
  }, []);

  // ── Load settings and history ──────────────────────────────────────────────
  useEffect(() => {
    const load = async () => {
      const [s, h] = await Promise.all([loadSettings(), loadHistory()]);
      setSettings(s);
      setTransferHistory(h);
      setSettingsLoaded(true);
    };
    load();
  }, []);

  // ── Settings save handler ──────────────────────────────────────────────────
  const handleSaveSettings = useCallback(async (s: AppSettings) => {
    setSettings(s);
    await saveSettings(s);
  }, []);

  // ── Init Teleport native engine ────────────────────────────────────────────
  useEffect(() => {
    if (!settingsLoaded) return;
    let mounted = true;

    const init = async () => {
      if (Platform.OS === 'android') {
        try {
          const perms = await PermissionsAndroid.requestMultiple([
            PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
            PermissionsAndroid.PERMISSIONS.READ_EXTERNAL_STORAGE,
            PermissionsAndroid.PERMISSIONS.WRITE_EXTERNAL_STORAGE,
            PermissionsAndroid.PERMISSIONS.CAMERA,
            'android.permission.NEARBY_WIFI_DEVICES' as any,
          ]);
          const granted = Object.values(perms).every(
            r => r === PermissionsAndroid.RESULTS.GRANTED || r === PermissionsAndroid.RESULTS.NEVER_ASK_AGAIN
          );
          setPermissionsGranted(granted);
        } catch (e) {
          console.error('[App] Permission error:', e);
        }
      } else {
        setPermissionsGranted(true);
      }

      const name = settingsRef.current.deviceName || 'TeleportMobile';
      const ok = await teleportService.initialize(name);
      if (mounted) {
        setInitialized(ok);
        if (ok) doHaptic('medium');
      }
    };
    init();

    // Progress events
    const unsubProgress = teleportService.onProgress(prog => {
      setProgress(prog.percent);
      if (prog.currentFile) setCurrentFileName(prog.currentFile);
      if (prog.totalBytes > 0) setCurrentFileSize(prog.totalBytes);
      const now = Date.now();
      const dt = (now - lastTimeRef.current) / 1000;
      if (dt >= 0.5) {
        const db = prog.bytesTransferred - lastBytesRef.current;
        setTransferSpeed(db / dt > 0 ? db / dt : 0);
        lastBytesRef.current = prog.bytesTransferred;
        lastTimeRef.current = now;
      }
    });

    // Complete events
    const unsubComplete = teleportService.onComplete(async code => {
      const ok = code === 0;
      if (ok) {
        setStatusMessage('sent');
        setProgress(100);
        setIsSending(false);
        setShowSuccess(true);
        if (currentFileNameRef.current && selectedDeviceRef.current) {
          const rec: TransferRecord = {
            id: generateTransferId(),
            type: 'sent',
            fileName: currentFileNameRef.current,
            fileSize: 0,
            deviceName: selectedDeviceRef.current.name,
            timestamp: Date.now(),
            status: 'success',
          };
          const updated = await addToHistory(rec);
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

    // Incoming files
    const unsubIncoming = teleportService.onIncomingFiles(info => {
      setIncomingSender(info.senderName);
      setIncomingFileCount(info.fileCount);
      setIncomingTotalSize(info.totalSize);
      if (settingsRef.current.autoAccept) {
        teleportService.acceptIncomingFiles();
        setStatusMessage('receiving');
      } else {
        setShowIncoming(true);
      }
    });

    // File received
    const unsubReceived = teleportService.onFileReceived(async info => {
      const rec: TransferRecord = {
        id: generateTransferId(),
        type: 'received',
        fileName: info.fileName,
        fileSize: info.fileSize,
        deviceName: info.senderName,
        timestamp: info.timestamp || Date.now(),
        status: 'success',
      };
      const updated = await addToHistory(rec);
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
  }, [settingsLoaded, doHaptic]);

  // ── WebRTC Service ──────────────────────────────────────────────────────────
  useEffect(() => {
    if (!settingsLoaded) return;
    const name = settings.deviceName || 'TeleportMobile';
    const svc = new WebRTCService(name);
    webRTCRef.current = svc;
    svc.onConnected = () => setWebRtcConnected(true);
    svc.onDisconnected = () => setWebRtcConnected(false);
    svc.onPeersUpdated = peers => {
      setWebPeers(prev => {
        const m = new Map(prev.map(p => [p.id, p]));
        for (const p of peers) m.set(p.id, p);
        return Array.from(m.values());
      });
    };
    svc.onIncomingFileRequest = (from, fromName, files) => {
      setWebIncomingFrom(from);
      setWebIncomingName(fromName);
      setWebIncomingFiles(files);
      if (settingsRef.current.autoAccept) {
        svc.acceptIncomingTransfer(from);
      } else {
        setShowWebIncoming(true);
        Vibration.vibrate([0, 100, 50, 100]);
      }
    };
    svc.onProgress = info => {
      setWebProgress(info.percent);
      setWebProgressFile(info.filename);
      if (info.direction === 'send') setWebIsSending(info.percent < 100);
    };
    svc.onTransferComplete = result => {
      setWebIsSending(false);
      setWebProgress(0);
      if (result.success) {
        setShowSuccess(true);
        doHaptic('medium');
        addToHistory({
          id: generateTransferId(),
          type: result.savedPath ? 'received' : 'sent',
          fileName: result.filename,
          fileSize: 0,
          deviceName: webPeers.find(p => p.id === result.peerId)?.name ?? result.peerId,
          timestamp: Date.now(),
          status: 'success',
        }).then(setTransferHistory);
      }
    };
    svc.onTransferError = result => {
      setWebIsSending(false);
      setWebProgress(0);
      if (result.error !== 'Transfer rejected by peer') {
        Alert.alert('WebRTC Transfer Failed', result.error ?? 'Unknown error');
      }
    };
    svc.start();
    return () => {
      svc.stop();
      webRTCRef.current = null;
      setWebRtcConnected(false);
      setWebPeers([]);
    };
  }, [settingsLoaded]); // eslint-disable-line react-hooks/exhaustive-deps

  // ── Network state ──────────────────────────────────────────────────────────
  useEffect(() => {
    isOnline().then(setIsOnlineState);
    const unsub = onNetworkChange(connected => {
      setIsOnlineState(connected);
      if (!connected) setStatusMessage('offline');
      else if (statusMessage === 'offline') setStatusMessage('');
    });
    return unsub;
  }, [statusMessage]);

  // ── App backgrounding ──────────────────────────────────────────────────────
  const appState = useRef(AppState.currentState);
  useEffect(() => {
    const sub = AppState.addEventListener('change', (next: AppStateStatus) => {
      if (appState.current.match(/active/) && next.match(/inactive|background/)) {
        if (isDiscovering) teleportService.stopDiscovery();
      } else if (appState.current.match(/inactive|background/) && next === 'active') {
        if (isDiscovering && initialized) teleportService.startDiscovery();
      }
      appState.current = next;
    });
    return () => sub?.remove();
  }, [isDiscovering, initialized]);

  // ── Device discovery callbacks ─────────────────────────────────────────────
  useEffect(() => {
    if (!initialized) return;
    const unsubD = teleportService.onDeviceDiscovered(device => {
      setDevices(prev => {
        if (prev.some(d => d.id === device.id)) {
          return prev.map(d => d.id === device.id ? device : d);
        }
        doHaptic('light');
        return [...prev, device];
      });
    });
    const unsubL = teleportService.onDeviceLost(id => {
      setDevices(prev => prev.filter(d => d.id !== id));
    });
    return () => { unsubD(); unsubL(); };
  }, [initialized, doHaptic]);

  // ── Discovery toggle ───────────────────────────────────────────────────────
  const toggleDiscovery = useCallback(async () => {
    doHaptic('medium');
    if (!isOnlineState) { Alert.alert('Offline', 'Cannot discover devices while offline'); return; }
    if (isDiscovering) {
      await teleportService.stopDiscovery();
      setIsDiscovering(false);
      setStatusMessage('');
    } else {
      try {
        const ok = await withTimeout(teleportService.startDiscovery(), 30000, 'Discovery timed out');
        setIsDiscovering(!!ok);
        setStatusMessage('scanning');
      } catch (e) {
        Alert.alert('Discovery Failed', e instanceof Error ? e.message : 'Unknown error');
      }
    }
  }, [isDiscovering, isOnlineState, doHaptic]);

  // ── Receive toggle ─────────────────────────────────────────────────────────
  const toggleReceiving = useCallback(async () => {
    doHaptic('medium');
    if (isReceiving) {
      await teleportService.stopReceiving();
      setIsReceiving(false);
      setStatusMessage('');
    } else {
      const safePath = validateDownloadPath(settings.downloadPath);
      try {
        const ok = await withTimeout(teleportService.startReceiving(safePath), 30000, 'Timed out');
        setIsReceiving(!!ok);
        if (ok) setStatusMessage('listening');
      } catch (e) {
        Alert.alert('Receive Failed', e instanceof Error ? e.message : 'Unknown error');
      }
    }
  }, [isReceiving, settings.downloadPath, doHaptic]);

  // ── Select files and send (LAN native) ────────────────────────────────────
  const selectAndSendFiles = useCallback(async (targetDevice?: TeleportDevice) => {
    const target = targetDevice || selectedDevice;
    if (!target) return;
    doHaptic('medium');
    if (!isOnlineState) { Alert.alert('Offline', 'Cannot send files while offline'); return; }
    try {
      const results = await DocumentPicker.pick({ allowMultiSelection: true, type: [DocumentPicker.types.allFiles] });
      if (results?.length > 0) {
        const filePaths = results.map(f => f.uri);
        const fileName = results[0].name || 'file';
        const fileSize = results[0].size || 0;
        setIsSending(true);
        setStatusMessage('sending');
        setProgress(0);
        setCurrentFileName(fileName);
        try {
          const started = await withRetry(
            () => withTimeout(teleportService.sendFiles(target.id, filePaths), 30000, 'Timed out'),
            2,
            (attempt) => setStatusMessage(`retrying (${attempt}/3)…`)
          );
          if (!started) {
            setStatusMessage('failed');
            Alert.alert('Transfer Failed', 'Could not connect to device');
            const rec: TransferRecord = { id: generateTransferId(), type: 'sent', fileName, fileSize, deviceName: target.name, timestamp: Date.now(), status: 'failed' };
            setTransferHistory(await addToHistory(rec));
            setIsSending(false); setProgress(0); setCurrentFileName('');
          }
        } catch (e) {
          setStatusMessage('failed');
          Alert.alert('Transfer Failed', e instanceof Error ? e.message : 'Unknown error');
          const rec: TransferRecord = { id: generateTransferId(), type: 'sent', fileName, fileSize, deviceName: target.name, timestamp: Date.now(), status: 'failed' };
          setTransferHistory(await addToHistory(rec));
          setIsSending(false); setProgress(0); setCurrentFileName('');
        }
      }
    } catch (err: any) {
      if (!DocumentPicker.isCancel(err)) Alert.alert('Error', 'Could not select files');
      setIsSending(false); setStatusMessage(''); setProgress(0); setCurrentFileName('');
    }
  }, [selectedDevice, isOnlineState, doHaptic]);

  // ── Select files and send (WebRTC) ────────────────────────────────────────
  const selectAndSendWebRTC = useCallback(async () => {
    if (!selectedWebPeer || !webRTCRef.current) return;
    doHaptic('medium');
    try {
      const results = await DocumentPicker.pick({ allowMultiSelection: true, type: [DocumentPicker.types.allFiles] });
      if (results?.length > 0) {
        const files = results.map(f => ({ uri: f.uri, name: f.name || 'file', size: f.size || 0, type: f.type || undefined }));
        setWebIsSending(true);
        await webRTCRef.current.sendFiles(selectedWebPeer.id, files);
      }
    } catch (e: any) {
      if (!DocumentPicker.isCancel(e)) Alert.alert('Error', 'Could not select files');
    }
  }, [selectedWebPeer, doHaptic]);

  // ── Cancel transfer ────────────────────────────────────────────────────────
  const handleCancelTransfer = useCallback(async () => {
    doHaptic('medium');
    await teleportService.cancelTransfer();
    setIsSending(false); setProgress(0); setTransferSpeed(0);
    setCurrentFileSize(0); setCurrentFileName(''); setStatusMessage('cancelled');
    if (currentFileNameRef.current && selectedDeviceRef.current) {
      const rec: TransferRecord = { id: generateTransferId(), type: 'sent', fileName: currentFileNameRef.current, fileSize: 0, deviceName: selectedDeviceRef.current.name, timestamp: Date.now(), status: 'failed' };
      setTransferHistory(await addToHistory(rec));
    }
  }, [doHaptic]);

  // ── Tab change ─────────────────────────────────────────────────────────────
  const handleTabChange = (tab: Tab) => {
    doHaptic('light');
    setActiveTab(tab);
    if (tab === 'send' && !isDiscovering && initialized) {
      teleportService.startDiscovery().then(ok => setIsDiscovering(!!ok));
    }
  };

  // ── Handle mode change ─────────────────────────────────────────────────────
  const handleModeChange = (mode: TransferMode) => {
    setTransferMode(mode);
    setSelectedDevice(null);
    setSelectedWebPeer(null);
    if (mode === 'qr') setShowQr(true);
  };

  // ── Manual IP connect (for Hotspot/QR results) ────────────────────────────
  const connectViaManualIp = useCallback((ip: string, port: number, name: string) => {
    const virtualDevice: TeleportDevice = {
      id: `manual_${ip}_${port}`,
      name,
      ip,
      port,
      os: 'unknown',
    };
    setSelectedDevice(virtualDevice);
    setActiveTab('send');
  }, []);

  // ── Render helpers ─────────────────────────────────────────────────────────

  const renderHistoryItem = ({ item }: { item: TransferRecord }) => (
    <View style={styles.historyItem}>
      <View style={[styles.historyDot, { backgroundColor: item.status === 'success' ? '#4ade80' : '#ef4444' }]} />
      <View style={styles.historyContent}>
        <Text style={styles.historyFileName} numberOfLines={1}>{item.fileName}</Text>
        <Text style={styles.historyMeta}>
          {item.type === 'sent' ? '↑' : '↓'} {item.deviceName.toLowerCase()} • {formatSize(item.fileSize)} • {formatTime(item.timestamp)}
        </Text>
      </View>
    </View>
  );

  const AnimatedTabContent = ({ children, visible }: { children: React.ReactNode; visible: boolean }) => {
    const opacity = useRef(new Animated.Value(0)).current;
    useEffect(() => {
      Animated.timing(opacity, { toValue: visible ? 1 : 0, duration: 180, useNativeDriver: true }).start();
    }, [visible]);
    if (!visible) return null;
    return <Animated.View style={[styles.tabContent, { opacity }]}>{children}</Animated.View>;
  };

  const modeCfg = MODE_CONFIG[transferMode]!;

  // ── The selected sending target (LAN or WebRTC) ───────────────────────────
  const hasSelectedTarget = selectedDevice !== null || selectedWebPeer !== null;
  const targetName = selectedDevice?.name || selectedWebPeer?.name || '';
  const targetMode = selectedWebPeer ? 'webrtc' : transferMode;

  // ============================================================================
  // RENDER
  // ============================================================================
  return (
    <SafeAreaView style={styles.container}>

      {/* ── Header ── */}
      <View style={styles.header}>
        <View style={styles.headerRow}>
          <DotText text="teleport" size={5} />
          <View style={styles.headerRight}>
            {/* Mode indicator dot */}
            <View style={[styles.modeDot, { backgroundColor: modeCfg.color }]} />
            <TouchableOpacity onPress={() => { doHaptic('light'); setShowSettings(true); }}>
              <Text style={styles.settingsIcon}>⚙</Text>
            </TouchableOpacity>
          </View>
        </View>
        <Text style={styles.status}>
          {statusMessage || (initialized ? `ready · ${modeCfg.label.toLowerCase()}` : 'loading…')}
        </Text>
      </View>

      {/* ── 3-Tab bar ── */}
      <View style={styles.tabsContainer}>
        <Animated.View
          style={[styles.tabIndicator, { width: tabWidth, transform: [{ translateX: tabIndicatorX }] }]}
        />
        {(['send', 'receive', 'history'] as Tab[]).map(tab => (
          <TouchableOpacity
            key={tab}
            style={styles.tab}
            onPress={() => handleTabChange(tab)}
            activeOpacity={0.6}>
            <Text style={[styles.tabText, activeTab === tab && styles.tabTextActive]}>{tab}</Text>
          </TouchableOpacity>
        ))}
      </View>

      {/* ════════════════════════════════════════════════════════════════════ */}
      {/* SEND TAB                                                           */}
      {/* ════════════════════════════════════════════════════════════════════ */}
      <AnimatedTabContent visible={activeTab === 'send'}>
        {hasSelectedTarget ? (
          /* ── Device selected: show send UI ── */
          <>
            <View style={styles.targetInfo}>
              <Text style={styles.targetLabel}>sending to</Text>
              <DotText text={targetName.toLowerCase().slice(0, 12)} size={5} />
              <Text style={[styles.modeDesc, { color: modeCfg.color, marginTop: 4 }]}>
                via {modeCfg.label}
              </Text>
            </View>

            {/* LAN send */}
            {selectedDevice && (isSending ? (
              <ProgressBar
                progress={progress}
                fileName={currentFileName}
                speed={transferSpeed}
                totalSize={currentFileSize}
                onCancel={handleCancelTransfer}
              />
            ) : (
              <>
                <AnimatedButton onPress={() => selectAndSendFiles()} color={modeCfg.color}>
                  <Text style={[styles.tabText, { color: modeCfg.color }]}>select files</Text>
                </AnimatedButton>
                <TouchableOpacity
                  style={styles.clearButton}
                  onPress={() => { doHaptic('light'); setSelectedDevice(null); setSelectedWebPeer(null); }}>
                  <Text style={styles.clearText}>change device</Text>
                </TouchableOpacity>
              </>
            ))}

            {/* WebRTC send */}
            {selectedWebPeer && (webIsSending ? (
              <ProgressBar progress={webProgress} fileName={webProgressFile} />
            ) : (
              <>
                <AnimatedButton onPress={selectAndSendWebRTC} color="#f472b6">
                  <Text style={[styles.tabText, { color: '#f472b6' }]}>select files</Text>
                </AnimatedButton>
                <TouchableOpacity
                  style={styles.clearButton}
                  onPress={() => { doHaptic('light'); setSelectedWebPeer(null); }}>
                  <Text style={styles.clearText}>change device</Text>
                </TouchableOpacity>
              </>
            ))}
          </>
        ) : (
          /* ── No device selected: show mode picker + device list ── */
          <>
            {/* Transfer mode selector */}
            <ModePicker current={transferMode} onChange={handleModeChange} />

            <View style={{ flex: 1 }}>

              {/* LAN mode */}
              {transferMode === 'lan' && (
                <View style={{ flex: 1 }}>
                  {devices.length === 0 && webPeers.length === 0 ? (
                    <View style={styles.emptyState}>
                      {isDiscovering ? (
                        <View style={styles.scanningIndicator}>
                          <PulseDot color="#4ade80" delay={0} />
                          <PulseDot color="#4ade80" delay={200} />
                          <PulseDot color="#4ade80" delay={400} />
                        </View>
                      ) : (
                        <TouchableOpacity onPress={toggleDiscovery}>
                          <Text style={styles.emptyText}>tap to scan for devices</Text>
                        </TouchableOpacity>
                      )}
                    </View>
                  ) : (
                    <ScrollView showsVerticalScrollIndicator={false}>
                      {devices.length > 0 && (
                        <>
                          <Text style={styles.sectionLabel}>lan devices</Text>
                          {devices.map(item => (
                            <TouchableOpacity
                              key={item.id}
                              style={styles.deviceItem}
                              onPress={() => { doHaptic('medium'); setSelectedDevice(item); }}>
                              <View style={[styles.deviceDot, { backgroundColor: '#4ade80' }]} />
                              <View style={styles.deviceContent}>
                                <Text style={styles.deviceName}>{item.name.toLowerCase()}</Text>
                                <Text style={styles.deviceInfo}>{item.os.toLowerCase()}</Text>
                              </View>
                            </TouchableOpacity>
                          ))}
                        </>
                      )}
                    </ScrollView>
                  )}
                  {!isDiscovering && devices.length === 0 && (
                    <AnimatedButton onPress={toggleDiscovery} color="#4ade80">
                      <Text style={[styles.tabText, { color: '#4ade80' }]}>discover devices</Text>
                    </AnimatedButton>
                  )}
                  {isDiscovering && (
                    <TouchableOpacity
                      style={styles.clearButton}
                      onPress={toggleDiscovery}>
                      <Text style={styles.clearText}>stop scanning</Text>
                    </TouchableOpacity>
                  )}
                </View>
              )}

              {/* WiFi Direct mode */}
              {transferMode === 'wifidirect' && (
                <WiFiDirectModeView
                  onDeviceSelected={(ip, port, name) => connectViaManualIp(ip, port, name)}
                />
              )}

              {/* Hotspot mode */}
              {transferMode === 'hotspot' && (
                <HotspotModeView
                  onDeviceReady={(ip, port, name) => connectViaManualIp(ip, port, name)}
                />
              )}

              {/* QR Code mode */}
              {transferMode === 'qr' && (
                <View style={{ flex: 1, alignItems: 'center', justifyContent: 'center' }}>
                  <Text style={[styles.modeDesc, { textAlign: 'center', fontSize: 14, marginBottom: 24 }]}>
                    Share your IP address for others to connect, or enter the remote device's IP to connect.
                  </Text>
                  <AnimatedButton onPress={() => setShowQr(true)} color="#a78bfa">
                    <Text style={[styles.tabText, { color: '#a78bfa' }]}>open connection panel</Text>
                  </AnimatedButton>
                </View>
              )}

              {/* WebRTC mode */}
              {transferMode === 'webrtc' && (
                <>
                  {webPeers.length === 0 ? (
                    <View style={styles.emptyState}>
                      <PulseDot color={webRtcConnected ? '#f472b6' : '#333'} />
                      <Text style={[styles.emptyText, { marginTop: 16 }]}>
                        {webRtcConnected ? 'looking for web peers…' : 'connecting to signaling server…'}
                      </Text>
                      <Text style={[styles.modeDesc, { marginTop: 8, textAlign: 'center' }]}>
                        Other Teleport users on any network will appear here.
                      </Text>
                    </View>
                  ) : (
                    <ScrollView showsVerticalScrollIndicator={false}>
                      <Text style={styles.sectionLabel}>
                        {'web peers ' + (webRtcConnected ? '●' : '○')}
                      </Text>
                      {webPeers.map(peer => (
                        <TouchableOpacity
                          key={peer.id}
                          style={[styles.deviceItem, { borderLeftWidth: 2, borderLeftColor: '#f472b6' }]}
                          onPress={() => { doHaptic('medium'); setSelectedWebPeer(peer); }}
                          activeOpacity={0.7}>
                          <View style={[styles.deviceDot, { backgroundColor: '#f472b6' }]} />
                          <View style={styles.deviceContent}>
                            <Text style={styles.deviceName}>{peer.name.toLowerCase()}</Text>
                            <Text style={styles.deviceInfo}>{peer.clientType} · webrtc</Text>
                          </View>
                        </TouchableOpacity>
                      ))}
                    </ScrollView>
                  )}
                </>
              )}
            </View>
          </>
        )}
      </AnimatedTabContent>

      {/* ════════════════════════════════════════════════════════════════════ */}
      {/* RECEIVE TAB                                                        */}
      {/* ════════════════════════════════════════════════════════════════════ */}
      <AnimatedTabContent visible={activeTab === 'receive'}>
        <AnimatedButton onPress={toggleReceiving} active={isReceiving}>
          <DotText text={isReceiving ? 'stop' : 'receive'} size={4} />
        </AnimatedButton>

        <View style={styles.receiveInfo}>
          {isReceiving ? (
            <View style={styles.listeningIndicator}>
              <PulseDot color="#4ade80" delay={0} />
              <Text style={styles.listeningText}>waiting for files</Text>
            </View>
          ) : (
            <Text style={styles.receiveText}>tap to accept incoming files</Text>
          )}
        </View>

        {/* WebRTC receive indicator */}
        {webRtcConnected && (
          <View style={[styles.hotspotCard, { marginTop: 16 }]}>
            <View style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
              <PulseDot color="#f472b6" />
              <Text style={[styles.sectionLabel, { color: '#f472b6', marginBottom: 0 }]}>
                webrtc active — ready for internet transfers
              </Text>
            </View>
          </View>
        )}
      </AnimatedTabContent>

      {/* ════════════════════════════════════════════════════════════════════ */}
      {/* HISTORY TAB                                                        */}
      {/* ════════════════════════════════════════════════════════════════════ */}
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
                  { text: 'Clear', style: 'destructive', onPress: async () => {
                    await clearHistory();
                    setTransferHistory([]);
                  }},
                ]);
              }}>
                <Text style={styles.clearHistoryText}>clear</Text>
              </TouchableOpacity>
            </View>
            <FlatList
              data={transferHistory}
              renderItem={renderHistoryItem}
              keyExtractor={item => item.id}
              style={styles.historyList}
              contentContainerStyle={styles.historyListContent}
              showsVerticalScrollIndicator={false}
              initialNumToRender={10}
              maxToRenderPerBatch={10}
              windowSize={5}
              removeClippedSubviews
              getItemLayout={(_, i) => ({ length: 60, offset: 60 * i, index: i })}
            />
          </>
        )}
      </AnimatedTabContent>

      {/* ── Modals ── */}
      <SettingsModal
        visible={showSettings}
        onClose={() => setShowSettings(false)}
        settings={settings}
        onSave={handleSaveSettings}
      />
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
      <IncomingFilesModal
        visible={showIncoming}
        senderName={incomingSender}
        fileCount={incomingFileCount}
        totalSize={incomingTotalSize}
        mode="lan"
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
      {/* WebRTC incoming */}
      <IncomingFilesModal
        visible={showWebIncoming}
        senderName={webIncomingName}
        fileCount={webIncomingFiles.length}
        totalSize={webIncomingFiles.reduce((s, f) => s + f.size, 0)}
        mode="webrtc"
        onAccept={() => {
          setShowWebIncoming(false);
          webRTCRef.current?.acceptIncomingTransfer(webIncomingFrom);
        }}
        onReject={() => {
          setShowWebIncoming(false);
          webRTCRef.current?.rejectIncomingTransfer(webIncomingFrom);
        }}
      />
      {/* QR Code modal */}
      <QrCodeModal
        visible={showQr}
        onClose={() => setShowQr(false)}
        settings={settings}
      />
    </SafeAreaView>
  );
};

// ============================================================================
// STYLES
// ============================================================================
const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: '#000' },
  header: { paddingHorizontal: 24, paddingTop: 50, paddingBottom: 16 },
  headerRow: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center' },
  headerRight: { flexDirection: 'row', alignItems: 'center', gap: 12 },
  modeDot: { width: 6, height: 6, borderRadius: 3 },
  settingsIcon: { fontSize: 20, color: '#444' },
  status: { fontSize: 12, color: '#444', marginTop: 6, letterSpacing: 2 },

  tabsContainer: { flexDirection: 'row', marginHorizontal: 24, marginBottom: 16, position: 'relative' },
  tabIndicator: { position: 'absolute', bottom: 0, height: 1, backgroundColor: '#fff' },
  tab: { flex: 1, paddingVertical: 10, alignItems: 'center' },
  tabText: { color: '#333', fontSize: 12, letterSpacing: 1, textTransform: 'lowercase' },
  tabTextActive: { color: '#fff' },
  tabContent: { flex: 1, paddingHorizontal: 24 },

  /* Mode picker */
  modePickerScroll: { maxHeight: 56, marginBottom: 16 },
  modePickerContent: { gap: 8, paddingRight: 8 },
  modeChip: {
    flexDirection: 'row', alignItems: 'center', gap: 6,
    paddingHorizontal: 14, paddingVertical: 8, borderRadius: 20,
    borderWidth: 1, borderColor: '#1a1a1a', backgroundColor: '#080808',
  },
  modeChipIcon: { fontSize: 14 },
  modeChipLabel: { color: '#444', fontSize: 12, letterSpacing: 0.5 },

  mainButton: {
    backgroundColor: '#0a0a0a', paddingVertical: 20, borderRadius: 2,
    alignItems: 'center', justifyContent: 'center', borderWidth: 1, borderColor: '#1a1a1a',
    marginBottom: 12,
  },
  buttonActive: { borderColor: '#333' },
  buttonDisabled: { opacity: 0.5 },

  deviceItem: {
    flexDirection: 'row', alignItems: 'center',
    backgroundColor: '#050505', paddingVertical: 16, paddingHorizontal: 16,
    borderRadius: 2, borderWidth: 1, borderColor: '#111', marginBottom: 10,
  },
  deviceSelected: { borderColor: '#333' },
  deviceDot: { width: 8, height: 8, borderRadius: 4, marginRight: 14 },
  deviceContent: { flex: 1 },
  deviceName: { color: '#fff', fontSize: 15, letterSpacing: 0.5 },
  deviceInfo: { color: '#444', fontSize: 11, marginTop: 3, letterSpacing: 0.5 },

  emptyState: { flex: 1, justifyContent: 'center', alignItems: 'center' },
  emptyText: { color: '#222', fontSize: 13, letterSpacing: 1 },

  sectionLabel: { color: '#333', fontSize: 11, letterSpacing: 2, marginBottom: 12, textTransform: 'lowercase' },
  modeDesc: { color: '#333', fontSize: 12, letterSpacing: 0.5, lineHeight: 18, marginBottom: 12 },

  scanningIndicator: { flexDirection: 'row', alignItems: 'center', gap: 6, marginBottom: 12 },
  pulseDot: { width: 8, height: 8, borderRadius: 4 },

  targetInfo: { marginBottom: 24 },
  targetLabel: { color: '#444', fontSize: 11, letterSpacing: 3, marginBottom: 6 },
  clearButton: { marginTop: 8, alignItems: 'center', paddingVertical: 14 },
  clearText: { color: '#333', fontSize: 13, letterSpacing: 2 },

  progressBarContainer: { paddingVertical: 16 },
  progressFileName: { color: '#666', fontSize: 12, letterSpacing: 0.5, marginBottom: 12 },
  progressBarTrack: { height: 2, backgroundColor: '#1a1a1a', borderRadius: 1, overflow: 'hidden' },
  progressBarFill: { height: '100%', backgroundColor: '#fff' },
  progressPercent: { color: '#fff', fontSize: 18, fontWeight: '200', letterSpacing: 2 },
  progressHeader: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center', marginBottom: 8 },
  progressStats: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center', marginTop: 8 },
  progressSize: { color: '#666', fontSize: 12, letterSpacing: 1 },
  progressSpeed: { color: '#4ade80', fontSize: 12, letterSpacing: 1, fontWeight: '500' },
  cancelButton: { padding: 4 },
  cancelButtonText: { color: '#ef4444', fontSize: 16, fontWeight: '600' },

  receiveInfo: { flex: 1, justifyContent: 'center', alignItems: 'center' },
  receiveText: { color: '#222', fontSize: 13, letterSpacing: 1 },
  listeningIndicator: { flexDirection: 'row', alignItems: 'center', gap: 12 },
  listeningText: { color: '#444', fontSize: 13, letterSpacing: 1 },

  historyHeader: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center', marginBottom: 16 },
  historyCount: { color: '#666', fontSize: 12, letterSpacing: 1 },
  clearHistoryText: { color: '#ef4444', fontSize: 12, letterSpacing: 1 },
  historyList: { flex: 1 },
  historyListContent: { paddingBottom: 24 },
  historyItem: { flexDirection: 'row', alignItems: 'center', paddingVertical: 14, borderBottomWidth: 1, borderBottomColor: '#111' },
  historyDot: { width: 6, height: 6, borderRadius: 3, marginRight: 14 },
  historyContent: { flex: 1 },
  historyFileName: { color: '#fff', fontSize: 14, letterSpacing: 0.5 },
  historyMeta: { color: '#444', fontSize: 11, marginTop: 3, letterSpacing: 0.5 },

  modalOverlay: { flex: 1, backgroundColor: 'rgba(0,0,0,0.9)', justifyContent: 'flex-end' },
  modalContent: { backgroundColor: '#0a0a0a', borderTopLeftRadius: 4, borderTopRightRadius: 4, padding: 24, paddingBottom: 40 },
  modalTitle: { color: '#fff', fontSize: 18, letterSpacing: 2, marginBottom: 24 },
  settingItem: { flexDirection: 'row', justifyContent: 'space-between', paddingVertical: 14, borderBottomWidth: 1, borderBottomColor: '#1a1a1a' },
  settingLabel: { color: '#666', fontSize: 13, letterSpacing: 1 },
  settingValue: { color: '#fff', fontSize: 13, letterSpacing: 0.5, maxWidth: '50%' },
  settingInput: { color: '#fff', fontSize: 13, letterSpacing: 0.5, textAlign: 'right', minWidth: 120, paddingVertical: 4, paddingHorizontal: 8, backgroundColor: '#1a1a1a', borderRadius: 4 },
  settingItemRow: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center', paddingVertical: 14, borderBottomWidth: 1, borderBottomColor: '#1a1a1a' },
  settingsButtonRow: { flexDirection: 'row', gap: 12, marginTop: 24 },
  saveButton: { flex: 1, backgroundColor: '#4ade80' },
  saveButtonText: { color: '#000', fontSize: 14, letterSpacing: 2, fontWeight: '600' },
  cancelButton2: { flex: 1, backgroundColor: '#333' },
  modalCloseButton: { flex: 1, alignItems: 'center', paddingVertical: 16, backgroundColor: '#111', borderRadius: 2 },
  modalCloseText: { color: '#fff', fontSize: 14, letterSpacing: 2 },

  successOverlay: { ...StyleSheet.absoluteFillObject, backgroundColor: 'rgba(0,0,0,0.95)', justifyContent: 'center', alignItems: 'center', zIndex: 1000 },
  successRing: { position: 'absolute', width: 80, height: 80, borderRadius: 40, borderWidth: 2, borderColor: '#4ade80' },
  successCircle: { width: 80, height: 80, borderRadius: 40, backgroundColor: '#4ade80', justifyContent: 'center', alignItems: 'center', marginBottom: 24 },
  successCheck: { fontSize: 40, color: '#000', fontWeight: '200' },
  successText: { fontSize: 24, color: '#fff', letterSpacing: 4, fontWeight: '200' },

  incomingOverlay: { flex: 1, backgroundColor: 'rgba(0,0,0,0.9)', justifyContent: 'center', alignItems: 'center', padding: 24 },
  incomingCard: { backgroundColor: '#0a0a0a', borderRadius: 4, padding: 28, width: '100%', maxWidth: 320, borderWidth: 1, borderColor: '#1a1a1a' },
  incomingTitle: { color: '#fff', fontSize: 18, letterSpacing: 2, marginBottom: 4 },
  incomingMode: { color: '#444', fontSize: 11, letterSpacing: 1, marginBottom: 16 },
  incomingSender: { flexDirection: 'row', alignItems: 'center', marginBottom: 8 },
  incomingDot: { width: 8, height: 8, borderRadius: 4, backgroundColor: '#4ade80', marginRight: 10 },
  incomingSenderName: { color: '#fff', fontSize: 16, letterSpacing: 0.5 },
  incomingInfo: { color: '#666', fontSize: 13, letterSpacing: 0.5, marginBottom: 24, marginLeft: 18 },
  incomingButtons: { flexDirection: 'row', gap: 12 },
  rejectButton: { flex: 1, backgroundColor: '#1a1a1a', paddingVertical: 14, borderRadius: 2, alignItems: 'center' },
  rejectText: { color: '#888', fontSize: 14, letterSpacing: 1 },
  acceptButton: { flex: 1, backgroundColor: '#4ade80', paddingVertical: 14, borderRadius: 2, alignItems: 'center' },
  acceptText: { color: '#000', fontSize: 14, letterSpacing: 1, fontWeight: '600' },

  /* Hotspot card */
  hotspotCard: { backgroundColor: '#080808', borderWidth: 1, borderColor: '#1a1a1a', borderRadius: 2, padding: 16, marginBottom: 16 },
  hotspotActiveRow: { flexDirection: 'row', alignItems: 'center' },
  hotspotRow: { flexDirection: 'row', justifyContent: 'space-between', paddingVertical: 8, borderBottomWidth: 1, borderBottomColor: '#111' },
  hotspotLabel: { color: '#444', fontSize: 12, letterSpacing: 1 },
  hotspotValue: { color: '#fff', fontSize: 13, letterSpacing: 0.5 },

  /* QR modal */
  qrModeToggle: { flexDirection: 'row', marginBottom: 20, gap: 8 },
  qrModeBtn: { flex: 1, paddingVertical: 10, borderRadius: 2, alignItems: 'center', borderWidth: 1, borderColor: '#1a1a1a', backgroundColor: '#0a0a0a' },
  qrModeBtnActive: { borderColor: '#a78bfa44', backgroundColor: '#a78bfa18' },
  qrModeBtnText: { color: '#444', fontSize: 12, letterSpacing: 1 },
  qrInfoBox: { flexDirection: 'row', justifyContent: 'space-between', paddingVertical: 12, borderBottomWidth: 1, borderBottomColor: '#1a1a1a' },
  qrInfoLabel: { color: '#444', fontSize: 12, letterSpacing: 1 },
  qrInfoValue: { color: '#fff', fontSize: 13, letterSpacing: 0.5 },
  qrHint: { color: '#333', fontSize: 12, letterSpacing: 0.5, lineHeight: 18, marginTop: 16 },

  cancelButton_unused: { flex: 1, backgroundColor: '#333' },
  cancelButtonSettings: { flex: 1, backgroundColor: '#333' },

});

// ============================================================================
// WRAP WITH ERROR BOUNDARY
// ============================================================================
const SafeApp = () => (
  <ErrorBoundary
    onError={(error, info) => reportCrash(error, { componentStack: info.componentStack })}>
    <App />
  </ErrorBoundary>
);

export default SafeApp;
