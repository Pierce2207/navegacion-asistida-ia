#!/usr/bin/env python3
"""Corta grabaciones largas en clips de 1 s (16 kHz mono, int16) listos para Edge Impulse.

Modo "palabras": encuentra cada palabra dicha (picos de energia separados por silencio) y guarda
1 s centrado en ella. Para grabaciones de "visión, ... visión, ..." o de otras palabras sueltas.
Modo "fijo": trozos consecutivos de 1 s (para ruido: calle, TV, cocina, viento...).

  python scripts/kws/cortar_clips.py grab/vision_sebas_1m.wav --etiqueta vision --salida clips/vision
  python scripts/kws/cortar_clips.py grab/otras_ana.wav --etiqueta otras_palabras --salida clips/otras_palabras
  python scripts/kws/cortar_clips.py grab/ruido_calle.wav --etiqueta ruido --modo fijo --salida clips/ruido

Los clips se llaman <etiqueta>.<grabacion>.<n>.wav. Escuchar unos cuantos al azar antes de subir:
un clip de "vision" con la palabra cortada o sin palabra le enseña mal al modelo.
"""
import argparse
import os
import wave

import numpy as np

SR = 16000
CLIP = SR  # 1 s


def read_wav(path):
    with wave.open(path, "rb") as w:
        sr, ch, sw = w.getframerate(), w.getnchannels(), w.getsampwidth()
        raw = w.readframes(w.getnframes())
    if sw != 2:
        raise SystemExit(f"{path}: solo WAV de 16 bit (este tiene {8 * sw} bit). Convertir con Audacity.")
    x = np.frombuffer(raw, dtype=np.int16).astype(np.float32)
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    if sr != SR:  # remuestreo lineal sencillo (suficiente para voz)
        n = int(len(x) * SR / sr)
        x = np.interp(np.linspace(0, len(x) - 1, n), np.arange(len(x)), x)
    return x


def write_clip(path, x):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(np.clip(x, -32768, 32767).astype(np.int16).tobytes())


def find_words(x, factor, min_gap_s, min_len_s, max_len_s):
    """Devuelve (inicio, fin) en muestras de cada tramo con voz."""
    hop = SR // 50  # tramas de 20 ms
    n = len(x) // hop
    frames = x[: n * hop].reshape(n, hop)
    rms = np.sqrt(np.mean((frames - frames.mean(axis=1, keepdims=True)) ** 2, axis=1) + 1e-9)
    floor = np.percentile(rms, 20)
    active = rms > max(floor * factor, 150)
    words, start, last_on = [], None, None
    for i, on in enumerate(active):
        if on:
            if start is None:
                start = i
            last_on = i
        elif start is not None and (i - last_on) * hop >= min_gap_s * SR:
            words.append((start * hop, (last_on + 1) * hop))
            start = None
    if start is not None:
        words.append((start * hop, (last_on + 1) * hop))
    kept, skipped = [], 0
    for s, e in words:
        dur = (e - s) / SR
        if min_len_s <= dur <= max_len_s:
            kept.append((s, e))
        else:
            skipped += 1
    return kept, skipped


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("entradas", nargs="+", help="archivos .wav")
    ap.add_argument("--etiqueta", required=True, help="vision | otras_palabras | ruido")
    ap.add_argument("--salida", required=True, help="carpeta de salida")
    ap.add_argument("--modo", choices=["palabras", "fijo"], default="palabras")
    ap.add_argument("--factor", type=float, default=4.0, help="cuanto mas fuerte que el ruido de fondo es voz")
    ap.add_argument("--pausa", type=float, default=0.30, help="silencio minimo entre palabras (s)")
    ap.add_argument("--min", type=float, default=0.20, help="duracion minima de una palabra (s)")
    ap.add_argument("--max", type=float, default=0.95, help="duracion maxima de una palabra (s)")
    args = ap.parse_args()

    os.makedirs(args.salida, exist_ok=True)
    rng = np.random.default_rng(0)
    grand_total = 0
    for path in args.entradas:
        x = read_wav(path)
        base = os.path.splitext(os.path.basename(path))[0].replace(".", "_")
        count = 0
        if args.modo == "fijo":
            for k in range(len(x) // CLIP):
                write_clip(os.path.join(args.salida, f"{args.etiqueta}.{base}.{k:04d}.wav"), x[k * CLIP : (k + 1) * CLIP])
                count += 1
            print(f"{path}: {count} clips de 1 s")
        else:
            words, skipped = find_words(x, args.factor, args.pausa, args.min, args.max)
            for s, e in words:
                # 1 s alrededor de la palabra, con un corrimiento al azar (el modelo la oira en
                # cualquier parte de la ventana deslizante del firmware)
                slack = CLIP - (e - s)
                start = s - int(rng.uniform(0.2, 0.8) * slack)
                start = min(max(start, 0), len(x) - CLIP)
                if start < 0:
                    continue
                write_clip(os.path.join(args.salida, f"{args.etiqueta}.{base}.{count:04d}.wav"), x[start : start + CLIP])
                count += 1
            print(f"{path}: {count} palabras cortadas" + (f", {skipped} tramos descartados (muy cortos/largos)" if skipped else ""))
        grand_total += count
    print(f"Total: {grand_total} clips en {args.salida}")


if __name__ == "__main__":
    main()
