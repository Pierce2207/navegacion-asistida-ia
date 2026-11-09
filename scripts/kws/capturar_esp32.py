#!/usr/bin/env python3
"""Graba audio del microfono de la placa (firmware del entorno mic-capture) a un WAV.

La placa manda bloques "AUD0" + uint16 n + n muestras int16 (16 kHz mono) por USB a 921600 baudios.
Graba hasta Ctrl+C o hasta --segundos. Luego cortar con cortar_clips.py.

Ejemplos:
  python scripts/kws/capturar_esp32.py --puerto COM5 --salida grab/vision_sebas_1m.wav
  python scripts/kws/capturar_esp32.py --puerto /dev/ttyUSB0 --salida grab/ruido_calle.wav --segundos 600

Sugerencia para "vision": decir "visión" cada ~2 s (pausa entre una y otra), 2-3 minutos por
persona y distancia. cortar_clips.py corta cada palabra en un clip de 1 s.
"""
import argparse
import struct
import sys
import time
import wave

import serial  # pyserial

SAMPLE_RATE = 16000
MAGIC = b"AUD0"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--puerto", required=True, help="COM5 en Windows, /dev/ttyUSB0 en Linux")
    ap.add_argument("--salida", required=True, help="archivo .wav de salida")
    ap.add_argument("--segundos", type=float, default=0, help="duracion maxima (0 = hasta Ctrl+C)")
    ap.add_argument("--baudios", type=int, default=921600)
    args = ap.parse_args()

    ser = serial.Serial(args.puerto, args.baudios, timeout=1)
    # Abrir el puerto reinicia la placa (DTR/RTS); en la ESP32-CAM-MB hay que dejarlas en bajo
    ser.dtr = False
    ser.rts = False
    print(f"Esperando audio de la placa en {args.puerto}... (Ctrl+C para terminar)")

    out = wave.open(args.salida, "wb")
    out.setnchannels(1)
    out.setsampwidth(2)
    out.setframerate(SAMPLE_RATE)

    buf = bytearray()
    total = 0
    started = None
    last_print = 0
    try:
        while True:
            chunk = ser.read(4096)
            if not chunk:
                continue
            buf.extend(chunk)
            while True:
                i = buf.find(MAGIC)
                if i < 0:
                    del buf[:-3]  # conserva un posible MAGIC partido
                    break
                if len(buf) < i + 6:
                    del buf[:i]
                    break
                (n,) = struct.unpack_from("<H", buf, i + 4)
                if n == 0 or n > 2048:  # basura (texto de arranque): saltar este MAGIC
                    del buf[: i + 4]
                    continue
                end = i + 6 + 2 * n
                if len(buf) < end:
                    del buf[:i]
                    break
                out.writeframes(bytes(buf[i + 6 : end]))
                total += n
                del buf[:end]
                if started is None:
                    started = time.time()
                    print("Grabando.")
            secs = total / SAMPLE_RATE
            if secs - last_print >= 5:
                last_print = secs
                rate = total / max(time.time() - started, 1e-6) if started else 0
                print(f"  {secs:6.0f} s grabados (~{rate:,.0f} muestras/s; deberia ser ~16000)")
            if args.segundos and secs >= args.segundos:
                break
    except KeyboardInterrupt:
        pass
    finally:
        out.close()
        ser.close()
    print(f"Listo: {total / SAMPLE_RATE:.1f} s en {args.salida}")
    if total == 0:
        print("No llego audio. ¿Subiste el entorno mic-capture? ¿Puerto correcto? ¿Monitor serie cerrado?")
        sys.exit(1)


if __name__ == "__main__":
    main()
