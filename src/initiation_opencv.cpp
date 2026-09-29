#include <iostream>
    #include <string>
    #include <opencv2/opencv.hpp>

    int main()
    {

        const std::string chemin_video = "../data/videos/clip1.mp4";


        cv::VideoCapture capture(chemin_video);


        if (capture.isOpened() == false) {
            std::cerr << "Erreur : Impossible d'ouvrir le fichier vidéo : "
                      << chemin_video << std::endl;
            return 1;
        }


        const double fps = capture.get(cv::CAP_PROP_FPS);
        int delai = 40;
        if (fps > 0) {
            delai = static_cast<int>(1000.0 / fps);
        }


        const std::string nom_fenetre = "Lecture Video Noir et Blanc (Luminance)";
        cv::namedWindow(nom_fenetre, cv::WINDOW_NORMAL);
        cv::resizeWindow(nom_fenetre, 1280, 360);


        cv::Mat frame;
        cv::Mat frame_nb;


        while (true) {

            if (capture.read(frame) == false || frame.empty() == true) {
                std::cout << "Fin de la vidéo atteinte." << std::endl;
                break;
            }


            if (frame_nb.empty() == true) {
                frame_nb.create(frame.rows, frame.cols, CV_8UC1);
            }


            const uint8_t* src = frame.data;
            uint8_t* dst = frame_nb.data;
            const int nb_pixels = frame.rows * frame.cols;


            for (int i = 0; i < nb_pixels; ++i) {
                uint8_t b = src[i * 3 + 0];
                uint8_t g = src[i * 3 + 1];
                uint8_t r = src[i * 3 + 2];

                dst[i] = static_cast<uint8_t>(0.299 * r + 0.587 * g + 0.114 * b);
            }


            cv::imshow(nom_fenetre, frame_nb);

            // Attente de 40 ms et détection de la touche quitter ('q' ou Échap)
            const char touche = static_cast<char>(cv::waitKey(delai));
            if (touche == 'q' || touche == 'Q' || touche == 27) {
                std::cout << "Lecture arrêtée par l'utilisateur." << std::endl;
                break;
            }
        }

        capture.release();
        cv::destroyAllWindows();

        return 0;
    }