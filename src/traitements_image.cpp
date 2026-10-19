#include "traitements_image.hpp"

#include <array>
#include <cmath>
#include <cstddef>

void normaliserMinMax(cv::Mat& canal)
{
    // Ne pas lire de pixels si aucune image n'a ete fournie.
    if (canal.empty()) return;
    CV_Assert(canal.type() == CV_8UC1);

    int minimum = 255;
    int maximum = 0;

    // Premiere passe : trouver les deux bornes sur l'ensemble du canal.
    // ptr() respecte le pas entre lignes, meme pour une sous-image non contigue.
    for (int y = 0; y < canal.rows; ++y) {
        const uchar* pixels = canal.ptr<uchar>(y);
        for (int x = 0; x < canal.cols; ++x) {
            const int valeur = pixels[x];
            if (valeur < minimum) minimum = valeur;
            if (valeur > maximum) maximum = valeur;
        }
    }

    const int etendue = maximum - minimum;
    // Deuxieme passe : etirer la luminosite entre 0 et 255.
    for (int y = 0; y < canal.rows; ++y) {
        uchar* pixels = canal.ptr<uchar>(y);
        for (int x = 0; x < canal.cols; ++x) {
            if (etendue == 0) {
                pixels[x] = 0;
            } else {
                const double valeur = 255.0 * (pixels[x] - minimum) / etendue;
                // L'arrondi manuel peut differer d'OpenCV d'un niveau.
                pixels[x] = static_cast<uchar>(std::lround(valeur));
            }
        }
    }
}

void egaliserHistogramme(cv::Mat& canal)
{
    if (canal.empty()) return;
    CV_Assert(canal.type() == CV_8UC1);

    // histogramme[v] compte les pixels ayant la luminosite v.
    std::array<std::size_t, 256> histogramme{};
    for (int y = 0; y < canal.rows; ++y) {
        const uchar* pixels = canal.ptr<uchar>(y);
        for (int x = 0; x < canal.cols; ++x) ++histogramme[pixels[x]];
    }

    int premier_niveau = 0;
    while (histogramme[premier_niveau] == 0) ++premier_niveau;
    const std::size_t total = canal.total();
    const std::size_t cumul_min = histogramme[premier_niveau];
    // Un seul niveau present : conserver ce canal uniforme.
    if (total == cumul_min) return;

    // Table = 255 * (cumul(v) - cumul_min) / (total - cumul_min).
    // La premiere case presente reste a zero grace a l'initialisation {}.
    std::array<uchar, 256> correspondance{};
    std::size_t cumul = 0;
    for (int niveau = premier_niveau + 1; niveau < 256; ++niveau) {
        cumul += histogramme[niveau];
        const double valeur = 255.0 * cumul / (total - cumul_min);
        correspondance[niveau] = static_cast<uchar>(std::lround(valeur));
    }

    for (int y = 0; y < canal.rows; ++y) {
        uchar* pixels = canal.ptr<uchar>(y);
        for (int x = 0; x < canal.cols; ++x) pixels[x] = correspondance[pixels[x]];
    }
}
