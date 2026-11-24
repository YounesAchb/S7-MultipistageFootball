#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

constexpr double LONGUEUR = 105.0;
constexpr double LARGEUR = 68.0;
constexpr int ECHELLE = 8;
constexpr int MARGE = 20;

struct Etat {
    cv::Mat imageOriginale, imageAffichee, H;
    std::vector<cv::Point2f> coinsImage;
    std::vector<cv::Point> clicsAffiches;
    double facteurX = 1.0, facteurY = 1.0;
    bool pointTestValide = false;
    cv::Point2f dernierTerrain;
    cv::Point dernierClic;
};
cv::Point surCarte(double X,double Y) {
    return cv::Point(MARGE+cvRound(X*ECHELLE),MARGE+cvRound(Y*ECHELLE));
}
cv::Mat dessinerTerrain() {
    cv::Mat carte(cvRound(LARGEUR*ECHELLE)+2*MARGE,
                  cvRound(LONGUEUR*ECHELLE)+2*MARGE,CV_8UC3,cv::Scalar(55,125,55));
    const cv::Scalar blanc(255,255,255);
    cv::rectangle(carte,surCarte(0,0),surCarte(LONGUEUR,LARGEUR),blanc,2);
    cv::line(carte,surCarte(52.5,0),surCarte(52.5,68),blanc,2);
    cv::circle(carte,surCarte(52.5,34),cvRound(9.15*ECHELLE),blanc,2);
    cv::rectangle(carte,surCarte(0,13.84),surCarte(16.5,54.16),blanc,2);
    cv::rectangle(carte,surCarte(88.5,13.84),surCarte(105,54.16),blanc,2);
    return carte;
}
void afficherImage(Etat& etat) {
    cv::Mat image=etat.imageAffichee.clone();
    for(size_t i=0;i<etat.clicsAffiches.size();++i) {
        cv::circle(image,etat.clicsAffiches[i],6,cv::Scalar(0,255,255),-1);
        cv::putText(image,std::to_string(i+1),etat.clicsAffiches[i]+cv::Point(8,-8),
                    cv::FONT_HERSHEY_SIMPLEX,0.7,cv::Scalar(0,255,255),2);
    }
    if(etat.pointTestValide)
        cv::circle(image,etat.dernierClic,6,cv::Scalar(0,0,255),-1);
    cv::imshow("Calibration vue de haut",image);
}
void clicSouris(int evenement,int x,int y,int,void* donnees) {
    if(evenement!=cv::EVENT_LBUTTONDOWN)return;
    Etat& etat=*static_cast<Etat*>(donnees);
    cv::Point2f pixel(static_cast<float>(x*etat.facteurX),
                      static_cast<float>(y*etat.facteurY));
    if(etat.coinsImage.size()<4) {
        etat.coinsImage.push_back(pixel);
        etat.clicsAffiches.emplace_back(x,y);
        std::cout<<"Coin "<<etat.coinsImage.size()<<" : "<<pixel<<"\n";
        if(etat.coinsImage.size()==4) {
            std::vector<cv::Point2f> terrain={{0,0},{105,0},{105,68},{0,68}};
            etat.H=cv::getPerspectiveTransform(etat.coinsImage,terrain);
            cv::FileStorage fs("calibration_vue_haut.yml",cv::FileStorage::WRITE);
            fs<<"H_image_vers_terrain"<<etat.H;
            fs<<"H_terrain_vers_image"<<etat.H.inv();
            fs<<"longueur_terrain"<<LONGUEUR;
            fs<<"largeur_terrain"<<LARGEUR;
            fs<<"largeur_image"<<etat.imageOriginale.cols;
            fs<<"hauteur_image"<<etat.imageOriginale.rows;
            fs.release();
            std::cout<<"\nHomographie calculee !\nFichier : calibration_vue_haut.yml\n"
                       "Clique maintenant sur un point du terrain.\n";
            cv::imshow("Mini-carte vue de haut",dessinerTerrain());
        }
    } else {
        std::vector<cv::Point2f> entree={pixel},sortie;
        cv::perspectiveTransform(entree,sortie,etat.H);
        if(sortie.empty()||!std::isfinite(sortie[0].x)||!std::isfinite(sortie[0].y)) {
            std::cout<<"Projection invalide\n";return;
        }
        etat.dernierTerrain=sortie[0]; etat.dernierClic=cv::Point(x,y);
        etat.pointTestValide=true;
        std::cout<<"\nPixel : "<<pixel<<"\nPosition terrain : X = "
                 <<sortie[0].x<<" m, Y = "<<sortie[0].y<<" m\n";
        cv::Mat carte=dessinerTerrain();
        if(sortie[0].x>=0&&sortie[0].x<=LONGUEUR&&sortie[0].y>=0&&sortie[0].y<=LARGEUR)
            cv::circle(carte,surCarte(sortie[0].x,sortie[0].y),8,cv::Scalar(0,0,255),-1);
        else std::cout<<"Point hors terrain\n";
        cv::imshow("Mini-carte vue de haut",carte);
    }
    afficherImage(etat);
}
void testerErreur(const Etat& etat,cv::Point2f attendu,const std::string& nom) {
    if(!etat.pointTestValide) {
        std::cout<<"Clique d'abord sur le point a tester.\n";return;
    }
    double erreur=cv::norm(etat.dernierTerrain-attendu);
    std::cout<<"\nTEST : "<<nom<<"\nAttendu : "<<attendu
             <<"\nCalcule : "<<etat.dernierTerrain
             <<"\nErreur : "<<erreur<<" m\n";
}
int main(){
    Etat etat;
    cv::VideoCapture video("data/videos/side_match.mp4");
    if(!video.isOpened()){std::cerr<<"Impossible d'ouvrir side_match.mp4\n";return 1;}
    if(!video.read(etat.imageOriginale)){std::cerr<<"Impossible de lire la premiere image\n";return 1;}
    int largeurAffichage=std::min(1200,etat.imageOriginale.cols);
    int hauteurAffichage=cvRound(etat.imageOriginale.rows*largeurAffichage/
                                static_cast<double>(etat.imageOriginale.cols));
    cv::resize(etat.imageOriginale,etat.imageAffichee,cv::Size(largeurAffichage,hauteurAffichage));
    etat.facteurX=static_cast<double>(etat.imageOriginale.cols)/etat.imageAffichee.cols;
    etat.facteurY=static_cast<double>(etat.imageOriginale.rows)/etat.imageAffichee.rows;
    cv::namedWindow("Calibration vue de haut");
    cv::namedWindow("Mini-carte vue de haut");
    cv::setMouseCallback("Calibration vue de haut",clicSouris,&etat);
    afficherImage(etat);
    cv::imshow("Mini-carte vue de haut",dessinerTerrain());
    std::cout<<"Selectionne les coins dans cet ordre :\n"
                "1. Haut gauche\n2. Haut droite\n3. Bas droite\n4. Bas gauche\n\n"
                "Apres calibration :\nC : tester le centre du terrain\n"
                "G : tester le point de penalty gauche\n"
                "D : tester le point de penalty droit\n"
                "R : recommencer la calibration\nEchap : quitter\n";
    while(true){
        int touche=cv::waitKey(30);
        if(touche==27)break;
        if(touche=='c'||touche=='C')testerErreur(etat,{52.5f,34.f},"Centre");
        if(touche=='g'||touche=='G')testerErreur(etat,{11.f,34.f},"Penalty gauche");
        if(touche=='d'||touche=='D')testerErreur(etat,{94.f,34.f},"Penalty droit");
        if(touche=='r'||touche=='R'){
            etat.coinsImage.clear();etat.clicsAffiches.clear();etat.H.release();
            etat.pointTestValide=false;
            afficherImage(etat);
            cv::imshow("Mini-carte vue de haut",dessinerTerrain());
            std::cout<<"\nCalibration reinitialisee.\n";
        }
    }
    cv::destroyAllWindows();
    return 0;
}
