#!/usr/bin/env python3
"""Graba con el microfono de la PC/laptop a 16 kHz mono (para sumar voces cuando no hay placa a mano).

Modo guiado (--guiado): muestra "¡Di VISIÓN!" cada --cada segundos y graba todo en un WAV largo;
despues cortar con cortar_clips.py. Sin --guiado graba de corrido (util para ruido de ambiente/TV).

  python scripts/kws/grabar_pc.py --salida grab/vision_ana_pc.wav --guiado --veces 60
  python scripts/kws/grabar_pc.py --salida grab/ruido_cocina.wav --segundos 300

Importante: el modelo va a escuchar el microfono INMP441 de la placa. Las muestras de PC ayudan a
tener mas voces, pero conviene que al menos 1/3 de los datos salgan de la placa (capturar_esp32.py).
"""
import argparse
import time
import wave

import numpy as np
import sounddevice as sd

SAMPLE_RATE = 16000


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--salida", required=True)
    ap.add_argument("--guiado", action="store_true", help="pide decir la palabra a intervalos")
    ap.add_argument("--palabra", default="VISIÓN")
    ap.add_argument("--veces", type=int, default=50, help="repeticiones en modo guiado")
    ap.add_argument("--cada", type=float, default=2.0, help="segundos entre repeticiones")
    ap.add_argument("--segundos", type=float, default=60, help="duracion sin --guiado")
    ap.add_argument("--dispositivo", default=None, help="indice o nombre del microfono (ver --listar)")
    ap.add_argument("--listar", action="store_true", help="lista los microfonos y sale")
    args = ap.parse_args()

    if args.listar:
        print(sd.query_devices())
        return

    duration = args.veces * args.cada + 1.0 if args.guiado else args.segundos
    frames = int(duration * SAMPLE_RATE)
    print(f"Grabando {duration:.0f} s en {args.salida} ...")
    rec = sd.rec(frames, samplerate=SAMPLE_RATE, channels=1, dtype="int16", device=args.dispositivo)
    t0 = time.time()
    if args.guiado:
        time.sleep(0.8)
        for i in range(args.veces):
            target = t0 + 0.8 + i * args.cada
            time.sleep(max(0, target - time.time()))
            print(f"  [{i + 1:3d}/{args.veces}]  ¡Di {args.palabra}!")
    sd.wait()

    with wave.open(args.salida, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        w.writeframes(np.asarray(rec).astype(np.int16).tobytes())
    peak = int(np.abs(rec).max())
    print(f"Listo. Pico {peak} de 32767" + ("  (muy bajo: acercate o sube el volumen del mic)" if peak < 3000 else ""))


if __name__ == "__main__":
    main()
