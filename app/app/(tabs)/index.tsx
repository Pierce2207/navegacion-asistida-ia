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
  help: { emoji: '🤝', title: 'NECESITA AYUDA', noun: 'alerta de ayuda' },
  fall: { emoji: '⚠️', title: 'CAÍDA DETECTADA', noun: 'alerta de caída' },
  sos: { emoji: '🆘', title: 'SOS', noun: 'alerta SOS' },
  emergency: { emoji: '🚨', title: 'EMERGENCIA', noun: 'alerta de emergencia' },
};

// The database stores these in English; only what the user sees is translated
const STATUS_LABELS: Record<string, string> = {
  active: 'ACTIVA',
  acknowledged: 'ATENDIDA',
  resolved: 'RESUELTA',
};

const PRIORITY_LABELS: Record<string, string> = {
  low: 'BAJA',
  medium: 'MEDIA',
  high: 'ALTA',
  critical: 'CRÍTICA',
};

function alertTypeInfo(type: string | null) {
  if (type && TYPE_INFO[type]) {
    return TYPE_INFO[type];
  }
  const readable = type ? type.replace(/_/g, ' ') : null;
  return {
    emoji: '⚠️',
    title: readable ? readable.toUpperCase() : 'ALERTA',
    noun: readable ? `alerta de ${readable}` : 'alerta',
  };
}

function statusLabel(status: string | null) {
  return STATUS_LABELS[status ?? ''] ?? (status ?? 'desconocido').replace(/_/g, ' ').toUpperCase();
}

function priorityLabel(priority: string | null) {
  if (!priority) {
    return undefined;
  }
  return PRIORITY_LABELS[priority] ?? priority.toUpperCase();
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
    // día/mes/año, como se escribe en Perú
    date: `${pad(date.getDate())}/${pad(date.getMonth() + 1)}/${date.getFullYear()}`,
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
        Alert.alert('No se pudo actualizar', `La alerta no se actualizó: ${updateError.message}`);
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
    const priority = priorityLabel(item.priority);
    const accessibilityLabel = [
      title,
      status,
      priority && `prioridad ${priority}`,
      time && `${time} ${date}`,
      item.location && `en ${item.location}`,
    ]
      .filter(Boolean)
      .join(', ');

    return (
      <Pressable
        style={styles.alertCard}
        onPress={() => setSelected(item)}
        accessibilityRole="button"
        accessibilityLabel={accessibilityLabel}
        accessibilityHint="Abre las acciones de la alerta">
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

  const dialogNoun = selected ? alertTypeInfo(selected.type).noun : 'alerta';

  return (
    <View style={styles.container}>
      <ScreenHeader
        title="Alertas de emergencia"
        subtitle={`${activeCount} ${activeCount === 1 ? 'activa' : 'activas'} • ${acknowledgedCount} ${
          acknowledgedCount === 1 ? 'atendida' : 'atendidas'
        }`}
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
              <StatCard count={activeCount} label="Alertas activas" color={HaloColors.red} />
              <StatCard count={acknowledgedCount} label="Atendidas" color={HaloColors.orange} />
              <StatCard count={resolvedCount} label="Resueltas" color={HaloColors.green} />
            </>
          }
          ListEmptyComponent={<Text style={styles.emptyText}>Todavía no hay alertas.</Text>}
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
              Acciones de la alerta
            </Text>
            <Text style={styles.dialogBody}>
              ¿Qué quieres hacer con esta {dialogNoun}?
            </Text>
            <View style={styles.dialogActions}>
              <Pressable
                style={styles.dialogButton}
                onPress={() => setSelected(null)}
                accessibilityRole="button"
                accessibilityLabel="Cancelar">
                <Text style={styles.dialogButtonText}>CANCELAR</Text>
              </Pressable>
              <Pressable
                style={styles.dialogButton}
                onPress={() => selected && updateStatus(selected, 'resolved')}
                accessibilityRole="button"
                accessibilityLabel="Marcar como resuelta">
                <Text style={styles.dialogButtonText}>MARCAR COMO RESUELTA</Text>
              </Pressable>
              <Pressable
                style={styles.dialogButton}
                onPress={() => selected && updateStatus(selected, 'acknowledged')}
                accessibilityRole="button"
                accessibilityLabel="Marcar como atendida">
                <Text style={styles.dialogButtonText}>MARCAR COMO ATENDIDA</Text>
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
