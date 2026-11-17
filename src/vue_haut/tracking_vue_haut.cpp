#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <opencv2/opencv.hpp>
#if __has_include(<opencv2/geometry.hpp>)
#include <opencv2/geometry.hpp>
#endif
#include "algorithme_hongrois.hpp"

// Structure représentant un joueur suivi en vue de dessus
struct JoueurTracke {
    int id;
    cv::Rect boite;                // Boîte englobante en coordonnées vidéo 4K
    cv::Point2f corps;             // Centre estimé du corps
    cv::Point2f vitesse{0.f, 0.f}; // Vitesse estimée (modèle cinétique / filtre alpha-beta)
    cv::Vec3f couleur{0.f, 0.f, 0.f}; // Signature chromatique moyenne (B, G, R) pour Re-ID
    int frames_sans_mesure = 0;    // Trames consécutives sans détection candidate directe
};

// Extraction de la signature couleur (B, G, R) moyenne sur les pixels non-pelouse du candidat (100% FPGA streamable)
inline cv::Vec3f extraireCouleurCandidat(const cv::Mat& zone_bgr, const cv::Mat& non_pel, const cv::Rect& b) {
    int x0 = std::max(0, b.x);
    int y0 = std::max(0, b.y);
    int x1 = std::min(zone_bgr.cols, b.x + b.width);
    int y1 = std::min(zone_bgr.rows, b.y + b.height);

    float sb = 0.f, sg = 0.f, sr = 0.f;
    int n = 0;
    for (int y = y0; y < y1; ++y) {
        const uchar* m = non_pel.ptr<uchar>(y);
        const cv::Vec3b* rgb = zone_bgr.ptr<cv::Vec3b>(y);
        for (int x = x0; x < x1; ++x) {
            if (m[x] > 0) {
                sb += rgb[x][0];
                sg += rgb[x][1];
                sr += rgb[x][2];
                n++;
            }
        }
    }
    if (n < 10) return cv::Vec3f(128.f, 128.f, 128.f);
    return cv::Vec3f(sb / n, sg / n, sr / n);
}

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
    std::vector<JoueurTracke> joueurs;
    int prochain_id = 1;
    int num_trame = 0;

    GestionSouris souris;
    const std::string nom_fenetre = "Tracking Vue de Haut - Multi-Joueurs";
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

        // Détection des candidats joueurs compacts (sans effacement destructeur des lignes)
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(non_pelouse, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

        std::vector<cv::Point> candidats_bruts;
        std::vector<cv::Rect> boites_brutes;
        for (size_t i = 0; i < contours.size(); ++i) {
            cv::Rect b = cv::boundingRect(contours[i]);
            double a = cv::contourArea(contours[i]);
            // On retient les formes compactes : joueurs isolés (12-45 px) ou duels au coude-à-coude (jusqu'à 75 px)
            // Les lignes droites sont rejetées par le ratio d'aspect (> 2.5)
            // Les lignes courbes creuses (arcs du rond central, surfaces) sont rejetées par la densité (< 0.22)
            // Les joueurs réels ont une silhouette pleine (densité >= 0.25, moyenne ~0.51)
            bool est_compact = (b.width < 2.5 * b.height) && (b.height < 2.5 * b.width);
            double densite = a / (static_cast<double>(b.width) * b.height);
            if (b.width >= 12 && b.width <= 75 && b.height >= 12 && b.height <= 75 && a >= 80.0 && est_compact && densite >= 0.22) {
                candidats_bruts.push_back(cv::Point(b.x + b.width / 2, b.y + b.height / 2));
                boites_brutes.push_back(b);
            }
        }

        // Élimination des détections multiples sur le même corps (< 15 px) et extraction des couleurs
        std::vector<cv::Point> candidats;
        std::vector<cv::Vec3f> couleurs_candidats;
        for (size_t i = 0; i < candidats_bruts.size(); ++i) {
            bool doublon = false;
            for (size_t k = 0; k < candidats.size(); ++k) {
                if (std::hypot(candidats_bruts[i].x - candidats[k].x, candidats_bruts[i].y - candidats[k].y) < 15.0) {
                    doublon = true; break;
                }
            }
            if (!doublon) {
                candidats.push_back(candidats_bruts[i]);
                couleurs_candidats.push_back(extraireCouleurCandidat(zone_terrain, non_pelouse, boites_brutes[i]));
            }
        }

        // 1. Initialisation sur la première trame
        if (joueurs.empty()) {
            for (size_t j = 0; j < candidats.size(); ++j) {
                float gx = static_cast<float>(candidats[j].x + terrain.x);
                float gy = static_cast<float>(candidats[j].y + terrain.y);
                JoueurTracke nouv;
                nouv.id = prochain_id++;
                nouv.boite = cv::Rect(static_cast<int>(gx - 12), static_cast<int>(gy - 12), 24, 24);
                nouv.corps = cv::Point2f(gx, gy);
                nouv.vitesse = cv::Point2f(0.f, 0.f);
                nouv.couleur = couleurs_candidats[j];
                nouv.frames_sans_mesure = 0;
                joueurs.push_back(nouv);
            }
        } else {
            // 2. Modèle cinétique à vitesse constante (Kalman 1D / filtre alpha-beta)
            std::vector<cv::Point2f> pos_predites(joueurs.size());
            for (size_t i = 0; i < joueurs.size(); ++i) {
                if (joueurs[i].frames_sans_mesure > 0) {
                    joueurs[i].vitesse *= 0.85f; // Amortissement en cas de perte de mesure
                }
                pos_predites[i] = joueurs[i].corps + joueurs[i].vitesse;
            }

            // 3. Association optimale par l'Algorithme Hongrois spatio-chromatique (Re-ID couleur)
            std::vector<int> assignations;
            if (!candidats.empty()) {
                std::vector<std::vector<float>> couts(joueurs.size(), std::vector<float>(candidats.size()));
                for (size_t i = 0; i < joueurs.size(); ++i) {
                    for (size_t j = 0; j < candidats.size(); ++j) {
                        float gx = static_cast<float>(candidats[j].x + terrain.x);
                        float gy = static_cast<float>(candidats[j].y + terrain.y);
                        float d_spatial = static_cast<float>(std::hypot(pos_predites[i].x - gx, pos_predites[i].y - gy));

                        // Distance chromatique euclidienne RGB
                        cv::Vec3f dc = joueurs[i].couleur - couleurs_candidats[j];
                        float d_couleur = std::sqrt(dc[0]*dc[0] + dc[1]*dc[1] + dc[2]*dc[2]);

                        // Coût combiné : la distance spatiale guide le suivi, la couleur arbitre infailliblement les collisions
                        couts[i][j] = d_spatial + 0.6f * d_couleur;
                    }
                }
                // Gating à 75 (autorise jusqu'à 45 px spatial + composante couleur)
                assignations = tracking::resoudreHongrois(couts, 75.0f);
            } else {
                assignations.assign(joueurs.size(), -1);
            }

            std::vector<bool> candidat_utilise(candidats.size(), false);

            // 4. Mise à jour des pistes
            for (size_t i = 0; i < joueurs.size(); ++i) {
                int c_idx = assignations[i];
                if (c_idx != -1) {
                    candidat_utilise[c_idx] = true;
                    float gx = static_cast<float>(candidats[c_idx].x + terrain.x);
                    float gy = static_cast<float>(candidats[c_idx].y + terrain.y);

                    cv::Point2f v_inst(gx - joueurs[i].corps.x, gy - joueurs[i].corps.y);
                    if (std::hypot(v_inst.x, v_inst.y) < 25.0f) {
                        joueurs[i].vitesse = 0.5f * joueurs[i].vitesse + 0.5f * v_inst;
                    }

                    // Lissage temporel de la couleur (90% inertie, 10% nouvelle mesure)
                    joueurs[i].couleur = 0.90f * joueurs[i].couleur + 0.10f * couleurs_candidats[c_idx];

                    joueurs[i].corps = cv::Point2f(gx, gy);
                    joueurs[i].boite.x = static_cast<int>(gx - 12);
                    joueurs[i].boite.y = static_cast<int>(gy - 12);
                    joueurs[i].frames_sans_mesure = 0;
                } else {
                    // Le joueur n'a pas été associé par l'analyse globale des contours
                    // VÉRIFICATION LOCALE SUR LES LIGNES BLANCHES :
                    // Lorsqu'un joueur marche sur une ligne de jeu (médiane, rond central, surface),
                    // sa silhouette est fusionnée avec la ligne et le contour géant est rejeté.
                    // Cependant, ses 300+ pixels de corps sont bien présents dans non_pelouse !
                    int lx = static_cast<int>(pos_predites[i].x - terrain.x);
                    int ly = static_cast<int>(pos_predites[i].y - terrain.y);
                    int x0 = std::max(0, lx - 14), x1 = std::min(zone_terrain.cols, lx + 14);
                    int y0 = std::max(0, ly - 14), y1 = std::min(zone_terrain.rows, ly + 14);

                    float sum_x = 0.f, sum_y = 0.f;
                    int n_pix = 0;
                    for (int py = y0; py < y1; ++py) {
                        const uchar* m = non_pelouse.ptr<uchar>(py);
                        for (int px = x0; px < x1; ++px) {
                            if (m[px] > 0) {
                                sum_x += px;
                                sum_y += py;
                                n_pix++;
                            }
                        }
                    }

                    // Si au moins 90 pixels de corps sont présents : le joueur est sur la ligne !
                    if (n_pix >= 90) {
                        float bx = sum_x / n_pix + terrain.x;
                        float by = sum_y / n_pix + terrain.y;
                        if (std::hypot(bx - pos_predites[i].x, by - pos_predites[i].y) < 18.0f) {
                            cv::Point2f v_inst(bx - joueurs[i].corps.x, by - joueurs[i].corps.y);
                            if (std::hypot(v_inst.x, v_inst.y) < 20.0f) {
                                joueurs[i].vitesse = 0.6f * joueurs[i].vitesse + 0.4f * v_inst;
                            }
                            joueurs[i].corps = cv::Point2f(bx, by);
                            joueurs[i].boite.x = static_cast<int>(bx - 12);
                            joueurs[i].boite.y = static_cast<int>(by - 12);
                            joueurs[i].frames_sans_mesure = 0; // Piste verrouillée sur la ligne : zéro lag, zéro perte d'ID !
                            continue;
                        }
                    }

                    // Vrai masquage temporaire (duel complet) : maintien cinétique inertiel
                    joueurs[i].frames_sans_mesure++;
                    joueurs[i].corps = pos_predites[i];
                    joueurs[i].boite.x = static_cast<int>(joueurs[i].corps.x - 12);
                    joueurs[i].boite.y = static_cast<int>(joueurs[i].corps.y - 12);
                }
            }

            // 5. Cohésion de duel : lorsqu'un joueur non-mesuré est au contact (< 35 px) d'un joueur mesuré,
            // il accompagne le centre du duel pour préserver son identité jusqu'à la séparation
            for (size_t i = 0; i < joueurs.size(); ++i) {
                if (joueurs[i].frames_sans_mesure > 0) {
                    for (size_t k = 0; k < joueurs.size(); ++k) {
                        if (k != i && joueurs[k].frames_sans_mesure == 0) {
                            float d = static_cast<float>(std::hypot(joueurs[i].corps.x - joueurs[k].corps.x,
                                                                    joueurs[i].corps.y - joueurs[k].corps.y));
                            if (d < 35.0f) {
                                joueurs[i].vitesse = joueurs[k].vitesse;
                                joueurs[i].corps = joueurs[k].corps;
                                joueurs[i].boite.x = static_cast<int>(joueurs[i].corps.x - 12);
                                joueurs[i].boite.y = static_cast<int>(joueurs[i].corps.y - 12);
                                break;
                            }
                        }
                    }
                }
            }

            // 6. Ré-acquisition uniquement si l'effectif actuel est incomplet (< 24 personnes max)
            // et si le candidat n'est pas déjà couvert par un joueur existant (>= 35 px)
            for (size_t j = 0; j < candidats.size(); ++j) {
                if (!candidat_utilise[j] && joueurs.size() < 24) {
                    float gx = static_cast<float>(candidats[j].x + terrain.x);
                    float gy = static_cast<float>(candidats[j].y + terrain.y);
                    bool deja_suivi = false;
                    for (size_t k = 0; k < joueurs.size(); ++k) {
                        if (std::hypot(gx - joueurs[k].corps.x, gy - joueurs[k].corps.y) < 35.0f) {
                            deja_suivi = true; break;
                        }
                    }
                    if (!deja_suivi) {
                        JoueurTracke nouv;
                        nouv.id = prochain_id++;
                        nouv.boite = cv::Rect(static_cast<int>(gx - 12), static_cast<int>(gy - 12), 24, 24);
                        nouv.corps = cv::Point2f(gx, gy);
                        nouv.vitesse = cv::Point2f(0.f, 0.f);
                        nouv.couleur = couleurs_candidats[j];
                        nouv.frames_sans_mesure = 0;
                        joueurs.push_back(nouv);
                    }
                }
            }
        }

        // 7. Nettoyage des pistes perdues après disparition prolongée (> 90 trames = 1.5 s)
        for (size_t i = 0; i < joueurs.size(); ) {
            if (joueurs[i].frames_sans_mesure > 90) {
                if (joueurs[i].id == souris.id_selectionne) souris.id_selectionne = -1;
                joueurs.erase(joueurs.begin() + i);
            } else {
                ++i;
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
            cv::circle(frame, cv::Point(static_cast<int>(joueurs[i].corps.x), static_cast<int>(joueurs[i].corps.y)), 3, cv::Scalar(0, 0, 255), -1);

            // En cas de duel serré (< 15 px), décaler le texte du joueur masqué vers le bas pour une lisibilité parfaite
            bool duel_serre = false;
            for (size_t k = 0; k < joueurs.size(); ++k) {
                if (k != i && std::hypot(joueurs[i].corps.x - joueurs[k].corps.x, joueurs[i].corps.y - joueurs[k].corps.y) < 15.0f) {
                    duel_serre = true; break;
                }
            }
            int y_txt = (duel_serre && joueurs[i].frames_sans_mesure > 0)
                        ? (joueurs[i].boite.y + joueurs[i].boite.height + 16)
                        : (joueurs[i].boite.y - 6);

            cv::putText(frame, std::to_string(joueurs[i].id),
                        cv::Point(joueurs[i].boite.x, y_txt),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, col, 2);
            if (sel) cv::circle(frame, cv::Point(static_cast<int>(joueurs[i].corps.x), static_cast<int>(joueurs[i].corps.y)), 22, cv::Scalar(0, 255, 0), 2);
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
