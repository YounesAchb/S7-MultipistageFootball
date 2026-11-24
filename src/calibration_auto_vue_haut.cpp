// Reconstruction du prototype experimental de detection automatique.
// Attention : selection par zones/seuils; les faux coins restent possibles.
// Toujours valider visuellement et par points independants avant utilisation.
#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <string>

bool intersection(const cv::Vec4i& a,const cv::Vec4i& b,cv::Point2f& p) {
    cv::Point2f A(a[0],a[1]),B(a[2],a[3]),C(b[0],b[1]),D(b[2],b[3]);
    cv::Point2f r=B-A,s=D-C;
    double det=r.x*s.y-r.y*s.x;
    if(std::abs(det)<1e-6)return false;
    cv::Point2f q=C-A;
    double t=(q.x*s.y-q.y*s.x)/det;
    p=A+static_cast<float>(t)*r;
    return true;
}
int main(int argc,char**argv){
    std::string cheminVideo=argc>1?argv[1]:"data/videos/side_match.mp4";
    cv::VideoCapture video(cheminVideo);
    cv::Mat original;
    if(!video.isOpened()||!video.read(original)){
        std::cerr<<"Impossible de lire la video\n";return 1;
    }
    int largeurTravail=std::min(1200,original.cols);
    double ratio=static_cast<double>(largeurTravail)/original.cols;
    cv::Mat image;
    cv::resize(original,image,cv::Size(),ratio,ratio);
    int W=image.cols,H=image.rows;
    cv::Mat hsv;
    cv::cvtColor(image,hsv,cv::COLOR_BGR2HSV);
    cv::Mat vert,blanc,voisinageVert,lignes;
    cv::inRange(hsv,cv::Scalar(32,30,25),cv::Scalar(93,255,255),vert);
    cv::inRange(hsv,cv::Scalar(0,0,165),cv::Scalar(179,55,255),blanc);
    cv::dilate(vert,voisinageVert,
        cv::getStructuringElement(cv::MORPH_RECT,cv::Size(11,11)));
    cv::bitwise_and(blanc,voisinageVert,lignes);
    cv::Mat horizontales,verticales;
    cv::morphologyEx(lignes,horizontales,cv::MORPH_CLOSE,
        cv::getStructuringElement(cv::MORPH_RECT,cv::Size(25,3)));
    cv::morphologyEx(lignes,verticales,cv::MORPH_CLOSE,
        cv::getStructuringElement(cv::MORPH_RECT,cv::Size(3,25)));
    cv::bitwise_or(horizontales,verticales,lignes);
    std::vector<cv::Vec4i> segments;
    cv::HoughLinesP(lignes,segments,1,CV_PI/180,45,65,35);
    std::array<cv::Vec4i,4> bords{};
    std::array<double,4> scores={-1,-1,-1,-1};
    cv::Mat visualisation=image.clone();
    for(const auto& s:segments){
        double dx=s[2]-s[0],dy=s[3]-s[1];
        double longueur=std::hypot(dx,dy);
        double milieuX=(s[0]+s[2])/2.0, milieuY=(s[1]+s[3])/2.0;
        int categorie=-1;
        bool horizontale=std::abs(dy)<0.035*std::abs(dx);
        bool verticale=std::abs(dx)<0.05*std::abs(dy);
        if(horizontale&&longueur>0.22*W){
            if(milieuY>0.04*H&&milieuY<0.18*H)categorie=0;
            else if(milieuY>0.82*H&&milieuY<0.98*H)categorie=2;
        } else if(verticale&&longueur>0.22*H){
            if(milieuX>0.11*W&&milieuX<0.19*W)categorie=3;
            else if(milieuX>0.70*W&&milieuX<0.92*W)categorie=1;
        }
        if(categorie>=0){
            if(longueur>scores[categorie]){scores[categorie]=longueur;bords[categorie]=s;}
            cv::line(visualisation,cv::Point(s[0],s[1]),cv::Point(s[2],s[3]),
                     cv::Scalar(255,255,0),2);
        }
    }
    const char* noms[4]={"Haut","Droite","Bas","Gauche"};
    std::cout<<"\n===== DETECTION DES BORDS =====\n";
    for(int i=0;i<4;++i){
        std::cout<<noms[i]<<" : ";
        if(scores[i]>=0){
            const auto& s=bords[i];
            std::cout<<"Trouve (longueur = "<<scores[i]<<" px)\n";
            cv::line(visualisation,cv::Point(s[0],s[1]),cv::Point(s[2],s[3]),
                     cv::Scalar(0,255,255),3);
        }else std::cout<<"NON TROUVE\n";
    }
    cv::imshow("Segments detectes",visualisation);
    for(double score:scores){
        if(score<0){std::cerr<<"Detection automatique incomplete. Utiliser la calibration manuelle.\n";
            cv::waitKey(0);return 1;}
    }
    std::vector<cv::Point2f> coins(4);
    bool valide=intersection(bords[0],bords[3],coins[0])&&
                intersection(bords[0],bords[1],coins[1])&&
                intersection(bords[2],bords[1],coins[2])&&
                intersection(bords[2],bords[3],coins[3]);
    if(!valide){std::cerr<<"Intersections impossibles\n";cv::waitKey(0);return 1;}
    double surface=std::abs(cv::contourArea(coins));
    valide=cv::isContourConvex(coins)&&surface>0.35*W*H;
    for(const auto& p:coins){
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||p.x<-.05*W||p.x>1.05*W||
           p.y<-.05*H||p.y>1.05*H)valide=false;
    }
    if(!valide){std::cerr<<"Geometrie suspecte : calibration refusee\n";
        cv::waitKey(0);return 1;}
    cv::Mat resultat=image.clone();
    for(int i=0;i<4;++i){
        cv::line(resultat,cv::Point(cvRound(coins[i].x),cvRound(coins[i].y)),
                 cv::Point(cvRound(coins[(i+1)%4].x),cvRound(coins[(i+1)%4].y)),
                 cv::Scalar(0,255,255),3);
        cv::circle(resultat,coins[i],7,cv::Scalar(0,0,255),-1);
        cv::putText(resultat,std::to_string(i+1),coins[i]+cv::Point2f(9,-9),
                    cv::FONT_HERSHEY_SIMPLEX,.8,cv::Scalar(0,0,255),2);
    }
    cv::imshow("Coins automatiques",resultat);
    std::cout<<"Coins estimes. S=valider/sauvegarder; autre touche=refuser.\n";
    int touche=cv::waitKey(0);
    if(touche!='s'&&touche!='S'){
        std::cout<<"Calibration non sauvegardee.\n";return 0;
    }
    for(auto& p:coins){p.x/=static_cast<float>(ratio);p.y/=static_cast<float>(ratio);}
    std::vector<cv::Point2f> terrain={{0.f,0.f},{105.f,0.f},{105.f,68.f},{0.f,68.f}};
    cv::Mat homographie=cv::getPerspectiveTransform(coins,terrain);
    size_t debut=cheminVideo.find_last_of("/\\");
    std::string nom=cheminVideo.substr(debut==std::string::npos?0:debut+1);
    size_t extension=nom.find_last_of('.');
    if(extension!=std::string::npos)nom.erase(extension);
    std::string sortie="calibration_"+nom+"_auto.yml";
    cv::FileStorage fs(sortie,cv::FileStorage::WRITE);
    if(!fs.isOpened())return 1;
    fs<<"H_image_vers_terrain"<<homographie;
    fs<<"H_terrain_vers_image"<<homographie.inv();
    fs<<"longueur_terrain"<<105.;fs<<"largeur_terrain"<<68.;
    fs<<"largeur_image"<<original.cols;fs<<"hauteur_image"<<original.rows;
    fs.release();
    std::cout<<"Sauvegarde : "<<sortie<<"\n";
    cv::destroyAllWindows();return 0;
}
