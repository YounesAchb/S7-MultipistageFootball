#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <cmath>
#include <opencv2/opencv.hpp>

#include "traitements_image.hpp"

namespace {
enum class ModeNormalisation { Manuel, OpenCV, Aucune };

struct Options {
    std::string video = "data/videos/clip1.mp4";
    ModeNormalisation mode = ModeNormalisation::Manuel;
    bool affichage = true;
    int maximum_images = 0; // 0 signifie : lire la video jusqu'a la fin.
    std::filesystem::path sortie;
};

const char* nomMode(ModeNormalisation mode)
{
    if (mode == ModeNormalisation::OpenCV) return "opencv";
    if (mode == ModeNormalisation::Aucune) return "aucune";
    return "manuel";
}

void afficherAide()
{
    std::cout << "Usage : detourage_terrain [video] [options]\n"
              << "  --normalisation manuel|opencv|aucune (defaut : manuel)\n"
              << "  --sans-affichage      teste sans ouvrir de fenetre\n"
              << "  --max-images N        limite le nombre d'images traitees\n"
              << "  --sortie DOSSIER      exporte CSV et captures de verification\n"
              << "  --help                affiche cette aide\n";
}

Options lireOptions(int argc, char* argv[])
{
    Options options;
    bool video_fournie = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        auto valeurSuivante = [&]() -> std::string {
            if (i + 1 >= argc) throw std::invalid_argument("Valeur manquante apres " + argument);
            if (std::string(argv[i + 1]).rfind("--", 0) == 0)
                throw std::invalid_argument("Valeur manquante apres " + argument);
            return argv[++i];
        };
        if (argument == "--sans-affichage") {
            options.affichage = false;
        } else if (argument == "--normalisation") {
            const std::string valeur = valeurSuivante();
            if (valeur == "manuel") options.mode = ModeNormalisation::Manuel;
            else if (valeur == "opencv") options.mode = ModeNormalisation::OpenCV;
            else if (valeur == "aucune") options.mode = ModeNormalisation::Aucune;
            else throw std::invalid_argument("Mode inconnu : " + valeur);
        } else if (argument == "--max-images") {
            const std::string valeur = valeurSuivante();
            std::size_t position = 0;
            const long long nombre = std::stoll(valeur, &position);
            if (position != valeur.size() || nombre <= 0 || nombre > std::numeric_limits<int>::max())
                throw std::invalid_argument("--max-images attend un entier strictement positif");
            options.maximum_images = static_cast<int>(nombre);
        } else if (argument == "--sortie") {
            options.sortie = valeurSuivante();
            if (options.sortie.empty()) throw std::invalid_argument("Le dossier de sortie est vide");
        } else if (argument.rfind("--", 0) == 0) {
            throw std::invalid_argument("Option inconnue : " + argument);
        } else {
            if (video_fournie) throw std::invalid_argument("Une seule video doit etre fournie");
            options.video = argument;
            video_fournie = true;
        }
    }
    return options;
}

void appliquerNormalisation(cv::Mat& luminosite, ModeNormalisation mode)
{
    // Un seul traitement est applique : on ne cumule pas les variantes.
    if (mode == ModeNormalisation::Manuel) normaliserMinMax(luminosite);
    else if (mode == ModeNormalisation::OpenCV)
        cv::normalize(luminosite, luminosite, 0, 255, cv::NORM_MINMAX);
    // Le mode aucune conserve la luminosite originale pour une comparaison future.
}

void enregistrerImage(const std::filesystem::path& chemin, const cv::Mat& image)
{
    if (!cv::imwrite(chemin.string(), image))
        throw std::runtime_error("Impossible d'enregistrer " + chemin.string());
}
} // namespace

int executerProgramme(const Options& options)
{
    const std::string& chemin_video = options.video;
    cv::VideoCapture capture(chemin_video);

    if (capture.isOpened() == false) {
        std::cerr << "Erreur : Impossible d'ouvrir le fichier video : " << chemin_video << std::endl;
        return 1;
    }

    const double fps = capture.get(cv::CAP_PROP_FPS);
    const double periode_image_ms = fps > 0 ? 1000.0 / fps : 40.0;

    const int largeur = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
    const int hauteur = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));

    if (largeur <= 0 || hauteur <= 0) {
        std::cerr << "Erreur : dimensions video invalides." << std::endl;
        return 1;
    }
    std::cout << "Normalisation de la boucle video : " << nomMode(options.mode) << std::endl;

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
    // Reference historique : le contour fixe du terrain utilise toujours cette
    // normalisation manuelle. Les modes de la boucle sont ainsi compares dans
    // la meme zone de terrain, sans modifier deux parametres en meme temps.
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


    const std::string fenetre_masque = "Multipistage - Masque Pelouse (Decor estompe 60%)";
    if (options.affichage) {
        cv::namedWindow(fenetre_focus, cv::WINDOW_NORMAL);
        cv::resizeWindow(fenetre_focus, taille_affichage.width, taille_affichage.height);
        cv::namedWindow(fenetre_masque, cv::WINDOW_NORMAL);
        cv::resizeWindow(fenetre_masque, taille_affichage.width, taille_affichage.height);
    }

    std::ofstream mesures;
    if (!options.sortie.empty()) {
        std::filesystem::create_directories(options.sortie);
        mesures.open(options.sortie / "mesures_application.csv");
        if (!mesures) throw std::runtime_error("Impossible d'ouvrir le CSV de mesures");
        mesures << "frame,mode,width,height,normalisation_ms,pelouse_pixels_image_entiere\n";
        enregistrerImage(options.sortie / "terrain_initial.png", masque_terrain);
    }

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
    int images_traitees = 0;
    double total_normalisation_ms = 0.0;
    while (options.maximum_images == 0 || images_traitees < options.maximum_images) {
        if (capture.read(frame) == false || frame.empty() == true) {
            std::cout << "Fin de la video atteinte." << std::endl;
            break;
        }

        // Conversion en HSV et normalisation de la luminosite sur le canal V
        cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);
        cv::extractChannel(hsv, luminosite, 2);
        // Etape D : la fonction manuelle est maintenant appliquee a chaque image.
        // Le mode opencv conserve une reference accessible sans modifier le code.
        // Le chronometre exclut lecture video, conversion HSV et affichage.
        const int64 debut_normalisation = cv::getTickCount();
        appliquerNormalisation(luminosite, options.mode);
        const double normalisation_ms = (cv::getTickCount() - debut_normalisation)
                                      * 1000.0 / frequence_horloge;
        total_normalisation_ms += normalisation_ms;

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

        if (mesures.is_open()) {
            mesures << images_traitees << ',' << nomMode(options.mode) << ','
                    << largeur << ',' << hauteur << ',' << normalisation_ms << ','
                    << cv::countNonZero(masque_pelouse) << '\n';
        }
        // Captures avant/apres sur les memes indices ; le masque brut reste
        // separe de son rendu, qui contient aussi du decor gris pour l'affichage.
        const bool capturer = images_traitees == 0 || (options.maximum_images > 0 &&
            (images_traitees == options.maximum_images / 2 ||
             images_traitees == options.maximum_images - 1));
        if (!options.sortie.empty() && capturer) {
            const std::string suffixe = std::to_string(images_traitees) + ".png";
            enregistrerImage(options.sortie / ("mask_" + suffixe), masque_affiche);
            enregistrerImage(options.sortie / ("color_" + suffixe), rendu_visuel);
            enregistrerImage(options.sortie / ("masque_brut_" + suffixe), masque_pelouse);
        }
        ++images_traitees;

        if (options.affichage) {
            cv::imshow(fenetre_focus, rendu_visuel);
            cv::imshow(fenetre_masque, masque_affiche);

            // La synchronisation sert seulement a la lecture avec fenetres.
            // Les tests sans affichage ne dorment pas entre les images.
            const double temps_lecture_ms = (cv::getTickCount() - debut_lecture)
                                           * 1000.0 / frequence_horloge;
            const double attente_ms = echeance_image_ms - temps_lecture_ms;
            const int touche = attente_ms > 0
                ? cv::waitKey(std::max(1, static_cast<int>(std::ceil(attente_ms))))
                : cv::pollKey();
            if (touche == 'q' || touche == 'Q' || touche == 27) {
                std::cout << "Lecture arretee par l'utilisateur." << std::endl;
                break;
            }
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
    if (options.affichage) cv::destroyAllWindows();
    if (mesures.is_open()) {
        mesures.flush();
        if (!mesures) throw std::runtime_error("Echec d'ecriture du CSV de mesures");
    }
    std::cout << "BILAN images=" << images_traitees << " mode=" << nomMode(options.mode)
              << " normalisation_moyenne_ms="
              << (images_traitees > 0 ? total_normalisation_ms / images_traitees : 0.0)
              << std::endl;
    if (options.maximum_images > 0 && images_traitees < options.maximum_images) {
        std::cerr << "Video trop courte pour le nombre d'images demande." << std::endl;
        return 2;
    }
    return images_traitees > 0 ? 0 : 1;
}

int main(int argc, char* argv[])
{
    try {
        // --help est gere avant toute ouverture de video ou creation de dossier.
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
                afficherAide();
                return 0;
            }
        }
        return executerProgramme(lireOptions(argc, argv));
    } catch (const std::exception& erreur) {
        std::cerr << "Erreur : " << erreur.what() << std::endl;
        return 1;
    }
}
