import cv2
import numpy as np
import os

print("=== Génération des Extraits Panoramiques 100% Purs (1 Joueur et 2 Joueurs) ===")

bg = cv2.imread('/Users/admin/S7-MultipistageFootball/out/pelouse_vide_parfaite.jpg')
h, w = bg.shape[:2]
fps = 25.0
fourcc = cv2.VideoWriter_fourcc(*'avc1')

cap = cv2.VideoCapture('/Users/admin/data/videos/clip3.mp4')
ret, frame_init = cap.read()

# Masques HSV
vert_min = np.array([38, 35, 25], dtype=np.uint8)
vert_max = np.array([78, 255, 255], dtype=np.uint8)

hsv_init = cv2.cvtColor(frame_init, cv2.COLOR_BGR2HSV)
hsv_init[:, :, 2] = cv2.normalize(hsv_init[:, :, 2], None, 0, 255, cv2.NORM_MINMAX)
pelouse_init = cv2.inRange(hsv_init, vert_min, vert_max)
pelouse_propre = cv2.morphologyEx(pelouse_init, cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7, 7)))
pelouse_fermee = cv2.morphologyEx(pelouse_propre, cv2.MORPH_CLOSE, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (35, 35)))

contours_terrain, _ = cv2.findContours(pelouse_fermee, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
c_terrain = max(contours_terrain, key=cv2.contourArea)
masque_terrain = np.zeros((h, w), dtype=np.uint8)
cv2.drawContours(masque_terrain, [c_terrain], -1, 255, cv2.FILLED)

lignes_blanches = cv2.bitwise_and(pelouse_fermee, ~pelouse_init)
cnts, _ = cv2.findContours(lignes_blanches, cv2.RETR_LIST, cv2.CHAIN_APPROX_SIMPLE)
clean_lines = np.zeros_like(lignes_blanches)
for c in cnts:
    bx, by, bw, bh = cv2.boundingRect(c)
    if cv2.contourArea(c) > 350 and bh > 1.2 * bw and bw < 60: continue
    cv2.drawContours(clean_lines, [c], -1, 255, cv2.FILLED)
masque_lignes = cv2.dilate(clean_lines, cv2.getStructuringElement(cv2.MORPH_RECT, (5, 5)))

noyau_lignes = cv2.getStructuringElement(cv2.MORPH_RECT, (3, 3))
noyau_corps = cv2.getStructuringElement(cv2.MORPH_RECT, (3, 15))

# 1. Acquisition des 250 trames brutes et suivi continu du joueur P1
frames_raw = []
cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
for f in range(250):
    ret, frame = cap.read()
    if not ret: break
    frames_raw.append(frame)

cap.release()
print(f"Acquis : {len(frames_raw)} trames brutes.")

p_pos = np.array([607.0, 622.0])
p_vel = np.array([0.0, 0.0])
track_p = []

for f, frame in enumerate(frames_raw):
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    hsv[:, :, 2] = cv2.normalize(hsv[:, :, 2], None, 0, 255, cv2.NORM_MINMAX)
    masque_pelouse = cv2.inRange(hsv, vert_min, vert_max)
    masque_pelouse = cv2.bitwise_or(masque_pelouse, masque_lignes)
    objets = cv2.bitwise_and(~masque_pelouse, masque_terrain)
    silhouettes = cv2.morphologyEx(objets, cv2.MORPH_OPEN, noyau_lignes)
    silhouettes = cv2.morphologyEx(silhouettes, cv2.MORPH_CLOSE, noyau_corps)
    cnts, _ = cv2.findContours(silhouettes, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    
    dets = []
    for c in cnts:
        bx, by, bw, bh = cv2.boundingRect(c)
        ca = cv2.contourArea(c)
        if ca < 35 or ca > 8000 or by < 135 or by > 750 or bx < 30 or (bx + bw) > w - 30: continue
        if bw > 2.0 * bh and bw > 35: continue
        if bw > 85 or bh > 160: continue
        if bw <= 14 and bh >= 40 and (bx < 180 or (bx + bw) > w - 180): continue
        if bh < int(0.85 * bw) and bw > 20: continue
        yp = by + bh
        xp = bx + bw // 2
        h_att = 22.0 + 0.11 * (yp - 130)
        if bh < int(0.48 * h_att): continue
        dets.append({'box': (bx, by, bw, bh), 'pieds': np.array([float(xp), float(yp)])})

    pos_pred = p_pos + p_vel
    best_d = None
    min_dist = 45.0
    for d in dets:
        dist = np.linalg.norm(pos_pred - d['pieds'])
        if dist < min_dist:
            min_dist = dist
            best_d = d
            
    if best_d is not None:
        p_vel = 0.7 * (best_d['pieds'] - p_pos) + 0.3 * p_vel
        p_pos = best_d['pieds']
        box = best_d['box']
    else:
        p_pos = p_pos + p_vel
        box = track_p[-1][1] if track_p else (int(p_pos[0]-15), int(p_pos[1]-60), 30, 60)
        
    track_p.append((f, box, p_pos.copy()))

print(f"Trajectoire P1 suivie sur {len(track_p)} trames sans aucune rupture.")

def extract_clean_patch(frame, box, target_center, pad=14):
    bx, by, bw, bh = box
    x0 = max(0, bx - pad)
    y0 = max(0, by - pad)
    x1 = min(w, bx + bw + pad)
    y1 = min(h, by + bh + pad)
    crop = frame[y0:y1, x0:x1].copy()
    
    hsv_c = cv2.cvtColor(crop, cv2.COLOR_BGR2HSV)
    hsv_c[:, :, 2] = cv2.normalize(hsv_c[:, :, 2], None, 0, 255, cv2.NORM_MINMAX)
    pel = cv2.inRange(hsv_c, vert_min, vert_max)
    fg = ~pel
    
    cnts, _ = cv2.findContours(fg, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    mask = np.zeros(crop.shape[:2], dtype=np.uint8)
    cx_rel = target_center[0] - x0
    cy_rel = target_center[1] - y0
    
    for c in cnts:
        area = cv2.contourArea(c)
        if area < 30: continue
        M = cv2.moments(c)
        if M['m00'] > 0:
            cx_c = M['m10'] / M['m00']
            cy_c = M['m01'] / M['m00']
            if abs(cx_c - cx_rel) < bw * 0.9 and abs(cy_c - cy_rel) < bh * 0.9:
                cv2.drawContours(mask, [c], -1, 255, cv2.FILLED)
                
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, cv2.getStructuringElement(cv2.MORPH_RECT, (5, 9)))
    mask = cv2.dilate(mask, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3)))
    return crop, mask, (x0, y0, x1, y1)

def paste_patch(target_img, crop, mask, coords, offset_x=0, offset_y=0, flip=False):
    x0, y0, x1, y1 = coords
    if flip:
        crop = cv2.flip(crop, 1)
        mask = cv2.flip(mask, 1)
        
    tx0 = max(0, x0 + offset_x)
    ty0 = max(0, y0 + offset_y)
    tx1 = min(w, tx0 + (x1 - x0))
    ty1 = min(h, ty0 + (y1 - y0))
    pw = tx1 - tx0
    ph = ty1 - ty0
    if pw <= 0 or ph <= 0: return
    
    m_crop = mask[:ph, :pw]
    p_crop = crop[:ph, :pw]
    bg_crop = target_img[ty0:ty1, tx0:tx1]
    
    m_soft = cv2.GaussianBlur(m_crop, (3, 3), 0) / 255.0
    m3 = np.repeat(m_soft[:, :, np.newaxis], 3, axis=2)
    target_img[ty0:ty1, tx0:tx1] = (p_crop * m3 + bg_crop * (1.0 - m3)).astype(np.uint8)

# 1. Écriture de extrait_panoramique_1_joueur.mp4
out1 = "/Users/admin/data/videos/extrait_panoramique_1_joueur.mp4"
writer1 = cv2.VideoWriter(out1, fourcc, fps, (w, h))
print(f"Génération de {out1}...")
for i in range(len(frames_raw)):
    comp = bg.copy()
    box = track_p[i][1]
    pos = track_p[i][2]
    crop, mask, coords = extract_clean_patch(frames_raw[i], box, (pos[0], pos[1] - box[3]//2))
    paste_patch(comp, crop, mask, coords)
    writer1.write(comp)
writer1.release()
print(" -> Extrait 1 joueur terminé.")

# 2. Écriture de extrait_panoramique_croisement_2_joueurs.mp4
out2 = "/Users/admin/data/videos/extrait_panoramique_croisement_2_joueurs.mp4"
writer2 = cv2.VideoWriter(out2, fourcc, fps, (w, h))
print(f"Génération de {out2}...")
nb = len(frames_raw)
for i in range(nb):
    comp = bg.copy()
    
    # Joueur 1 : va de gauche à droite
    box1 = track_p[i][1]
    pos1 = track_p[i][2]
    crop1, mask1, coords1 = extract_clean_patch(frames_raw[i], box1, (pos1[0], pos1[1] - box1[3]//2))
    paste_patch(comp, crop1, mask1, coords1)
    
    # Joueur 2 : va de droite à gauche, croisement net au centre
    rev_i = nb - 1 - i
    box2 = track_p[rev_i][1]
    pos2 = track_p[rev_i][2]
    crop2, mask2, coords2 = extract_clean_patch(frames_raw[rev_i], box2, (pos2[0], pos2[1] - box2[3]//2))
    # Inversion horizontale pour courir dans l'autre sens
    paste_patch(comp, crop2, mask2, coords2, offset_x=0, offset_y=16, flip=True)
    
    writer2.write(comp)
writer2.release()
print(" -> Extrait croisement 2 joueurs terminé.")
print("\nExtraction 100% propre achevée !")
