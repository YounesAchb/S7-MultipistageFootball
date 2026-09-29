#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <opencv2/opencv.hpp>
#include <opencv2/geometry.hpp>

int main()
{
    const std::string chemin_video = "../data/videos/clip1.mp4";
    cv::VideoCapture capture(chemin_video);

    if (capture.isOpened() == false) {
        std::cerr << "Erreur : Impossible d'ouvrir le fichier video : " << chemin_video << std::endl;
        return 1;
    }

    const double fps = capture.get(cv::CAP_PROP_FPS);
    int delai = 40;
    if (fps > 0) {
        delai = static_cast<int>(1000.0 / fps);
    }

    const int largeur = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
    const int hauteur = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));

    // Parametres de seuillage HSV de la pelouse
    const cv::Scalar vert_min(38, 35, 25);
    const cv::Scalar vert_max(78, 255, 255);

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
    cv::normalize(canaux_init[2], canaux_init[2], 0, 255, cv::NORM_MINMAX);
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

    // Configuration des fenetres d affichage
    const std::string fenetre_focus = "Multipistage - Focus Terrain (Decor 60% couleur)";
    cv::namedWindow(fenetre_focus, cv::WINDOW_NORMAL);
    cv::resizeWindow(fenetre_focus, 1280, 360);

    const std::string fenetre_masque = "Multipistage - Masque Pelouse (Decor estompe 60%)";
    cv::namedWindow(fenetre_masque, cv::WINDOW_NORMAL);
    cv::resizeWindow(fenetre_masque, 1280, 360);

    // Matrices memoires reutilisees dans la boucle de traitement
    cv::Mat frame, hsv, masque_pelouse;
    cv::Mat masque_affiche, gray_frame, decor_estompe_gray, frame_estompee, rendu_visuel;
    std::vector<cv::Mat> canaux_hsv;

    while (true) {
        if (capture.read(frame) == false || frame.empty() == true) {
            std::cout << "Fin de la video atteinte." << std::endl;
            break;
        }

        // Conversion en HSV et normalisation de la luminosite sur le canal V
        cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);
        cv::split(hsv, canaux_hsv);
        cv::normalize(canaux_hsv[2], canaux_hsv[2], 0, 255, cv::NORM_MINMAX);
        cv::merge(canaux_hsv, hsv);

        // Seuillage binaire de la pelouse verte
        cv::inRange(hsv, vert_min, vert_max, masque_pelouse);

        // Rendu visuel d analyse couleur avec decor estompe a 60%
        cv::addWeighted(frame, 0.60, cv::Mat::zeros(frame.size(), frame.type()), 0.40, 0, frame_estompee);
        rendu_visuel = frame.clone();
        frame_estompee.copyTo(rendu_visuel, ~masque_terrain);

        // Rendu du masque avec pelouse blanche, silhouettes et decor estompe
        cv::cvtColor(frame, gray_frame, cv::COLOR_BGR2GRAY);
        decor_estompe_gray = gray_frame * 0.60;
        masque_affiche = masque_pelouse.clone();
        decor_estompe_gray.copyTo(masque_affiche, ~masque_terrain);

        // Affichage synchronise des deux fenetres
        cv::imshow(fenetre_focus, rendu_visuel);
        cv::imshow(fenetre_masque, masque_affiche);

        const char touche = static_cast<char>(cv::waitKey(delai));
        if (touche == 'q' || touche == 'Q' || touche == 27) {
            std::cout << "Lecture arretee par l'utilisateur." << std::endl;
            break;
        }
    }

    capture.release();
    cv::destroyAllWindows();

    return 0;
}