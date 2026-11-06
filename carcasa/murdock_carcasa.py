"""
Carcasa del asistente MURDOCK (Halo+ Visión) para impresión 3D.

Genera dos piezas en STL:
  - murdock_frente.stl : cuerpo principal (cara frontal con cámara, parlante, micrófono y el
                         texto MURDOCK grabado; paredes; estantes internos)
  - murdock_tapa.stl   : tapa trasera deslizable, con traba y presillas para la correa del pecho

Uso:
  python murdock_carcasa.py            -> escribe los STL y las vistas previas (PNG)
Requiere: pip install manifold3d numpy matplotlib

Todas las medidas están en milímetros y se cambian en la sección PARÁMETROS. Las medidas de los
módulos son las estándar; MEDIR los reales con regla o vernier antes de imprimir (sobre todo el
portapilas, el parlante, la placa soldada y la posición de la lente de la cámara).

Sistema de coordenadas (visto de frente, como lo ve la gente):
  x: de izquierda a derecha del usuario es -x .. +x visto desde DENTRO del aparato
  y: de abajo hacia arriba
  z: desde la cara frontal (z = 0) hacia el pecho del usuario
"""

import math
import os

import numpy as np
from manifold3d import CrossSection, FillRule, JoinType, Manifold, Mesh

# ============================== PARÁMETROS ==============================

# --- Caja ---
PARED = 2.8            # grosor de las paredes
INT_ANCHO = 96.0       # espacio interior en x
INT_ALTO = 104.0       # espacio interior en y
INT_FONDO = 46.0       # espacio interior en z (de la cara frontal a la tapa)
RADIO_ESQUINA = 6.0    # redondeo de las esquinas verticales

# --- Tapa deslizable ---
TAPA_GROSOR = 2.2
RANURA_PROF = 1.4      # cuánto entra la tapa en cada pared
HOLGURA = 0.3          # holgura de impresión entre tapa y ranura (subir a 0.4 si queda dura)
LABIO = 1.4            # material detrás de la ranura que sostiene la tapa

# --- Cámara (lente de la ESP32-CAM) ---
CAMARA_X = 0.0         # centro de la lente (0 = centrado)
CAMARA_Y = 88.0        # altura del centro de la lente desde el piso interior
CAMARA_DIAM = 14.0     # ventana (la lente OV2640 mide ~8 mm; se deja margen)

# --- Placa soldada (perfboard con la ESP32-CAM en zócalo, PCF8574 y transistores) ---
PLACA_ANCHO = 70.0
PLACA_ALTO = 50.0
PLACA_Y = 52.0         # borde inferior de la placa
PLACA_SEPARACION = 16.0  # distancia de la cara frontal a la placa (= altura zócalo + ESP + cámara)
PLACA_AGUJERO_INSET = 3.0  # distancia del agujero de montaje al borde de la placa
TORNILLO_PLACA = 2.4   # agujero para tornillo autorroscante M2.5 (o pegar con silicona)

# --- Parlante ---
PARLANTE_X = -24.0
PARLANTE_Y = 26.0
PARLANTE_DIAM = 40.0
PARLANTE_REJILLA_RADIO = 15.0
REJILLA_AGUJERO = 2.4

# --- Micrófono INMP441 (placa redonda; su agujero de sonido mira a la cara frontal) ---
MIC_X = 30.0
MIC_Y = 26.0
MIC_PLACA_DIAM = 14.0
MIC_AGUJERO = 2.2

# --- Texto frontal ---
TEXTO = "MURDOCK"
TEXTO_Y = 51.0         # altura del centro del texto (sobre la cara frontal)
TEXTO_ALTO = 10.0      # altura de las letras
TEXTO_PROF = 0.8       # profundidad del grabado

# --- Estantes para la capa trasera (módulos de energía, GPS, amplificador) ---
CAPA_TRASERA_Z = 24.0  # los estantes van desde aquí hasta la tapa
ESTANTE_GROSOR = 2.0
ESTANTE1_Y = 46.0      # LM2596 (izquierda) y cargador USB-C (derecha)
ESTANTE2_Y = 68.0      # BMS, MAX98357A y placa del GPS

# --- Portapilas 2x18650 (en el piso, capa trasera) ---
PORTAPILAS_ANCHO = 77.0
PORTAPILAS_ALTO = 41.0
PORTAPILAS_GROSOR = 21.0

# --- Cargador USB-C (puerto en la pared derecha) ---
USB_ANCHO = 10.0       # recorte del conector tipo C (con margen para el enchufe)
USB_ALTO = 4.6
USB_Y = ESTANTE1_Y + ESTANTE_GROSOR + 3.2   # centro del conector sobre el estante
USB_Z = 35.0

# --- Interruptor KCD11 (pared izquierda) ---
INTERRUPTOR_ANCHO = 19.2   # medir el interruptor; el KCD11 pide ~19 x 13 mm
INTERRUPTOR_ALTO = 13.0
INTERRUPTOR_Y = 88.0
INTERRUPTOR_Z = 35.0

# --- Botón (pared superior, pulsador 12x12 con capuchón) ---
BOTON_X = 26.0
BOTON_Z = 12.0
BOTON_AGUJERO = 8.4

# --- Antena del GPS (pegada al techo interior, mirando al cielo) ---
GPS_ANTENA_X = -26.0
GPS_ANTENA_Z = 35.0
GPS_ANTENA_LADO = 25.6

# --- Salida del cable de los motores (pared inferior) ---
MOTORES_X = 30.0
MOTORES_Z = 30.0
MOTORES_ANCHO = 9.0
MOTORES_ALTO = 6.0

# --- Correa del pecho ---
CORREA_ANCHO = 26.0    # cinta de 25 mm
CORREA_GROSOR = 3.4
PRESILLA_X = 34.0      # distancia del centro a cada presilla
INCLINACION_GRADOS = 8.0  # inclina la cámara hacia abajo (la parte de arriba se aleja del pecho)

SEGMENTOS = 64

# ============================ AYUDANTES ============================

EXT_ANCHO = INT_ANCHO + 2 * PARED
EXT_ALTO = INT_ALTO + 2 * PARED
Z_TAPA = PARED + INT_FONDO                  # cara interior de la tapa
Z_FIN = Z_TAPA + TAPA_GROSOR + LABIO        # fin de las paredes laterales


def caja(x0, x1, y0, y1, z0, z1):
    x0, x1 = min(x0, x1), max(x0, x1)
    y0, y1 = min(y0, y1), max(y0, y1)
    z0, z1 = min(z0, z1), max(z0, z1)
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])


def cilindro_z(cx, cy, d, z0, z1, seg=SEGMENTOS):
    return Manifold.cylinder(z1 - z0, d / 2, d / 2, seg).translate([cx, cy, z0])


def cilindro_y(cx, cz, d, y0, y1, seg=SEGMENTOS):
    c = Manifold.cylinder(y1 - y0, d / 2, d / 2, seg)  # a lo largo de z
    return c.rotate([-90, 0, 0]).translate([cx, y0, cz])


def rectangulo_redondeado(ancho, alto, r):
    r = min(r, ancho / 2 - 0.01, alto / 2 - 0.01)
    sq = CrossSection.square([ancho - 2 * r, alto - 2 * r], True)
    return sq.offset(r, JoinType.Round, 2.0, SEGMENTOS)


def prisma(seccion, z0, z1):
    return Manifold.extrude(seccion, z1 - z0).translate([0, 0, z0])


def texto_seccion(texto, alto):
    """Contorno del texto con la fuente de matplotlib, centrado en (0, 0)."""
    from matplotlib.font_manager import FontProperties
    from matplotlib.textpath import TextPath

    fp = FontProperties(family="DejaVu Sans", weight="bold")
    tp = TextPath((0, 0), texto, size=alto, prop=fp)
    polys = [np.asarray(p) for p in tp.to_polygons(closed_only=True) if len(p) >= 3]
    todos = np.vstack(polys)
    cx = (todos[:, 0].min() + todos[:, 0].max()) / 2
    cy = (todos[:, 1].min() + todos[:, 1].max()) / 2
    escala = alto / (todos[:, 1].max() - todos[:, 1].min())  # altura real de las mayúsculas
    contornos = [((p - [cx, cy]) * escala).tolist() for p in polys]
    return CrossSection(contornos, FillRule.EvenOdd)


# =============================== FRENTE ===============================

def cuerpo():
    # Origen del interior: x centrado, y = 0 en el piso interior, z = 0 en la cara frontal
    x0, x1 = -EXT_ANCHO / 2, EXT_ANCHO / 2
    y0, y1 = -PARED, INT_ALTO + PARED

    exterior = prisma(rectangulo_redondeado(EXT_ANCHO, EXT_ALTO, RADIO_ESQUINA), 0, Z_FIN)
    exterior = exterior.translate([0, INT_ALTO / 2, 0])
    hueco = prisma(rectangulo_redondeado(INT_ANCHO, INT_ALTO, RADIO_ESQUINA - PARED), PARED, Z_FIN + 1)
    hueco = hueco.translate([0, INT_ALTO / 2, 0])
    c = exterior - hueco

    # Ranura de la tapa en las paredes izquierda, derecha e inferior
    rz0, rz1 = Z_TAPA - HOLGURA / 2, Z_TAPA + TAPA_GROSOR + HOLGURA / 2
    ranura = caja(-INT_ANCHO / 2 - RANURA_PROF, INT_ANCHO / 2 + RANURA_PROF,
                  -RANURA_PROF, INT_ALTO + PARED + 1, rz0, rz1)
    c = c - ranura
    # La pared de arriba se corta desde la ranura hacia atrás: por ahí entra la tapa
    c = c - caja(x0 - 1, x1 + 1, INT_ALTO - 0.01, y1 + 1, rz0, Z_FIN + 1)

    # --- Cara frontal ---
    c = c - cilindro_z(CAMARA_X, CAMARA_Y, CAMARA_DIAM, -1, PARED + 1)
    # chaflán hacia afuera para no recortar el campo de visión
    c = c - Manifold.cylinder(PARED * 0.6, CAMARA_DIAM / 2 + 3, CAMARA_DIAM / 2, SEGMENTOS)\
        .translate([CAMARA_X, CAMARA_Y, -0.01])

    paso = REJILLA_AGUJERO + 1.6
    for i in range(-8, 9):
        for j in range(-8, 9):
            px = i * paso + (paso / 2 if j % 2 else 0)
            py = j * paso * 0.866
            if px * px + py * py <= PARLANTE_REJILLA_RADIO ** 2:
                c = c - cilindro_z(PARLANTE_X + px, PARLANTE_Y + py, REJILLA_AGUJERO, -1, PARED + 1, 16)

    c = c - cilindro_z(MIC_X, MIC_Y, MIC_AGUJERO, -1, PARED + 1, 24)

    # Texto grabado. Visto desde fuera (mirando hacia +z) la x queda invertida: se refleja
    texto = prisma(texto_seccion(TEXTO, TEXTO_ALTO).mirror([1, 0]), -1, TEXTO_PROF)
    c = c - texto.translate([0, TEXTO_Y, 0])

    # --- Soportes internos de la cara frontal ---
    # Aro del parlante
    aro = cilindro_z(PARLANTE_X, PARLANTE_Y, PARLANTE_DIAM + 3.2, PARED, PARED + 4) - \
        cilindro_z(PARLANTE_X, PARLANTE_Y, PARLANTE_DIAM + 0.6, PARED - 1, PARED + 5)
    c = c + aro
    # Aro del micrófono, con junta para que el sonido del parlante no entre por detrás
    aro_mic = cilindro_z(MIC_X, MIC_Y, MIC_PLACA_DIAM + 3.2, PARED, PARED + 3) - \
        cilindro_z(MIC_X, MIC_Y, MIC_PLACA_DIAM + 0.5, PARED - 1, PARED + 4)
    c = c + aro_mic

    # Postes de la placa soldada
    for sx in (-1, 1):
        for yy in (PLACA_Y + PLACA_AGUJERO_INSET, PLACA_Y + PLACA_ALTO - PLACA_AGUJERO_INSET):
            px = sx * (PLACA_ANCHO / 2 - PLACA_AGUJERO_INSET)
            poste = cilindro_z(px, yy, 6.0, PARED - 0.1, PARED + PLACA_SEPARACION) - \
                cilindro_z(px, yy, TORNILLO_PLACA, PARED + 2, PARED + PLACA_SEPARACION + 1, 24)
            c = c + poste

    # --- Estantes de la capa trasera (unidos a las paredes laterales) ---
    for ey in (ESTANTE1_Y, ESTANTE2_Y):
        estante = caja(-INT_ANCHO / 2 - 0.1, INT_ANCHO / 2 + 0.1, ey, ey + ESTANTE_GROSOR,
                       CAPA_TRASERA_Z, Z_TAPA - HOLGURA)
        # ventanas para pasar cables entre capas y aligerar
        for vx in (-30, 0, 30):
            estante = estante - caja(vx - 9, vx + 9, ey - 1, ey + ESTANTE_GROSOR + 1,
                                     CAPA_TRASERA_Z + 4, Z_TAPA - 6)
        # cartela bajo el estante para que no se doble
        for sx in (-1, 1):
            cart = caja(sx * INT_ANCHO / 2 - (8 if sx > 0 else 0), sx * INT_ANCHO / 2 + (0 if sx > 0 else 8),
                        ey - 8, ey, CAPA_TRASERA_Z, CAPA_TRASERA_Z + 2)
            c = c + cart
        c = c + estante

    # --- Cuna del portapilas (en el piso, capa trasera) ---
    pz0 = Z_TAPA - HOLGURA - PORTAPILAS_GROSOR - 0.6
    for sx in (-1, 1):
        tope = caja(sx * (PORTAPILAS_ANCHO / 2 + 0.4), sx * (PORTAPILAS_ANCHO / 2 + 2.4), 0, 10, pz0, Z_TAPA - 1)
        c = c + tope
    frente_cuna = caja(-PORTAPILAS_ANCHO / 2, PORTAPILAS_ANCHO / 2, 0, 8, pz0 - 2, pz0)
    c = c + frente_cuna

    # --- Pared derecha: USB-C del cargador ---
    usb = prisma(rectangulo_redondeado(USB_ANCHO, USB_ALTO, USB_ALTO / 2 - 0.1), -1, PARED + 2)  # acostado: ancho en z
    usb = usb.rotate([0, 90, 0]).translate([INT_ANCHO / 2 - 1, USB_Y, USB_Z])
    c = c - usb

    # --- Pared izquierda: interruptor ---
    c = c - caja(-EXT_ANCHO / 2 - 1, -INT_ANCHO / 2 + 1,
                 INTERRUPTOR_Y - INTERRUPTOR_ALTO / 2, INTERRUPTOR_Y + INTERRUPTOR_ALTO / 2,
                 INTERRUPTOR_Z - INTERRUPTOR_ANCHO / 2, INTERRUPTOR_Z + INTERRUPTOR_ANCHO / 2)

    # --- Pared superior: botón y alojamiento del pulsador ---
    c = c - cilindro_y(BOTON_X, BOTON_Z, BOTON_AGUJERO, INT_ALTO - 1, INT_ALTO + PARED + 1)
    guia = caja(BOTON_X - 8.5, BOTON_X + 8.5, INT_ALTO - 6, INT_ALTO, BOTON_Z - 8.5, BOTON_Z + 8.5) - \
        caja(BOTON_X - 6.3, BOTON_X + 6.3, INT_ALTO - 7, INT_ALTO + 1, BOTON_Z - 6.3, BOTON_Z + 6.3)
    c = c + guia

    # --- Techo: marco para la antena cerámica del GPS ---
    marco = caja(GPS_ANTENA_X - GPS_ANTENA_LADO / 2 - 1.6, GPS_ANTENA_X + GPS_ANTENA_LADO / 2 + 1.6,
                 INT_ALTO - 3, INT_ALTO, GPS_ANTENA_Z - GPS_ANTENA_LADO / 2 - 1.6,
                 GPS_ANTENA_Z + GPS_ANTENA_LADO / 2 + 1.6) - \
        caja(GPS_ANTENA_X - GPS_ANTENA_LADO / 2, GPS_ANTENA_X + GPS_ANTENA_LADO / 2,
             INT_ALTO - 4, INT_ALTO + 1, GPS_ANTENA_Z - GPS_ANTENA_LADO / 2,
             GPS_ANTENA_Z + GPS_ANTENA_LADO / 2)
    c = c + marco

    # --- Pared inferior: salida del cable de motores y ventilación del regulador ---
    salida = prisma(rectangulo_redondeado(MOTORES_ANCHO, MOTORES_ALTO, 2.0), -1, PARED + 2)
    salida = salida.rotate([90, 0, 0]).translate([MOTORES_X, PARED - 0.5, MOTORES_Z])
    c = c - salida
    # ranuras de aire en la pared izquierda, a la altura del LM2596
    for k in range(5):
        zz = CAPA_TRASERA_Z + 3 + k * 4
        c = c - caja(-EXT_ANCHO / 2 - 1, -INT_ANCHO / 2 + 1, ESTANTE1_Y + 4, ESTANTE1_Y + 16, zz, zz + 1.8)

    return c


# ================================ TAPA ================================

def tapa():
    ancho = INT_ANCHO + 2 * RANURA_PROF - 2 * HOLGURA
    # desde el fondo de la ranura inferior hasta el borde exterior de arriba
    y_bajo = -RANURA_PROF + HOLGURA
    y_alto = INT_ALTO + PARED
    alto = y_alto - y_bajo
    placa = prisma(rectangulo_redondeado(ancho, alto, 2.0), 0, TAPA_GROSOR)
    placa = placa.translate([0, y_bajo + alto / 2, Z_TAPA])

    # Traba: lengüeta flexible arriba al centro (unida por abajo, libre arriba). Su diente, en la
    # cara interior, pasa doblando la lengüeta bajo el borde de la pared superior y al final
    # queda enganchado debajo de ella: así la tapa no sube sola. Para abrir, empujar la
    # lengüeta hacia afuera (hacia el pecho) y deslizar la tapa hacia arriba.
    for sx in (-1, 1):
        placa = placa - caja(sx * 7.5 - 0.6, sx * 7.5 + 0.6, INT_ALTO - 22, y_alto + 1,
                             Z_TAPA - 1, Z_TAPA + TAPA_GROSOR + 1)
    diente = caja(-5.5, 5.5, INT_ALTO - 2.4, INT_ALTO - 0.4, Z_TAPA - 0.9, Z_TAPA + 0.01)
    placa = placa + diente
    # Relieve en la lengüeta para encontrarla y empujarla con el dedo
    for k in range(3):
        placa = placa + caja(-5, 5, INT_ALTO - 8 - k * 2.4, INT_ALTO - 7 - k * 2.4,
                             Z_TAPA + TAPA_GROSOR - 0.01, Z_TAPA + TAPA_GROSOR + 0.8)

    # Presillas para la correa, sobre cuñas que inclinan la cámara hacia abajo
    inclin = math.tan(math.radians(INCLINACION_GRADOS))
    base = Z_TAPA + TAPA_GROSOR
    for sx in (-1, 1):
        cx = sx * PRESILLA_X
        ancho_p = 12.0
        y_c0, y_c1 = 10.0, INT_ALTO - 22.0   # la cuña ocupa la mayor parte del alto
        # cuña: más gruesa arriba (lejos del pecho) que abajo
        cuna = cuna_solida(cx - ancho_p / 2, cx + ancho_p / 2, y_c0, y_c1, base, 2.0, 2.0 + (y_c1 - y_c0) * inclin)
        placa = placa + cuna
        # puente de la presilla a media altura de la cuña
        ym = (y_c0 + y_c1) / 2
        grosor_cuna = 2.0 + (ym - y_c0) * inclin
        z_puente0 = base + grosor_cuna
        puente = caja(cx - ancho_p / 2, cx + ancho_p / 2, ym - CORREA_ANCHO / 2 - 3, ym + CORREA_ANCHO / 2 + 3,
                      z_puente0, z_puente0 + CORREA_GROSOR + 2.4)
        hueco = caja(cx - ancho_p / 2 - 1, cx + ancho_p / 2 + 1, ym - CORREA_ANCHO / 2, ym + CORREA_ANCHO / 2,
                     z_puente0 - 0.5, z_puente0 + CORREA_GROSOR)
        placa = placa + (puente - hueco)
        # el hueco también se abre en la cuña para que la cinta pase
        placa = placa - caja(cx - ancho_p / 2 - 1, cx + ancho_p / 2 + 1, ym - CORREA_ANCHO / 2,
                             ym + CORREA_ANCHO / 2, base + 0.8, z_puente0 + CORREA_GROSOR)
    return placa


def cuna_solida(x0, x1, y0, y1, z_base, h_bajo, h_alto):
    """Prisma con base rectangular en z = z_base, más alto en y1 que en y0."""
    verts = np.array([
        [x0, y0, z_base], [x1, y0, z_base], [x1, y1, z_base], [x0, y1, z_base],
        [x0, y0, z_base + h_bajo], [x1, y0, z_base + h_bajo],
        [x1, y1, z_base + h_alto], [x0, y1, z_base + h_alto],
    ], dtype=np.float32)
    caras = np.array([
        [0, 2, 1], [0, 3, 2],          # base
        [4, 5, 6], [4, 6, 7],          # cara superior inclinada
        [0, 1, 5], [0, 5, 4],          # y0
        [2, 3, 7], [2, 7, 6],          # y1
        [1, 2, 6], [1, 6, 5],          # x1
        [3, 0, 4], [3, 4, 7],          # x0
    ], dtype=np.uint32)
    return Manifold(Mesh(vert_properties=verts, tri_verts=caras))


# ============================ EXPORTAR ============================

def guardar_stl(m, ruta):
    mesh = m.to_mesh()
    v = np.asarray(mesh.vert_properties)[:, :3]
    t = np.asarray(mesh.tri_verts)
    tri = v[t]
    normales = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    largo = np.linalg.norm(normales, axis=1, keepdims=True)
    normales = normales / np.where(largo == 0, 1, largo)
    datos = np.zeros(len(t), dtype=[("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")])
    datos["n"] = normales
    datos["v"] = tri
    with open(ruta, "wb") as f:
        f.write(b"MURDOCK carcasa".ljust(80, b" "))
        f.write(np.uint32(len(t)).tobytes())
        f.write(datos.tobytes())


def main():
    aqui = os.path.dirname(os.path.abspath(__file__))
    frente = cuerpo()
    t = tapa()
    for nombre, pieza in (("frente", frente), ("tapa", t)):
        if pieza.status().name != "NoError" or pieza.is_empty():
            raise SystemExit(f"La pieza {nombre} no es un sólido válido: {pieza.status()}")
    guardar_stl(frente, os.path.join(aqui, "murdock_frente.stl"))
    # La tapa se guarda acostada (cara interior abajo) para imprimirla sin soportes
    guardar_stl(t.translate([0, 0, -Z_TAPA]), os.path.join(aqui, "murdock_tapa.stl"))
    b = frente.bounding_box()
    print(f"frente: {b[3]-b[0]:.1f} x {b[4]-b[1]:.1f} x {b[5]-b[2]:.1f} mm, volumen {frente.volume()/1000:.1f} cm3")
    b = t.bounding_box()
    print(f"tapa:   {b[3]-b[0]:.1f} x {b[4]-b[1]:.1f} x {b[5]-b[2]:.1f} mm, volumen {t.volume()/1000:.1f} cm3")
    return frente, t


if __name__ == "__main__":
    main()
