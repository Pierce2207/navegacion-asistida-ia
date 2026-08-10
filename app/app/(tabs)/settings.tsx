import { ReactNode, useCallback, useEffect, useRef, useState } from 'react';
import {
  ActivityIndicator,
  Alert,
  GestureResponderEvent,
  Modal,
  PanResponder,
  Pressable,
  ScrollView,
  StyleSheet,
  Switch,
  Text,
  TextInput,
  View,
} from 'react-native';

import { getApiBaseUrl } from '@/app/utils/api';
import { COMPANION_DEVICE_ID } from '@/app/utils/device';
import { ScreenHeader } from '@/components/ScreenHeader';
import { HaloColors } from '@/constants/Colors';

type DeviceSettings = {
  device_name: string;
  wake_word: string;
  language: string;
  volume: number;
  danger_sensitivity: string;
  button_press_behavior: string;
  auto_distress_timeout: number;
  notify_companion: boolean;
  location_sharing_enabled: boolean;
  vibration_enabled: boolean;
};

// Mirrors the API's /settings defaults for the fields this screen edits.
const DEFAULT_SETTINGS: DeviceSettings = {
  device_name: '',
  wake_word: 'Hey Sentra',
  language: 'en-US',
  volume: 100,
  danger_sensitivity: 'medium',
  button_press_behavior: 'confirm_safe',
  auto_distress_timeout: 10,
  notify_companion: true,
  location_sharing_enabled: true,
  vibration_enabled: true,
};

type Option<T> = { label: string; value: T };

const LANGUAGE_OPTIONS: Option<string>[] = [
  { label: 'English (US)', value: 'en-US' },
  { label: 'Chinese (Simplified)', value: 'zh-CN' },
];

const SENSITIVITY_OPTIONS: Option<string>[] = [
  { label: 'Low', value: 'low' },
  { label: 'Medium', value: 'medium' },
  { label: 'High', value: 'high' },
];

const BUTTON_BEHAVIOR_OPTIONS: Option<string>[] = [
  { label: 'Send Alert', value: 'send_alert' },
  { label: 'Confirm Safe', value: 'confirm_safe' },
];

const TIMEOUT_OPTIONS: Option<number>[] = [5, 10, 15, 30, 60].map((seconds) => ({
  label: `${seconds} seconds`,
  value: seconds,
}));

const clampVolume = (value: number) => Math.min(100, Math.max(0, value));

function Section({ title, children }: { title: string; children: ReactNode }) {
  return (
    <View style={styles.card}>
      <Text style={styles.cardTitle} accessibilityRole="header">
        {title}
      </Text>
      {children}
    </View>
  );
}

function LabeledInput({
  label,
  value,
  onChangeText,
}: {
  label: string;
  value: string;
  onChangeText: (text: string) => void;
}) {
  return (
    <View style={styles.field}>
      <Text style={styles.fieldLabel}>{label}</Text>
      <TextInput
        style={styles.input}
        value={value}
        onChangeText={onChangeText}
        accessibilityLabel={label}
        autoCapitalize="none"
        autoCorrect={false}
      />
    </View>
  );
}

function Dropdown<T extends string | number>({
  label,
  value,
  options,
  onChange,
}: {
  label: string;
  value: T;
  options: Option<T>[];
  onChange: (value: T) => void;
}) {
  const [open, setOpen] = useState(false);
  const selected = options.find((option) => option.value === value);
  const selectedLabel = selected?.label ?? String(value);

  return (
    <View style={styles.field}>
      <Text style={styles.fieldLabel}>{label}</Text>
      <Pressable
        style={styles.dropdown}
        onPress={() => setOpen(true)}
        accessibilityRole="button"
        accessibilityLabel={label}
        accessibilityValue={{ text: selectedLabel }}>
        <Text style={styles.dropdownValue}>{selectedLabel}</Text>
        <Text style={styles.dropdownCaret}>▾</Text>
      </Pressable>
      <Modal visible={open} transparent animationType="fade" onRequestClose={() => setOpen(false)}>
        <Pressable style={styles.modalBackdrop} onPress={() => setOpen(false)}>
          <View style={styles.modalSheet}>
            <Text style={styles.modalTitle}>{label}</Text>
            {options.map((option) => (
              <Pressable
                key={String(option.value)}
                style={styles.modalOption}
                accessibilityRole="button"
                accessibilityState={{ selected: option.value === value }}
                onPress={() => {
                  onChange(option.value);
                  setOpen(false);
                }}>
                <Text
                  style={[
                    styles.modalOptionText,
                    option.value === value && styles.modalOptionSelected,
                  ]}>
                  {option.label}
                </Text>
              </Pressable>
            ))}
          </View>
        </Pressable>
      </Modal>
    </View>
  );
}

function VolumeSlider({ value, onChange }: { value: number; onChange: (value: number) => void }) {
  const trackWidth = useRef(0);
  const onChangeRef = useRef(onChange);
  onChangeRef.current = onChange;

  const responder = useRef(
    PanResponder.create({
      onStartShouldSetPanResponder: () => true,
      onMoveShouldSetPanResponder: () => true,
      onPanResponderGrant: setFromTouch,
      onPanResponderMove: setFromTouch,
    })
  ).current;

  function setFromTouch(event: GestureResponderEvent) {
    if (trackWidth.current <= 0) {
      return;
    }
    const percent = (event.nativeEvent.locationX / trackWidth.current) * 100;
    onChangeRef.current(clampVolume(Math.round(percent)));
  }

  return (
    <View style={styles.field}>
      <Text style={styles.fieldLabel}>Volume: {value}%</Text>
      <View
        style={styles.sliderTouchArea}
        onLayout={(event) => {
          trackWidth.current = event.nativeEvent.layout.width;
        }}
        accessibilityRole="adjustable"
        accessibilityLabel="Volume"
        accessibilityValue={{ min: 0, max: 100, now: value, text: `${value}%` }}
        accessibilityActions={[{ name: 'increment' }, { name: 'decrement' }]}
        onAccessibilityAction={(event) => {
          if (event.nativeEvent.actionName === 'increment') {
            onChange(clampVolume(value + 10));
          } else if (event.nativeEvent.actionName === 'decrement') {
            onChange(clampVolume(value - 10));
          }
        }}
        {...responder.panHandlers}>
        <View style={styles.sliderTrack} pointerEvents="none">
          <View style={[styles.sliderFill, { width: `${value}%` }]} />
          <View style={[styles.sliderThumb, { left: `${value}%` }]} />
        </View>
      </View>
      <View style={styles.sliderEndpoints} pointerEvents="none">
        <Text style={styles.sliderEndpointText}>0%</Text>
        <Text style={styles.sliderEndpointText}>100%</Text>
      </View>
      <View style={styles.stepperRow}>
        <Pressable
          style={styles.stepperButton}
          onPress={() => onChange(clampVolume(value - 10))}
          accessibilityRole="button"
          accessibilityLabel="Decrease volume by 10 percent">
          <Text style={styles.stepperText}>- 10%</Text>
        </Pressable>
        <Pressable
          style={styles.stepperButton}
          onPress={() => onChange(clampVolume(value + 10))}
          accessibilityRole="button"
          accessibilityLabel="Increase volume by 10 percent">
          <Text style={styles.stepperText}>+ 10%</Text>
        </Pressable>
      </View>
    </View>
  );
}

function ToggleRow({
  label,
  value,
  onChange,
}: {
  label: string;
  value: boolean;
  onChange: (value: boolean) => void;
}) {
  return (
    <View style={styles.toggleRow}>
      <Text style={styles.toggleLabel}>{label}</Text>
      <Switch
        value={value}
        onValueChange={onChange}
        trackColor={{ true: HaloColors.blue }}
        accessibilityLabel={label}
      />
    </View>
  );
}

export default function SettingsScreen() {
  const [settings, setSettings] = useState<DeviceSettings>(DEFAULT_SETTINGS);
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const update = <K extends keyof DeviceSettings>(key: K, value: DeviceSettings[K]) =>
    setSettings((previous) => ({ ...previous, [key]: value }));

  const loadSettings = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const response = await fetch(
        `${getApiBaseUrl()}/settings?device_id=${encodeURIComponent(COMPANION_DEVICE_ID)}`
      );
      if (!response.ok) {
        throw new Error(`Failed to fetch settings (${response.status})`);
      }
      const data = await response.json();
      setSettings({
        device_name: data.device_name ?? DEFAULT_SETTINGS.device_name,
        wake_word: data.wake_word ?? DEFAULT_SETTINGS.wake_word,
        language: data.language ?? DEFAULT_SETTINGS.language,
        volume: data.volume ?? DEFAULT_SETTINGS.volume,
        danger_sensitivity: data.danger_sensitivity ?? DEFAULT_SETTINGS.danger_sensitivity,
        button_press_behavior: data.button_press_behavior ?? DEFAULT_SETTINGS.button_press_behavior,
        auto_distress_timeout: data.auto_distress_timeout ?? DEFAULT_SETTINGS.auto_distress_timeout,
        notify_companion: data.notify_companion ?? DEFAULT_SETTINGS.notify_companion,
        location_sharing_enabled:
          data.location_sharing_enabled ?? DEFAULT_SETTINGS.location_sharing_enabled,
        vibration_enabled: data.vibration_enabled ?? DEFAULT_SETTINGS.vibration_enabled,
      });
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed to load settings');
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    loadSettings();
  }, [loadSettings]);

  const saveSettings = async () => {
    setSaving(true);
    setError(null);
    try {
      const response = await fetch(`${getApiBaseUrl()}/settings`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ device_id: COMPANION_DEVICE_ID, ...settings }),
      });
      if (!response.ok) {
        throw new Error(`Failed to save settings (${response.status})`);
      }
      Alert.alert('Settings Saved', 'Device settings have been updated.');
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed to save settings');
    } finally {
      setSaving(false);
    }
  };

  return (
    <View style={styles.container}>
      <ScreenHeader title="Device Settings" subtitle="Configure Device behavior" />
      <ScrollView contentContainerStyle={styles.scrollContent} keyboardShouldPersistTaps="handled">
        {error ? (
          <View style={styles.errorBanner} accessibilityRole="alert">
            <Text style={styles.errorText}>{error}</Text>
          </View>
        ) : null}
        {loading ? (
          <ActivityIndicator color={HaloColors.blue} style={styles.loader} />
        ) : (
          <>
            <Section title="Device Configuration">
              <LabeledInput
                label="Device Name"
                value={settings.device_name}
                onChangeText={(text) => update('device_name', text)}
              />
              <LabeledInput
                label="Wake Word"
                value={settings.wake_word}
                onChangeText={(text) => update('wake_word', text)}
              />
            </Section>
            <Section title="Language & Speech">
              <Dropdown
                label="Language"
                value={settings.language}
                options={LANGUAGE_OPTIONS}
                onChange={(value) => update('language', value)}
              />
              <VolumeSlider value={settings.volume} onChange={(value) => update('volume', value)} />
            </Section>
            <Section title="Alert Configuration">
              <Dropdown
                label="Danger Sensitivity"
                value={settings.danger_sensitivity}
                options={SENSITIVITY_OPTIONS}
                onChange={(value) => update('danger_sensitivity', value)}
              />
              <Dropdown
                label="Emergency Button Behavior"
                value={settings.button_press_behavior}
                options={BUTTON_BEHAVIOR_OPTIONS}
                onChange={(value) => update('button_press_behavior', value)}
              />
              <Dropdown
                label="Auto Distress Timeout"
                value={settings.auto_distress_timeout}
                options={TIMEOUT_OPTIONS}
                onChange={(value) => update('auto_distress_timeout', value)}
              />
            </Section>
            <Section title="Features">
              <ToggleRow
                label="Notify Companion"
                value={settings.notify_companion}
                onChange={(value) => update('notify_companion', value)}
              />
              <ToggleRow
                label="Location Sharing"
                value={settings.location_sharing_enabled}
                onChange={(value) => update('location_sharing_enabled', value)}
              />
              <ToggleRow
                label="Vibration Feedback"
                value={settings.vibration_enabled}
                onChange={(value) => update('vibration_enabled', value)}
              />
            </Section>
            <Pressable
              style={[styles.button, styles.buttonPrimary, saving && styles.buttonDisabled]}
              onPress={saveSettings}
              disabled={saving}
              accessibilityRole="button"
              accessibilityLabel="Save Settings">
              <Text style={styles.buttonPrimaryText}>
                {saving ? 'Saving…' : 'Save Settings'}
              </Text>
            </Pressable>
            <Pressable
              style={[styles.button, styles.buttonOutlineRed]}
              onPress={() => setSettings(DEFAULT_SETTINGS)}
              accessibilityRole="button"
              accessibilityLabel="Reset to Defaults">
              <Text style={styles.buttonOutlineRedText}>Reset to Defaults</Text>
            </Pressable>
            <Pressable
              style={[styles.button, styles.buttonOutlineBlue]}
              onPress={loadSettings}
              accessibilityRole="button"
              accessibilityLabel="Refresh Settings">
              <Text style={styles.buttonOutlineBlueText}>Refresh Settings</Text>
            </Pressable>
          </>
        )}
      </ScrollView>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: HaloColors.background,
  },
  scrollContent: {
    padding: 16,
    paddingBottom: 32,
  },
  loader: {
    marginTop: 32,
  },
  errorBanner: {
    backgroundColor: '#FDECEA',
    borderColor: HaloColors.red,
    borderWidth: 1,
    borderRadius: 12,
    padding: 12,
    marginBottom: 12,
  },
  errorText: {
    color: HaloColors.red,
    fontSize: 14,
  },
  card: {
    backgroundColor: HaloColors.card,
    borderRadius: 14,
    padding: 16,
    marginBottom: 16,
    shadowColor: '#000',
    shadowOpacity: 0.06,
    shadowRadius: 6,
    shadowOffset: { width: 0, height: 2 },
    elevation: 2,
  },
  cardTitle: {
    fontSize: 17,
    fontWeight: 'bold',
    color: HaloColors.textPrimary,
    marginBottom: 12,
  },
  field: {
    marginBottom: 14,
  },
  fieldLabel: {
    fontSize: 14,
    color: HaloColors.textPrimary,
    marginBottom: 6,
  },
  input: {
    borderWidth: 1,
    borderColor: HaloColors.border,
    borderRadius: 8,
    paddingHorizontal: 12,
    paddingVertical: 10,
    fontSize: 15,
    color: HaloColors.textPrimary,
    backgroundColor: HaloColors.card,
  },
  dropdown: {
    borderWidth: 1,
    borderColor: HaloColors.border,
    borderRadius: 8,
    paddingHorizontal: 12,
    paddingVertical: 12,
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
  },
  dropdownValue: {
    fontSize: 15,
    color: HaloColors.textPrimary,
  },
  dropdownCaret: {
    fontSize: 14,
    color: HaloColors.textSecondary,
  },
  modalBackdrop: {
    flex: 1,
    backgroundColor: 'rgba(0, 0, 0, 0.4)',
    justifyContent: 'center',
    padding: 32,
  },
  modalSheet: {
    backgroundColor: HaloColors.card,
    borderRadius: 14,
    paddingVertical: 8,
  },
  modalTitle: {
    fontSize: 16,
    fontWeight: 'bold',
    color: HaloColors.textPrimary,
    paddingHorizontal: 16,
    paddingVertical: 12,
  },
  modalOption: {
    paddingHorizontal: 16,
    paddingVertical: 14,
  },
  modalOptionText: {
    fontSize: 15,
    color: HaloColors.textPrimary,
  },
  modalOptionSelected: {
    color: HaloColors.blue,
    fontWeight: 'bold',
  },
  sliderTouchArea: {
    height: 36,
    justifyContent: 'center',
  },
  sliderTrack: {
    height: 4,
    borderRadius: 2,
    backgroundColor: HaloColors.border,
  },
  sliderFill: {
    height: 4,
    borderRadius: 2,
    backgroundColor: HaloColors.blue,
  },
  sliderThumb: {
    position: 'absolute',
    top: -7,
    width: 18,
    height: 18,
    marginLeft: -9,
    borderRadius: 9,
    backgroundColor: HaloColors.blue,
  },
  sliderEndpoints: {
    flexDirection: 'row',
    justifyContent: 'space-between',
  },
  sliderEndpointText: {
    fontSize: 12,
    color: HaloColors.textSecondary,
  },
  stepperRow: {
    flexDirection: 'row',
    gap: 12,
    marginTop: 10,
  },
  stepperButton: {
    flex: 1,
    backgroundColor: HaloColors.background,
    borderRadius: 8,
    paddingVertical: 10,
    alignItems: 'center',
  },
  stepperText: {
    color: HaloColors.blue,
    fontSize: 14,
    fontWeight: '600',
  },
  toggleRow: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    paddingVertical: 8,
  },
  toggleLabel: {
    fontSize: 15,
    color: HaloColors.textPrimary,
  },
  button: {
    borderRadius: 10,
    paddingVertical: 14,
    alignItems: 'center',
    marginBottom: 12,
  },
  buttonPrimary: {
    backgroundColor: HaloColors.blue,
  },
  buttonPrimaryText: {
    color: HaloColors.onDark,
    fontSize: 16,
    fontWeight: 'bold',
  },
  buttonDisabled: {
    opacity: 0.6,
  },
  buttonOutlineRed: {
    backgroundColor: HaloColors.card,
    borderWidth: 1,
    borderColor: HaloColors.red,
  },
  buttonOutlineRedText: {
    color: HaloColors.red,
    fontSize: 16,
    fontWeight: 'bold',
  },
  buttonOutlineBlue: {
    backgroundColor: HaloColors.card,
    borderWidth: 1,
    borderColor: HaloColors.blue,
  },
  buttonOutlineBlueText: {
    color: HaloColors.blue,
    fontSize: 16,
    fontWeight: 'bold',
  },
});
