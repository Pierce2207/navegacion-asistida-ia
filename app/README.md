# Halo Companion App

Mobile app for [Halo](https://github.com/ilikecandy/SSCS-2025-Arduino),
a wearable assistant for people with vision loss. Built with Expo and
React Native.

What it does:
- Live alert feed: every hazard Halo announces is logged here, so a
  family member can see what happened and where
- SOS notifications: five presses of Halo's button push an alert to this
  app (Supabase -> Firebase Cloud Messaging)
- Device settings: language and wake word preferences, fetched by the
  ESP32 on startup

Part of the project that won 1st place in the IEEE SSCS Arduino Contest
2025. See also the [API](https://github.com/StockerMC/SSCS-2025-API) and
[firmware](https://github.com/ilikecandy/SSCS-2025-Arduino).
