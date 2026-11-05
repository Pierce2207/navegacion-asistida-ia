// api/navigation.js
// Navegacion a pie con OpenStreetMap (gratis, sin clave). El firmware hace UNA consulta corta y recibe
// todo armado y en español: la ESP32 no tiene memoria para el JSON completo de un ruteador ni para
// varias conexiones TLS seguidas.
//
// GET /api/navigation?action=where&lat=&lon=
//   -> { ok, text: "Estás en Av. Arequipa 1234, Miraflores" }
// GET /api/navigation?action=route&lat=&lon=&dest=texto   (o &dlat=&dlon= para recalcular)
//   -> { ok, destination, distance_m, duration_s, summary, steps: [{ lat, lon, text, side, distance_m }] }
//      Cada paso es una maniobra: el firmware la anuncia al acercarse a (lat, lon) y hace vibrar
//      el motor "side" (left | right | center).
//
// Servicios: Nominatim (direcciones y búsqueda) y el OSRM público de routing.openstreetmap.de con el
// perfil a pie. Sus políticas de uso piden identificar la app con un User-Agent y pocas consultas
// por segundo; este endpoint hace 1-2 por pedido.

const USER_AGENT = "HaloVision/1.0 (proyecto academico de navegacion asistida)";
const NOMINATIM = "https://nominatim.openstreetmap.org";
const OSRM_FOOT = "https://routing.openstreetmap.de/routed-foot/route/v1/foot";
const MAX_STEPS = 40;

export default async function handler(req, res) {
  if (req.method !== "GET") {
    res.setHeader("Allow", "GET");
    return res.status(405).json({ ok: false, error: "Metodo no soportado" });
  }

  const lat = Number(req.query.lat);
  const lon = Number(req.query.lon);
  if (!Number.isFinite(lat) || !Number.isFinite(lon)) {
    return res.status(400).json({ ok: false, error: "lat y lon son requeridos" });
  }

  try {
    if (req.query.action === "where") {
      return res.status(200).json(await whereAmI(lat, lon));
    }
    if (req.query.action === "route") {
      const dest = String(req.query.dest || "").trim();
      // dlat/dlon: destination already known (rerouting after the user went off the route)
      const dlat = Number(req.query.dlat);
      const dlon = Number(req.query.dlon);
      const known = Number.isFinite(dlat) && Number.isFinite(dlon)
        ? { name: dest || "tu destino", lat: dlat, lon: dlon }
        : null;
      if (!dest && !known) {
        return res.status(400).json({ ok: false, error: "dest o dlat/dlon son requeridos" });
      }
      return res.status(200).json(await route(lat, lon, dest, known));
    }
    return res.status(400).json({ ok: false, error: "action debe ser where o route" });
  } catch (err) {
    return res.status(502).json({ ok: false, error: err.message });
  }
}

async function getJson(url) {
  const r = await fetch(url, { headers: { "User-Agent": USER_AGENT, "Accept-Language": "es" } });
  if (!r.ok) {
    throw new Error(`${new URL(url).host} respondio ${r.status}`);
  }
  return r.json();
}

// --- ¿Dónde estoy? ---

async function whereAmI(lat, lon) {
  const j = await getJson(
    `${NOMINATIM}/reverse?format=jsonv2&zoom=18&addressdetails=1&lat=${lat}&lon=${lon}`
  );
  if (!j.address) {
    return { ok: false, text: "No pude saber en qué calle estás." };
  }
  return { ok: true, text: `Estás en ${describeAddress(j.address)}` };
}

function describeAddress(a) {
  const street = [a.road || a.pedestrian || a.footway || a.path, a.house_number].filter(Boolean).join(" ");
  const area = a.neighbourhood || a.suburb || a.quarter;
  const district = a.city_district || a.city || a.town || a.village;
  return [street, area, district].filter(Boolean).filter((v, i, all) => all.indexOf(v) === i).join(", ");
}

// --- Ruta a pie ---

async function route(lat, lon, dest, known) {
  const place = known || (await findPlace(lat, lon, dest));
  if (!place) {
    return { ok: false, error: "not_found", summary: `No encontré "${dest}" cerca de ti.` };
  }

  const r = await getJson(
    `${OSRM_FOOT}/${lon},${lat};${place.lon},${place.lat}?overview=false&steps=true`
  );
  if (r.code !== "Ok" || !r.routes || !r.routes.length) {
    return { ok: false, error: "no_route", summary: `No encontré un camino a pie hasta ${place.name}.` };
  }

  const best = r.routes[0];
  const all = best.legs[0].steps.map((s) => ({
    lat: round(s.maneuver.location[1]),
    lon: round(s.maneuver.location[0]),
    text: instruction(s),
    side: sideOf(s.maneuver),
    distance_m: Math.round(s.distance),
  }));
  const steps = mergeCloseSteps(all).slice(0, MAX_STEPS);

  const distance = Math.round(best.distance);
  return {
    ok: true,
    destination: { name: place.name, lat: place.lat, lon: place.lon },
    distance_m: distance,
    duration_s: Math.round(best.duration),
    summary: `Ruta a ${place.name}: ${metersText(distance)} caminando, unos ${Math.max(1, Math.round(best.duration / 60))} minutos. ${steps[0] ? steps[0].text : ""}`.trim(),
    steps,
  };
}

// OpenStreetMap dibuja veredas y cruces con muchos quiebres: "izquierda 8 m, derecha 5 m, izquierda".
// Caminando no da tiempo a escuchar cada uno, así que los giros a menos de MERGE_M del anterior se
// dicen juntos en una sola indicación ("Gira a la izquierda, luego a la derecha y sigue 90 metros").
const MERGE_M = 15;
function mergeCloseSteps(steps) {
  const out = [];
  for (const s of steps) {
    const prev = out[out.length - 1];
    const isLast = s === steps[steps.length - 1];
    if (prev && prev.distance_m < MERGE_M && !isLast && out.length > 1) {
      const turn = s.text.replace(/^Gira /, "").replace(/\.$/, "");
      prev.text = `${prev.text.replace(/ y sigue [^.]*\./, "").replace(/\.$/, "")}, luego ${turn}.`;
      prev.distance_m += s.distance_m;
      if (prev.side === "center") prev.side = s.side;
      continue;
    }
    out.push({ ...s });
  }
  return out;
}

// Busca primero cerca del usuario (~5 km); si no hay nada, en todo el Perú
// En Perú "Plaza de Armas" y "Plaza Mayor" son la misma plaza, pero OSM suele usar solo un nombre
// (la de Lima figura como "Plaza Mayor"): se buscan ambos
function queryVariants(dest) {
  const v = [dest];
  if (/plaza de armas/i.test(dest)) v.push(dest.replace(/plaza de armas/i, "Plaza Mayor"));
  else if (/plaza mayor/i.test(dest)) v.push(dest.replace(/plaza mayor/i, "Plaza de Armas"));
  return v;
}

async function findPlace(lat, lon, dest) {
  const d = 0.05;
  const viewbox = `${lon - d},${lat + d},${lon + d},${lat - d}`;
  let hits = [];
  for (const v of queryVariants(dest)) {
    const q = encodeURIComponent(v);
    hits = hits.concat(await getJson(`${NOMINATIM}/search?format=jsonv2&limit=10&q=${q}&viewbox=${viewbox}&bounded=1`));
  }
  if (!hits.length) {
    hits = await getJson(`${NOMINATIM}/search?format=jsonv2&limit=10&countrycodes=pe&q=${encodeURIComponent(dest)}`);
  }
  if (!hits.length) {
    return null;
  }
  // Nominatim ordena por relevancia, no por distancia: hay muchas "Plaza de Armas", la útil es la más cercana
  const dist2 = (h) => (Number(h.lat) - lat) ** 2 + ((Number(h.lon) - lon) * Math.cos((lat * Math.PI) / 180)) ** 2;
  const h = hits.reduce((best, x) => (dist2(x) < dist2(best) ? x : best));
  return { name: h.name || h.display_name.split(",")[0], lat: Number(h.lat), lon: Number(h.lon) };
}

// --- Indicaciones en español a partir de las maniobras de OSRM ---

const TURN = {
  left: "gira a la izquierda",
  "slight left": "gira ligeramente a la izquierda",
  "sharp left": "gira cerrado a la izquierda",
  right: "gira a la derecha",
  "slight right": "gira ligeramente a la derecha",
  "sharp right": "gira cerrado a la derecha",
  straight: "sigue derecho",
  uturn: "da la vuelta",
};

function instruction(step) {
  const m = step.maneuver;
  const street = step.name ? ` por ${step.name}` : "";
  const onto = step.name ? ` en ${step.name}` : "";
  const then = step.distance >= 20 ? ` y sigue ${metersText(step.distance)}` : "";

  switch (m.type) {
    case "depart":
      return `Camina${street} ${metersText(step.distance)}.`;
    case "arrive":
      return "Has llegado a tu destino.";
    case "roundabout":
    case "rotary":
      return `En la rotonda, toma la salida ${m.exit || 1}${onto}.`;
    case "continue":
    case "new name":
      return `Continúa${street}${then}.`;
    default: {
      const turn = TURN[m.modifier] || "continúa";
      return `${capitalize(turn)}${onto}${then}.`;
    }
  }
}

function sideOf(m) {
  if (m.type === "arrive" || m.type === "depart") return "center";
  if (!m.modifier) return "center";
  if (m.modifier.includes("left")) return "left";
  if (m.modifier.includes("right")) return "right";
  return "center";
}

function metersText(m) {
  if (m >= 1000) return `${(m / 1000).toFixed(1).replace(".", ",")} kilómetros`;
  return `${Math.max(10, Math.round(m / 10) * 10)} metros`;
}

function capitalize(s) {
  return s.charAt(0).toUpperCase() + s.slice(1);
}

function round(v) {
  return Math.round(v * 1e6) / 1e6;
}
