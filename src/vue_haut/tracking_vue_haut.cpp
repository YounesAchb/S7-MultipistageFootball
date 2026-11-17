#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <opencv2/opencv.hpp>
#include <opencv2/geometry.hpp>
#include "algorithme_hongrois.hpp"

// Structure représentant un joueur suivi en vue de dessus
struct JoueurTracke {
    int id;
    cv::Rect boite;                // Boîte englobante 4K (24x24 normalisée)
    cv::Point2f corps;             // Centre précis du corps (x, y)
    cv::Point2f vitesse;           // Vitesse estimée (dx, dy en pixels/trame)
    cv::Vec3f couleur;             // Signature chromatique moyenne (B, G, R)
    int frames_sans_mesure;        // Nombre de trames consécutives sans détection directe
};

// Met à jour la position et la boîte 24x24 d'un joueur
void deplacerJoueur(JoueurTracke& j, float x, float y) {
    j.corps.x = x;
    j.corps.y = y;
    j.boite.x = static_cast<int>(x - 12);
    j.boite.y = static_cast<int>(y - 12);
    j.boite.width = 24;
    j.boite.height = 24;
}

// Crée un nouveau joueur tracké
JoueurTracke creerNouveauJoueur(int id, float x, float y, const cv::Vec3f& couleur) {
    JoueurTracke j;
    j.id = id;
    j.vitesse = cv::Point2f(0.0f, 0.0f);
    j.couleur = couleur;
    j.frames_sans_mesure = 0;
    deplacerJoueur(j, x, y);
    return j;
}

// Extraction de la signature couleur (B, G, R) moyenne sur les pixels non-pelouse (100% FPGA streamable)
cv::Vec3f extraireCouleurCandidat(const cv::Mat& zone_bgr, const cv::Mat& masque_non_pelouse, const cv::Rect& b) {
    int x0 = std::max(0, b.x);
    int y0 = std::max(0, b.y);
    int x1 = std::min(zone_bgr.cols, b.x + b.width);
    int y1 = std::min(zone_bgr.rows, b.y + b.height);

    float somme_b = 0.0f, somme_g = 0.0f, somme_r = 0.0f;
    int nb_pixels = 0;
    for (int y = y0; y < y1; ++y) {
        const uchar* ptr_masque = masque_non_pelouse.ptr<uchar>(y);
        const cv::Vec3b* ptr_bgr = zone_bgr.ptr<cv::Vec3b>(y);
        for (int x = x0; x < x1; ++x) {
            if (ptr_masque[x] > 0) {
                somme_b += ptr_bgr[x][0];
                somme_g += ptr_bgr[x][1];
                somme_r += ptr_bgr[x][2];
                nb_pixels++;
            }
        }
    }
    if (nb_pixels < 10) return cv::Vec3f(128.0f, 128.0f, 128.0f);
    return cv::Vec3f(somme_b / nb_pixels, somme_g / nb_pixels, somme_r / nb_pixels);
}

// État de la sélection interactive par clic de souris
struct GestionSouris {
    int x;
    int y;
    int id_selectionne;
    bool nouveau_clic;
};

void onMouse(int event, int x, int y, int flags, void* userdata) {
    if (event == cv::EVENT_LBUTTONDOWN) {
        GestionSouris* souris = static_cast<GestionSouris*>(userdata);
        souris->x = x;
        souris->y = y;
        souris->nouveau_clic = true;
    }
}

// Transmission des coordonnées des joueurs au format CSV
void transmettreCoordonnees(std::ofstream& flux, int trame, const std::vector<JoueurTracke>& joueurs) {
    for (size_t i = 0; i < joueurs.size(); ++i) {
        flux << trame << "," << joueurs[i].id << ","
             << joueurs[i].corps.x << "," << joueurs[i].corps.y << "\n";
    }
}

// Détection d'une colonne de ligne de but par présence de pelouse adjacente
int detecterColonneLigneDeBut(const cv::Mat& blanc, const cv::Mat& pelouse, int x_min, int x_max, int decalage_pelouse, int defaut_x) {
    int meilleur_x = defaut_x;
    int score_max = -1;
    for (int x = x_min; x < x_max; ++x) {
        int score = 0;
        for (int y = 100; y < 600; ++y) {
            if (blanc.at<uchar>(y, x) > 0 && pelouse.at<uchar>(y, x + decalage_pelouse) > 0) score++;
        }
        if (score > score_max) { score_max = score; meilleur_x = x; }
    }
    return meilleur_x;
}

// Détection de l'altitude Y des coins gauche et droit
void detecterAltitudeCoins(const cv::Mat& blanc, int x_gauche, int x_droite, int y_min, int y_max, int& y_gauche, int& y_droite) {
    int score_max_g = -1, score_max_d = -1;
    for (int y = y_min; y < y_max; ++y) {
        int score_g = 0, score_d = 0;
        for (int dx = -2; dx <= 15; ++dx) {
            if (blanc.at<uchar>(y, x_gauche + dx) > 0) score_g++;
        }
        for (int dx = -15; dx <= 2; ++dx) {
            if (blanc.at<uchar>(y, x_droite + dx) > 0) score_d++;
        }
        if (score_g > score_max_g) { score_max_g = score_g; y_gauche = y; }
        if (score_d > score_max_d) { score_max_d = score_d; y_droite = y; }
    }
}

int main(int argc, char* argv[]) {
    std::string chemin = (argc > 1) ? argv[1] : "/Users/admin/data/vue_haut/D_20220220_1_1530_1560.mp4";
    cv::VideoCapture cap(chemin);
    if (!cap.isOpened()) {
        std::cerr << "Erreur ouverture vidéo : " << chemin << std::endl;
        return 1;
    }

    // 1. Calibrage automatique du terrain sur vignette 720p (seuillage direct FPGA)
    cv::Mat premiere_trame, small;
    if (!cap.read(premiere_trame) || premiere_trame.empty()) return 1;
    cv::resize(premiere_trame, small, cv::Size(1280, 720));
    int sH = small.rows, sW = small.cols;

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

    int x_left = detecterColonneLigneDeBut(blanc, pelouse, 140, 185, 5, 158);
    int x_right = detecterColonneLigneDeBut(blanc, pelouse, 1115, 1155, -5, 1135);

    int y_tl = 39, y_tr = 37, y_bl = 665, y_br = 679;
    detecterAltitudeCoins(blanc, x_left, x_right, 25, 70, y_tl, y_tr);
    detecterAltitudeCoins(blanc, x_left, x_right, 645, 695, y_bl, y_br);

    double ex = static_cast<double>(premiere_trame.cols) / sW;
    double ey = static_cast<double>(premiere_trame.rows) / sH;
    cv::Point pt_tl(static_cast<int>(x_left * ex), static_cast<int>(y_tl * ey));
    cv::Point pt_tr(static_cast<int>(x_right * ex), static_cast<int>(y_tr * ey));
    cv::Point pt_bl(static_cast<int>(x_left * ex), static_cast<int>(y_bl * ey));
    cv::Point pt_br(static_cast<int>(x_right * ex), static_cast<int>(y_br * ey));

    cv::Rect terrain(std::min(pt_tl.x, pt_bl.x),
                     std::min(pt_tl.y, pt_tr.y),
                     std::max(pt_tr.x, pt_br.x) - std::min(pt_tl.x, pt_bl.x),
                     std::max(pt_bl.y, pt_br.y) - std::min(pt_tl.y, pt_tr.y));

    std::cout << "Calibration des 4 corners : TL(" << pt_tl.x << "," << pt_tl.y << ") TR(" << pt_tr.x << "," << pt_tr.y
              << ") BL(" << pt_bl.x << "," << pt_bl.y << ") BR(" << pt_br.x << "," << pt_br.y << ")" << std::endl;

    cap.set(cv::CAP_PROP_POS_FRAMES, 0);
    std::ofstream fichier_coords("coordonnees_joueurs.csv");
    fichier_coords << "trame,id,x,y\n";

    const int MARGE = 35;
    std::vector<JoueurTracke> joueurs;
    int prochain_id = 1, num_trame = 0;

    GestionSouris souris;
    souris.x = -1;
    souris.y = -1;
    souris.id_selectionne = -1;
    souris.nouveau_clic = false;

    const std::string nom_fenetre = "Tracking Vue de Haut - Multi-Joueurs";
    cv::namedWindow(nom_fenetre, cv::WINDOW_NORMAL);
    cv::resizeWindow(nom_fenetre, 1280, 720);
    cv::setMouseCallback(nom_fenetre, onMouse, &souris);

    // Cadence native de la vidéo pour asservir la vitesse d'affichage en temps réel (1.0x)
    double fps = cap.get(cv::CAP_PROP_FPS);
    if (fps <= 0.0 || std::isnan(fps)) fps = 30.0;
    double delai_nominal_ms = 1000.0 / fps; // ~33.36 ms pour 29.97 FPS

    cv::Mat frame, zone_terrain, non_pelouse, rendu;

    // 2. Boucle de suivi vidéo temps réel
    while (cap.read(frame)) {
        if (frame.empty()) break;
        int64_t t_debut = cv::getTickCount();
        num_trame++;

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

        // Détection candidats compacts (ratio < 2.5) et pleins (densité >= 0.22, rejetant les lignes creuses comme le rond central)
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(non_pelouse, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

        std::vector<cv::Point> candidats_bruts;
        std::vector<cv::Rect> boites_brutes;
        for (size_t i = 0; i < contours.size(); ++i) {
            cv::Rect b = cv::boundingRect(contours[i]);
            double aire = cv::contourArea(contours[i]);
            double densite = aire / (static_cast<double>(b.width) * b.height);
            bool compact = (b.width < 2.5 * b.height) && (b.height < 2.5 * b.width);

            if (b.width >= 12 && b.width <= 75 && b.height >= 12 && b.height <= 75 &&
                aire >= 80.0 && compact && densite >= 0.22) {
                candidats_bruts.push_back(cv::Point(b.x + b.width / 2, b.y + b.height / 2));
                boites_brutes.push_back(b);
            }
        }

        // Dédoublonnage spatial (< 15 px) et extraction de la signature couleur
        std::vector<cv::Point> candidats;
        std::vector<cv::Vec3f> couleurs_candidats;
        for (size_t i = 0; i < candidats_bruts.size(); ++i) {
            bool doublon = false;
            for (size_t k = 0; k < candidats.size(); ++k) {
                float dist = std::hypot(candidats_bruts[i].x - candidats[k].x,
                                        candidats_bruts[i].y - candidats[k].y);
                if (dist < 15.0f) { doublon = true; break; }
            }
            if (!doublon) {
                candidats.push_back(candidats_bruts[i]);
                couleurs_candidats.push_back(extraireCouleurCandidat(zone_terrain, non_pelouse, boites_brutes[i]));
            }
        }

        if (joueurs.empty()) {
            for (size_t j = 0; j < candidats.size(); ++j) {
                float gx = static_cast<float>(candidats[j].x + terrain.x);
                float gy = static_cast<float>(candidats[j].y + terrain.y);
                joueurs.push_back(creerNouveauJoueur(prochain_id++, gx, gy, couleurs_candidats[j]));
            }
        } else {
            // Modèle cinétique prédictif (Kalman 1D / alpha-beta)
            std::vector<cv::Point2f> pos_predites(joueurs.size());
            for (size_t i = 0; i < joueurs.size(); ++i) {
                if (joueurs[i].frames_sans_mesure > 0) {
                    joueurs[i].vitesse.x *= 0.85f;
                    joueurs[i].vitesse.y *= 0.85f;
                }
                pos_predites[i].x = joueurs[i].corps.x + joueurs[i].vitesse.x;
                pos_predites[i].y = joueurs[i].corps.y + joueurs[i].vitesse.y;
            }

            // Association optimale par Algorithme Hongrois spatio-chromatique (gating 75)
            std::vector<int> assignations(joueurs.size(), -1);
            if (!candidats.empty()) {
                std::vector<std::vector<float>> couts(joueurs.size(), std::vector<float>(candidats.size()));
                for (size_t i = 0; i < joueurs.size(); ++i) {
                    for (size_t j = 0; j < candidats.size(); ++j) {
                        float gx = static_cast<float>(candidats[j].x + terrain.x);
                        float gy = static_cast<float>(candidats[j].y + terrain.y);
                        float d_spatial = std::hypot(pos_predites[i].x - gx, pos_predites[i].y - gy);

                        cv::Vec3f dc = joueurs[i].couleur - couleurs_candidats[j];
                        float d_couleur = std::sqrt(dc[0] * dc[0] + dc[1] * dc[1] + dc[2] * dc[2]);

                        couts[i][j] = d_spatial + 0.6f * d_couleur;
                    }
                }
                assignations = tracking::resoudreHongrois(couts, 75.0f);
            }

            std::vector<bool> candidat_utilise(candidats.size(), false);
            for (size_t i = 0; i < joueurs.size(); ++i) {
                int c_idx = assignations[i];
                if (c_idx != -1) {
                    candidat_utilise[c_idx] = true;
                    float gx = static_cast<float>(candidats[c_idx].x + terrain.x);
                    float gy = static_cast<float>(candidats[c_idx].y + terrain.y);

                    cv::Point2f v_inst(gx - joueurs[i].corps.x, gy - joueurs[i].corps.y);
                    if (std::hypot(v_inst.x, v_inst.y) < 25.0f) {
                        joueurs[i].vitesse.x = 0.5f * joueurs[i].vitesse.x + 0.5f * v_inst.x;
                        joueurs[i].vitesse.y = 0.5f * joueurs[i].vitesse.y + 0.5f * v_inst.y;
                    }
                    joueurs[i].couleur = 0.90f * joueurs[i].couleur + 0.10f * couleurs_candidats[c_idx];
                    deplacerJoueur(joueurs[i], gx, gy);
                    joueurs[i].frames_sans_mesure = 0;
                } else {
                    // Fallback local sur les lignes blanches (fenêtre 28x28) : évite le lag et la perte d'ID
                    int lx = static_cast<int>(pos_predites[i].x - terrain.x);
                    int ly = static_cast<int>(pos_predites[i].y - terrain.y);
                    int x0 = std::max(0, lx - 14), x1 = std::min(zone_terrain.cols, lx + 14);
                    int y0 = std::max(0, ly - 14), y1 = std::min(zone_terrain.rows, ly + 14);

                    float sum_x = 0.0f, sum_y = 0.0f;
                    int n_pix = 0;
                    for (int py = y0; py < y1; ++py) {
                        const uchar* m = non_pelouse.ptr<uchar>(py);
                        for (int px = x0; px < x1; ++px) {
                            if (m[px] > 0) { sum_x += px; sum_y += py; n_pix++; }
                        }
                    }
                    if (n_pix >= 90) {
                        float bx = sum_x / n_pix + terrain.x, by = sum_y / n_pix + terrain.y;
                        if (std::hypot(bx - pos_predites[i].x, by - pos_predites[i].y) < 18.0f) {
                            cv::Point2f v_inst(bx - joueurs[i].corps.x, by - joueurs[i].corps.y);
                            if (std::hypot(v_inst.x, v_inst.y) < 20.0f) {
                                joueurs[i].vitesse.x = 0.6f * joueurs[i].vitesse.x + 0.4f * v_inst.x;
                                joueurs[i].vitesse.y = 0.6f * joueurs[i].vitesse.y + 0.4f * v_inst.y;
                            }
                            deplacerJoueur(joueurs[i], bx, by);
                            joueurs[i].frames_sans_mesure = 0;
                            continue;
                        }
                    }
                    joueurs[i].frames_sans_mesure++;
                    deplacerJoueur(joueurs[i], pos_predites[i].x, pos_predites[i].y);
                }
            }

            // 5. Cohésion de duel (< 35 px) : le joueur occulté colle au centre du duel jusqu'à la séparation
            for (size_t i = 0; i < joueurs.size(); ++i) {
                if (joueurs[i].frames_sans_mesure > 0) {
                    for (size_t k = 0; k < joueurs.size(); ++k) {
                        if (k != i && joueurs[k].frames_sans_mesure == 0) {
                            float d = std::hypot(joueurs[i].corps.x - joueurs[k].corps.x,
                                                 joueurs[i].corps.y - joueurs[k].corps.y);
                            if (d < 35.0f) {
                                joueurs[i].vitesse = joueurs[k].vitesse;
                                deplacerJoueur(joueurs[i], joueurs[k].corps.x, joueurs[k].corps.y);
                                break;
                            }
                        }
                    }
                }
            }

            // 6. Ré-acquisition uniquement si l'effectif actuel est incomplet (< 24 joueurs)
            for (size_t j = 0; j < candidats.size(); ++j) {
                if (!candidat_utilise[j] && joueurs.size() < 24) {
                    float gx = static_cast<float>(candidats[j].x + terrain.x);
                    float gy = static_cast<float>(candidats[j].y + terrain.y);
                    bool deja = false;
                    for (size_t k = 0; k < joueurs.size(); ++k) {
                        if (std::hypot(gx - joueurs[k].corps.x, gy - joueurs[k].corps.y) < 35.0f) {
                            deja = true; break;
                        }
                    }
                    if (!deja) {
                        joueurs.push_back(creerNouveauJoueur(prochain_id++, gx, gy, couleurs_candidats[j]));
                    }
                }
            }
        }

        // 7. Nettoyage des pistes perdues après disparition prolongée (> 90 trames = 1.5s)
        for (size_t i = 0; i < joueurs.size(); ) {
            if (joueurs[i].frames_sans_mesure > 90) {
                if (joueurs[i].id == souris.id_selectionne) souris.id_selectionne = -1;
                joueurs.erase(joueurs.begin() + i);
            } else {
                ++i;
            }
        }

        transmettreCoordonnees(fichier_coords, num_trame, joueurs);

        if (souris.nouveau_clic) {
            double ex_m = static_cast<double>(frame.cols) / 1280.0;
            double ey_m = static_cast<double>(frame.rows) / 720.0;
            int cx = static_cast<int>(souris.x * ex_m), cy = static_cast<int>(souris.y * ey_m);
            double dist_min = 45.0;
            for (size_t i = 0; i < joueurs.size(); ++i) {
                double d = std::hypot(joueurs[i].corps.x - cx, joueurs[i].corps.y - cy);
                if (d < dist_min) {
                    dist_min = d;
                    souris.id_selectionne = joueurs[i].id;
                }
            }
            souris.nouveau_clic = false;
        }

        // Rendu visuel (boîtes, corps, identifiants, déport vertical en cas de duel serré)
        for (size_t i = 0; i < joueurs.size(); ++i) {
            bool sel = (joueurs[i].id == souris.id_selectionne);
            cv::Scalar col = sel ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 255, 255);
            cv::rectangle(frame, joueurs[i].boite, col, sel ? 3 : 2);
            cv::circle(frame, cv::Point(static_cast<int>(joueurs[i].corps.x), static_cast<int>(joueurs[i].corps.y)), 3, cv::Scalar(0, 0, 255), -1);

            bool duel_serre = false;
            for (size_t k = 0; k < joueurs.size(); ++k) {
                if (k != i && std::hypot(joueurs[i].corps.x - joueurs[k].corps.x, joueurs[i].corps.y - joueurs[k].corps.y) < 15.0f) {
                    duel_serre = true; break;
                }
            }
            int y_txt = (duel_serre && joueurs[i].frames_sans_mesure > 0)
                        ? (joueurs[i].boite.y + joueurs[i].boite.height + 16)
                        : (joueurs[i].boite.y - 6);

            cv::putText(frame, std::to_string(joueurs[i].id), cv::Point(joueurs[i].boite.x, y_txt),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, col, 2);
            if (sel) {
                cv::circle(frame, cv::Point(static_cast<int>(joueurs[i].corps.x), static_cast<int>(joueurs[i].corps.y)), 22, cv::Scalar(0, 255, 0), 2);
            }
        }

        cv::line(frame, pt_tl, pt_tr, cv::Scalar(0, 255, 0), 2);
        cv::line(frame, pt_tr, pt_br, cv::Scalar(0, 255, 0), 2);
        cv::line(frame, pt_br, pt_bl, cv::Scalar(0, 255, 0), 2);
        cv::line(frame, pt_bl, pt_tl, cv::Scalar(0, 255, 0), 2);

        cv::resize(frame, rendu, cv::Size(1280, 720));

        if (souris.id_selectionne != -1) {
            for (size_t i = 0; i < joueurs.size(); ++i) {
                if (joueurs[i].id == souris.id_selectionne) {
                    cv::Rect pan(rendu.cols - 250, 15, 235, 55);
                    cv::Mat overlay = rendu(pan).clone();
                    overlay.setTo(cv::Scalar(20, 20, 20));
                    cv::addWeighted(overlay, 0.75, rendu(pan), 0.25, 0, rendu(pan));
                    cv::rectangle(rendu, pan, cv::Scalar(0, 255, 0), 2);

                    cv::putText(rendu, "Joueur ID : " + std::to_string(joueurs[i].id),
                                cv::Point(pan.x + 10, pan.y + 22), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);
                    cv::putText(rendu, "Coord : (" + std::to_string(static_cast<int>(joueurs[i].corps.x)) + ", " +
                                                     std::to_string(static_cast<int>(joueurs[i].corps.y)) + ")",
                                cv::Point(pan.x + 10, pan.y + 44), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
                    break;
                }
            }
        } else {
            cv::putText(rendu, "Cliquez sur un joueur pour le selectionner",
                        cv::Point(rendu.cols - 390, 25), cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(180, 180, 180), 1);
        }

        cv::imshow(nom_fenetre, rendu);

        // Régulation dynamique de la cadence pour vitesse réelle 1.0x (29.97 FPS natif)
        int64_t t_fin = cv::getTickCount();
        double duree_calcul_ms = (t_fin - t_debut) * 1000.0 / cv::getTickFrequency();
        int delai_attente = static_cast<int>(delai_nominal_ms - duree_calcul_ms);
        if (delai_attente < 1) delai_attente = 1;

        char c = static_cast<char>(cv::waitKey(delai_attente));
        if (c == 'q' || c == 'Q' || c == 27) break;
        if (c == ' ') {
            // Pause interactive avec la barre d'espace
            while (true) {
                char touche_pause = static_cast<char>(cv::waitKey(30));
                if (touche_pause == ' ') break;
                if (touche_pause == 'q' || touche_pause == 'Q' || touche_pause == 27) {
                    c = touche_pause;
                    break;
                }
            }
            if (c == 'q' || c == 'Q' || c == 27) break;
        }
    }

    fichier_coords.close();
    cap.release();
    cv::destroyAllWindows();
    return 0;
}
