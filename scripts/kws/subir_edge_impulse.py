#!/usr/bin/env python3
"""Sube clips WAV a un proyecto de Edge Impulse (API de ingestion) con su etiqueta.

La API key del proyecto se lee de la variable de entorno EI_API_KEY (Edge Impulse -> Dashboard ->
Keys). NUNCA escribirla en el codigo ni subirla a git.

  set EI_API_KEY=ei_xxx            (Windows cmd)      $env:EI_API_KEY="ei_xxx"   (PowerShell)
  export EI_API_KEY=ei_xxx         (Linux/macOS)

  python scripts/kws/subir_edge_impulse.py clips/vision --etiqueta vision
  python scripts/kws/subir_edge_impulse.py clips/ruido --etiqueta ruido
  # Separar por PERSONA (mejor que al azar): las voces de prueba no se usan para entrenar
  python scripts/kws/subir_edge_impulse.py clips/vision --etiqueta vision --prueba-si-contiene _luis_

Sin --prueba-si-contiene, Edge Impulse reparte solo ~80% entrenamiento / 20% prueba.
"""
import argparse
import glob
import os
import sys

import requests

INGESTION = "https://ingestion.edgeimpulse.com/api/{category}/files"


def upload(files, label, category, api_key):
    url = INGESTION.format(category=category)
    ok = 0
    for i in range(0, len(files), 50):  # de a 50 archivos por pedido
        batch = files[i : i + 50]
        handles = [open(f, "rb") for f in batch]
        try:
            r = requests.post(
                url,
                headers={"x-api-key": api_key, "x-label": label, "x-disallow-duplicates": "1"},
                files=[("data", (os.path.basename(f), h, "audio/wav")) for f, h in zip(batch, handles)],
                timeout=120,
            )
        finally:
            for h in handles:
                h.close()
        if r.status_code != 200:
            print(f"  Error {r.status_code}: {r.text[:300]}")
            continue
        ok += len(batch)
        print(f"  {category}: {ok}/{len(files)} subidos")
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("carpeta", help="carpeta con los .wav de UNA clase")
    ap.add_argument("--etiqueta", required=True, help="vision | otras_palabras | ruido")
    ap.add_argument("--prueba-si-contiene", default=None,
                    help="los archivos cuyo nombre contenga este texto van a 'testing'; el resto a 'training'")
    args = ap.parse_args()

    api_key = os.environ.get("EI_API_KEY")
    if not api_key:
        sys.exit("Falta la variable de entorno EI_API_KEY (Edge Impulse -> Dashboard -> Keys)")
    files = sorted(glob.glob(os.path.join(args.carpeta, "*.wav")))
    if not files:
        sys.exit(f"No hay .wav en {args.carpeta}")

    if args.prueba_si_contiene:
        test = [f for f in files if args.prueba_si_contiene in os.path.basename(f)]
        train = [f for f in files if f not in test]
        print(f"{len(train)} a entrenamiento, {len(test)} a prueba (etiqueta '{args.etiqueta}')")
        total = upload(train, args.etiqueta, "training", api_key) + upload(test, args.etiqueta, "testing", api_key)
    else:
        print(f"{len(files)} archivos, reparto automatico 80/20 (etiqueta '{args.etiqueta}')")
        total = upload(files, args.etiqueta, "split", api_key)
    print(f"Listo: {total} de {len(files)} subidos.")


if __name__ == "__main__":
    main()
