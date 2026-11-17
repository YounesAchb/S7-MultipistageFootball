#ifndef ALGORITHME_HONGROIS_HPP
#define ALGORITHME_HONGROIS_HPP

#include <vector>
#include <algorithm>
#include <cmath>
#include <limits>

/**
 * @brief Espace de nommage pour les algorithmes d'association et de suivi multi-objets (MOT).
 */
namespace tracking {

/**
 * @brief Résolution optimale du problème d'assignation linéaire (Linear Sum Assignment Problem)
 *        par l'algorithme Hongrois / Kuhn-Munkres en complexité O(N^3).
 * 
 * Cette fonction est entièrement autonome, indépendante d'OpenCV, et adaptée pour un portage
 * SoC FPGA / HLS (manipulation de matrices de distances et arithmétique élémentaire).
 * 
 * @param couts Matrice rectangulaire de taille [N_pistes x M_detections] représentant les distances ou coûts.
 *              couts[i][j] = distance entre le joueur suivi i et la détection candidate j.
 * @param seuil_cout_max Distance maximale autorisée pour valider une assignation. Si le coût optimal
 *                       dépasse ce seuil, le joueur reste non assigné (-1).
 * @return std::vector<int> Vecteur de taille N_pistes où le i-ème élément contient :
 *                          - l'indice j de la détection candidate assignée au joueur i.
 *                          - -1 si aucun candidat n'a pu lui être associé de manière valide.
 */
inline std::vector<int> resoudreHongrois(const std::vector<std::vector<float>>& couts, float seuil_cout_max = 50.0f) {
    if (couts.empty() || couts[0].empty()) {
        return {};
    }

    const int n_lignes = static_cast<int>(couts.size());
    const int n_cols = static_cast<int>(couts[0].size());
    const int n = std::max(n_lignes, n_cols);

    const float INFINI = 1e9f;

    // Matrice carrée 1-indexée (n+1) x (n+1) initialisée à 0 (coût nul pour les associations fictives)
    std::vector<std::vector<float>> a(n + 1, std::vector<float>(n + 1, 0.0f));
    for (int i = 0; i < n_lignes; ++i) {
        for (int j = 0; j < n_cols; ++j) {
            a[i + 1][j + 1] = couts[i][j];
        }
    }

    std::vector<float> u(n + 1, 0.0f), v(n + 1, 0.0f);
    std::vector<int> p(n + 1, 0), way(n + 1, 0);

    for (int i = 1; i <= n; ++i) {
        p[0] = i;
        int j0 = 0;
        std::vector<float> minv(n + 1, INFINI);
        std::vector<bool> used(n + 1, false);

        do {
            used[j0] = true;
            int i0 = p[j0];
            float delta = INFINI;
            int j1 = 0;

            for (int j = 1; j <= n; ++j) {
                if (!used[j]) {
                    float cur = a[i0][j] - u[i0] - v[j];
                    if (cur < minv[j]) {
                        minv[j] = cur;
                        way[j] = j0;
                    }
                    if (minv[j] < delta) {
                        delta = minv[j];
                        j1 = j;
                    }
                }
            }

            if (j1 == 0 || delta >= INFINI) {
                break; // Sécurité anti-bouclage infini
            }

            for (int j = 0; j <= n; ++j) {
                if (used[j]) {
                    u[p[j]] += delta;
                    v[j] -= delta;
                } else {
                    minv[j] -= delta;
                }
            }
            j0 = j1;
        } while (p[j0] != 0);

        do {
            int j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0 != 0);
    }

    // Récupération des assignations finales pour chaque piste d'origine
    std::vector<int> assignations(n_lignes, -1);
    for (int j = 1; j <= n_cols; ++j) {
        int i = p[j];
        if (i >= 1 && i <= n_lignes) {
            if (couts[i - 1][j - 1] <= seuil_cout_max) {
                assignations[i - 1] = j - 1;
            }
        }
    }

    return assignations;
}

} // namespace tracking

#endif // ALGORITHME_HONGROIS_HPP
