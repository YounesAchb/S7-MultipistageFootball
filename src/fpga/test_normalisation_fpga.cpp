/**
 * =========================================================================================
 * PROJET ENSEA S7 — EdgeTrack-Foot
 * Fichier : src/fpga/test_normalisation_fpga.cpp
 * 
 * OBJET :
 *   Code isolé de l'IP de Normalisation d'Image pour SoC FPGA (Zynq / UltraScale+).
 *   Ce fichier contient UNIQUEMENT le cœur algorithmique matériel et son pilote logiciel,
 *   sans aucune interface graphique superflue, pour intégration directe par le binôme FPGA.
 * 
 * ARCHITECTURE CO-DESIGN MATÉRIEL / LOGICIEL :
 *   1. LOGICIEL (ARM Cortex-A) :
 *      - Échantillonne rapidement la luminance de l'image (1 pixel sur 64).
 *      - Détermine les bornes utiles [min, max] en éliminant les 1% extrêmes.
 *      - Remplit la table de transcodage LUT de 256 octets via le bus AXI-Lite.
 * 
 *   2. MATÉRIEL (PL FPGA / Vitis HLS) :
 *      - Lit le flux de pixels AXI4-Stream au fil de l'eau (24 bits BGR).
 *      - Applique la normalisation via 1 Block RAM (BRAM LUT) en 1 cycle par pixel (II=1).
 *      - Zéro division matérielle, zéro buffer de trame en mémoire interne.
 * =========================================================================================
 */

#include <iostream>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <opencv2/opencv.hpp>

// =========================================================================================
// 1. KERNEL MATÉRIEL FPGA (Code synthétisable en Vitis HLS / Vivado)
// =========================================================================================

/**
 * @brief Traitement unitaire d'un pixel RGB sur FPGA.
 *        Transposition directe en logique combinatoire ou pipeline synchrone 1 cycle.
 */
inline void fpga_normalisation_pixel(uint8_t in_b, uint8_t in_g, uint8_t in_r,
                                     const uint8_t lut[256],
                                     uint8_t& out_b, uint8_t& out_g, uint8_t& out_r) {
    #pragma HLS INLINE
    #pragma HLS PIPELINE II=1

    // Transcodage direct par table de correspondance BRAM (Look-Up Table)
    // Coût matériel : 1 seule Block RAM de 256 octets, 0 DSP, 0 division.
    out_b = lut[in_b];
    out_g = lut[in_g];
    out_r = lut[in_r];
}

/**
 * @brief IP complète de normalisation en streaming (AXI4-Stream).
 *        Traite un flux continu de pixels à la volée.
 */
void fpga_normalisation_stream(const uint8_t* in_pixels, uint8_t* out_pixels,
                               int nb_pixels, const uint8_t lut[256]) {
    #pragma HLS INTERFACE axis port=in_pixels
    #pragma HLS INTERFACE axis port=out_pixels
    #pragma HLS INTERFACE s_axilite port=lut bundle=control
    #pragma HLS INTERFACE s_axilite port=nb_pixels bundle=control
    #pragma HLS INTERFACE s_axilite port=return bundle=control

    for (int i = 0; i < nb_pixels; ++i) {
        #pragma HLS PIPELINE II=1
        int idx = i * 3;
        fpga_normalisation_pixel(in_pixels[idx + 0], in_pixels[idx + 1], in_pixels[idx + 2],
                                 lut,
                                 out_pixels[idx + 0], out_pixels[idx + 1], out_pixels[idx + 2]);
    }
}

// =========================================================================================
// 2. PILOTE LOGICIEL PROCESSEUR (PS - ARM Cortex-A)
// =========================================================================================

/**
 * @brief Calcule la table de transcodage (LUT) à partir des bornes [val_min, val_max].
 *        Cette table est envoyée une fois par trame aux registres de l'IP FPGA.
 */
void arm_calculer_lut(uint8_t val_min, uint8_t val_max, uint8_t lut[256]) {
    if (val_max <= val_min) {
        val_min = 0;
        val_max = 255;
    }
    int plage = static_cast<int>(val_max) - static_cast<int>(val_min);

    for (int i = 0; i < 256; ++i) {
        if (i <= val_min) {
            lut[i] = 0;
        } else if (i >= val_max) {
            lut[i] = 255;
        } else {
            // Étirement linéaire sur 8 bits : ((i - min) * 255) / plage
            int val_norm = ((i - val_min) * 255) / plage;
            lut[i] = static_cast<uint8_t>(val_norm > 255 ? 255 : (val_norm < 0 ? 0 : val_norm));
        }
    }
}

/**
 * @brief Analyse statistique rapide pour déterminer [val_min, val_max] sans surcharger le CPU.
 *        Sous-échantillonnage d'un pixel sur 64 (consomme moins de 0.1 ms sur ARM Cortex-A).
 */
void arm_analyser_histogramme(const uint8_t* in_pixels, int nb_pixels, uint8_t& val_min, uint8_t& val_max) {
    int histo[256] = {0};
    int echantillons = 0;

    for (int i = 0; i < nb_pixels; i += 64) {
        int idx = i * 3;
        // Calcul de luminance entière rapide : Y = (77*R + 150*G + 29*B) >> 8
        int lum = (77 * in_pixels[idx + 2] + 150 * in_pixels[idx + 1] + 29 * in_pixels[idx + 0]) >> 8;
        if (lum > 255) lum = 255;
        histo[lum]++;
        echantillons++;
    }

    // Exclusion des 1% extrêmes pour rejeter reflets spéculaires et pixels morts
    int seuil_1pct = echantillons / 100;
    int seuil_99pct = echantillons - seuil_1pct;
    int cumul = 0;
    val_min = 0;
    val_max = 255;

    for (int i = 0; i < 256; ++i) {
        cumul += histo[i];
        if (cumul >= seuil_1pct && val_min == 0) {
            val_min = static_cast<uint8_t>(i);
        }
        if (cumul >= seuil_99pct) {
            val_max = static_cast<uint8_t>(i);
            break;
        }
    }
}

// =========================================================================================
// 3. BANC DE TEST ET VÉRIFICATION MATÉRIELLE
// =========================================================================================

int main(int argc, char* argv[]) {
    std::cout << "========================================================" << std::endl;
    std::cout << "  IP NORMALISATION D'IMAGE POUR FPGA (ENSEA S7)        " << std::endl;
    std::cout << "  Module Co-Design HW/SW isole                         " << std::endl;
    std::cout << "========================================================" << std::endl;

    // Chargement d'une image ou création d'une trame de test
    cv::Mat image_entree;
    if (argc > 1) {
        image_entree = cv::imread(argv[1]);
    }
    if (image_entree.empty()) {
        std::string video_defaut = "/Users/admin/data/vue_haut/D_20220220_1_1530_1560.mp4";
        cv::VideoCapture cap(video_defaut);
        if (cap.isOpened()) {
            cap.read(image_entree);
        }
    }
    if (image_entree.empty()) {
        std::cout << "[INFO] Creation d'une trame synthetique de test (1920x1080)..." << std::endl;
        image_entree.create(1080, 1920, CV_8UC3);
        image_entree.setTo(cv::Scalar(40, 95, 35)); // Vert sombre simulant zone ombragée
    }

    int nb_pixels = image_entree.rows * image_entree.cols;
    cv::Mat image_sortie(image_entree.rows, image_entree.cols, CV_8UC3);

    // Étape A : Partie ARM CPU -> Analyse statistique et génération de la LUT
    uint8_t val_min = 0, val_max = 255;
    arm_analyser_histogramme(image_entree.data, nb_pixels, val_min, val_max);

    uint8_t table_lut[256];
    arm_calculer_lut(val_min, val_max, table_lut);

    std::cout << "[CPU ARM] Bornes detectees   : [" << static_cast<int>(val_min) 
              << " .. " << static_cast<int>(val_max) << "]" << std::endl;
    std::cout << "[CPU ARM] LUT generee       : " << static_cast<int>(val_max - val_min) 
              << " niveaux etires sur [0..255]." << std::endl;

    // Étape B : Partie FPGA -> Traitement en flux continu AXI4-Stream
    fpga_normalisation_stream(image_entree.data, image_sortie.data, nb_pixels, table_lut);

    std::cout << "[FPGA PL] Flux traite       : " << nb_pixels << " pixels traites a II=1." << std::endl;
    std::cout << "[FPGA PL] Debit theorique   : 300 Mpixels/s a 300 MHz (soit ~140 FPS en 1080p, ~35 FPS en 4K)." << std::endl;

    // Étape C : Sauvegarde du résultat
    std::string nom_sortie = "normalisation_fpga_resultat.png";
    cv::imwrite(nom_sortie, image_sortie);
    std::cout << "[SUCCES] Image normalisee enregistree : " << nom_sortie << std::endl;

    return 0;
}
