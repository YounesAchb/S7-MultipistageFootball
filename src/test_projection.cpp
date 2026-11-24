#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>
#include <cmath>
#include "projection_terrain.hpp"

constexpr int ECHELLE = 8;
constexpr int MARGE = 20;
struct Contexte {
    ProjectionTerrain projection;
    cv::Mat imageOriginale, imageAffichee;
};
cv::Point surCarte(float X, float Y) {
    return cv::Point(MARGE + cvRound(X*ECHELLE), MARGE + cvRound(Y*ECHELLE));
}
cv::Mat dessinerTerrain() {
    cv::Mat carte(68*ECHELLE+2*MARGE,105*ECHELLE+2*MARGE,CV_8UC3,cv::Scalar(55,125,55));
    const cv::Scalar blanc(255,255,255);
    cv::rectangle(carte,surCarte(0,0),surCarte(105,68),blanc,2);
    cv::line(carte,surCarte(52.5f,0),surCarte(52.5f,68),blanc,2);
    cv::circle(carte,surCarte(52.5f,34),cvRound(9.15*ECHELLE),blanc,2);
    cv::rectangle(carte,surCarte(0,13.84f),surCarte(16.5f,54.16f),blanc,2);
    cv::rectangle(carte,surCarte(88.5f,13.84f),surCarte(105,54.16f),blanc,2);
    return carte;
}
void clicSouris(int evenement,int x,int y,int,void* donnees) {
    if (evenement != cv::EVENT_LBUTTONDOWN) return;
    Contexte* contexte=static_cast<Contexte*>(donnees);
    float facteurX=static_cast<float>(contexte->imageOriginale.cols)/contexte->imageAffichee.cols;
    float facteurY=static_cast<float>(contexte->imageOriginale.rows)/contexte->imageAffichee.rows;
    cv::Point2f pixel(x*facteurX,y*facteurY),positionTerrain;
    if (!contexte->projection.projeter(pixel,positionTerrain)) {
        std::cout<<"Projection impossible\n"; return;
    }
    std::cout<<"\nPixel : "<<pixel<<"\nPosition terrain : X = "
             <<positionTerrain.x<<" m, Y = "<<positionTerrain.y<<" m\n";
    cv::Mat image=contexte->imageAffichee.clone();
    cv::circle(image,cv::Point(x,y),6,cv::Scalar(0,0,255),-1);
    cv::imshow("Video panoramique",image);
    cv::Mat carte=dessinerTerrain();
    if(positionTerrain.x>=0 && positionTerrain.x<=105 && positionTerrain.y>=0 && positionTerrain.y<=68) {
        cv::circle(carte,surCarte(positionTerrain.x,positionTerrain.y),8,cv::Scalar(0,0,255),-1);
    }else std::cout<<"Attention : point hors du terrain\n";
    cv::imshow("Mini-carte",carte);
}
int main() {
    Contexte contexte;
    if(!contexte.projection.charger("calibration_soccertrack.yml")) {
        std::cerr<<"Erreur chargement calibration\n"; return 1;
    }
    cv::VideoCapture video("data/videos/YN_VIDEO.mp4");
    if(!video.isOpened()) {std::cerr<<"Erreur : video introuvable\n";return 1;}
    if(!video.read(contexte.imageOriginale)) {std::cerr<<"Impossible de lire l'image\n";return 1;}
    if(contexte.imageOriginale.cols!=4096 || contexte.imageOriginale.rows!=1080) {
        std::cerr<<"Dimensions differentes de la calibration 4096 x 1080\n";return 1;
    }
    int largeurAffichage=1400;
    int hauteurAffichage=cvRound(contexte.imageOriginale.rows*largeurAffichage/
                                static_cast<double>(contexte.imageOriginale.cols));
    cv::resize(contexte.imageOriginale,contexte.imageAffichee,
               cv::Size(largeurAffichage,hauteurAffichage));
    cv::namedWindow("Video panoramique",cv::WINDOW_AUTOSIZE);
    cv::namedWindow("Mini-carte",cv::WINDOW_AUTOSIZE);
    cv::setMouseCallback("Video panoramique",clicSouris,&contexte);
    cv::imshow("Video panoramique",contexte.imageAffichee);
    cv::imshow("Mini-carte",dessinerTerrain());
    std::cout<<"Clique sur les pieds d'un joueur. Echap ou Q pour quitter.\n";
    while(true){int touche=cv::waitKey(30);if(touche==27||touche=='q'||touche=='Q')break;}
    cv::destroyAllWindows();
    return 0;
}
