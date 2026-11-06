"""Genera imágenes PNG de la carcasa (carpeta vistas/) a partir del modelo de murdock_carcasa.py."""

import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

import murdock_carcasa as M

COLOR_FRENTE = np.array([0.20, 0.36, 0.30])
COLOR_TAPA = np.array([0.55, 0.58, 0.60])
LUZ = np.array([0.4, 0.6, -0.7])
LUZ = LUZ / np.linalg.norm(LUZ)


def triangulos(m):
    mesh = m.to_mesh()
    v = np.asarray(mesh.vert_properties)[:, :3]
    return v[np.asarray(mesh.tri_verts)]


def dibujar(ax, tri, color, luz=LUZ):
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
    brillo = 0.35 + 0.65 * np.clip(np.abs(n @ luz), 0, 1)
    color = np.asarray(color)
    if color.ndim == 1:
        color = np.repeat(color[None, :], len(tri), axis=0)
    colores = np.clip(color * brillo[:, None] * 1.25, 0, 1)
    pc = Poly3DCollection(tri, facecolors=colores, edgecolors="none", linewidths=0)
    ax.add_collection3d(pc)


def encuadrar(ax, tri):
    pts = tri.reshape(-1, 3)
    lo, hi = pts.min(0), pts.max(0)
    c = (lo + hi) / 2
    r = (hi - lo).max() / 2 * 1.05
    ax.set_xlim(c[0] - r, c[0] + r)
    ax.set_ylim(c[1] - r, c[1] + r)
    ax.set_zlim(c[2] - r, c[2] + r)
    ax.set_box_aspect((1, 1, 1))
    ax.set_axis_off()


def vista(piezas, elev, azim, titulo, ruta, luz=LUZ):
    fig = plt.figure(figsize=(7, 7), dpi=110)
    ax = fig.add_subplot(111, projection="3d", proj_type="ortho")
    # Todas las piezas en una sola colección: matplotlib ordena por profundidad dentro de una
    # colección pero no entre colecciones (la tapa quedaba dibujada detrás de la caja)
    tris, cols = [], []
    for m, color in piezas:
        # coordenadas del modelo: x ancho, y alto, z fondo -> en matplotlib z hacia arriba.
        # Intercambiar dos ejes refleja la imagen; negar x la deja como en la realidad.
        t = triangulos(m)[:, :, [0, 2, 1]] * np.array([-1.0, 1.0, 1.0])
        tris.append(t)
        cols.append(np.repeat(color[None, :], len(t), axis=0))
    t = np.concatenate(tris)
    dibujar(ax, t, np.concatenate(cols), luz[[0, 2, 1]] * np.array([-1.0, 1.0, 1.0]))
    encuadrar(ax, t)
    ax.view_init(elev=elev, azim=azim)
    ax.set_title(titulo, fontsize=13)
    fig.tight_layout()
    fig.savefig(ruta)
    plt.close(fig)


def main():
    aqui = os.path.dirname(os.path.abspath(__file__))
    salida = os.path.join(aqui, "vistas")
    os.makedirs(salida, exist_ok=True)
    frente = M.cuerpo()
    tapa = M.tapa()
    # En matplotlib (x, z_modelo, y_modelo): mirar desde z negativo = azim -90
    vista([(frente, COLOR_FRENTE)], 12, -90 + 25, "Frente: cámara, MURDOCK, parlante y micrófono",
          os.path.join(salida, "1_frente.png"), np.array([0.3, 0.5, -0.8]))
    vista([(frente, COLOR_FRENTE)], 35, 90 + 30, "Por dentro (sin tapa): estantes, postes y cuna de baterías",
          os.path.join(salida, "2_interior.png"), np.array([0.3, 0.6, 0.8]))
    vista([(frente, COLOR_FRENTE), (tapa, COLOR_TAPA)], 20, 90 - 35, "Atrás: tapa deslizable y presillas para la correa",
          os.path.join(salida, "3_atras.png"), np.array([-0.3, 0.6, 0.8]))
    # Derecha e izquierda de quien lo lleva puesto (el modelo +x es su derecha)
    vista([(frente, COLOR_FRENTE), (tapa, COLOR_TAPA)], 15, 180 + 20, "Lado derecho del usuario: USB-C de carga",
          os.path.join(salida, "4_lado_derecho.png"), np.array([0.8, 0.5, -0.3]))
    vista([(frente, COLOR_FRENTE), (tapa, COLOR_TAPA)], 15, -20, "Lado izquierdo del usuario: interruptor y ventilación",
          os.path.join(salida, "5_lado_izquierdo.png"), np.array([-0.8, 0.5, -0.3]))
    vista([(frente, COLOR_FRENTE), (tapa, COLOR_TAPA)], 60, -90 + 10, "Arriba: botón (la tapa entra por aquí)",
          os.path.join(salida, "6_arriba.png"), np.array([0.2, 0.9, -0.3]))
    print("vistas guardadas en", salida)


if __name__ == "__main__":
    main()
