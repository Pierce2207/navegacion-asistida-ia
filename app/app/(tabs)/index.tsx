import { useCallback, useEffect, useState } from 'react';
import {
  ActivityIndicator,
  Alert,
  FlatList,
  ListRenderItemInfo,
  Modal,
  Pressable,
  RefreshControl,
  StyleSheet,
  Text,
  View,
} from 'react-native';

import { Enums, Tables } from '@/app/utils/database.types';
import { supabase } from '@/app/utils/supabase';
import { ScreenHeader } from '@/components/ScreenHeader';
import { HaloColors } from '@/constants/Colors';

type AlertRow = Tables<'alerts'>;

const STATUS_COLORS: Record<string, string> = {
  active: HaloColors.red,
  acknowledged: HaloColors.orange,
  resolved: HaloColors.green,
};

const PRIORITY_COLORS: Record<string, string> = {
  high: HaloColors.orange,
  critical: HaloColors.red,
};

// Alert types other than 'help' share the same payload shape.
const TYPE_INFO: Record<string, { emoji: string; title: string; noun: string }> = {
  help: { emoji: '🤝', title: 'HELP NEEDED', noun: 'help alert' },
  fall: { emoji: '⚠️', title: 'FALL DETECTED', noun: 'fall alert' },
  sos: { emoji: '🆘', title: 'SOS', noun: 'SOS alert' },
  emergency: { emoji: '🚨', title: 'EMERGENCY', noun: 'emergency alert' },
};

function alertTypeInfo(type: string | null) {
  if (type && TYPE_INFO[type]) {
    return TYPE_INFO[type];
  }
  const readable = type ? type.replace(/_/g, ' ') : null;
  return {
    emoji: '⚠️',
    title: readable ? readable.toUpperCase() : 'ALERT',
    noun: readable ? `${readable} alert` : 'alert',
  };
}

function statusLabel(status: string | null) {
  return (status ?? 'unknown').replace(/_/g, ' ').toUpperCase();
}

function statusColor(status: string | null) {
  return STATUS_COLORS[status ?? ''] ?? HaloColors.textSecondary;
}

function priorityColor(priority: string | null) {
  return PRIORITY_COLORS[priority ?? ''] ?? HaloColors.textSecondary;
}

function alertTimeValue(alert: AlertRow) {
  return alert.timestamp ?? alert.created_at;
}

function formatTimestamp(iso: string) {
  const date = new Date(iso);
  if (Number.isNaN(date.getTime())) {
    return { time: '', date: '' };
  }
  const pad = (value: number) => String(value).padStart(2, '0');
  const hours = date.getHours();
  const twelveHour = hours % 12 === 0 ? 12 : hours % 12;
  const meridiem = hours < 12 ? 'a.m.' : 'p.m.';
  return {
    time: `${twelveHour}:${pad(date.getMinutes())} ${meridiem}`,
    date: `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}`,
  };
}

function sortNewestFirst(alerts: AlertRow[]) {
  return [...alerts].sort(
    (a, b) => new Date(alertTimeValue(b)).getTime() - new Date(alertTimeValue(a)).getTime()
  );
}

function StatCard({ count, label, color }: { count: number; label: string; color: string }) {
  return (
    <View
      style={[styles.statCard, { backgroundColor: color }]}
      accessible
      accessibilityLabel={`${count} ${label}`}>
      <Text style={styles.statCount}>{count}</Text>
      <Text style={styles.statLabel}>{label}</Text>
    </View>
  );
}

export default function HomeScreen() {
  const [alerts, setAlerts] = useState<AlertRow[]>([]);
  const [loading, setLoading] = useState(true);
  const [refreshing, setRefreshing] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [selected, setSelected] = useState<AlertRow | null>(null);

  const upsertAlert = useCallback((incoming: AlertRow) => {
    setAlerts((previous) => {
      const exists = previous.some((alert) => alert.id === incoming.id);
      const next = exists
        ? previous.map((alert) => (alert.id === incoming.id ? incoming : alert))
        : [...previous, incoming];
      return sortNewestFirst(next);
    });
  }, []);

  const fetchAlerts = useCallback(async () => {
    const { data, error: fetchError } = await supabase
      .from('alerts')
      .select('*')
      .order('timestamp', { ascending: false });
    if (fetchError) {
      setError(fetchError.message);
      return;
    }
    setError(null);
    setAlerts(sortNewestFirst(data ?? []));
  }, []);

  useEffect(() => {
    fetchAlerts().finally(() => setLoading(false));
  }, [fetchAlerts]);

  useEffect(() => {
    const channel = supabase
      .channel('alerts-feed')
      .on(
        'postgres_changes',
        { event: 'INSERT', schema: 'public', table: 'alerts' },
        (payload) => upsertAlert(payload.new as AlertRow)
      )
      .on(
        'postgres_changes',
        { event: 'UPDATE', schema: 'public', table: 'alerts' },
        (payload) => upsertAlert(payload.new as AlertRow)
      )
      .subscribe();
    return () => {
      supabase.removeChannel(channel);
    };
  }, [upsertAlert]);

  const onRefresh = useCallback(async () => {
    setRefreshing(true);
    await fetchAlerts();
    setRefreshing(false);
  }, [fetchAlerts]);

  const updateStatus = useCallback(
    async (alert: AlertRow, status: Enums<'alert_status'>) => {
      setSelected(null);
      upsertAlert({ ...alert, status });
      const { data, error: updateError } = await supabase
        .from('alerts')
        .update({ status })
        .eq('id', alert.id)
        .select()
        .single();
      if (updateError) {
        upsertAlert(alert);
        Alert.alert('Update Failed', `The alert was not updated: ${updateError.message}`);
        return;
      }
      upsertAlert(data);
    },
    [upsertAlert]
  );

  const activeCount = alerts.filter((alert) => alert.status === 'active').length;
  const acknowledgedCount = alerts.filter((alert) => alert.status === 'acknowledged').length;
  const resolvedCount = alerts.filter((alert) => alert.status === 'resolved').length;

  const renderAlert = ({ item }: ListRenderItemInfo<AlertRow>) => {
    const { time, date } = formatTimestamp(alertTimeValue(item));
    const { emoji, title } = alertTypeInfo(item.type);
    const status = statusLabel(item.status);
    const priority = item.priority?.toUpperCase();
    const accessibilityLabel = [
      title,
      status,
      priority && `priority ${priority}`,
      time && `${time} ${date}`,
      item.location && `at ${item.location}`,
    ]
      .filter(Boolean)
      .join(', ');

    return (
      <Pressable
        style={styles.alertCard}
        onPress={() => setSelected(item)}
        accessibilityRole="button"
        accessibilityLabel={accessibilityLabel}
        accessibilityHint="Opens alert actions">
        <View style={styles.alertHeader}>
          <Text style={styles.alertTitle}>{emoji} {title}</Text>
          <View>
            <Text style={styles.timestampText}>{time}</Text>
            <Text style={styles.timestampText}>{date}</Text>
          </View>
        </View>
        {item.message ? <Text style={styles.alertMessage}>{item.message}</Text> : null}
        <View style={styles.alertMeta}>
          <View style={[styles.statusPill, { backgroundColor: statusColor(item.status) }]}>
            <Text style={styles.statusPillText}>{status}</Text>
          </View>
          {priority ? (
            <Text style={[styles.priorityText, { color: priorityColor(item.priority) }]}>
              {priority}
            </Text>
          ) : null}
          {item.location ? (
            <Text style={styles.locationText} numberOfLines={1}>
              📍 {item.location}
            </Text>
          ) : null}
        </View>
      </Pressable>
    );
  };

  const dialogNoun = selected ? alertTypeInfo(selected.type).noun : 'alert';

  return (
    <View style={styles.container}>
      <ScreenHeader
        title="Emergency Alerts"
        subtitle={`${activeCount} Active • ${acknowledgedCount} Acknowledged`}
      />
      {loading ? (
        <ActivityIndicator color={HaloColors.blue} style={styles.loader} />
      ) : (
        <FlatList
          data={alerts}
          keyExtractor={(item) => String(item.id)}
          renderItem={renderAlert}
          contentContainerStyle={styles.listContent}
          refreshControl={<RefreshControl refreshing={refreshing} onRefresh={onRefresh} />}
          ListHeaderComponent={
            <>
              {error ? (
                <View style={styles.errorBanner} accessibilityRole="alert">
                  <Text style={styles.errorText}>{error}</Text>
                </View>
              ) : null}
              <StatCard count={activeCount} label="Active Alerts" color={HaloColors.red} />
              <StatCard count={acknowledgedCount} label="Acknowledged" color={HaloColors.orange} />
              <StatCard count={resolvedCount} label="Resolved" color={HaloColors.green} />
            </>
          }
          ListEmptyComponent={<Text style={styles.emptyText}>No alerts yet.</Text>}
        />
      )}
      <Modal
        visible={selected !== null}
        transparent
        animationType="fade"
        onRequestClose={() => setSelected(null)}>
        <Pressable style={styles.dialogBackdrop} onPress={() => setSelected(null)}>
          <View style={styles.dialog}>
            <Text style={styles.dialogTitle} accessibilityRole="header">
              Alert Actions
            </Text>
            <Text style={styles.dialogBody}>
              What would you like to do with this {dialogNoun}?
            </Text>
            <View style={styles.dialogActions}>
              <Pressable
                style={styles.dialogButton}
                onPress={() => setSelected(null)}
                accessibilityRole="button"
                accessibilityLabel="Cancel">
                <Text style={styles.dialogButtonText}>CANCEL</Text>
              </Pressable>
              <Pressable
                style={styles.dialogButton}
                onPress={() => selected && updateStatus(selected, 'resolved')}
                accessibilityRole="button"
                accessibilityLabel="Mark as resolved">
                <Text style={styles.dialogButtonText}>MARK AS RESOLVED</Text>
              </Pressable>
              <Pressable
                style={styles.dialogButton}
                onPress={() => selected && updateStatus(selected, 'acknowledged')}
                accessibilityRole="button"
                accessibilityLabel="Acknowledge alert">
                <Text style={styles.dialogButtonText}>ACKNOWLEDGE ALERT</Text>
              </Pressable>
            </View>
          </View>
        </Pressable>
      </Modal>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: HaloColors.background,
  },
  loader: {
    marginTop: 32,
  },
  listContent: {
    padding: 16,
    paddingBottom: 32,
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
  emptyText: {
    color: HaloColors.textSecondary,
    fontSize: 15,
    textAlign: 'center',
    marginTop: 24,
  },
  statCard: {
    borderRadius: 14,
    padding: 16,
    marginBottom: 12,
  },
  statCount: {
    color: HaloColors.onDark,
    fontSize: 28,
    fontWeight: 'bold',
  },
  statLabel: {
    color: HaloColors.onDark,
    fontSize: 14,
    marginTop: 2,
  },
  alertCard: {
    backgroundColor: HaloColors.card,
    borderRadius: 14,
    borderLeftWidth: 5,
    borderLeftColor: HaloColors.orange,
    padding: 16,
    marginBottom: 12,
    shadowColor: '#000',
    shadowOpacity: 0.06,
    shadowRadius: 6,
    shadowOffset: { width: 0, height: 2 },
    elevation: 2,
  },
  alertHeader: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'flex-start',
    marginBottom: 8,
  },
  alertTitle: {
    color: HaloColors.orange,
    fontSize: 15,
    fontWeight: 'bold',
    flexShrink: 1,
    marginRight: 8,
  },
  timestampText: {
    color: HaloColors.textSecondary,
    fontSize: 12,
    textAlign: 'right',
  },
  alertMessage: {
    color: HaloColors.textPrimary,
    fontSize: 14,
    marginBottom: 10,
  },
  alertMeta: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
  },
  statusPill: {
    borderRadius: 12,
    paddingHorizontal: 10,
    paddingVertical: 3,
  },
  statusPillText: {
    color: HaloColors.onDark,
    fontSize: 11,
    fontWeight: 'bold',
  },
  priorityText: {
    fontSize: 11,
    fontWeight: 'bold',
  },
  locationText: {
    color: HaloColors.textSecondary,
    fontSize: 12,
    flexShrink: 1,
  },
  dialogBackdrop: {
    flex: 1,
    backgroundColor: 'rgba(0, 0, 0, 0.4)',
    justifyContent: 'center',
    padding: 32,
  },
  dialog: {
    backgroundColor: '#2E3235',
    borderRadius: 14,
    padding: 20,
  },
  dialogTitle: {
    color: HaloColors.onDark,
    fontSize: 18,
    fontWeight: 'bold',
    marginBottom: 12,
  },
  dialogBody: {
    color: HaloColors.onDarkSecondary,
    fontSize: 14,
    marginBottom: 16,
  },
  dialogActions: {
    alignItems: 'flex-end',
  },
  dialogButton: {
    paddingVertical: 10,
    paddingHorizontal: 8,
  },
  dialogButtonText: {
    color: HaloColors.teal,
    fontSize: 14,
    fontWeight: 'bold',
  },
});
