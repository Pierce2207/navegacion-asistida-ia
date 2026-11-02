// scripts/test_api_keys.js
//
// Prueba rapida de las claves de Gemini y Deepgram, corriendo en la PC (no en el ESP32).
// Esto NO reemplaza el pipeline real del firmware (que usa la camara/microfono/altavoz
// fisicos del dispositivo) — solo confirma que las cuentas y las claves funcionan,
// util para grabar en el video mientras el hardware no esta armado.
//
// Uso: node scripts/test_api_keys.js
// Requiere Node 18+ (usa fetch nativo).

const fs = require("fs");
const path = require("path");

function readKeysFromSecrets() {
  const secretsPath = path.join(__dirname, "..", "firmware", "src", "secrets.cpp");
  const content = fs.readFileSync(secretsPath, "utf8");
  const geminiMatch = content.match(/GEMINI_API_KEY\s*=\s*"([^"]+)"/);
  const deepgramMatch = content.match(/DEEPGRAM_API_KEY\s*=\s*"([^"]+)"/);
  return {
    gemini: geminiMatch ? geminiMatch[1] : null,
    deepgram: deepgramMatch ? deepgramMatch[1] : null,
  };
}

async function testGemini(apiKey) {
  console.log("\n=== Probando Gemini (texto) ===");
  if (!apiKey) {
    console.log("No se encontro GEMINI_API_KEY en firmware/src/secrets.cpp");
    return;
  }
  // gemini-2.0-flash quedo descontinuado (Google recomienda gemini-3.6-flash).
  const url = `https://generativelanguage.googleapis.com/v1beta/models/gemini-3.6-flash:generateContent?key=${apiKey}`;
  const body = {
    contents: [{ parts: [{ text: "Responde en una sola frase: ¿que eres y para que sirves?" }] }],
  };

  try {
    const res = await fetch(url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    const data = await res.json();

    if (!res.ok) {
      console.log(`Error HTTP ${res.status}:`, JSON.stringify(data));
      return;
    }

    const text = data?.candidates?.[0]?.content?.parts?.[0]?.text ?? "(sin texto)";
    console.log("Gemini respondio:");
    console.log(text.trim());
  } catch (err) {
    console.log("Fallo la llamada a Gemini:", err.message);
  }
}

async function checkLiveModel(apiKey) {
  console.log("\n=== Revisando si el modelo Live del firmware sigue vigente ===");
  if (!apiKey) return;
  const firmwareModel = "gemini-2.5-flash-live-preview";
  try {
    const res = await fetch(
      `https://generativelanguage.googleapis.com/v1beta/models?key=${apiKey}&pageSize=200`
    );
    const data = await res.json();
    if (!res.ok) {
      console.log(`Error HTTP ${res.status}:`, JSON.stringify(data));
      return;
    }
    const names = (data.models || []).map((m) => m.name.replace("models/", ""));
    const liveModels = names.filter((n) => n.includes("live"));

    console.log("Modelos 'live' disponibles hoy:", liveModels.length ? liveModels : "(ninguno encontrado)");

    if (names.includes(firmwareModel)) {
      console.log(`OK: el firmware usa "${firmwareModel}" y SIGUE disponible.`);
    } else {
      console.log(
        `ATENCION: el firmware usa "${firmwareModel}" y NO aparece en la lista actual de modelos.`
      );
      console.log(
        `Puede que ya este descontinuado. Habria que actualizar vision_assistant.cpp con uno de los modelos "live" listados arriba.`
      );
    }
  } catch (err) {
    console.log("Fallo la consulta de modelos:", err.message);
  }
}

async function testDeepgram(apiKey) {
  console.log("\n=== Probando Deepgram ===");
  if (!apiKey) {
    console.log("No se encontro DEEPGRAM_API_KEY en firmware/src/secrets.cpp");
    return;
  }
  // Audio de ejemplo publico de Deepgram, solo para la prueba.
  const audioUrl = "https://static.deepgram.com/examples/Bueller-Life-moves-pretty-fast.wav";

  try {
    const res = await fetch("https://api.deepgram.com/v1/listen?model=nova-2&smart_format=true", {
      method: "POST",
      headers: {
        Authorization: `Token ${apiKey}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ url: audioUrl }),
    });
    const data = await res.json();

    if (!res.ok) {
      console.log(`Error HTTP ${res.status}:`, JSON.stringify(data));
      return;
    }

    const transcript =
      data?.results?.channels?.[0]?.alternatives?.[0]?.transcript ?? "(sin transcripcion)";
    console.log("Deepgram transcribio el audio de prueba:");
    console.log(transcript);
  } catch (err) {
    console.log("Fallo la llamada a Deepgram:", err.message);
  }
}

(async () => {
  const keys = readKeysFromSecrets();
  await testGemini(keys.gemini);
  await checkLiveModel(keys.gemini);
  await testDeepgram(keys.deepgram);
  console.log(
    "\nListo. Esto solo confirma que las claves funcionan — el pipeline real (camara, microfono, altavoz) corre en el ESP32 y necesita el hardware armado."
  );
})();
