-- Tablas del backend original de Halo (settings, alerts, tokens de notificaciones).
-- Estas no venian documentadas en el repo original (SSCS-2025-API no publica su
-- schema.sql), se reconstruyeron a partir de api/index.ts y
-- app/app/utils/database.types.ts, que ya reflejan la forma exacta que usa el codigo.
-- Ejecutar en el SQL Editor de Supabase (Project > SQL Editor > New query),
-- junto (o despues) de sql/hazards_schema.sql.

create type alert_priority as enum ('low', 'medium', 'high', 'critical');
create type alert_status as enum ('active', 'acknowledged', 'resolved');
create type alert_type as enum ('help', 'fall', 'sos', 'emergency');

-- Feed de alertas de emergencia que ve la app (pantalla "Emergency Alerts").
-- Poblada por POST /notifications en api/index.ts.
create table if not exists alerts (
  id bigint generated always as identity primary key,
  created_at timestamptz not null default now(),
  device_id text,
  location text,
  message text,
  priority alert_priority,
  status alert_status,
  timestamp timestamptz,
  type alert_type
);

create index if not exists alerts_status_idx on alerts (status);
create index if not exists alerts_device_id_idx on alerts (device_id);

-- Token de Firebase Cloud Messaging de la app companera, para poder enviarle
-- push notifications desde el backend (POST /notifications lo busca por
-- unique_device_id = 'companion_app').
create table if not exists tokens (
  id bigint generated always as identity primary key,
  created_at timestamptz not null default now(),
  fcm_token text not null,
  unique_device_id text not null unique
);

-- Configuracion por dispositivo (idioma, wake word, sensibilidad de peligro, etc.),
-- leida/escrita por GET+POST /settings en api/index.ts y consultada por el
-- firmware al arrancar.
create table if not exists user_settings (
  unique_device_id text primary key,
  language text not null default 'en-US',
  volume int not null default 90,
  speech_mode text not null default 'verbose',
  alert_types_enabled text[] not null default array['all'],
  danger_sensitivity text not null default 'medium',
  notify_companion boolean not null default true,
  location_sharing_enabled boolean not null default true,
  auto_distress_timeout int not null default 10,
  emergency_contacts jsonb not null default '[]',
  button_press_behavior text not null default 'send_alert',
  device_name text not null default '',
  wake_word text not null default 'Halo',
  fetch_interval int not null default 300,
  vibration_enabled boolean not null default true,
  haptic_pattern text not null default 'pulse',
  high_contrast_mode boolean not null default false,
  home_location text,
  last_updated timestamptz not null default now()
);

-- Este proyecto no tiene autenticacion de usuarios (el firmware y la app usan
-- directamente la clave anonima de Supabase), asi que igual que hazards.sql,
-- dejamos estas tablas sin RLS - Supabase lo activa por defecto en tablas nuevas
-- y bloquea todo si no hay una politica explicita.
alter table alerts disable row level security;
alter table tokens disable row level security;
alter table user_settings disable row level security;
