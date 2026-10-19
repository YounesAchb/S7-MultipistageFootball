#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <array>
#include <cmath>
#include <opencv2/opencv.hpp>
//Normalisation à la main 
void normaliserMinMax(cv::Mat& canal)
{
    int minimum = 255;
    int maximum = 0;

    // Premiere passe : chercher les valeurs minimale et maximale.
    for (int y = 0; y < canal.rows; ++y) {
        const uchar* pixels = canal.ptr<uchar>(y);

        for (int x = 0; x < canal.cols; ++x) {
            int valeur = pixels[x];

            if (valeur < minimum) minimum = valeur;
            if (valeur > maximum) maximum = valeur;
        }
    }

    const int etendue = maximum - minimum;

    // Deuxieme passe : appliquer la formule a chaque pixel.
    for (int y = 0; y < canal.rows; ++y) {
        uchar* pixels = canal.ptr<uchar>(y);

        for (int x = 0; x < canal.cols; ++x) {
            if (etendue == 0) {
                pixels[x] = 0;
            } else {
                double valeur = 255.0 * (pixels[x] - minimum) / etendue;
                pixels[x] = static_cast<uchar>(std::lround(valeur));
            }
        }
    }
}

void egaliserHistogramme(cv::Mat& canal)
{
    if (canal.empty()) return;
    CV_Assert(canal.type() == CV_8UC1);

    std::array<std::size_t, 256> histogramme{};
    for (int y = 0; y < canal.rows; ++y) {
        const uchar* pixels = canal.ptr<uchar>(y);
        for (int x = 0; x < canal.cols; ++x) {
            ++histogramme[pixels[x]];
        }
    }

    int premier_niveau = 0;
    while (histogramme[premier_niveau] == 0) ++premier_niveau;
    const std::size_t total = canal.total();
    const std::size_t cumul_min = histogramme[premier_niveau];
    if (total == cumul_min) return;

    // Retirer le premier cumul permet de faire correspondre le minimum a 0.
    std::array<uchar, 256> correspondance{};
    std::size_t cumul = 0;
    for (int niveau = premier_niveau + 1; niveau < 256; ++niveau) {
        cumul += histogramme[niveau];
        const double valeur = 255.0 * cumul / (total - cumul_min);
        correspondance[niveau] = static_cast<uchar>(std::lround(valeur));
    }

    for (int y = 0; y < canal.rows; ++y) {
        uchar* pixels = canal.ptr<uchar>(y);
        for (int x = 0; x < canal.cols; ++x) {
            pixels[x] = correspondance[pixels[x]];
        }
    }
}

int main(int argc, char* argv[])
{
    const std::string chemin_video = argc > 1 ? argv[1] : "data/videos/clip1.mp4";
    cv::VideoCapture capture(chemin_video);

    if (capture.isOpened() == false) {
        std::cerr << "Erreur : Impossible d'ouvrir le fichier video : " << chemin_video << std::endl;
        return 1;
    }

    const double fps = capture.get(cv::CAP_PROP_FPS);
    const double periode_image_ms = fps > 0 ? 1000.0 / fps : 40.0;

    const int largeur = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
    const int hauteur = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));

    // Parametres de seuillage HSV de la pelouse
    const cv::Scalar vert_min(38, 35, 25);
    const cv::Scalar vert_max(78, 255, 255);
    // Les reflets desaturent la pelouse; le contour initial garde ses seuils.
    const cv::Scalar couleur_min(vert_min[0], 25, 0);
    const cv::Scalar couleur_max(vert_max[0], vert_max[1], 255);

    // Calcul du contour panoramique du terrain sur la premiere image
    std::cout << "Calcul du contour panoramique du terrain..." << std::endl;

    cv::Mat premiere_frame;
    if (capture.read(premiere_frame) == false || premiere_frame.empty() == true) {
        std::cerr << "Erreur : Impossible de lire la premiere image." << std::endl;
        return 1;
    }

    cv::Mat hsv_init, pelouse_init, pelouse_propre, pelouse_fermee;
    cv::cvtColor(premiere_frame, hsv_init, cv::COLOR_BGR2HSV);

    std::vector<cv::Mat> canaux_init;
    cv::split(hsv_init, canaux_init);
    normaliserMinMax(canaux_init[2]);
    cv::merge(canaux_init, hsv_init);

    cv::inRange(hsv_init, vert_min, vert_max, pelouse_init);

    // Nettoyage morphologique des bruits isoles dans les tribunes
    cv::Mat noyau_nettoyage = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(7, 7));
    cv::morphologyEx(pelouse_init, pelouse_propre, cv::MORPH_OPEN, noyau_nettoyage);

    // Fermeture elliptique pour relier les zones d herbe sans angles droits
    cv::Mat noyau_fermeture = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(35, 35));
    cv::morphologyEx(pelouse_propre, pelouse_fermee, cv::MORPH_CLOSE, noyau_fermeture);

    // Extraction du plus grand contour correspondant a la surface du terrain
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(pelouse_fermee, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    int index_terrain = -1;
    double aire_max = 0.0;
    for (size_t i = 0; i < contours.size(); ++i) {
        double aire = cv::contourArea(contours[i]);
        if (aire > aire_max) {
            aire_max = aire;
            index_terrain = static_cast<int>(i);
        }
    }

    cv::Mat masque_terrain = cv::Mat::zeros(hauteur, largeur, CV_8UC1);
    if (index_terrain >= 0) {
        cv::drawContours(masque_terrain, contours, index_terrain, cv::Scalar(255), cv::FILLED);

        // Suppression des asperites residuelles vers les tribunes
        cv::Mat noyau_rasage = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(21, 21));
        cv::morphologyEx(masque_terrain, masque_terrain, cv::MORPH_OPEN, noyau_rasage);

        // Marge de securite pour proteger les crampons et le corps sur les lignes de touche
        cv::Mat noyau_marge = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(15, 15));
        cv::dilate(masque_terrain, masque_terrain, noyau_marge);

        // Extension progressive sur l aile gauche pour englober la cage de but
        for (int x = 0; x < std::min(950, largeur); ++x) {
            float facteur = 1.0f - static_cast<float>(x) / 950.0f;
            int decalage = static_cast<int>(80.0f * facteur);
            for (int y = 0; y < hauteur; ++y) {
                if (masque_terrain.at<uchar>(y, x) > 0) {
                    int y_haut = std::max(0, y - decalage);
                    for (int dy = y_haut; dy < y; ++dy) {
                        masque_terrain.at<uchar>(dy, x) = 255;
                    }
                    break;
                }
            }
        }

        // Lissage gaussien pour une courbure panoramique continue sans crenaux
        cv::GaussianBlur(masque_terrain, masque_terrain, cv::Size(21, 21), 0);
        cv::threshold(masque_terrain, masque_terrain, 127, 255, cv::THRESH_BINARY);

        std::cout << "Contour panoramique detecte et lisse avec succes ("
                  << (aire_max / (largeur * hauteur) * 100.0) << "% du cadre) avec but gauche inclus." << std::endl;
    }

    capture.set(cv::CAP_PROP_POS_FRAMES, 0);

    // Le masque reste en pleine resolution; seuls les rendus sont redimensionnes.
    const double echelle_affichage = std::min(1.0, 1280.0 / largeur);
    const cv::Size taille_affichage(
        std::max(1, static_cast<int>(std::round(largeur * echelle_affichage))),
        std::max(1, static_cast<int>(std::round(hauteur * echelle_affichage))));
    cv::Mat terrain_affiche, decor_affiche;
    cv::resize(masque_terrain, terrain_affiche, taille_affichage, 0, 0, cv::INTER_NEAREST);
    cv::bitwise_not(terrain_affiche, decor_affiche);
    std::cout << "Source : " << largeur << "x" << hauteur << ", " << fps << " images/s." << std::endl;

    // Configuration des fenetres d affichage
    const std::string fenetre_focus = "Multipistage - Focus Terrain (Decor 60% couleur)";
    cv::namedWindow(fenetre_focus, cv::WINDOW_NORMAL);
    cv::resizeWindow(fenetre_focus, taille_affichage.width, taille_affichage.height);

    const std::string fenetre_masque = "Multipistage - Masque Pelouse (Decor estompe 60%)";
    cv::namedWindow(fenetre_masque, cv::WINDOW_NORMAL);
    cv::resizeWindow(fenetre_masque, taille_affichage.width, taille_affichage.height);

    // Matrices memoires reutilisees dans la boucle de traitement
    cv::Mat frame, hsv, masque_pelouse;
    cv::Mat luminosite, masque_luminosite, frame_affichee;
    cv::Mat hsv_affiche, luminosite_affichee;
    cv::Mat masque_affiche, gray_frame, decor_estompe_gray, frame_estompee, rendu_visuel;
    const double frequence_horloge = cv::getTickFrequency();
    const int64 debut_lecture = cv::getTickCount();
    int64 debut_mesure = debut_lecture;
    double echeance_image_ms = periode_image_ms;
    int images_mesure = 0;
    while (true) {
        if (capture.read(frame) == false || frame.empty() == true) {
            std::cout << "Fin de la video atteinte." << std::endl;
            break;
        }

        // Conversion en HSV et normalisation de la luminosite sur le canal V
        cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);
        cv::extractChannel(hsv, luminosite, 2);
        cv::normalize(luminosite, luminosite, 0, 255, cv::NORM_MINMAX);

        // Seuillage binaire de la pelouse verte
        cv::inRange(hsv, couleur_min, couleur_max, masque_pelouse);
        cv::inRange(luminosite, cv::Scalar(vert_min[2]), cv::Scalar(vert_max[2]), masque_luminosite);
        cv::bitwise_and(masque_pelouse, masque_luminosite, masque_pelouse);
    
        cv::resize(frame, frame_affichee, taille_affichage, 0, 0, cv::INTER_LINEAR);
        cv::cvtColor(frame_affichee, gray_frame, cv::COLOR_BGR2GRAY);
        // Egalisation du rendu couleur uniquement; le masque garde ses seuils.
        cv::cvtColor(frame_affichee, hsv_affiche, cv::COLOR_BGR2HSV);
        cv::extractChannel(hsv_affiche, luminosite_affichee, 2);
        egaliserHistogramme(luminosite_affichee);
        cv::insertChannel(luminosite_affichee, hsv_affiche, 2);
        cv::cvtColor(hsv_affiche, frame_affichee, cv::COLOR_HSV2BGR);
        cv::resize(masque_pelouse, masque_affiche, taille_affichage, 0, 0, cv::INTER_NEAREST);

        // Rendu visuel d analyse couleur avec decor estompe a 60%
        frame_affichee.convertTo(frame_estompee, -1, 0.60);
        frame_affichee.copyTo(rendu_visuel);
        frame_estompee.copyTo(rendu_visuel, decor_affiche);

        // Rendu du masque avec pelouse blanche, silhouettes et decor estompe
        gray_frame.convertTo(decor_estompe_gray, -1, 0.60);
        decor_estompe_gray.copyTo(masque_affiche, decor_affiche);

        // Affichage synchronise des deux fenetres
        cv::imshow(fenetre_focus, rendu_visuel);
        cv::imshow(fenetre_masque, masque_affiche);

        // Une echeance commune compense aussi les depassements du delai Windows.
        const double temps_lecture_ms = (cv::getTickCount() - debut_lecture) * 1000.0 / frequence_horloge;
        const double attente_ms = echeance_image_ms - temps_lecture_ms;
        const int touche = attente_ms > 0
            ? cv::waitKey(std::max(1, static_cast<int>(std::ceil(attente_ms))))
            : cv::pollKey();
        if (touche == 'q' || touche == 'Q' || touche == 27) {
            std::cout << "Lecture arretee par l'utilisateur." << std::endl;
            break;
        }

        ++images_mesure;
        const int64 maintenant = cv::getTickCount();
        const double temps_ecoule_ms = (maintenant - debut_lecture) * 1000.0 / frequence_horloge;
        if (temps_ecoule_ms - echeance_image_ms > periode_image_ms) {
            echeance_image_ms = temps_ecoule_ms;
        }
        echeance_image_ms += periode_image_ms;
        const double secondes_mesure = (maintenant - debut_mesure) / frequence_horloge;
        if (secondes_mesure >= 5.0) {
            std::cout << "Lecture : " << std::round(images_mesure / secondes_mesure * 10.0) / 10.0
                      << " images/s (source : " << fps << ")." << std::endl;
            debut_mesure = maintenant;
            images_mesure = 0;
        }
    }

    capture.release();
    cv::destroyAllWindows();

    return 0;
}
