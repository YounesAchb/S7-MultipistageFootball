import cv2
import numpy as np
import os

print("--- Génération des extraits vidéo panoramiques simplifiés ---")

bg_path = "/Users/admin/S7-MultipistageFootball/out/pelouse_vide_propre.jpg"
if not os.path.exists(bg_path):
    print("Erreur : fond de pelouse vide introuvable")
    exit(1)

bg = cv2.imread(bg_path)
h, w = bg.shape[:2]
fps = 25.0
fourcc = cv2.VideoWriter_fourcc(*'mp4v')

video_in_path = "/Users/admin/data/videos/clip3.mp4"
cap = cv2.VideoCapture(video_in_path)
if not cap.isOpened():
    print("Erreur : impossible d'ouvrir clip3.mp4")
    exit(1)

# 1. Extraction des trajectoires de référence
# Nous suivons deux joueurs réels très nets du clip 3 :
# Joueur A (Track #25) : départ (607, 622) -> (1932, 394)
# Joueur B (Track #34) : départ (2059, 216) -> (2501, 204)
# Joueur C (Track #14 / Gardien) : reste autour de (865, 344)

# Calibrage morphologique pour segmenter proprement les joueurs
vert_min = np.array([38, 35, 25], dtype=np.uint8)
vert_max = np.array([78, 255, 255], dtype=np.uint8)
kernel_open = cv2.getStructuringElement(cv2.MORPH_RECT, (3, 3))
kernel_close = cv2.getStructuringElement(cv2.MORPH_RECT, (3, 15))

print("1/4 : Analyse et extraction des silhouettes de référence...")
frames_raw = []
for f in range(300): # 12 secondes
    ret, frame = cap.read()
    if not ret: break
    frames_raw.append(frame)
cap.release()

nb_f = len(frames_raw)
print(f"Total trames lues : {nb_f}")

def get_player_patch(frame, center_x, center_y, box_w=90, box_h=120):
    x0 = max(0, int(center_x - box_w // 2))
    y0 = max(0, int(center_y - box_h // 2))
    x1 = min(w, int(center_x + box_w // 2))
    y1 = min(h, int(center_y + box_h // 2))
    
    pf = frame[y0:y1, x0:x1]
    pb = bg[y0:y1, x0:x1]
    
    diff = cv2.absdiff(pf, pb)
    diff_g = cv2.cvtColor(diff, cv2.COLOR_BGR2GRAY)
    _, m = cv2.threshold(diff_g, 22, 255, cv2.THRESH_BINARY)
    m = cv2.morphologyEx(m, cv2.MORPH_CLOSE, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5)))
    m = cv2.dilate(m, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3)))
    m_soft = cv2.GaussianBlur(m, (7, 7), 0) / 255.0
    return pf, pb, m_soft, x0, y0, x1, y1

# ==============================================================================
# CLIP 1 : extrait_panoramique_1_joueur.mp4
# ==============================================================================
out1 = "/Users/admin/data/videos/extrait_panoramique_1_joueur.mp4"
writer1 = cv2.VideoWriter(out1, fourcc, fps, (w, h))
print(f"Génération de {out1} (1 seul joueur en course continue)...")

for i in range(nb_f):
    frame = frames_raw[i]
    composite = bg.copy()
    
    # Trajectoire du joueur #25
    # pos : départ x=607, y=600 -> x=1932, y=390
    t = float(i) / (nb_f - 1)
    px = 607.0 + t * (1932.0 - 607.0)
    py = 600.0 + t * (390.0 - 600.0)
    
    pf, pb, m_soft, x0, y0, x1, y1 = get_player_patch(frame, px, py, 90, 130)
    if pf.shape == pb.shape:
        m3 = np.repeat(m_soft[:, :, np.newaxis], 3, axis=2)
        blended = (pf * m3 + pb * (1.0 - m3)).astype(np.uint8)
        composite[y0:y1, x0:x1] = blended
        
    writer1.write(composite)

writer1.release()
print(" -> Terminé.")

# ==============================================================================
# CLIP 2 : extrait_panoramique_croisement_2_joueurs.mp4
# ==============================================================================
out2 = "/Users/admin/data/videos/extrait_panoramique_croisement_2_joueurs.mp4"
writer2 = cv2.VideoWriter(out2, fourcc, fps, (w, h))
print(f"Génération de {out2} (2 joueurs qui se croisent au milieu du terrain)...")

for i in range(nb_f):
    frame = frames_raw[i]
    composite = bg.copy()
    t = float(i) / (nb_f - 1)
    
    # Joueur 1 : va de gauche à droite (x=1300 -> 2100, y=420 -> 400)
    p1x = 1300.0 + t * 800.0
    p1y = 420.0 - t * 20.0
    
    # Joueur 2 : va de droite à gauche (x=2100 -> 1300, y=390 -> 430)
    p2x = 2100.0 - t * 800.0
    p2y = 390.0 + t * 40.0
    
    # On incruste d'abord Joueur 2, puis Joueur 1 pour simuler la profondeur
    for px, py in [(p2x, p2y), (p1x, p1y)]:
        pf, pb, m_soft, x0, y0, x1, y1 = get_player_patch(frame, px, py, 90, 130)
        if pf.shape == pb.shape:
            m3 = np.repeat(m_soft[:, :, np.newaxis], 3, axis=2)
            cur = composite[y0:y1, x0:x1]
            blended = (pf * m3 + cur * (1.0 - m3)).astype(np.uint8)
            composite[y0:y1, x0:x1] = blended
            
    writer2.write(composite)

writer2.release()
print(" -> Terminé.")

# ==============================================================================
# CLIP 3 : extrait_panoramique_3_joueurs.mp4
# ==============================================================================
out3 = "/Users/admin/data/videos/extrait_panoramique_3_joueurs.mp4"
writer3 = cv2.VideoWriter(out3, fourcc, fps, (w, h))
print(f"Génération de {out3} (3 joueurs : Gardien + Duel Attaquant/Défenseur)...")

for i in range(nb_f):
    frame = frames_raw[i]
    composite = bg.copy()
    t = float(i) / (nb_f - 1)
    
    # Gardien au but gauche : oscillation latérale
    gk_x = 865.0 + 35.0 * np.sin(i * 0.08)
    gk_y = 340.0 + 8.0 * np.cos(i * 0.08)
    
    # Défenseur qui recule
    def_x = 1150.0 - t * 120.0
    def_y = 380.0 + t * 40.0
    
    # Attaquant qui avance vers le but
    att_x = 1500.0 - t * 280.0
    att_y = 420.0 - t * 20.0
    
    for px, py in [(gk_x, gk_y), (def_x, def_y), (att_x, att_y)]:
        pf, pb, m_soft, x0, y0, x1, y1 = get_player_patch(frame, px, py, 90, 130)
        if pf.shape == pb.shape:
            m3 = np.repeat(m_soft[:, :, np.newaxis], 3, axis=2)
            cur = composite[y0:y1, x0:x1]
            blended = (pf * m3 + cur * (1.0 - m3)).astype(np.uint8)
            composite[y0:y1, x0:x1] = blended
            
    writer3.write(composite)

writer3.release()
print(" -> Terminé.")

# ==============================================================================
# CLIP 4 : extrait_panoramique_demi_terrain.mp4 (Extrait réel brut de match)
# ==============================================================================
out4 = "/Users/admin/data/videos/extrait_panoramique_demi_terrain.mp4"
# On prend le demi-terrain droit (x de 2048 à 4096), redimensionné en 4096x1080
# ou conservé en 2048x1080 (panoramique demi-terrain réel)
writer4 = cv2.VideoWriter(out4, fourcc, fps, (2048, 1080))
print(f"Génération de {out4} (demi-terrain réel match 2048x1080 avec ~4 joueurs)...")

for i in range(nb_f):
    frame = frames_raw[i]
    crop = frame[:, 2048:4096]
    writer4.write(crop)

writer4.release()
print(" -> Terminé.")

print("\nTous les extraits vidéo panoramiques ont été créés avec succès dans /Users/admin/data/videos/ !")
