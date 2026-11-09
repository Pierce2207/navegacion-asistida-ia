// scripts/test_hazards.js
//
// Prueba la red de peligros compartidos (api/hazards.js) SIN hardware: simula varios dispositivos
// que reportan el mismo peligro y comprueba que
//   1. el primer reporte crea un peligro "unverified" que el GET todavia NO devuelve,
//   2. los reportes de otros dispositivos a pocos metros se fusionan (merged) en vez de duplicarse,
//   3. al 3er reporte queda "verified" y GET /api/hazards?lat=..&lon=..&radius=.. lo devuelve,
//   4. un punto a 1 km no lo ve.
//
// Para no ensuciar datos reales usa la categoria "test" y un punto EN EL MAR, ~55 km frente a Lima
// (ningun aparato pasa por ahi; el firmware solo pide peligros a <15 m de su GPS). Cada corrida
// elige un punto distinto, asi no se fusiona con corridas anteriores. Los peligros vencen a las
// 48 h (expires_at) y dejan de salir en el GET; para borrar las filas de la tabla, en el SQL
// Editor de Supabase:   delete from hazards where category like 'test%';
//
// Uso (Node 18+, sin dependencias):
//   node scripts/test_hazards.js                 # contra produccion (Vercel)
//   node scripts/test_hazards.js --base http://localhost:3000
//   node scripts/test_hazards.js --dispositivos 4
//   node scripts/test_hazards.js --mismo-dispositivo   # ademas: ¿un solo aparato puede verificar solo?
//   node scripts/test_hazards.js --dry-run       # muestra lo que haria, sin enviar nada

const args = process.argv.slice(2);
function argValue(name, fallback) {
  const i = args.indexOf(name);
  return i >= 0 && args[i + 1] ? args[i + 1] : fallback;
}
const BASE = argValue("--base", "https://navegacion-asistida-ia.vercel.app").replace(/\/$/, "");
const DEVICES = Math.max(3, parseInt(argValue("--dispositivos", "3"), 10) || 3);
const CHECK_SAME_DEVICE = args.includes("--mismo-dispositivo");
const DRY_RUN = args.includes("--dry-run");

const CATEGORY = "test";
const VERIFICATION_THRESHOLD = 3; // igual que api/hazards.js
const MERGE_RADIUS_M = 15;        // igual que api/hazards.js

// Punto de prueba en el mar frente a Lima, con un desplazamiento al azar de hasta ~1 km por corrida
const BASE_LAT = -12.3;
const BASE_LON = -77.6;
const LAT0 = BASE_LAT + (Math.random() - 0.5) * 0.02;
const LON0 = BASE_LON + (Math.random() - 0.5) * 0.02;
const RUN_ID = Date.now().toString(36);

// Desplaza un punto unos metros (para simular el error del GPS de cada aparato)
function offset(lat, lon, northM, eastM) {
  const dLat = northM / 111320;
  const dLon = eastM / (111320 * Math.cos((lat * Math.PI) / 180));
  return { lat: lat + dLat, lon: lon + dLon };
}

let failures = 0;
let warnings = 0;
function check(ok, message, detail) {
  console.log(`${ok ? "  OK  " : "  FALLA"} ${message}`);
  if (!ok) {
    failures++;
    if (detail !== undefined) console.log("        ->", JSON.stringify(detail));
  }
  return ok;
}
function warn(message) {
  warnings++;
  console.log(`  AVISO ${message}`);
}

async function post(body) {
  if (DRY_RUN) {
    console.log("  [dry-run] POST /api/hazards", JSON.stringify(body));
    return { status: 0, data: null };
  }
  const res = await fetch(`${BASE}/api/hazards`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
  const text = await res.text();
  let data;
  try { data = JSON.parse(text); } catch { data = text; }
  return { status: res.status, data };
}

async function getNearby(lat, lon, radius) {
  const url = `${BASE}/api/hazards?lat=${lat}&lon=${lon}&radius=${radius}`;
  if (DRY_RUN) {
    console.log("  [dry-run] GET", url.replace(BASE, ""));
    return { status: 0, data: [] };
  }
  const res = await fetch(url);
  const text = await res.text();
  let data;
  try { data = JSON.parse(text); } catch { data = text; }
  return { status: res.status, data };
}

const findHazard = (list, id) => (Array.isArray(list) ? list.find((h) => h.id === id) : undefined);

async function mainFlow() {
  console.log(`Red de peligros: ${BASE}`);
  console.log(`Punto de prueba: ${LAT0.toFixed(6)}, ${LON0.toFixed(6)} (mar frente a Lima), categoria "${CATEGORY}"`);
  console.log(`Dispositivos simulados: ${DEVICES}${DRY_RUN ? "  [DRY RUN: no se envia nada]" : ""}\n`);

  // 0. El GET no debe devolver nada de categoria test en ese punto antes de empezar
  console.log("0) Estado inicial");
  const before = await getNearby(LAT0, LON0, 50);
  if (!DRY_RUN) {
    check(before.status === 200, `GET responde 200 (${before.status})`, before.data);
    const leftovers = Array.isArray(before.data) ? before.data.filter((h) => h.category === CATEGORY) : [];
    if (leftovers.length) warn(`ya habia ${leftovers.length} peligro(s) "test" verificados aqui (otra corrida)`);
  }

  // 1..N. Cada dispositivo reporta el mismo peligro, con unos metros de error de GPS
  let hazardId = null;
  for (let i = 0; i < DEVICES; i++) {
    const deviceId = `test-${RUN_ID}-disp${i + 1}`;
    const p = i === 0 ? { lat: LAT0, lon: LON0 } : offset(LAT0, LON0, 3 * i, -2 * i); // < 15 m
    console.log(`\n${i + 1}) ${deviceId} reporta en ${p.lat.toFixed(6)}, ${p.lon.toFixed(6)}`);
    const r = await post({ lat: p.lat, lon: p.lon, category: CATEGORY, confidence: 0.9, device_id: deviceId });
    if (DRY_RUN) continue;

    const expectedCount = i + 1;
    const expectedStatus = expectedCount >= VERIFICATION_THRESHOLD ? "verified" : "unverified";
    if (i === 0) {
      check(r.status === 201, `crea un peligro nuevo (HTTP ${r.status}, esperado 201)`, r.data);
      check(r.data && r.data.merged === false, "merged = false", r.data);
      hazardId = r.data && r.data.id;
    } else {
      check(r.status === 200, `se fusiona con el existente (HTTP ${r.status}, esperado 200)`, r.data);
      check(r.data && r.data.merged === true, "merged = true", r.data);
      check(r.data && r.data.id === hazardId, "mismo id que el primer reporte", r.data && r.data.id);
    }
    check(r.data && r.data.verified_count === expectedCount,
      `verified_count = ${r.data && r.data.verified_count} (esperado ${expectedCount})`);
    check(r.data && r.data.status === expectedStatus,
      `status = ${r.data && r.data.status} (esperado ${expectedStatus})`);

    // Antes del umbral, nadie mas debe recibir el aviso
    if (expectedCount < VERIFICATION_THRESHOLD) {
      const g = await getNearby(LAT0, LON0, 50);
      check(g.status === 200 && !findHazard(g.data, hazardId),
        "GET todavia NO lo devuelve (sin verificar)", g.data);
    }
  }

  if (!DRY_RUN) {
    // N+1. Ya verificado: GET cercano lo devuelve, GET a 1 km no
    console.log(`\n${DEVICES + 1}) Consultas despues de verificar`);
    const near = offset(LAT0, LON0, 6, 6);
    const g = await getNearby(near.lat, near.lon, MERGE_RADIUS_M);
    check(g.status === 200, `GET radius=${MERGE_RADIUS_M} responde 200 (${g.status})`, g.data);
    const h = findHazard(g.data, hazardId);
    check(!!h, "GET lo devuelve a ~8 m (lo que consulta el firmware)", g.data);
    if (h) {
      check(h.status === "verified", `status = ${h.status}`);
      check(h.category === CATEGORY, `category = ${h.category}`);
      check(typeof h.distance_m === "number" && h.distance_m < MERGE_RADIUS_M,
        `distance_m = ${h.distance_m && h.distance_m.toFixed(1)} m (< ${MERGE_RADIUS_M})`);
    }
    const far = offset(LAT0, LON0, 1000, 0);
    const gFar = await getNearby(far.lat, far.lon, 100);
    check(gFar.status === 200 && !findHazard(gFar.data, hazardId), "GET a 1 km con radius=100 NO lo devuelve", gFar.data);
  }

  // Opcional: ¿un mismo aparato que reporta 3 veces lo verifica solo? (api/hazards.js no mira
  // device_id al fusionar; si pasa, un solo aparato con un falso positivo lo anuncia a todos)
  if (CHECK_SAME_DEVICE) {
    console.log(`\n${DEVICES + 2}) Un solo dispositivo reporta 3 veces (otro punto, a ~200 m)`);
    const p = offset(LAT0, LON0, -200, 0);
    let last = null;
    for (let i = 0; i < 3; i++) {
      last = await post({ lat: p.lat, lon: p.lon, category: CATEGORY, confidence: 0.9, device_id: `test-${RUN_ID}-solo` });
    }
    if (!DRY_RUN) {
      if (last.data && last.data.status === "verified") {
        warn("un solo dispositivo verifico el peligro: la API no exige reportes de aparatos DISTINTOS");
      } else {
        check(true, `un solo dispositivo no alcanza para verificar (status ${last.data && last.data.status})`);
      }
    }
  }

  console.log("\n----------------------------------------");
  if (DRY_RUN) {
    console.log("Dry run terminado (no se envio nada).");
    return 0;
  }
  console.log(failures === 0
    ? `RESULTADO: OK (${warnings} aviso(s)). Peligro de prueba: ${hazardId}`
    : `RESULTADO: ${failures} FALLA(S), ${warnings} aviso(s).`);
  console.log("Limpieza (opcional, SQL Editor de Supabase): delete from hazards where category like 'test%';");
  return failures === 0 ? 0 : 1;
}

mainFlow()
  .then((code) => process.exit(code))
  .catch((err) => {
    console.error("\nError de red o inesperado:", err.message);
    console.error("¿Hay internet? ¿Supabase esta pausado? (ver CLAUDE.md: 'Resume project')");
    process.exit(2);
  });
