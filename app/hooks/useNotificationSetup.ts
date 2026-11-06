import notifee, { AndroidImportance, AndroidVisibility, Event, Notification } from '@notifee/react-native';
import messaging from '@react-native-firebase/messaging';
import { useCallback, useEffect } from 'react';

import { COMPANION_DEVICE_ID } from '@/app/utils/device';
import { supabase } from '@/app/utils/supabase';
import usePushNotification from '@/hooks/usePushNotification';

async function onMessageReceived(message: Notification) {
  console.log('Message', message);
  if (!message || !message.data || message.data.type !== 'partial_notification') {
    return;
  }

  // @ts-expect-error
  const notifeeData = JSON.parse(message.data.notifee || '{}');
  const alert = notifeeData?.body;
  if (!alert || (!alert.title && !alert.message)) {
    return;
  }

  await notifee.displayNotification({
    title: alert.title,
    body: alert.message,
    android: {
      channelId: 'messages',
      importance: AndroidImportance.HIGH,
      visibility: AndroidVisibility.PUBLIC,
    },
  });
}

export default function useNotificationSetup() {
  useEffect(() => {
    notifee.createChannel({
      id: 'messages',
      name: 'Alertas de emergencia',
      lights: false,
      vibration: true,
      bypassDnd: true,
      importance: AndroidImportance.HIGH,
      visibility: AndroidVisibility.PUBLIC,
      sound: 'sound',
    });

    notifee.onBackgroundEvent(async (event: Event) => {});

    messaging().setBackgroundMessageHandler(async (message: Notification) => {
      console.log('A new message arrived! (BACKGROUND)');
      await onMessageReceived(message);
    });
    const unsubscribe = messaging().onMessage(async (message: Notification) => {
      console.log('A new message arrived! (FOREGROUND)');
      await onMessageReceived(message);
    });
    return unsubscribe;
  }, []);

  const {
    requestUserPermission,
    getFCMToken,
    onNotificationOpenedAppFromBackground,
    onNotificationOpenedAppFromQuit,
  } = usePushNotification();

  const saveTokenToDatastore = useCallback(async (fcmToken: string) => {
    try {
      const { error } = await supabase
        .from('tokens')
        .upsert(
          {
            unique_device_id: COMPANION_DEVICE_ID,
            fcm_token: fcmToken,
          },
          { onConflict: 'unique_device_id' }
        );

      if (error) {
        console.error('Error saving token to Supabase:', error);
      }
    } catch (error) {
      console.error('Error saving token to datastore:', error);
    }
  }, []);

  const onAppBootstrap = useCallback(async () => {
    try {
      await messaging().registerDeviceForRemoteMessages();

      const fcmToken = await getFCMToken();
      await saveTokenToDatastore(fcmToken);

      requestUserPermission();
      onNotificationOpenedAppFromQuit();
      onNotificationOpenedAppFromBackground();
    } catch (error) {
      console.log('Error during app bootstrap:', error);
    }
  }, [getFCMToken, requestUserPermission, onNotificationOpenedAppFromQuit, onNotificationOpenedAppFromBackground, saveTokenToDatastore]);

  useEffect(() => {
    onAppBootstrap();

    const unsubscribe = messaging().onTokenRefresh(async (newToken) => {
      await saveTokenToDatastore(newToken);
    });

    return unsubscribe;
  }, [onAppBootstrap, saveTokenToDatastore]);
}
