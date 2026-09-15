# EdgeTrack-Foot : Système Embarqué de Tracking et d'Analyse Tactique de Football

Système autonome de vision par ordinateur et d'analyse athlétique en temps réel à partir d'un flux vidéo grand-angle fixe. 

L'objectif est de transformer un flux vidéo filmé en hauteur en métriques physiques utiles pour le staff technique (entraîneurs, préparateurs physiques)

### Fonctionnalités principales visées
* **Détection & Segmentation :** Isolation de l'aire de jeu (masquage pelouse) et extraction des silhouettes de joueurs.
* **Tracking Multi-Cibles (MOT) :** Maintien temporel des boîtes englobantes et des identifiants (`ID #1` à `ID #n`) au cours des phases de jeu.
* **Projection Métrique (Homographie) :** Conversion projective temps réel des coordonnées images (pieds des joueurs) vers un repère terrain normé en mètres ($105\text{ m} \times 68\text{ m}$).
* **Télémétrie Athlétique :** Estimation des vitesses (km/h), vitesses maximales de sprint et distances cumulées.
* **Visualisation Dynamique :** Affichage synchronisé de la vidéo annotée (carrés de suivi, IDs, vitesses) et d'un radar tactique 2D vue du ciel.

ACHBAD Younes
BOUCHAREB Badreddine
SABBAR Amine

---

