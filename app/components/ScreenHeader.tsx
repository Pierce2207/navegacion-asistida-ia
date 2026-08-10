import { StyleSheet, Text, View } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { HaloColors } from '@/constants/Colors';

export function ScreenHeader({ title, subtitle }: { title: string; subtitle?: string }) {
  const insets = useSafeAreaInsets();

  return (
    <View style={[styles.header, { paddingTop: insets.top + 12 }]}>
      <Text style={styles.title} accessibilityRole="header">
        {title}
      </Text>
      {subtitle ? <Text style={styles.subtitle}>{subtitle}</Text> : null}
    </View>
  );
}

const styles = StyleSheet.create({
  header: {
    backgroundColor: HaloColors.surfaceDark,
    paddingHorizontal: 20,
    paddingBottom: 16,
  },
  title: {
    color: HaloColors.onDark,
    fontSize: 24,
    fontWeight: 'bold',
  },
  subtitle: {
    color: HaloColors.onDarkSecondary,
    fontSize: 14,
    marginTop: 4,
  },
});
