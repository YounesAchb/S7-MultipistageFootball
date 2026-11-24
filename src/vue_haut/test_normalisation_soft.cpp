#include <opencv2/opencv.hpp>

void trouverMinMax(const cv::Mat& image, int& nmin, int& nmax) {
    int histogramme[256] = {0};
    int nb_valeurs = 0;

    for (int y = 0; y < image.rows; y++) {
        const uchar* ligne = image.ptr<uchar>(y);
        for (int x = 0; x < image.cols * image.channels(); x++) {
            histogramme[ligne[x]]++;
            nb_valeurs++;
        }
    }

    int seuil = nb_valeurs / 100;

    int cumul = 0;
    nmin = 0;
    while (cumul + histogramme[nmin] <= seuil) {
        cumul = cumul + histogramme[nmin];
        nmin++;
    }

    cumul = 0;
    nmax = 255;
    while (cumul + histogramme[nmax] <= seuil) {
        cumul = cumul + histogramme[nmax];
        nmax--;
    }
}

cv::Mat normaliser(const cv::Mat& image, int nmin, int nmax) {
    cv::Mat resultat = image.clone();

    for (int y = 0; y < image.rows; y++) {
        const uchar* entree = image.ptr<uchar>(y);
        uchar* sortie = resultat.ptr<uchar>(y);
        for (int x = 0; x < image.cols * image.channels(); x++) {
            int valeur = (entree[x] - nmin) * 255 / (nmax - nmin);
            if (valeur < 0) valeur = 0;
            if (valeur > 255) valeur = 255;
            sortie[x] = valeur;
        }
    }
    return resultat;
}

int main() {
    cv::VideoCapture video("/Users/admin/data/vue_haut/D_20220220_1_1530_1560.mp4");
    cv::Mat couleur, gris;

    while (video.read(couleur)) {
        cv::cvtColor(couleur, gris, cv::COLOR_BGR2GRAY);

        int nmin_gris, nmax_gris;
        trouverMinMax(gris, nmin_gris, nmax_gris);
        cv::Mat gris_normalise = normaliser(gris, nmin_gris, nmax_gris);

        int nmin_couleur, nmax_couleur;
        trouverMinMax(couleur, nmin_couleur, nmax_couleur);
        cv::Mat couleur_normalisee = normaliser(couleur, nmin_couleur, nmax_couleur);

        cv::Mat gris_3_canaux, gris_normalise_3_canaux;
        cv::cvtColor(gris, gris_3_canaux, cv::COLOR_GRAY2BGR);
        cv::cvtColor(gris_normalise, gris_normalise_3_canaux, cv::COLOR_GRAY2BGR);

        cv::Mat haut, bas, fenetre;
        cv::hconcat(gris_3_canaux, gris_normalise_3_canaux, haut);
        cv::hconcat(couleur, couleur_normalisee, bas);
        cv::vconcat(haut, bas, fenetre);
        cv::resize(fenetre, fenetre, cv::Size(1280, 720));

        cv::imshow("Normalisation", fenetre);
        if (cv::waitKey(1) == 'q') break;
    }
    return 0;
}
