#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <map>
#include <deque>
#include <iomanip>
#include <sstream>
#include <limits>
#include <array>
#include <opencv2/opencv.hpp>
#include "algorithme_hongrois.hpp"

// Structure représentant un joueur suivi en vue de dessus
struct JoueurTracke {
    int id;
    cv::Rect boite;                // Boîte englobante 4K (24x24 normalisée)
    cv::Point2f corps;             // Centre précis du corps (x, y)
    cv::Point2f vitesse;           // Vitesse estimée (dx, dy en pixels/trame)
    cv::Vec3f couleur;             // Signature chromatique moyenne (B, G, R)
    int frames_sans_mesure;        // Nombre de trames consécutives sans détection directe
    bool mesure_directe = false;   // Vrai uniquement si assigne a un candidat detecte
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
    j.mesure_directe = true;
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

// Estimation de vitesse a partir de positions dans le repere terrain (metres).
// On estime la pente d'une regression lineaire sur ~0.8 seconde de mesures.
// Cela limite les fluctuations dues a la detection et a la projection.
struct EchantillonPosition {
    int trame;
    cv::Point2f position;
};

struct HistoriqueVitesse {
    std::deque<EchantillonPosition> mesures;
};

// Renvoie NaN tant que les mesures ne sont pas suffisantes / fiables.
double calculerVitesseKmh(HistoriqueVitesse& hist,
                          int numeroTrame,
                          const cv::Point2f& p,
                          double fps,
                          bool mesureDirecte) {
    const double invalide = std::numeric_limits<double>::quiet_NaN();
    const int fenetre = std::max(3, cvRound(0.8 * fps));
    const int coupure = std::max(2, cvRound(0.15 * fps));
    constexpr double VITESSE_LIMITE_M_S = 12.5; // 45 km/h : rejet des sauts suspects

    // Les positions predites ou issues d'un fallback ne servent pas de mesures de vitesse.
    if (!mesureDirecte || !std::isfinite(p.x) || !std::isfinite(p.y))
        return invalide;

    if (!hist.mesures.empty()) {
        const EchantillonPosition& dernier = hist.mesures.back();
        const int deltaTrames = numeroTrame - dernier.trame;
        if (deltaTrames <= 0 || deltaTrames > coupure) {
            hist.mesures.clear(); // interruption du suivi : nouvelle fenetre
        } else {
            const double dt = static_cast<double>(deltaTrames) / fps;
            const double saut_m_s = cv::norm(p - dernier.position) / dt;
            if (saut_m_s > VITESSE_LIMITE_M_S) {
                // Une erreur d'ID ou de position peut creer une vitesse enorme.
                hist.mesures.clear();
                hist.mesures.push_back({numeroTrame, p});
                return invalide;
            }
        }
    }

    hist.mesures.push_back({numeroTrame, p});
    while (!hist.mesures.empty() &&
           numeroTrame - hist.mesures.front().trame > fenetre) {
        hist.mesures.pop_front();
    }
    if (hist.mesures.size() < 5) return invalide;
    const double duree = static_cast<double>(
        hist.mesures.back().trame - hist.mesures.front().trame) / fps;
    if (duree < 0.40) return invalide;

    // Regression lineaire du mouvement X(t), Y(t).
    // Le temps est mesure en secondes, dans l'horloge de la VIDEO.
    const double n = static_cast<double>(hist.mesures.size());
    double sommeT = 0, sommeT2 = 0, sommeX = 0, sommeY = 0;
    double sommeTX = 0, sommeTY = 0;
    const int debut = hist.mesures.front().trame;
    for (const auto& e : hist.mesures) {
        const double t = static_cast<double>(e.trame - debut) / fps;
        sommeT += t;
        sommeT2 += t * t;
        sommeX += e.position.x;
        sommeY += e.position.y;
        sommeTX += t * e.position.x;
        sommeTY += t * e.position.y;
    }
    const double denom = n * sommeT2 - sommeT * sommeT;
    if (denom <= 1e-10) return invalide;

    const double vx = (n * sommeTX - sommeT * sommeX) / denom;
    const double vy = (n * sommeTY - sommeT * sommeY) / denom;
    const double vitesse = std::hypot(vx, vy); // m/s
    if (!std::isfinite(vitesse) || vitesse > VITESSE_LIMITE_M_S)
        return invalide;
    return 3.6 * vitesse; // km/h
}

std::string texteVitesse(double kmh) {
    if (!std::isfinite(kmh)) return "-- km/h";
    std::ostringstream os;
    os << std::fixed << std::setprecision(1) << kmh << " km/h";
    return os.str();
}

// ============================================================================
// Classification experimentale des EQUIPES a partir de la couleur du maillot.
// Cette version utilise les images BGR BRUTES. Elle n'implemente pas la
// normalisation FPGA des collegues (qui est developpee sur une autre branche).
// A/B sont des groupes de couleur arbitraires, PAS des numeros de maillot.
// ============================================================================

// BGR OpenCV : A = bleu, B = rouge, -1 = gris (inconnu).
cv::Scalar couleurEquipe(int equipe) {
    if (equipe == 0) return cv::Scalar(255, 90, 30);
    if (equipe == 1) return cv::Scalar(35, 50, 245);
    return cv::Scalar(165, 165, 165);
}

std::string nomEquipe(int equipe) {
    return equipe == 0 ? "A" : (equipe == 1 ? "B" : "?");
}

// Extraction d'une signature Lab du centre SUPERIEUR de la bounding box.
// On utilise les pixels non-pelouse afin de limiter la contamination du gazon.
// La mediane par canal est moins sensible aux lignes blanches/artefacts que
// la moyenne de toute la bounding box (mais ce n'est pas un segmentateur parfait).
bool extraireMaillotLab(const cv::Mat& imageBgr, const cv::Mat& masqueNonPelouse,
                        const cv::Rect& terrain, const cv::Rect& boite,
                        cv::Vec3f& signature) {
    const cv::Rect cadre(0, 0, imageBgr.cols, imageBgr.rows);
    const cv::Rect zone = boite & cadre;
    if (zone.width < 8 || zone.height < 8) return false;

    // Zone centrale du haut du corps ; a ajuster si la video montre plutot
    // les jambes dans cette portion du joueur.
    const cv::Rect torse(
        zone.x + zone.width / 4,
        zone.y + zone.height / 5,
        std::max(1, zone.width / 2),
        std::max(1, zone.height * 3 / 5));
    const cv::Rect roi = torse & cadre;
    if (roi.width <= 0 || roi.height <= 0) return false;

    cv::Mat lab;
    cv::cvtColor(imageBgr(roi), lab, cv::COLOR_BGR2Lab);
    std::array<std::vector<uchar>, 3> valeurs;
    for (int y = 0; y < roi.height; ++y) {
        const cv::Vec3b* ligneLab = lab.ptr<cv::Vec3b>(y);
        for (int x = 0; x < roi.width; ++x) {
            const int xImage = roi.x + x;
            const int yImage = roi.y + y;
            const int localX = xImage - terrain.x;
            const int localY = yImage - terrain.y;
            if (localX < 0 || localY < 0 ||
                localX >= masqueNonPelouse.cols || localY >= masqueNonPelouse.rows)
                continue;
            if (masqueNonPelouse.at<uchar>(localY, localX) == 0) continue;
            for (int c = 0; c < 3; ++c) valeurs[c].push_back(ligneLab[x][c]);
        }
    }
    if (valeurs[0].size() < 8) return false;
    for (int c = 0; c < 3; ++c) {
        auto& v = valeurs[c];
        std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
        signature[c] = static_cast<float>(v[v.size() / 2]);
    }
    return true;
}

struct ProfilMaillot {
    cv::Vec3f lab = cv::Vec3f(0, 0, 0);
    int observations = 0;
    int votes[2] = {0, 0};
    int equipe = -1;
};

class ClassificateurEquipes {
    std::map<int, ProfilMaillot> profils_;
    cv::Vec3f centres_[2] = {cv::Vec3f(0,0,0), cv::Vec3f(0,0,0)};
    bool pret_ = false;

    static float distanceLab(const cv::Vec3f& a, const cv::Vec3f& b) {
        // Distance euclidienne approximative dans l'espace Lab 8 bits d'OpenCV.
        // La luminance compte un peu moins que les deux axes chromatiques.
        const cv::Vec3f d = a - b;
        return std::sqrt(0.45f*d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    }

public:
    bool pret() const { return pret_; }
    int equipe(int id) const {
        auto it = profils_.find(id);
        return it == profils_.end() ? -1 : it->second.equipe;
    }
    void oublier(int id) { profils_.erase(id); }

    void actualiser(const cv::Mat& image, const cv::Mat& masqueNonPelouse,
                    const cv::Rect& terrain, const std::vector<JoueurTracke>& joueurs,
                    int numeroTrame) {
        // 1) Construire une moyenne temporelle de maillot PAR ID.
        for (const auto& joueur : joueurs) {
            if (!joueur.mesure_directe) continue;
            cv::Vec3f observation;
            if (!extraireMaillotLab(image, masqueNonPelouse,
                                   terrain, joueur.boite, observation)) continue;
            auto& p = profils_[joueur.id];
            if (p.observations == 0) p.lab = observation;
            else {
                const float alpha = p.observations < 12
                    ? 1.0f / static_cast<float>(p.observations + 1)
                    : 0.10f;
                p.lab = (1.0f - alpha) * p.lab + alpha * observation;
            }
            ++p.observations;
        }

        // 2) Apprendre UNE FOIS deux couleurs dominantes sans annotation manuelle.
        // Evite de declencher sur les premieres detections instables.
        if (!pret_ && numeroTrame >= 25 && numeroTrame % 5 == 0) {
            std::vector<cv::Vec3f> couleurs;
            for (const auto& j : joueurs) {
                auto it = profils_.find(j.id);
                if (it != profils_.end() && it->second.observations >= 8)
                    couleurs.push_back(it->second.lab);
            }
            if (couleurs.size() >= 8) {
                cv::Mat echantillons(static_cast<int>(couleurs.size()), 3, CV_32F);
                for (int i = 0; i < echantillons.rows; ++i)
                    for (int c = 0; c < 3; ++c)
                        echantillons.at<float>(i, c) = couleurs[i][c];
                cv::Mat labels, centres;
                cv::kmeans(echantillons, 2, labels,
                           cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 50, 0.15),
                           8, cv::KMEANS_PP_CENTERS, centres);
                cv::Vec3f a(centres.at<float>(0,0), centres.at<float>(0,1), centres.at<float>(0,2));
                cv::Vec3f b(centres.at<float>(1,0), centres.at<float>(1,1), centres.at<float>(1,2));
                int nombres[2] = {0, 0};
                for (int k = 0; k < labels.rows; ++k)
                    ++nombres[labels.at<int>(k,0)];
                if (nombres[0] >= 3 && nombres[1] >= 3 && distanceLab(a,b) >= 18.0f) {
                    centres_[0] = a;
                    centres_[1] = b;
                    pret_ = true;
                    std::cout << "[EQUIPES] Deux groupes chromatiques appris : "
                              << nombres[0] << " / " << nombres[1]
                              << " joueurs. A=bleu, B=rouge, ?=inconnu.\n";
                }
            }
        }
        if (!pret_) return;

        // 3) Stabiliser l'equipe par vote au fil des frames.
        // En cas d'ambiguite, de gardien ou d'arbitre, on ne force pas A/B.
        for (const auto& j : joueurs) {
            if (!j.mesure_directe) continue;
            auto it = profils_.find(j.id);
            if (it == profils_.end() || it->second.observations < 5) continue;
            auto& p = it->second;
            const float d0 = distanceLab(p.lab, centres_[0]);
            const float d1 = distanceLab(p.lab, centres_[1]);
            const float petit = std::min(d0, d1);
            const float ecart = std::abs(d0 - d1);
            if (petit > 42.0f || ecart < 9.0f) continue;  // inconnue/ambigu
            const int choix = d0 < d1 ? 0 : 1;
            ++p.votes[choix];
            if (p.votes[0] + p.votes[1] > 45) {
                p.votes[0] = (p.votes[0] + 1) / 2;
                p.votes[1] = (p.votes[1] + 1) / 2;
            }
            if (p.votes[choix] >= 6 && p.votes[choix] >= p.votes[1-choix] + 4)
                p.equipe = choix;
        }
    }
};

// Dessin du terrain en metres ; meme repere que la matrice H.
cv::Mat creerMiniCarte(double longueurTerrain, double largeurTerrain) {
    const double E = 8.0;   // 8 pixels de carte par metre
    const int M = 30;
    auto pt = [&](double X, double Y) {
        return cv::Point(M + cvRound(E * X), M + cvRound(E * Y));
    };
    cv::Mat carte(M * 2 + cvRound(E * largeurTerrain),
                  M * 2 + cvRound(E * longueurTerrain),
                  CV_8UC3, cv::Scalar(50, 115, 50));
    const cv::Scalar blanc(255, 255, 255);
    const double milieuX = longueurTerrain / 2.0;
    const double milieuY = largeurTerrain / 2.0;
    cv::rectangle(carte, pt(0, 0), pt(longueurTerrain, largeurTerrain), blanc, 2);
    cv::line(carte, pt(milieuX, 0), pt(milieuX, largeurTerrain), blanc, 2);
    cv::circle(carte, pt(milieuX, milieuY), cvRound(E * 9.15), blanc, 2);
    if (longueurTerrain > 33.0 && largeurTerrain > 40.32) {
        cv::rectangle(carte, pt(0, milieuY - 20.16),
                      pt(16.5, milieuY + 20.16), blanc, 2);
        cv::rectangle(carte, pt(longueurTerrain - 16.5, milieuY - 20.16),
                      pt(longueurTerrain, milieuY + 20.16), blanc, 2);
        cv::circle(carte, pt(11.0, milieuY), 3, blanc, -1);
        cv::circle(carte, pt(longueurTerrain - 11.0, milieuY), 3, blanc, -1);
    }
    return carte;
}

int main(int argc, char* argv[]) {
    std::string chemin = (argc > 1) ? argv[1] : "data/videos/D_20220220_1_1530_1560.mp4";
    std::string cheminCalibration = (argc > 2) ? argv[2] : "calibration_vue_haut.yml";
    cv::VideoCapture cap(chemin);
    if (!cap.isOpened()) {
        std::cerr << "Erreur ouverture vidéo : " << chemin << std::endl;
        return 1;
    }

    // ===== INTEGRATION DE L'HOMOGRAPHIE (VUE DE DESSUS) =====
    // La matrice a ete calculee UNE fois par calibration_vue_haut.cpp.
    cv::FileStorage fs(cheminCalibration, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        std::cerr << "Impossible d'ouvrir la calibration : " << cheminCalibration << "\n";
        std::cerr << "Lancer d'abord calibration_vue_haut.exe sur la meme video.\n";
        return 1;
    }
    cv::Mat H;
    fs["H_image_vers_terrain"] >> H;
    int largeurCalibration = 0, hauteurCalibration = 0;
    double longueurTerrain = 105.0, largeurTerrain = 68.0;
    fs["largeur_image"] >> largeurCalibration;
    fs["hauteur_image"] >> hauteurCalibration;
    if (!fs["longueur_terrain"].empty()) fs["longueur_terrain"] >> longueurTerrain;
    if (!fs["largeur_terrain"].empty()) fs["largeur_terrain"] >> largeurTerrain;
    fs.release();
    if (H.empty() || H.rows != 3 || H.cols != 3 ||
        longueurTerrain <= 0 || largeurTerrain <= 0) {
        std::cerr << "Calibration invalide : H doit etre une matrice 3x3.\n";
        return 1;
    }
    H.convertTo(H, CV_64F);
    std::cout << "[HOMOGRAPHIE] Matrice chargee : " << cheminCalibration << "\n";
    // ============================================================

    // 1. Calibrage automatique du terrain sur vignette 720p (seuillage direct FPGA)
    cv::Mat premiere_trame, small;
    if (!cap.read(premiere_trame) || premiere_trame.empty()) return 1;
    if (largeurCalibration > 0 && hauteurCalibration > 0 &&
        (premiere_trame.cols != largeurCalibration ||
         premiere_trame.rows != hauteurCalibration)) {
        std::cerr << "Erreur : dimensions de la video ("
                  << premiere_trame.cols << "x" << premiere_trame.rows
                  << ") differentes de la calibration ("
                  << largeurCalibration << "x" << hauteurCalibration << ").\n";
        return 1;
    }

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
    std::ofstream fichier_metres("coordonnees_joueurs_equipes.csv");
    if (!fichier_metres.is_open()) {
        std::cerr << "Impossible de creer coordonnees_joueurs_equipes.csv\n";
        return 1;
    }
    fichier_metres << "trame,id,u_px,v_px,X_m,Y_m,predite,mesure_directe,vitesse_kmh,equipe\n";


    const int MARGE = 35;
    std::vector<JoueurTracke> joueurs;
    std::map<int, HistoriqueVitesse> historiques;
    ClassificateurEquipes classificateur;
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
    cv::namedWindow("Mini-carte joueurs (metres)", cv::WINDOW_NORMAL);
    cv::resizeWindow("Mini-carte joueurs (metres)", 950, 630);


    // Les FPS de la VIDEO servent au calcul de vitesse (m/s),
    // independamment des ralentissements eventuels de l'affichage.
    // Cadence native de la vidéo pour asservir la vitesse d'affichage en temps réel (1.0x)
    double fps = cap.get(cv::CAP_PROP_FPS);
    // Sécurité cross-platform : sous Windows/Linux, OpenCV renvoie parfois 1000 ou 0
    if (fps <= 5.0 || fps > 120.0 || std::isnan(fps)) {
        fps = 30.0;
    }
    double delai_nominal_ms = 1000.0 / fps; // ~33.36 ms pour 30 FPS
    std::cout << "[INFO] Cadence video regulee : " << fps << " FPS (" 
              << static_cast<int>(delai_nominal_ms) << " ms/trame)" << std::endl;

    cv::Mat frame, zone_terrain, non_pelouse, rendu;

    // 2. Boucle de suivi vidéo temps réel
    

    double somme_temps_ms = 0.0;
    while (true) {
        int64_t t_debut = cv::getTickCount();
        if (!cap.read(frame) || frame.empty()) break;
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
                    joueurs[i].mesure_directe = true;
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
                            joueurs[i].mesure_directe = false;
                            continue;
                        }
                    }
                    joueurs[i].frames_sans_mesure++;
                    joueurs[i].mesure_directe = false;
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
                historiques.erase(joueurs[i].id);
                classificateur.oublier(joueurs[i].id);
                joueurs.erase(joueurs.begin() + i);
            } else {
                ++i;
            }
        }

        transmettreCoordonnees(fichier_coords, num_trame, joueurs);
        classificateur.actualiser(frame, non_pelouse, terrain, joueurs, num_trame);

        std::map<int, double> vitessesKmh;
        std::map<int, cv::Point2f> positionsTerrain;
        // ===== PROJECTION EN TEMPS REEL DES IDS VERS LE TERRAIN =====
        // Le tracker travaille en pixels 4K. On applique H a ces points,
        // sans changer ni le suivi ni l'association des identifiants.
        cv::Mat miniCarte = creerMiniCarte(longueurTerrain, largeurTerrain);
        if (!joueurs.empty()) {
            std::vector<cv::Point2f> pixels, metres;
            pixels.reserve(joueurs.size());
            for (const auto& j : joueurs) pixels.push_back(j.corps);
            cv::perspectiveTransform(pixels, metres, H);

            const double echelle = 8.0;
            const int marge = 30;
            for (size_t i = 0; i < joueurs.size(); ++i) {
                const cv::Point2f& p = metres[i];
                if (!std::isfinite(p.x) || !std::isfinite(p.y)) continue;
                const JoueurTracke& j = joueurs[i];
                const bool dansTerrain = p.x >= 0 && p.x <= longueurTerrain &&
                                         p.y >= 0 && p.y <= largeurTerrain;
                double kmh = std::numeric_limits<double>::quiet_NaN();
                if (dansTerrain) {
                    positionsTerrain[j.id] = p;
                    kmh = calculerVitesseKmh(
                        historiques[j.id], num_trame, p, fps, j.mesure_directe);
                }
                vitessesKmh[j.id] = kmh;
                // predite=1 uniquement si la piste n'a pas eu de mesure/fallback.
                int predite = j.frames_sans_mesure > 0 ? 1 : 0;
                fichier_metres << num_trame << "," << j.id << ","
                              << j.corps.x << "," << j.corps.y << ","
                              << p.x << "," << p.y << "," << predite << ","
                              << (j.mesure_directe ? 1 : 0) << ",";
                if (std::isfinite(kmh)) fichier_metres << std::fixed << std::setprecision(2) << kmh;
                fichier_metres << "," << classificateur.equipe(j.id) << "\n";

                if (dansTerrain) {
                    cv::Point marqueur(marge + cvRound(echelle * p.x),
                                      marge + cvRound(echelle * p.y));
                    // BGR : bleu pour A, rouge pour B, gris pour un joueur non classe.
                    cv::Scalar couleur = couleurEquipe(classificateur.equipe(j.id));
                    cv::circle(miniCarte, marqueur, 6, couleur, -1);
                    if (predite) cv::circle(miniCarte, marqueur, 8, cv::Scalar(0, 165, 255), 1);
                    std::string libelle = std::to_string(j.id) + " [" + nomEquipe(classificateur.equipe(j.id)) + "] : " + texteVitesse(kmh);
                    cv::putText(miniCarte, libelle, marqueur + cv::Point(7, -7),
                                cv::FONT_HERSHEY_SIMPLEX, 0.43, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
                }
            }
        }
        cv::putText(miniCarte,
                    classificateur.pret() ? "A bleu | B rouge | ? inconnu" : "Apprentissage equipes en cours...",
                    cv::Point(30, 21), cv::FONT_HERSHEY_SIMPLEX,
                    0.54, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
        cv::imshow("Mini-carte joueurs (metres)", miniCarte);
        // ============================================================



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
            cv::Scalar col = sel ? cv::Scalar(0, 255, 0) : couleurEquipe(classificateur.equipe(joueurs[i].id));
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

            double kmhJoueur = std::numeric_limits<double>::quiet_NaN();
            auto itV = vitessesKmh.find(joueurs[i].id);
            if (itV != vitessesKmh.end()) kmhJoueur = itV->second;
            std::string etiquette = std::to_string(joueurs[i].id) + " [" + nomEquipe(classificateur.equipe(joueurs[i].id)) + "] | " + texteVitesse(kmhJoueur);
            cv::putText(frame, etiquette, cv::Point(joueurs[i].boite.x, y_txt),
                        cv::FONT_HERSHEY_SIMPLEX, 0.95, col, 2, cv::LINE_AA);
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
                    cv::Rect pan(rendu.cols - 310, 15, 295, 106);
                    cv::Mat overlay = rendu(pan).clone();
                    overlay.setTo(cv::Scalar(20, 20, 20));
                    cv::addWeighted(overlay, 0.78, rendu(pan), 0.22, 0, rendu(pan));
                    cv::rectangle(rendu, pan, cv::Scalar(0, 255, 0), 2);

                    const int id = joueurs[i].id;
                    auto itPos = positionsTerrain.find(id);
                    auto itVit = vitessesKmh.find(id);
                    const double vitesse = itVit != vitessesKmh.end()
                        ? itVit->second : std::numeric_limits<double>::quiet_NaN();
                    cv::putText(rendu, "Joueur ID : " + std::to_string(id) + " / Eq " + nomEquipe(classificateur.equipe(id)),
                                cv::Point(pan.x + 10, pan.y + 23), cv::FONT_HERSHEY_SIMPLEX,
                                0.63, cv::Scalar(0, 255, 0), 2);
                    if (itPos != positionsTerrain.end()) {
                        std::ostringstream os;
                        os << std::fixed << std::setprecision(1)
                           << "X=" << itPos->second.x << "m  Y=" << itPos->second.y << "m";
                        cv::putText(rendu, os.str(), cv::Point(pan.x + 10, pan.y + 49),
                                    cv::FONT_HERSHEY_SIMPLEX, 0.49, cv::Scalar(255,255,255), 1);
                    }
                    cv::putText(rendu, "Vitesse : " + texteVitesse(vitesse),
                                cv::Point(pan.x + 10, pan.y + 77), cv::FONT_HERSHEY_SIMPLEX,
                                0.58, cv::Scalar(255,255,255), 2);
                    cv::putText(rendu, "Estimation ~0.8s (si mesure valide)",
                                cv::Point(pan.x + 10, pan.y + 97), cv::FONT_HERSHEY_SIMPLEX,
                                0.34, cv::Scalar(200,200,200), 1);
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

        somme_temps_ms += duree_calcul_ms;

if (num_trame % 30 == 0) {
    double moyenne = somme_temps_ms / 30.0;

    std::cout << "Temps moyen : "
              << moyenne << " ms/frame"
              << " | FPS max approx : "
              << 1000.0 / moyenne
              << std::endl;

    somme_temps_ms = 0.0;
}


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
    fichier_metres.close();
    cap.release();
    cv::destroyAllWindows();
    return 0;
}




