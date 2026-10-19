#ifndef TRAITEMENTS_IMAGE_HPP
#define TRAITEMENTS_IMAGE_HPP

#include <opencv2/core.hpp>

// Les fonctions travaillent directement sur un canal de luminosite V.
// Entree : matrice mono-canal 8 bits (CV_8UC1). Taille et type sont conserves.
// Une matrice vide est acceptee. Un autre type provoque une erreur OpenCV.

// Cherche le minimum et le maximum, puis applique la formule min max.
// Pour un canal uniforme, la sortie vaut 0 afin d'eviter une division par zero.
void normaliserMinMax(cv::Mat& canal);

// Calcule manuellement histogramme, cumul et table de correspondance.
// Pour un canal uniforme, la valeur initiale est conservee.
void egaliserHistogramme(cv::Mat& canal);

#endif
