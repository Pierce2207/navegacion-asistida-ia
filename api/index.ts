require('dotenv').config();
const fs = require('fs');
const path = require('path')
const admin = require("firebase-admin");
const express = require('express')
const cors = require("cors");
const { createClient } = require('@supabase/supabase-js');

const app = express()

app.use(express.json());
app.use(cors());

// Initialize Supabase
const supabaseUrl = process.env.SUPABASE_URL;
const supabaseKey = process.env.SUPABASE_ANON_KEY;

if (!supabaseUrl || !supabaseKey) {
  throw new Error("Supabase URL and Key must be set in environment variables");
}

const supabase = createClient(supabaseUrl, supabaseKey)

const serviceAccountPath = path.join(__dirname, "../serviceAccountKey.json");

if (!fs.existsSync(serviceAccountPath)) {
  throw new Error("serviceAccountKey.json not found in project root - download it from the Firebase console (Project settings > Service accounts)");
}

const serviceAccount = require(serviceAccountPath);

// Initialize Firebase
admin.initializeApp({
  credential: admin.credential.cert(serviceAccount),
});

app.get("/settings", async (req, res) => {
  try {
    const deviceId = req.query.device_id;
  
    if (!deviceId) {
      return res.status(400).json({ error: 'device_id is required' });
    }

    // Fetch settings from Supabase
    const { data, error } = await supabase
      .from('user_settings')
      .select('*')
      .eq('unique_device_id', deviceId)
      .single();

    if (error && error.code !== 'PGRST116') {
      console.error('Error fetching settings:', error);
      return res.status(500).json({ error: 'Failed to fetch settings' });
    }

    // If no settings found, return defaults
    if (!data) {
      const defaultSettings = {
        unique_device_id: deviceId,
        language: 'en-US',
        volume: 90,
        speech_mode: 'verbose',
        alert_types_enabled: ['all'],
        danger_sensitivity: 'medium',
        notify_companion: true,
        location_sharing_enabled: true,
        auto_distress_timeout: 10,
        emergency_contacts: [],
        button_press_behavior: 'send_alert',
        device_name: '',
        wake_word: 'Halo',
        fetch_interval: 300,
        vibration_enabled: true,
        haptic_pattern: 'pulse',
        high_contrast_mode: false,
        last_updated: new Date().toISOString(),
      };

      return res.json(defaultSettings);
    }

    // Return the fetched settings
    return res.json(data);

  } catch (error) {
    console.error('API Error:', error);
    return res.status(500).json({ error: 'Internal server error' });
  }
});

const settingsValidators = {
  device_name: v => typeof v === 'string',
  wake_word: v => typeof v === 'string',
  language: v => typeof v === 'string',
  speech_mode: v => typeof v === 'string',
  danger_sensitivity: v => typeof v === 'string',
  button_press_behavior: v => typeof v === 'string',
  haptic_pattern: v => typeof v === 'string',
  home_location: v => v === null || typeof v === 'string',
  volume: v => typeof v === 'number' && v >= 0 && v <= 100,
  auto_distress_timeout: v => typeof v === 'number' && v >= 0,
  fetch_interval: v => typeof v === 'number' && v >= 0,
  notify_companion: v => typeof v === 'boolean',
  location_sharing_enabled: v => typeof v === 'boolean',
  vibration_enabled: v => typeof v === 'boolean',
  high_contrast_mode: v => typeof v === 'boolean',
  alert_types_enabled: v => Array.isArray(v) && v.every(t => typeof t === 'string'),
  emergency_contacts: v => Array.isArray(v),
};

app.post("/settings", async (req, res) => {
  try {
    // unique_device_id and last_updated are server-managed, ignore echoes from GET
    const { device_id, unique_device_id, last_updated, ...settings } = req.body;

    if (!device_id) {
      return res.status(400).json({ error: 'device_id is required' });
    }

    for (const [field, value] of Object.entries(settings)) {
      const validator = settingsValidators[field];
      if (!validator) {
        return res.status(400).json({ error: `Unknown field: ${field}` });
      }
      if (!validator(value)) {
        return res.status(400).json({ error: `Invalid value for field: ${field}` });
      }
    }

    // Update settings in Supabase
    const { error } = await supabase
      .from('user_settings')
      .upsert({
        unique_device_id: device_id,
        ...settings,
        last_updated: new Date().toISOString(),
      }, {
        onConflict: 'unique_device_id',
      });

    if (error) {
      console.error('Error updating settings:', error);
      return res.status(500).json({ error: 'Failed to update settings' });
    }

    return res.json({ success: true });

  } catch (error) {
    console.error('API Error:', error);
    return res.status(500).json({ error: 'Internal server error' });
  }
});

// Partial Push Notification
async function sendPartialNotification(token, data) {
  return admin.messaging().send({
    token,
    data: {
      type: "partial_notification",
      notifee: JSON.stringify({
        body: data,
        android: {
          channelId: "default",
        },
      }),
    },
  });
}

// Declare a notification route
app.post("/notifications", async (req, res) => {
  try {
    const data = req.body;
    console.log(data);

    // Persist before delivery so an alert is never lost to a missing token
    // Firmware payload maps to alerts columns: alert_type -> type, severity -> priority
    const { error: insertError } = await supabase
      .from('alerts')
      .insert({
        device_id: data.device_id || 'companion_app',
        type: alertTypes.includes(data.alert_type) ? data.alert_type : 'help',
        message: data.message,
        location: data.location,
        priority: alertPriorities.includes(data.severity) ? data.severity : 'medium',
        status: 'active',
        timestamp: new Date().toISOString(),
      });

    if (insertError) {
      console.error('Error saving alert to Supabase:', insertError);
    }

    const { data: tokenData, error } = await supabase
      .from('tokens')
      .select('fcm_token')
      .eq('unique_device_id', 'companion_app')
      .single();

    const token = tokenData && tokenData.fcm_token;
    if (error || !token) {
      console.error('Error fetching token from Supabase:', error);
      return res.status(404).json({ error: "FCM token not found", alert_saved: !insertError });
    }

    await sendPartialNotification(token, data);
    res.json({ status: "OK", alert_saved: !insertError });
  } catch (error) {
    console.error('Error sending notification:', error);
    res.status(500).json({ error: "Internal server error" });
  }
});

const alertStatuses = ['active', 'acknowledged', 'resolved'];
const alertTypes = ['help', 'fall', 'sos', 'emergency'];
const alertPriorities = ['low', 'medium', 'high', 'critical'];

app.get("/alerts", async (req, res) => {
  try {
    const { device_id, status } = req.query;
    const limit = Math.min(parseInt(req.query.limit, 10) || 50, 200);

    if (status && !alertStatuses.includes(status)) {
      return res.status(400).json({ error: `status must be one of: ${alertStatuses.join(', ')}` });
    }

    let query = supabase
      .from('alerts')
      .select('*')
      .order('created_at', { ascending: false })
      .limit(limit);

    if (device_id) query = query.eq('device_id', device_id);
    if (status) query = query.eq('status', status);

    const { data, error } = await query;

    if (error) {
      console.error('Error fetching alerts:', error);
      return res.status(500).json({ error: 'Failed to fetch alerts' });
    }

    return res.json(data);

  } catch (error) {
    console.error('API Error:', error);
    return res.status(500).json({ error: 'Internal server error' });
  }
});

app.patch("/alerts/:id", async (req, res) => {
  try {
    const { status } = req.body;

    if (!alertStatuses.includes(status)) {
      return res.status(400).json({ error: `status must be one of: ${alertStatuses.join(', ')}` });
    }

    const { data, error } = await supabase
      .from('alerts')
      .update({ status })
      .eq('id', req.params.id)
      .select()
      .single();

    if (error && error.code !== 'PGRST116') {
      console.error('Error updating alert:', error);
      return res.status(500).json({ error: 'Failed to update alert' });
    }

    if (!data) {
      return res.status(404).json({ error: 'Alert not found' });
    }

    return res.json(data);

  } catch (error) {
    console.error('API Error:', error);
    return res.status(500).json({ error: 'Internal server error' });
  }
});

app.get("/", async (req, res) => {
  res.send("OK");
});

const port = 4321
app.listen(port, () => {
  console.log(`listening on port ${port}`)
})

module.exports = app;
