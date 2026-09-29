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

    
    const std::string nom_fenetre = "Multipistage - Video Originale";
    cv::namedWindow(nom_fenetre, cv::WINDOW_NORMAL);
    cv::resizeWindow(nom_fenetre, 1280, 360);

    
    const std::string fenetre_masque = "Masque Pelouse (Blanc = Herbe)";
    cv::namedWindow(fenetre_masque, cv::WINDOW_NORMAL);
    cv::resizeWindow(fenetre_masque, 1280, 360);

    
    const cv::Scalar vert_min(42, 35, 35);
    const cv::Scalar vert_max(85, 255, 255);

    cv::Mat frame;
    cv::Mat hsv;
    cv::Mat masque_pelouse;


    while (true) {
        if (capture.read(frame) == false || frame.empty() == true) {
            std::cout << "Fin de la vidéo atteinte." << std::endl;
            break;
        }


        cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);


        cv::inRange(hsv, vert_min, vert_max, masque_pelouse);


        cv::imshow(nom_fenetre, frame);
        cv::imshow(fenetre_masque, masque_pelouse);

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
