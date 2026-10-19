#include "traitements_image.hpp"

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using Horloge = std::chrono::steady_clock;
using Traitement = void (*)(cv::Mat&);

// Le calcul manuel et OpenCV peuvent arrondir une valeur de deux façons
// différentes. On accepte donc au maximum un niveau sur 255.
constexpr int TOLERANCE = 1;

struct Difference {
    int maximum = 0;
    int pixelsDifferents = 0;
};

Difference comparerPixels(const cv::Mat& premiere, const cv::Mat& seconde) {
    if (premiere.type() != CV_8UC1 || seconde.type() != CV_8UC1 ||
        premiere.size() != seconde.size() || premiere.empty()) {
        throw std::invalid_argument("La comparaison attend deux images CV_8UC1 non vides de meme taille.");
    }

    cv::Mat difference;
    cv::absdiff(premiere, seconde, difference);
    double maximum = 0.0;
    cv::minMaxLoc(difference, nullptr, &maximum);
    return {static_cast<int>(maximum), cv::countNonZero(difference)};
}

std::string decrire(const Difference& difference) {
    return "ecart maximal=" + std::to_string(difference.maximum) +
           ", pixels differents=" + std::to_string(difference.pixelsDifferents);
}

void normalisationOpenCV(cv::Mat& canal) {
    cv::normalize(canal, canal, 0, 255, cv::NORM_MINMAX);
}

void egalisationOpenCV(cv::Mat& canal) {
    cv::equalizeHist(canal, canal);
}

// Chaque test échoué est affiché, puis les autres continuent. Le code de
// retour final reste non nul dès qu'une vérification a échoué.
class SuiteTests {
public:
    void verifier(const std::string& nom, const std::function<void()>& test) {
        ++nombre_;
        try {
            test();
            std::cout << "[OK] " << nom << '\n';
        } catch (const std::exception& erreur) {
            ++echecs_;
            std::cerr << "[ECHEC] " << nom << " : " << erreur.what() << '\n';
        }
    }

    int nombre() const { return nombre_; }
    int echecs() const { return echecs_; }

private:
    int nombre_ = 0;
    int echecs_ = 0;
};

void exiger(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void verifierFaceAOpenCV(const cv::Mat& entree, Traitement manuel, Traitement reference) {
    // Les deux fonctions reçoivent une copie de la même image de départ.
    cv::Mat resultatManuel = entree.clone();
    cv::Mat resultatReference = entree.clone();
    const unsigned char* adresseInitiale = resultatManuel.data;
    manuel(resultatManuel);
    reference(resultatReference);

    exiger(resultatManuel.type() == entree.type() && resultatManuel.size() == entree.size(),
           "Le traitement a change le type ou les dimensions.");
    exiger(resultatManuel.data == adresseInitiale,
           "Le traitement doit ecrire dans les pixels de la matrice fournie.");
    const Difference difference = comparerPixels(resultatManuel, resultatReference);
    exiger(difference.maximum <= TOLERANCE, decrire(difference));
}

void verifierSousMatrice(Traitement manuel, Traitement reference) {
    cv::Mat image(14, 19, CV_8UC1);
    cv::RNG generateur(0x20261019);
    generateur.fill(image, cv::RNG::UNIFORM, 60, 180);

    const cv::Rect zone(3, 2, 11, 7);
    cv::Mat attendu = image.clone();
    cv::Mat zoneAttendue = attendu(zone);
    reference(zoneAttendue);

    // Une ROI n'occupe ici qu'une partie de chaque ligne : elle est non
    // contiguë. Une fonction correcte doit respecter le pas entre les lignes.
    cv::Mat sousMatrice = image(zone);
    exiger(!sousMatrice.isContinuous(), "La ROI de test devait etre non contigue.");
    const unsigned char* adresseInitiale = sousMatrice.data;
    manuel(sousMatrice);
    exiger(sousMatrice.data == adresseInitiale, "Le traitement a detache la ROI de son image.");

    const Difference difference = comparerPixels(image(zone), zoneAttendue);
    exiger(difference.maximum <= TOLERANCE, decrire(difference));

    // On vérifie aussi que le traitement n'a touché aucun pixel à l'extérieur.
    cv::Mat ecart;
    cv::absdiff(image, attendu, ecart);
    ecart(zone).setTo(0);
    exiger(cv::countNonZero(ecart) == 0, "Des pixels exterieurs a la ROI ont ete modifies.");
}

void verifierTypeInvalide(int type, Traitement traitement) {
    cv::Mat image(4, 6, type, cv::Scalar::all(80));
    bool exceptionAttendue = false;
    try {
        traitement(image);
    } catch (const cv::Exception&) {
        exceptionAttendue = true;
    }
    exiger(exceptionAttendue, "Un type autre que CV_8UC1 doit lever cv::Exception.");
}

int executerTestsMathematiques() {
    SuiteTests suite;

    suite.verifier("Min max : petite matrice au resultat connu", [] {
        cv::Mat image = (cv::Mat_<unsigned char>(2, 3) << 10, 20, 30, 40, 50, 60);
        const cv::Mat attendu = (cv::Mat_<unsigned char>(2, 3) << 0, 51, 102, 153, 204, 255);
        verifierFaceAOpenCV(image, normaliserMinMax, normalisationOpenCV);
        normaliserMinMax(image);
        exiger(comparerPixels(image, attendu).maximum == 0,
               "La formule 255 * (valeur - 10) / (60 - 10) donne un resultat inattendu.");
    });

    suite.verifier("Egalisation : petite matrice au resultat connu", [] {
        cv::Mat image = (cv::Mat_<unsigned char>(2, 4) << 10, 10, 10, 20, 30, 30, 40, 40);
        const cv::Mat attendu = (cv::Mat_<unsigned char>(2, 4) << 0, 0, 0, 51, 153, 153, 255, 255);
        verifierFaceAOpenCV(image, egaliserHistogramme, egalisationOpenCV);
        egaliserHistogramme(image);
        exiger(comparerPixels(image, attendu).maximum == 0,
               "Le cumul de l'histogramme ne donne pas la table attendue.");
    });

    for (int valeur : {0, 80, 255}) {
        suite.verifier("Min max : image uniforme " + std::to_string(valeur), [valeur] {
            cv::Mat image(13, 17, CV_8UC1, cv::Scalar(valeur));
            verifierFaceAOpenCV(image, normaliserMinMax, normalisationOpenCV);
            normaliserMinMax(image);
            exiger(cv::countNonZero(image) == 0,
                   "Min=max : tous les pixels doivent devenir 0 sans division par zero.");
        });

        suite.verifier("Egalisation : image uniforme " + std::to_string(valeur), [valeur] {
            cv::Mat image(13, 17, CV_8UC1, cv::Scalar(valeur));
            const cv::Mat attendu = image.clone();
            verifierFaceAOpenCV(image, egaliserHistogramme, egalisationOpenCV);
            egaliserHistogramme(image);
            exiger(comparerPixels(image, attendu).maximum == 0,
                   "Une image uniforme doit rester inchangee lors de l'egalisation.");
        });
    }

    for (const auto& traitement : std::vector<std::pair<std::string, Traitement>>{
             {"Min max", normaliserMinMax}, {"Egalisation", egaliserHistogramme}}) {
        suite.verifier(traitement.first + " : image vide", [traitement] {
            cv::Mat vide;
            traitement.second(vide);
            exiger(vide.empty(), "Une image vide doit rester vide.");
        });
    }

    struct CasAleatoire {
        std::string nom;
        int lignes;
        int colonnes;
        int minimum;
        int maximum;
    };
    const std::vector<CasAleatoire> casAleatoires = {
        {"plage restreinte 80..120", 157, 211, 80, 120},
        {"plage complete 0..255", 157, 211, 0, 255},
        {"niveaux sombres 0..9", 37, 53, 0, 9},
        {"niveaux clairs 247..255", 37, 53, 247, 255},
        {"une seule ligne 0..255", 1, 257, 0, 255},
        {"une seule colonne 80..120", 257, 1, 80, 120},
    };
    cv::RNG generateur(0x20261019);  // Même graine : mêmes données à chaque lancement.
    for (const auto& cas : casAleatoires) {
        cv::Mat image(cas.lignes, cas.colonnes, CV_8UC1);
        generateur.fill(image, cv::RNG::UNIFORM, cas.minimum, cas.maximum + 1);
        image.at<unsigned char>(0, 0) = static_cast<unsigned char>(cas.minimum);
        image.at<unsigned char>(image.rows - 1, image.cols - 1) = static_cast<unsigned char>(cas.maximum);

        suite.verifier("Min max : aleatoire " + cas.nom, [&image] {
            verifierFaceAOpenCV(image, normaliserMinMax, normalisationOpenCV);
        });
        suite.verifier("Egalisation : aleatoire " + cas.nom, [&image] {
            verifierFaceAOpenCV(image, egaliserHistogramme, egalisationOpenCV);
        });
    }

    suite.verifier("Min max : ROI non contigue et exterieur preserve", [] {
        verifierSousMatrice(normaliserMinMax, normalisationOpenCV);
    });
    suite.verifier("Egalisation : ROI non contigue et exterieur preserve", [] {
        verifierSousMatrice(egaliserHistogramme, egalisationOpenCV);
    });

    const std::vector<std::pair<std::string, int>> typesInvalides = {
        {"CV_8UC3", CV_8UC3}, {"CV_16UC1", CV_16UC1},
        {"CV_32FC1", CV_32FC1}, {"CV_8SC1", CV_8SC1},
    };
    for (const auto& type : typesInvalides) {
        suite.verifier("Min max : refuse " + type.first, [type] {
            verifierTypeInvalide(type.second, normaliserMinMax);
        });
        suite.verifier("Egalisation : refuse " + type.first, [type] {
            verifierTypeInvalide(type.second, egaliserHistogramme);
        });
    }

    std::cout << "\nTests mathematiques : " << (suite.nombre() - suite.echecs())
              << '/' << suite.nombre() << " reussis.\n";
    return suite.echecs();
}

struct Options {
    fs::path video;
    fs::path sortie = "resultats_normalisation";
    int images = 150;
    bool aide = false;
};

void afficherAide(const char* executable) {
    std::cout
        << "Tests des traitements manuels (aucune fenetre graphique).\n\n"
        << "Usage :\n  " << executable << "\n  " << executable
        << " --video CHEMIN --frames N --output DOSSIER\n\n"
        << "Sans argument : tests mathematiques uniquement.\n"
        << "--video CHEMIN   Ajoute un test sequentiel sur une video.\n"
        << "--frames N       Nombre exact d'images attendu (defaut : 150).\n"
        << "--output DOSSIER CSV et captures (defaut : resultats_normalisation).\n"
        << "--help           Affiche cette aide.\n\n"
        << "Les temps CSV portent sur un appel de chaque normalisation par image,\n"
        << "hors lecture, copies, conversion HSV, masque et ecriture des captures.\n"
        << "Ce sont des mesures indicatives, pas un benchmark de la chaine video.\n";
}

Options lireOptions(int argc, char** argv) {
    Options options;
    bool imagesFournies = false;
    bool sortieFournie = false;
    bool videoFournie = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            options.aide = true;
            continue;
        }
        if (argument != "--video" && argument != "--frames" && argument != "--output") {
            throw std::invalid_argument("Option inconnue : " + argument);
        }
        if (i + 1 >= argc || std::string(argv[i + 1]).rfind("--", 0) == 0) {
            throw std::invalid_argument("Valeur manquante apres " + argument);
        }
        const std::string valeur = argv[++i];
        if (valeur.empty()) {
            throw std::invalid_argument("La valeur de " + argument + " ne peut pas etre vide.");
        }
        if (argument == "--video") {
            if (videoFournie) {
                throw std::invalid_argument("--video ne doit etre fourni qu'une fois.");
            }
            options.video = fs::path(valeur);
            videoFournie = true;
        } else if (argument == "--output") {
            if (sortieFournie) {
                throw std::invalid_argument("--output ne doit etre fourni qu'une fois.");
            }
            options.sortie = fs::path(valeur);
            sortieFournie = true;
        } else {
            if (imagesFournies) {
                throw std::invalid_argument("--frames ne doit etre fourni qu'une fois.");
            }
            std::size_t fin = 0;
            long long nombre = 0;
            try {
                nombre = std::stoll(valeur, &fin);
            } catch (const std::exception&) {
                throw std::invalid_argument("--frames attend un entier strictement positif.");
            }
            if (fin != valeur.size() || nombre <= 0 || nombre > std::numeric_limits<int>::max()) {
                throw std::invalid_argument("--frames attend un entier strictement positif representable par int.");
            }
            options.images = static_cast<int>(nombre);
            imagesFournies = true;
        }
    }
    if (!options.aide && options.video.empty() && (imagesFournies || sortieFournie)) {
        throw std::invalid_argument("--frames et --output necessitent --video.");
    }
    return options;
}

double millisecondes(Horloge::time_point debut, Horloge::time_point fin) {
    return std::chrono::duration<double, std::milli>(fin - debut).count();
}

void enregistrerImage(const fs::path& chemin, const cv::Mat& image) {
    if (!cv::imwrite(chemin.string(), image)) {
        throw std::runtime_error("Echec d'ecriture de la capture : " + chemin.string());
    }
}

void enregistrerCaptures(const fs::path& dossier, int index, const cv::Mat& imageBrute,
                         const cv::Mat& vManuel, const cv::Mat& vReference,
                         const cv::Mat& hsvManuel, const cv::Mat& hsvReference,
                         const cv::Mat& masqueManuel, const cv::Mat& masqueReference) {
    std::ostringstream nom;
    nom << "frame_" << std::setw(6) << std::setfill('0') << index;
    const fs::path captures = dossier / nom.str();
    fs::create_directories(captures);
    enregistrerImage(captures / "image_brute.png", imageBrute);
    enregistrerImage(captures / "luminosite_manuelle.png", vManuel);
    enregistrerImage(captures / "luminosite_reference.png", vReference);
    enregistrerImage(captures / "masque_manuel.png", masqueManuel);
    enregistrerImage(captures / "masque_reference.png", masqueReference);

    // Ces rendus aident à lire visuellement les résultats. Les comparaisons
    // numériques portent sur V et sur le masque, avant toute conversion BGR.
    cv::Mat couleur;
    cv::cvtColor(hsvManuel, couleur, cv::COLOR_HSV2BGR);
    enregistrerImage(captures / "couleur_manuelle.png", couleur);
    cv::cvtColor(hsvReference, couleur, cv::COLOR_HSV2BGR);
    enregistrerImage(captures / "couleur_reference.png", couleur);
}

bool executerTestVideo(const Options& options) {
    if (!fs::is_regular_file(options.video)) {
        throw std::runtime_error("Video introuvable : " + options.video.string());
    }
    cv::VideoCapture capture(options.video.string());
    if (!capture.isOpened()) {
        throw std::runtime_error("OpenCV ne parvient pas a ouvrir la video : " + options.video.string());
    }

    fs::create_directories(options.sortie);
    const fs::path cheminCsv = options.sortie / "comparaison_video.csv";
    std::ofstream csv(cheminCsv);
    if (!csv) {
        throw std::runtime_error("Impossible de creer le CSV : " + cheminCsv.string());
    }
    csv << "frame,width,height,minmaxDiffMax,minmaxDiffPixels,masqueDiffPixels,"
           "normalisationManuelleMs,normalisationOpenCVMs\n";
    csv << std::fixed << std::setprecision(6);

    const cv::Scalar seuilBas(38, 25, 25);
    const cv::Scalar seuilHaut(78, 255, 255);
    int imagesTraitees = 0;
    int ecartMaximal = 0;
    int imagesHorsTolerance = 0;
    unsigned long long pixelsDifferents = 0;
    unsigned long long pixelsMasqueDifferents = 0;
    double tempsManuel = 0.0;
    double tempsOpenCV = 0.0;

    std::cout << "\nVideo : " << options.video.string() << '\n'
              << "Images demandees : " << options.images << '\n'
              << "Comparaison a pleine resolution ; H=38..78, S=25..255, V=25..255.\n";

    for (int index = 0; index < options.images; ++index) {
        // Lecture séquentielle : aucun saut approximatif à un numéro d'image.
        cv::Mat imageBrute;
        if (!capture.read(imageBrute) || imageBrute.empty()) {
            break;
        }
        if (imageBrute.type() != CV_8UC3) {
            throw std::runtime_error("La video doit fournir des images BGR CV_8UC3.");
        }

        cv::Mat hsv;
        std::vector<cv::Mat> canaux;
        cv::cvtColor(imageBrute, hsv, cv::COLOR_BGR2HSV);
        cv::split(hsv, canaux);

        // Le canal V garde ici exactement les dimensions de l'image décodée.
        // Les copies se font AVANT le chronomètre : elles ne sont pas incluses.
        cv::Mat vManuel = canaux[2].clone();
        cv::Mat vReference = canaux[2].clone();
        const auto debutManuel = Horloge::now();
        normaliserMinMax(vManuel);
        const auto finManuel = Horloge::now();
        const auto debutOpenCV = Horloge::now();
        normalisationOpenCV(vReference);
        const auto finOpenCV = Horloge::now();
        const double dureeManuelle = millisecondes(debutManuel, finManuel);
        const double dureeOpenCV = millisecondes(debutOpenCV, finOpenCV);

        const Difference difference = comparerPixels(vManuel, vReference);
        if (difference.maximum > TOLERANCE) {
            ++imagesHorsTolerance;
        }

        // On garde le même H, le même S et les mêmes seuils pour les deux
        // versions. Seul le calcul de la normalisation de V change.
        std::vector<cv::Mat> canauxManuels = {canaux[0], canaux[1], vManuel};
        std::vector<cv::Mat> canauxReference = {canaux[0], canaux[1], vReference};
        cv::Mat hsvManuel;
        cv::Mat hsvReference;
        cv::merge(canauxManuels, hsvManuel);
        cv::merge(canauxReference, hsvReference);
        cv::Mat masqueManuel;
        cv::Mat masqueReference;
        cv::inRange(hsvManuel, seuilBas, seuilHaut, masqueManuel);
        cv::inRange(hsvReference, seuilBas, seuilHaut, masqueReference);
        const Difference differenceMasque = comparerPixels(masqueManuel, masqueReference);

        csv << index << ',' << imageBrute.cols << ',' << imageBrute.rows << ','
            << difference.maximum << ',' << difference.pixelsDifferents << ','
            << differenceMasque.pixelsDifferents << ','
            << dureeManuelle << ',' << dureeOpenCV << '\n';
        if (!csv) {
            throw std::runtime_error("Echec pendant l'ecriture du CSV.");
        }

        if (index == 0 || index == options.images / 2 || index == options.images - 1) {
            enregistrerCaptures(options.sortie, index, imageBrute, vManuel, vReference,
                                hsvManuel, hsvReference, masqueManuel, masqueReference);
        }

        ++imagesTraitees;
        ecartMaximal = std::max(ecartMaximal, difference.maximum);
        pixelsDifferents += static_cast<unsigned long long>(difference.pixelsDifferents);
        pixelsMasqueDifferents += static_cast<unsigned long long>(differenceMasque.pixelsDifferents);
        tempsManuel += dureeManuelle;
        tempsOpenCV += dureeOpenCV;
    }
    csv.flush();
    if (!csv) {
        throw std::runtime_error("Echec de finalisation du CSV.");
    }

    std::cout << "Images traitees : " << imagesTraitees << '/' << options.images << '\n'
              << "Ecart maximal sur V : " << ecartMaximal << " niveau(x).\n"
              << "Pixels V differents, cumules sur les images : " << pixelsDifferents << '\n'
              << "Pixels de masque differents, cumules : " << pixelsMasqueDifferents << '\n';
    if (imagesTraitees > 0) {
        std::cout << std::fixed << std::setprecision(4)
                  << "Temps moyen par appel manuel : " << tempsManuel / imagesTraitees << " ms.\n"
                  << "Temps moyen par appel OpenCV : " << tempsOpenCV / imagesTraitees << " ms.\n";
    }
    std::cout << "Un appel par fonction et par image ; mesures indicatives hors lecture,\n"
                 "copies, conversions, masques et captures. Aucun FPS global n'est mesure.\n"
              << "CSV : " << fs::absolute(cheminCsv).string() << '\n';

    // Une différence d'un niveau peut franchir un seuil et modifier le masque.
    // On rapporte donc ces pixels, sans exiger artificiellement zéro différence.
    if (imagesTraitees != options.images) {
        std::cerr << "[ECHEC] La video s'est arretee avant le nombre d'images demande.\n";
        return false;
    }
    if (imagesHorsTolerance > 0) {
        std::cerr << "[ECHEC] " << imagesHorsTolerance
                  << " image(s) depassent la tolerance d'un niveau.\n";
        return false;
    }
    std::cout << "[OK] Comparaison video dans la tolerance annoncee.\n";
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    try {
        options = lireOptions(argc, argv);
    } catch (const std::exception& erreur) {
        std::cerr << "Erreur d'arguments : " << erreur.what() << '\n'
                  << "Utiliser --help pour afficher l'aide.\n";
        return 2;
    }
    if (options.aide) {
        afficherAide(argv[0]);
        return 0;
    }

    try {
        // La vidéo n'est testée que lorsque les calculs élémentaires passent.
        if (executerTestsMathematiques() != 0) {
            return 1;
        }
        if (!options.video.empty() && !executerTestVideo(options)) {
            return 1;
        }
        return 0;
    } catch (const std::exception& erreur) {
        std::cerr << "[ECHEC] " << erreur.what() << '\n';
        return 1;
    }
}
