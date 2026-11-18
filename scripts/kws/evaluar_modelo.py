#!/usr/bin/env python3
"""Mide FALSOS POSITIVOS POR HORA y aciertos del modelo de "vision", con la MISMA logica que el
firmware (ventana de 1 s cada 250 ms, umbral, 2 de 3 ventanas, 1.5 s de pausa tras detectar).

Necesita el modelo exportado como "Linux (x86)" / "Linux (AARCH64)" desde Edge Impulse
(Deployment -> Linux -> archivo .eim) y `pip install edge_impulse_linux`. Corre en Linux, macOS
o WSL (en Windows nativo no corre el .eim).

  # 1 hora (o mas) de audio SIN "visión": TV, conversacion, calle, radio...
  python scripts/kws/evaluar_modelo.py modelo.eim --negativos grab/tv_1h.wav grab/calle_30min.wav
  # grabaciones con un numero conocido de "visión" (p. ej. las de grabar_pc.py --guiado --veces 60)
  python scripts/kws/evaluar_modelo.py modelo.eim --positivos grab/vision_luis.wav --esperadas 60

Imprime una tabla por umbral para elegir WAKE_LOCAL_THRESHOLD (objetivo: < 1 falso positivo/hora
sin perder demasiados aciertos). Los WAV deben ser 16 kHz mono 16 bit (cortar_clips.py convierte).
"""
import argparse
import os
import sys
import wave

import numpy as np

SR = 16000
WINDOW = SR
HOP = SR // 4          # WAKE_LOCAL_HOP_MS = 250
MIN_HITS = 2           # WAKE_LOCAL_MIN_HITS
HISTORY = 3            # WAKE_LOCAL_HISTORY
COOLDOWN_S = 1.5       # WAKE_LOCAL_COOLDOWN_MS
THRESHOLDS = [0.5, 0.6, 0.7, 0.8, 0.85, 0.9, 0.95]


def read_wav(path):
    with wave.open(path, "rb") as w:
        if w.getframerate() != SR or w.getnchannels() != 1 or w.getsampwidth() != 2:
            sys.exit(f"{path}: debe ser 16 kHz, mono, 16 bit")
        return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)


def window_scores(runner, label, x):
    scores = []
    for start in range(0, len(x) - WINDOW + 1, HOP):
        res = runner.classify(x[start : start + WINDOW].astype(int).tolist())
        scores.append(float(res["result"]["classification"].get(label, 0.0)))
    return np.array(scores)


def count_detections(scores, threshold):
    """Replica wakeWordLocalUpdate() de firmware/src/wake_word_local.cpp."""
    history = [False] * HISTORY
    pos, last, count = 0, None, 0
    for i, s in enumerate(scores):
        t = i * HOP / SR
        history[pos] = s >= threshold
        pos = (pos + 1) % HISTORY
        if last is not None and t - last < COOLDOWN_S:
            continue
        if sum(history) >= MIN_HITS:
            count += 1
            last = t
            history = [False] * HISTORY
    return count


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("modelo", help="archivo .eim exportado de Edge Impulse")
    ap.add_argument("--negativos", nargs="*", default=[], help="WAV largos sin la palabra")
    ap.add_argument("--positivos", nargs="*", default=[], help="WAV con 'visión' dicha --esperadas veces en total")
    ap.add_argument("--esperadas", type=int, default=0, help="cuantas 'visión' hay en total en --positivos")
    ap.add_argument("--etiqueta", default="vision")
    args = ap.parse_args()

    from edge_impulse_linux.runner import ImpulseRunner  # pip install edge_impulse_linux

    runner = ImpulseRunner(os.path.abspath(args.modelo))
    try:
        info = runner.init()
        params = info["model_parameters"]
        print(f"Modelo: {info['project']['owner']} / {info['project']['name']}  clases: {params['labels']}")
        if params.get("frequency") and int(params["frequency"]) != SR:
            sys.exit("El modelo no es de 16 kHz")

        neg_scores, neg_hours = [], 0.0
        for path in args.negativos:
            x = read_wav(path)
            neg_hours += len(x) / SR / 3600
            print(f"Clasificando {path} ({len(x) / SR / 60:.1f} min)...")
            neg_scores.append(window_scores(runner, args.etiqueta, x))
        pos_scores = []
        for path in args.positivos:
            x = read_wav(path)
            print(f"Clasificando {path} ({len(x) / SR / 60:.1f} min)...")
            pos_scores.append(window_scores(runner, args.etiqueta, x))
    finally:
        runner.stop()

    print("\numbral | falsos positivos | FP por hora | aciertos")
    for th in THRESHOLDS:
        fp = sum(count_detections(s, th) for s in neg_scores)
        fph = f"{fp / neg_hours:6.2f}" if neg_hours else "   -  "
        if pos_scores and args.esperadas:
            hits = sum(count_detections(s, th) for s in pos_scores)
            acc = f"{hits}/{args.esperadas} ({100 * min(hits, args.esperadas) / args.esperadas:.0f}%)"
        else:
            acc = "-"
        print(f"  {th:.2f} | {fp:16d} | {fph:>11} | {acc}")
    if neg_hours and neg_hours < 1:
        print(f"\nOjo: solo {neg_hours * 60:.0f} min de negativos; para medir '< 1 por hora' hace falta 1 h o mas.")


if __name__ == "__main__":
    main()
