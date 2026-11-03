#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#if __has_include(<opencv2/geometry.hpp>)
#include <opencv2/geometry.hpp>
#endif

// Structure représentant un joueur détecté et suivi
struct JoueurTracke {
    int id;
    cv::Rect boite;              // Boîte englobante en coordonnées vidéo 4K
    cv::Point pieds;             // Point de contact au sol (sol 2D)
    cv::Point vitesse = cv::Point(0, 0); // Vitesse estimée (pixels/trame)
    int aire;                    // Surface de la silhouette en pixels
    int disparitions = 0;        // Compteur de frames d'occlusion
    int immobile_count = 0;      // Compteur d'immobilité (rejet poteaux et repères fixes)
};

int main(int argc, char* argv[])
{
    cv::VideoCapture capture;
    if (argc > 1) {
        capture.open(argv[1]);
    }

    if (!capture.isOpened()) {
        const std::vector<std::string> chemins = {
            "../data/videos/clip3.mp4",
            "../../data/videos/clip3.mp4",
            "./data/videos/clip3.mp4",
            "/Users/admin/data/videos/clip3.mp4"
        };
        for (const auto& c : chemins) {
            capture.open(c);
            if (capture.isOpened()) break;
        }
    }

    if (!capture.isOpened()) {
        std::cerr << "Erreur : Impossible d'ouvrir la video." << std::endl;
        return 1;
    }

    const double fps = capture.get(cv::CAP_PROP_FPS);
    const int delai = (fps > 0) ? std::max(1, static_cast<int>(1000.0 / fps) - 15) : 30;
    const int largeur = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
    const int hauteur = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));

    // Facteurs d'échelle préservant rigoureusement le ratio d'aspect de la vidéo
    const int disp_w = 1280;
    const int disp_h = std::max(200, static_cast<int>(std::round(disp_w * static_cast<double>(hauteur) / largeur)));
    const double sx = static_cast<double>(disp_w) / largeur;
    const double sy = static_cast<double>(disp_h) / hauteur;

    // Paramètres HSV de la pelouse
    const cv::Scalar vert_min(38, 35, 25);
    const cv::Scalar vert_max(78, 255, 255);

    // 1. Détection du masque panoramique du terrain sur la première frame
    cv::Mat frame_init, hsv_init, pelouse_init, pelouse_propre, pelouse_fermee;
    if (!capture.read(frame_init) || frame_init.empty()) return 1;

    cv::cvtColor(frame_init, hsv_init, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> canaux;
    cv::split(hsv_init, canaux);
    cv::normalize(canaux[2], canaux[2], 0, 255, cv::NORM_MINMAX);
    cv::merge(canaux, hsv_init);
    cv::inRange(hsv_init, vert_min, vert_max, pelouse_init);

    cv::morphologyEx(pelouse_init, pelouse_propre, cv::MORPH_OPEN,
                     cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(7, 7)));
    cv::morphologyEx(pelouse_propre, pelouse_fermee, cv::MORPH_CLOSE,
                     cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(35, 35)));

    // Extraction des lignes blanches du terrain (évite l'agglomération et la perte de suivi)
    cv::Mat lignes_blanches, masque_lignes;
    cv::bitwise_and(pelouse_fermee, ~pelouse_init, lignes_blanches);
    std::vector<std::vector<cv::Point>> contours_lignes;
    cv::findContours(lignes_blanches, contours_lignes, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    cv::Mat lignes_pures = cv::Mat::zeros(hauteur, largeur, CV_8UC1);
    for (const auto& cl : contours_lignes) {
        cv::Rect bl = cv::boundingRect(cl);
        double al = cv::contourArea(cl);
        if (al > 350 && bl.height > 1.2 * bl.width && bl.width < 60) continue;
        cv::drawContours(lignes_pures, std::vector<std::vector<cv::Point>>{cl}, -1, cv::Scalar(255), cv::FILLED);
    }
    cv::dilate(lignes_pures, masque_lignes, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5)));

    std::vector<std::vector<cv::Point>> contours_terrain;
    cv::findContours(pelouse_fermee, contours_terrain, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    int idx_terrain = -1;
    double aire_max = 0.0;
    for (size_t i = 0; i < contours_terrain.size(); ++i) {
        double a = cv::contourArea(contours_terrain[i]);
        if (a > aire_max) {
            aire_max = a;
            idx_terrain = static_cast<int>(i);
        }
    }

    cv::Mat masque_terrain = cv::Mat::zeros(hauteur, largeur, CV_8UC1);
    if (idx_terrain >= 0) {
        cv::drawContours(masque_terrain, contours_terrain, idx_terrain, cv::Scalar(255), cv::FILLED);
        cv::morphologyEx(masque_terrain, masque_terrain, cv::MORPH_OPEN,
                         cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(21, 21)));
        cv::GaussianBlur(masque_terrain, masque_terrain, cv::Size(21, 21), 0);
        cv::threshold(masque_terrain, masque_terrain, 127, 255, cv::THRESH_BINARY);
    } else {
        std::cerr << "Avertissement : Terrain non detecte, repli sur tout le cadre." << std::endl;
        masque_terrain.setTo(255);
    }

    cv::Mat noyau_lignes = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::Mat noyau_corps = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 15));

    // Étalonnage initial du décor statique (poteaux de but, panneaux publicitaires, mires)
    // Permet d'éliminer instantanément dès la 1ère frame les faux positifs sur les objets immobiles
    auto extraire_points_candidats = [&](const cv::Mat& f_src) {
        cv::Mat hsv_c, pel_c, obj_c, sil_c;
        std::vector<cv::Mat> can_c;
        cv::cvtColor(f_src, hsv_c, cv::COLOR_BGR2HSV);
        cv::split(hsv_c, can_c);
        cv::normalize(can_c[2], can_c[2], 0, 255, cv::NORM_MINMAX);
        cv::merge(can_c, hsv_c);
        cv::inRange(hsv_c, vert_min, vert_max, pel_c);
        cv::bitwise_or(pel_c, masque_lignes, pel_c);
        cv::bitwise_and(~pel_c, masque_terrain, obj_c);
        cv::morphologyEx(obj_c, sil_c, cv::MORPH_OPEN, noyau_lignes);
        cv::morphologyEx(sil_c, sil_c, cv::MORPH_CLOSE, noyau_corps);

        std::vector<std::vector<cv::Point>> cnts;
        cv::findContours(sil_c, cnts, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        std::vector<cv::Point> pts;
        for (const auto& c : cnts) {
            cv::Rect b = cv::boundingRect(c);
            int ca = static_cast<int>(cv::contourArea(c));
            if (ca < 35 || ca > 8000 || b.y < 135 || b.y > 750 || b.x < 30 || (b.x + b.width) > largeur - 30) continue;
            if (b.width > 2.0 * b.height && b.width > 35) continue;
            if (b.width > 85 || b.height > 160) continue;
            pts.push_back(cv::Point(b.x + b.width / 2, b.y + b.height));
        }
        return pts;
    };

    capture.set(cv::CAP_PROP_POS_FRAMES, 0);
    cv::Mat f0, f4;
    std::vector<cv::Point> decor_statique;
    if (capture.read(f0) && !f0.empty()) {
        for (int i = 0; i < 4; ++i) capture.read(f4);
        if (!f4.empty()) {
            auto pts0 = extraire_points_candidats(f0);
            auto pts4 = extraire_points_candidats(f4);
            for (const auto& p0 : pts0) {
                for (const auto& p4_pt : pts4) {
                    if (cv::norm(p0 - p4_pt) < 2.0) {
                        decor_statique.push_back(p0);
                        break;
                    }
                }
            }
        }
    }

    capture.set(cv::CAP_PROP_POS_FRAMES, 0);

    const std::string fenetre = "Multipistage - Detection & Suivi des Joueurs";
    cv::namedWindow(fenetre, cv::WINDOW_AUTOSIZE);

    cv::Mat frame, hsv, masque_pelouse, objets, silhouettes;
    cv::Mat frame_estompee, rendu_4k, rendu_disp;

    std::vector<JoueurTracke> pistes;
    int prochain_id = 1;

    while (true) {
        if (!capture.read(frame) || frame.empty()) {
            capture.set(cv::CAP_PROP_POS_FRAMES, 0); // Boucle continue
            pistes.clear();                         // Reinitialisation propre pour eviter les faux raccords
            prochain_id = 1;
            if (!capture.read(frame) || frame.empty()) break;
        }

        // 2. Extraction des silhouettes sur le terrain
        cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);
        cv::split(hsv, canaux);
        cv::normalize(canaux[2], canaux[2], 0, 255, cv::NORM_MINMAX);
        cv::merge(canaux, hsv);
        cv::inRange(hsv, vert_min, vert_max, masque_pelouse);
        cv::bitwise_or(masque_pelouse, masque_lignes, masque_pelouse);

        cv::bitwise_and(~masque_pelouse, masque_terrain, objets);
        cv::morphologyEx(objets, silhouettes, cv::MORPH_OPEN, noyau_lignes);
        cv::morphologyEx(silhouettes, silhouettes, cv::MORPH_CLOSE, noyau_corps);

        // 3. Détection des silhouettes par contours simples et directs
        std::vector<std::vector<cv::Point>> contours_joueurs;
        cv::findContours(silhouettes, contours_joueurs, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        std::vector<JoueurTracke> detections;

        for (const auto& c : contours_joueurs) {
            cv::Rect b = cv::boundingRect(c);
            int ca = static_cast<int>(cv::contourArea(c));
            int cx = b.x, cy = b.y, cw = b.width, ch = b.height;

            // Filtrage bruits, bannières, bordures de caméra et lettrages
            if (ca < 35 || ca > 8000 || cy < 135 || cy > 750 || cx < 30 || (cx + cw) > largeur - 30) continue;
            if (cw > 2.0 * ch && cw > 35) continue;
            if (cw > 85 || ch > 160) continue; // Plafond 160 px adapté aux grands joueurs proches

            // Rejet des poteaux verticaux métalliques aux extrémités du terrain
            if (cw <= 14 && ch >= 40 && (cx < 180 || (cx + cw) > largeur - 180)) continue;

            // Rejet des lignes blanches horizontales au sol (surfaces de réparation, ligne de but)
            if (ch < static_cast<int>(0.85 * cw) && cw > 20) continue;

            // Rejet des artefacts carrés ou trapus de petite taille (résidus de pelouse ou marquages)
            if (ca < 300 && ch < static_cast<int>(1.15 * cw)) continue;

            int yp = cy + ch;
            int xp = cx + cw / 2;
            int h_attendue = static_cast<int>(22.0 + 0.11 * (yp - 130));
            if (ch < static_cast<int>(0.48 * h_attendue)) continue;

            // Rejet immédiat dès la 1ère frame des éléments du décor étalonnés (poteaux, repères fixes)
            bool est_decor = false;
            for (const auto& s : decor_statique) {
                if (std::abs(xp - s.x) < 20 && std::abs(yp - s.y) < 20) {
                    est_decor = true;
                    break;
                }
            }
            if (est_decor) continue;

            JoueurTracke d;
            d.boite = b;
            d.pieds = cv::Point(xp, yp);
            d.aire = ca;

            // Fusion des fragments d'un même corps (tête, torse, jambes, pieds, membres)
            bool fusion = false;
            for (auto& ex : detections) {
                int dx = std::max(0, std::max(ex.boite.x - (d.boite.x + d.boite.width),
                                              d.boite.x - (ex.boite.x + ex.boite.width)));
                int dy = std::max(0, std::max(ex.boite.y - (d.boite.y + d.boite.height),
                                              d.boite.y - (ex.boite.y + ex.boite.height)));
                if (dx <= 12 && dy <= 12) {
                    // Chevauchement horizontal entre les deux boîtes
                    int overlap_x = std::min(ex.boite.x + ex.boite.width, d.boite.x + d.boite.width) -
                                    std::max(ex.boite.x, d.boite.x);

                    // Deux joueurs distincts courant côte à côte ne se chevauchent pas (overlap_x < 6)
                    // et font tous deux plus de 14 px de large et 20 px de haut
                    bool fragment_ex = (ex.boite.width < 14 || ex.boite.height < 20 || ex.aire < 120);
                    bool fragment_d  = (d.boite.width < 14  || d.boite.height < 20  || d.aire < 120);
                    if (overlap_x < 6 && !fragment_ex && !fragment_d) {
                        continue; // Deux joueurs distincts côte à côte : ne pas fusionner
                    }

                    ex.boite = ex.boite | d.boite;
                    ex.pieds = cv::Point(ex.boite.x + ex.boite.width / 2, ex.boite.y + ex.boite.height);
                    ex.aire += d.aire;
                    fusion = true;
                    break;
                }
            }
            if (!fusion) detections.push_back(d);
        }

        // 4. Suivi temporel avec prédiction de mouvement et amortissement cinématique
        const double DIST_MAX = 45.0;
        std::vector<bool> assigne(detections.size(), false);

        for (auto& p : pistes) {
            double dmin = 1e9;
            int idx_match = -1;
            cv::Point pos_pred = p.pieds + p.vitesse; // Prédiction de trajectoire
            for (size_t j = 0; j < detections.size(); ++j) {
                if (assigne[j]) continue;
                // Un joueur sur le terrain ne peut pas sauter verticalement de plus de 16 px en une trame
                if (std::abs(pos_pred.y - detections[j].pieds.y) > 16) continue;
                double dist = cv::norm(pos_pred - detections[j].pieds);
                if (dist < dmin && dist <= DIST_MAX) {
                    dmin = dist;
                    idx_match = static_cast<int>(j);
                }
            }
            if (idx_match >= 0) {
                cv::Point raw_depl = (detections[idx_match].pieds - p.pieds);
                // Plafonnement physique de la vitesse (évite l'effet "fusée" vers le ciel)
                int dx_clamp = std::clamp(raw_depl.x, -24, 24);
                int dy_clamp = std::clamp(raw_depl.y, -8, 8);
                cv::Point depl(dx_clamp, dy_clamp);

                if (cv::norm(raw_depl) < 1.5) {
                    p.immobile_count++;
                } else {
                    p.immobile_count = std::max(0, p.immobile_count - 2);
                }
                // Lissage passe-bas de la vitesse pour amortir les à-coups
                p.vitesse = cv::Point((p.vitesse.x + depl.x) / 2, (p.vitesse.y + depl.y) / 2);
                p.boite = detections[idx_match].boite;
                p.pieds = detections[idx_match].pieds;
                p.aire = detections[idx_match].aire;
                p.disparitions = 0;
                assigne[idx_match] = true;
            } else {
                p.disparitions++;
                // Friction cinématique lors de la perte de contact (ralentit puis s'arrête naturellement)
                p.vitesse.x = static_cast<int>(p.vitesse.x * 0.65f);
                p.vitesse.y = static_cast<int>(p.vitesse.y * 0.65f);
                p.pieds += p.vitesse;
                p.boite.x += p.vitesse.x;
                p.boite.y += p.vitesse.y;
            }
        }

        for (size_t j = 0; j < detections.size(); ++j) {
            if (!assigne[j]) {
                detections[j].id = prochain_id++;
                pistes.push_back(detections[j]);
            }
        }

        // Purge des pistes perdues, immobiles ou ayant dérivé hors du terrain vers le ciel
        pistes.erase(std::remove_if(pistes.begin(), pistes.end(),
                     [](const JoueurTracke& p) {
                         return p.disparitions > 15 || p.immobile_count > 60 || p.pieds.y < 135 || p.boite.y < 110;
                     }),
                     pistes.end());

        // 5. Rendu visuel net à résolution d'affichage
        cv::addWeighted(frame, 0.60, cv::Mat::zeros(frame.size(), frame.type()), 0.40, 0, frame_estompee);
        rendu_4k = frame.clone();
        frame_estompee.copyTo(rendu_4k, ~masque_terrain);
        cv::resize(rendu_4k, rendu_disp, cv::Size(disp_w, disp_h), 0, 0, cv::INTER_LINEAR);

        int nb_visibles = 0;
        for (const auto& j : pistes) {
            if (j.disparitions > 4) continue; // Maintien visuel inertiel fluide lors des transitions
            if (j.immobile_count > 25) continue; // Suppression des infrastructures fixes (poteaux, repères)
            if (j.pieds.y < 135 || j.boite.y < 110) continue; // Interdiction absolue d'afficher des boîtes dans le ciel/tribunes
            nb_visibles++;

            // Calcul du carré à l'écran avec marge nette
            int bx = std::max(0, static_cast<int>(j.boite.x * sx) - 2);
            int by = std::max(0, static_cast<int>(j.boite.y * sy) - 2);
            int bw = std::max(18, static_cast<int>(j.boite.width * sx) + 4);
            int bh = std::max(22, static_cast<int>(j.boite.height * sy) + 4);
            cv::Rect b_disp(bx, by, bw, bh);

            // Double contour pour lisibilité absolue (noir + jaune d'or)
            cv::rectangle(rendu_disp, b_disp, cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
            cv::rectangle(rendu_disp, b_disp, cv::Scalar(0, 230, 255), 2, cv::LINE_AA);

            // Point de contact des pieds au sol
            cv::Point pt_pieds(static_cast<int>(j.pieds.x * sx), static_cast<int>(j.pieds.y * sy));
            cv::circle(rendu_disp, pt_pieds, 3, cv::Scalar(0, 0, 255), cv::FILLED);
            cv::circle(rendu_disp, pt_pieds, 1, cv::Scalar(255, 255, 255), cv::FILLED);

            // Étiquette avec l'identifiant du joueur
            std::string tag = "#" + std::to_string(j.id);
            int baseline = 0;
            cv::Size sz = cv::getTextSize(tag, cv::FONT_HERSHEY_SIMPLEX, 0.40, 1, &baseline);
            int y_badge = std::max(0, by - sz.height - 3);
            cv::Rect rect_badge(bx, y_badge, sz.width + 6, sz.height + 3);
            cv::rectangle(rendu_disp, rect_badge, cv::Scalar(0, 0, 0), cv::FILLED);
            cv::rectangle(rendu_disp, rect_badge, cv::Scalar(0, 230, 255), 1);
            cv::putText(rendu_disp, tag, cv::Point(bx + 3, y_badge + sz.height),
                        cv::FONT_HERSHEY_SIMPLEX, 0.40, cv::Scalar(0, 230, 255), 1, cv::LINE_AA);
        }

        // Compteur simple et discret
        cv::putText(rendu_disp, "Joueurs suivis : " + std::to_string(nb_visibles),
                    cv::Point(20, 25), cv::FONT_HERSHEY_SIMPLEX, 0.65, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);

        cv::imshow(fenetre, rendu_disp);

        const char touche = static_cast<char>(cv::waitKey(delai));
        if (touche == 'q' || touche == 'Q' || touche == 27) break;
    }

    capture.release();
    cv::destroyAllWindows();
    return 0;
}
