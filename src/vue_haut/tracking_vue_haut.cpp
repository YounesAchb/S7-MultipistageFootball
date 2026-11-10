#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <opencv2/opencv.hpp>
#if __has_include(<opencv2/geometry.hpp>)
#include <opencv2/geometry.hpp>
#endif

// Structure représentant un joueur suivi en vue de dessus
struct JoueurTracke {
    int id;
    cv::Rect boite;   // Boîte englobante en coordonnées vidéo 4K
    cv::Point corps;  // Centre du corps entier
};

// État de la sélection interactive par clic de souris
struct GestionSouris {
    int x_clic = -1, y_clic = -1, id_selectionne = -1;
    bool nouveau_clic = false;
};

void onMouse(int event, int x, int y, int, void* userdata) {
    if (event == cv::EVENT_LBUTTONDOWN) {
        GestionSouris* s = static_cast<GestionSouris*>(userdata);
        s->x_clic = x; s->y_clic = y; s->nouveau_clic = true;
    }
}

// Transmission des coordonnées des joueurs (format CSV exploitable pour vitesse/trajectoire)
void transmettreCoordonnees(std::ofstream& flux, int trame, const std::vector<JoueurTracke>& joueurs) {
    for (size_t i = 0; i < joueurs.size(); ++i) {
        flux << trame << "," << joueurs[i].id << ","
             << joueurs[i].corps.x << "," << joueurs[i].corps.y << "\n";
    }
}

int main(int argc, char* argv[]) {
    std::string chemin = (argc > 1) ? argv[1] : "/Users/admin/data/vue_haut/D_20220220_1_1530_1560.mp4";
    cv::VideoCapture cap(chemin);
    if (!cap.isOpened()) {
        std::cerr << "Erreur ouverture vidéo : " << chemin << std::endl;
        return 1;
    }

    // 1. Lecture de la première image pour calibrer exactement les 4 corners
    cv::Mat premiere_trame;
    if (!cap.read(premiere_trame) || premiere_trame.empty()) {
        std::cerr << "Erreur lecture première trame pour calibration" << std::endl;
        return 1;
    }

    cv::Mat small;
    cv::resize(premiere_trame, small, cv::Size(1280, 720));
    int sH = small.rows, sW = small.cols;

    // Masques pelouse et lignes blanches (comparateurs BGR directs, 0 morphologyEx, 100% portable FPGA)
    cv::Mat pelouse(sH, sW, CV_8UC1), blanc(sH, sW, CV_8UC1);
    for (int y = 0; y < sH; ++y) {
        const cv::Vec3b* ptr = small.ptr<cv::Vec3b>(y);
        uchar* p_pel = pelouse.ptr<uchar>(y);
        uchar* p_bla = blanc.ptr<uchar>(y);
        for (int x = 0; x < sW; ++x) {
            int b = ptr[x][0], g = ptr[x][1], r = ptr[x][2];
            p_pel[x] = (g > r + 15 && g > b + 25) ? 255 : 0;
            p_bla[x] = (r > 140 && g > 140 && b > 120 && std::abs(r - g) < 30) ? 255 : 0;
        }
    }

    // Détection des lignes de but gauche et droite (verticales avec pelouse intérieure)
    int x_left = 158, x_right = 1135, max_l = -1, max_r = -1;
    for (int x = 140; x < 185; ++x) {
        int c = 0;
        for (int y = 100; y < 600; ++y) if (blanc.at<uchar>(y, x) && pelouse.at<uchar>(y, x + 5)) c++;
        if (c > max_l) { max_l = c; x_left = x; }
    }
    for (int x = 1115; x < 1155; ++x) {
        int c = 0;
        for (int y = 100; y < 600; ++y) if (blanc.at<uchar>(y, x) && pelouse.at<uchar>(y, x - 5)) c++;
        if (c > max_r) { max_r = c; x_right = x; }
    }

    // Détection individuelle de l'altitude Y de chaque corner (gère l'inclinaison/perspective)
    int y_tl = 39, y_tr = 37, max_tl = -1, max_tr = -1;
    int y_bl = 665, y_br = 679, max_bl = -1, max_br = -1;

    for (int y = 25; y < 70; ++y) {
        int c_l = 0, c_r = 0;
        for (int dx = -2; dx <= 15; ++dx) if (blanc.at<uchar>(y, x_left + dx)) c_l++;
        for (int dx = -15; dx <= 2; ++dx) if (blanc.at<uchar>(y, x_right + dx)) c_r++;
        if (c_l > max_tl) { max_tl = c_l; y_tl = y; }
        if (c_r > max_tr) { max_tr = c_r; y_tr = y; }
    }
    for (int y = 645; y < 695; ++y) {
        int c_l = 0, c_r = 0;
        for (int dx = -2; dx <= 15; ++dx) if (blanc.at<uchar>(y, x_left + dx)) c_l++;
        for (int dx = -15; dx <= 2; ++dx) if (blanc.at<uchar>(y, x_right + dx)) c_r++;
        if (c_l > max_bl) { max_bl = c_l; y_bl = y; }
        if (c_r > max_br) { max_br = c_r; y_br = y; }
    }

    // Coordonnées 4K des 4 corners réels
    double ex = static_cast<double>(premiere_trame.cols) / sW;
    double ey = static_cast<double>(premiere_trame.rows) / sH;
    cv::Point pt_tl(static_cast<int>(x_left * ex), static_cast<int>(y_tl * ey));
    cv::Point pt_tr(static_cast<int>(x_right * ex), static_cast<int>(y_tr * ey));
    cv::Point pt_br(static_cast<int>(x_right * ex), static_cast<int>(y_br * ey));
    cv::Point pt_bl(static_cast<int>(x_left * ex), static_cast<int>(y_bl * ey));

    // Boîte englobante pour le découpage ROI
    int min_x = std::min(pt_tl.x, pt_bl.x), max_x = std::max(pt_tr.x, pt_br.x);
    int min_y = std::min(pt_tl.y, pt_tr.y), max_y = std::max(pt_bl.y, pt_br.y);
    cv::Rect terrain(min_x, min_y, max_x - min_x, max_y - min_y);

    std::cout << "Calibration des 4 corners réussie avec compensation d'inclinaison : "
              << "TL(" << pt_tl.x << "," << pt_tl.y << ") TR(" << pt_tr.x << "," << pt_tr.y << ") "
              << "BL(" << pt_bl.x << "," << pt_bl.y << ") BR(" << pt_br.x << "," << pt_br.y << ")" << std::endl;

    // Rembobinage de la vidéo pour démarrer le tracking dès le début
    cap.set(cv::CAP_PROP_POS_FRAMES, 0);

    // Initialisation du tracking et de la sortie CSV
    std::ofstream fichier_coords("coordonnees_joueurs.csv");
    fichier_coords << "trame,id,x,y\n";

    const int MARGE = 35;
    cv::TermCriteria critere(cv::TermCriteria::EPS | cv::TermCriteria::COUNT, 10, 1);
    std::vector<JoueurTracke> joueurs;
    int prochain_id = 1;
    int num_trame = 0;

    GestionSouris souris;
    const std::string nom_fenetre = "Tracking CamShift Vue de Haut";
    cv::namedWindow(nom_fenetre, cv::WINDOW_NORMAL);
    cv::resizeWindow(nom_fenetre, 1280, 720);
    cv::setMouseCallback(nom_fenetre, onMouse, &souris);

    cv::Mat frame, zone_terrain, non_pelouse, rendu;

    // 2. Traitement complet de la vidéo
    while (cap.read(frame)) {
        if (frame.empty()) break;
        num_trame++;

        // Masque BGR des éléments non-pelouse dans la zone du terrain (seuillage direct FPGA)
        zone_terrain = frame(terrain);
        non_pelouse.create(zone_terrain.rows, zone_terrain.cols, CV_8UC1);
        for (int y = 0; y < zone_terrain.rows; ++y) {
            const cv::Vec3b* in = zone_terrain.ptr<cv::Vec3b>(y);
            uchar* out = non_pelouse.ptr<uchar>(y);
            for (int x = 0; x < zone_terrain.cols; ++x) {
                out[x] = (in[x][1] > in[x][2] + 15 && in[x][1] > in[x][0] + 25) ? 0 : 255;
            }
        }

        // Élimination des bordures extérieures
        non_pelouse(cv::Rect(0, 0, zone_terrain.cols, MARGE)).setTo(0);
        non_pelouse(cv::Rect(0, zone_terrain.rows - MARGE, zone_terrain.cols, MARGE)).setTo(0);
        non_pelouse(cv::Rect(0, 0, MARGE, zone_terrain.rows)).setTo(0);
        non_pelouse(cv::Rect(zone_terrain.cols - MARGE, 0, MARGE, zone_terrain.rows)).setTo(0);

        // Détection des candidats joueurs et effacement des lignes de jeu
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(non_pelouse, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

        std::vector<cv::Point> candidats;
        for (size_t i = 0; i < contours.size(); ++i) {
            cv::Rect b = cv::boundingRect(contours[i]);
            double a = cv::contourArea(contours[i]);
            if (b.width > 45 || b.height > 45) {
                cv::drawContours(non_pelouse, contours, static_cast<int>(i), cv::Scalar(0), 12);
            } else if (b.width >= 12 && b.width <= 45 && b.height >= 12 && b.height <= 45 && a >= 80.0) {
                candidats.push_back(cv::Point(b.x + b.width / 2, b.y + b.height / 2));
            }
        }

        // Suivi CamShift pour chaque joueur
        for (size_t i = 0; i < joueurs.size(); ++i) {
            cv::Rect b_loc(joueurs[i].boite.x - terrain.x, joueurs[i].boite.y - terrain.y,
                           joueurs[i].boite.width, joueurs[i].boite.height);
            b_loc = b_loc & cv::Rect(0, 0, zone_terrain.cols, zone_terrain.rows);
            if (b_loc.area() <= 0) continue;

            cv::RotatedRect rot = cv::CamShift(non_pelouse, b_loc, critere);
            b_loc = rot.boundingRect();
            b_loc.width = std::max(16, std::min(32, b_loc.width));
            b_loc.height = std::max(16, std::min(32, b_loc.height));

            joueurs[i].boite = cv::Rect(b_loc.x + terrain.x, b_loc.y + terrain.y, b_loc.width, b_loc.height);
            joueurs[i].corps = cv::Point(joueurs[i].boite.x + joueurs[i].boite.width / 2,
                                        joueurs[i].boite.y + joueurs[i].boite.height / 2);
        }

        // Gestion des collisions (dédoublonnage in-place)
        for (size_t i = 0; i < joueurs.size(); ++i) {
            for (size_t j = i + 1; j < joueurs.size(); ) {
                if (std::abs(joueurs[i].corps.x - joueurs[j].corps.x) < 18 &&
                    std::abs(joueurs[i].corps.y - joueurs[j].corps.y) < 18) {
                    if (joueurs[j].id == souris.id_selectionne) souris.id_selectionne = joueurs[i].id;
                    joueurs.erase(joueurs.begin() + j);
                } else ++j;
            }
        }

        // Ajout initial et ré-acquisition des joueurs
        for (size_t i = 0; i < candidats.size(); ++i) {
            int gx = candidats[i].x + terrain.x, gy = candidats[i].y + terrain.y;
            bool deja_suivi = false;
            for (size_t k = 0; k < joueurs.size(); ++k) {
                if (std::abs(gx - joueurs[k].corps.x) < 25 && std::abs(gy - joueurs[k].corps.y) < 25) {
                    deja_suivi = true; break;
                }
            }
            if (!deja_suivi) {
                JoueurTracke j = {prochain_id++, cv::Rect(gx - 12, gy - 12, 24, 24), cv::Point(gx, gy)};
                joueurs.push_back(j);
            }
        }

        // Transmission des coordonnées au fichier CSV pour exploitation externe
        transmettreCoordonnees(fichier_coords, num_trame, joueurs);

        // Sélection par clic de souris
        if (souris.nouveau_clic) {
            double ech_x = static_cast<double>(frame.cols) / 1280.0, ech_y = static_cast<double>(frame.rows) / 720.0;
            int cx = static_cast<int>(souris.x_clic * ech_x), cy = static_cast<int>(souris.y_clic * ech_y);
            double dist_min = 45.0;
            for (size_t i = 0; i < joueurs.size(); ++i) {
                double d = std::hypot(joueurs[i].corps.x - cx, joueurs[i].corps.y - cy);
                if (d < dist_min) { dist_min = d; souris.id_selectionne = joueurs[i].id; }
            }
            souris.nouveau_clic = false;
        }

        // Rendu visuel (boîtes, corps, identifiants)
        for (size_t i = 0; i < joueurs.size(); ++i) {
            bool sel = (joueurs[i].id == souris.id_selectionne);
            cv::Scalar col = sel ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 255, 255);
            cv::rectangle(frame, joueurs[i].boite, col, sel ? 3 : 2);
            cv::circle(frame, joueurs[i].corps, 3, cv::Scalar(0, 0, 255), -1);
            cv::putText(frame, std::to_string(joueurs[i].id),
                        cv::Point(joueurs[i].boite.x, joueurs[i].boite.y - 6),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, col, 2);
            if (sel) cv::circle(frame, joueurs[i].corps, 22, cv::Scalar(0, 255, 0), 2);
        }

        // Tracé précis du quadrilatère de jeu suivant exactement l'inclinaison des 4 corners
        cv::line(frame, pt_tl, pt_tr, cv::Scalar(0, 255, 0), 2);
        cv::line(frame, pt_tr, pt_br, cv::Scalar(0, 255, 0), 2);
        cv::line(frame, pt_br, pt_bl, cv::Scalar(0, 255, 0), 2);
        cv::line(frame, pt_bl, pt_tl, cv::Scalar(0, 255, 0), 2);

        cv::resize(frame, rendu, cv::Size(1280, 720));

        // Panneau haut-droit : ID et coordonnées du joueur sélectionné
        if (souris.id_selectionne != -1) {
            for (size_t i = 0; i < joueurs.size(); ++i) {
                if (joueurs[i].id == souris.id_selectionne) {
                    cv::Rect pan(rendu.cols - 250, 15, 235, 55);
                    cv::Mat roi = rendu(pan), overlay = roi.clone();
                    overlay.setTo(cv::Scalar(20, 20, 20));
                    cv::addWeighted(overlay, 0.75, roi, 0.25, 0, roi);
                    cv::rectangle(rendu, pan, cv::Scalar(0, 255, 0), 2);

                    cv::putText(rendu, "Joueur ID : " + std::to_string(joueurs[i].id),
                                cv::Point(pan.x + 10, pan.y + 22), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);
                    cv::putText(rendu, "Coord : (" + std::to_string(joueurs[i].corps.x) + ", "
                                                   + std::to_string(joueurs[i].corps.y) + ")",
                                cv::Point(pan.x + 10, pan.y + 44), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
                    break;
                }
            }
        } else {
            cv::putText(rendu, "Cliquez sur un joueur pour le selectionner",
                        cv::Point(rendu.cols - 390, 25), cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(180, 180, 180), 1);
        }

        cv::imshow(nom_fenetre, rendu);
        char c = static_cast<char>(cv::waitKey(1));
        if (c == 'q' || c == 'Q' || c == 27) break;
    }

    fichier_coords.close();
    cap.release();
    cv::destroyAllWindows();
    return 0;
}
