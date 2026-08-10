-- Red comunitaria de peligros compartidos
-- Ejecutar en el SQL Editor de Supabase (Project > SQL Editor > New query)

create extension if not exists postgis;

create table if not exists hazards (
  id uuid primary key default gen_random_uuid(),
  location geography(Point, 4326) not null,
  category text not null,               -- cyclist | stairs | head_level_obstacle | crosswalk | construction | otro
  confidence float,                     -- confianza que reporto Gemini al detectarlo
  reported_by text,                     -- id de dispositivo anonimizado (no una cuenta de usuario)
  verified_count int not null default 1,
  status text not null default 'unverified', -- 'unverified' | 'verified'
  created_at timestamptz not null default now(),
  expires_at timestamptz not null default (now() + interval '48 hours')
);

-- Indice geoespacial: imprescindible para que las busquedas "que hay cerca de mi" sean rapidas
create index if not exists hazards_location_idx on hazards using gist (location);
create index if not exists hazards_category_idx on hazards (category);
create index if not exists hazards_expires_idx on hazards (expires_at);

-- Busca un peligro existente de la misma categoria a menos de radius_m metros,
-- reportado en los ultimos 30 dias. Usada por api/hazards.js para decidir si
-- un reporte nuevo debe fusionarse con uno existente en vez de duplicarse.
create or replace function find_nearby_hazard(lat float, lon float, radius_m float, cat text)
returns setof hazards as $$
  select *
  from hazards
  where category = cat
    and created_at > now() - interval '30 days'
    and ST_DWithin(location, ST_SetSRID(ST_MakePoint(lon, lat), 4326)::geography, radius_m)
  order by created_at desc
  limit 1;
$$ language sql stable;

-- Devuelve los peligros verificados y vigentes cerca de una ubicacion.
-- Usada por api/hazards.js para el endpoint GET (el firmware la consulta
-- en cada ciclo de GPS).
create or replace function nearby_hazards(lat float, lon float, radius_m float)
returns table (
  id uuid,
  lat float,
  lon float,
  category text,
  status text,
  verified_count int,
  distance_m float
) as $$
  select
    h.id,
    ST_Y(h.location::geometry) as lat,
    ST_X(h.location::geometry) as lon,
    h.category,
    h.status,
    h.verified_count,
    ST_Distance(h.location, ST_SetSRID(ST_MakePoint(lon, lat), 4326)::geography) as distance_m
  from hazards h
  where h.status = 'verified'
    and h.expires_at > now()
    and ST_DWithin(h.location, ST_SetSRID(ST_MakePoint(lon, lat), 4326)::geography, radius_m)
  order by distance_m asc;
$$ language sql stable;
