# Travail sur la normalisation des images

Auteur : Amine

Je reprends le programme de détourage du terrain du groupe pour
implémenter et comparer des traitements mathématiques manuels.

## Travail réalisé

- Ajout d'une normalisation min max manuelle sur le canal V de
  la première image. La boucle vidéo utilise encore cv::normalize.
- Ajout d'une égalisation d'histogramme manuelle, actuellement
  appliquée uniquement au rendu couleur.
- Essais de réglage du masque pour mieux conserver la pelouse
  dans certaines zones brillantes.

## Objectif de la séance

Comparer la normalisation manuelle et OpenCV sur les mêmes images,
mesurer le temps de calcul et vérifier l'effet sur les masques.

Ces essais doivent préparer l'intégration avec la détection
et le tracking du groupe.