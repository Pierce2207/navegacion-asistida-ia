// api/hazards.js
// Endpoint serverless (Vercel) para la red comunitaria de peligros compartidos.

// POST /api/hazards        -> el firmware reporta un peligro detectado (obstacle_alert + GPS valido)
// GET  /api/hazards?lat=&lon=&radius= -> el firmware consulta peligros verificados cerca de su ubicacion

import { createClient } from "@supabase/supabase-js";

const supabase = createClient(
  process.env.SUPABASE_URL,
  process.env.SUPABASE_ANON_KEY
);

const VERIFICATION_THRESHOLD = 3; // reportes independientes minimos para marcar 'verified'
const MERGE_RADIUS_M = 15;        // distancia para considerar que es "el mismo" peligro

export default async function handler(req, res) {
  if (req.method === "POST") {
    return handleReport(req, res);
  }
  if (req.method === "GET") {
    return handleNearbyQuery(req, res);
  }
  res.setHeader("Allow", "GET, POST");
  return res.status(405).json({ error: "Metodo no soportado" });
}

async function handleReport(req, res) {
  const { lat, lon, category, confidence, device_id } = req.body || {};

  if (typeof lat !== "number" || typeof lon !== "number" || !category) {
    return res.status(400).json({ error: "lat, lon y category son requeridos" });
  }

  // 1. Buscar si ya existe un reporte reciente y cercano de la misma categoria
  const { data: nearby, error: searchError } = await supabase.rpc(
    "find_nearby_hazard",
    { lat, lon, radius_m: MERGE_RADIUS_M, cat: category }
  );

  if (searchError) {
    return res.status(500).json({ error: searchError.message });
  }

  // 2. Si existe, se fusiona (incrementa verified_count) en vez de duplicar
  if (nearby && nearby.length > 0) {
    const hazard = nearby[0];
    const newCount = hazard.verified_count + 1;
    const newStatus = newCount >= VERIFICATION_THRESHOLD ? "verified" : "unverified";

    const { data, error } = await supabase
      .from("hazards")
      .update({ verified_count: newCount, status: newStatus })
      .eq("id", hazard.id)
      .select()
      .single();

    if (error) return res.status(500).json({ error: error.message });
    return res.status(200).json({ merged: true, ...data });
  }

  // 3. Si no existe, se crea un reporte nuevo como 'unverified'
  const { data, error } = await supabase
    .from("hazards")
    .insert({
      location: `SRID=4326;POINT(${lon} ${lat})`,
      category,
      confidence: confidence ?? null,
      reported_by: device_id ?? "anonimo",
    })
    .select()
    .single();

  if (error) return res.status(500).json({ error: error.message });
  return res.status(201).json({ merged: false, ...data });
}

async function handleNearbyQuery(req, res) {
  const lat = Number(req.query.lat);
  const lon = Number(req.query.lon);
  const radius = Number(req.query.radius) || 100;

  if (Number.isNaN(lat) || Number.isNaN(lon)) {
    return res.status(400).json({ error: "lat y lon son requeridos como numeros" });
  }

  // Solo devuelve peligros con status='verified' (ver nearby_hazards en el SQL):
  // un reporte sin confirmar de otros dispositivos no se anuncia a nadie mas.
  const { data, error } = await supabase.rpc("nearby_hazards", {
    lat,
    lon,
    radius_m: radius,
  });

  if (error) return res.status(500).json({ error: error.message });
  return res.status(200).json(data);
}
