// Reconstruction de secours a partir des resultats et de la methode utilises
// dans le projet en novembre 2026. A comparer avec une sauvegarde originale si trouvee.
#include <opencv2/opencv.hpp>
#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>
#include <string>
#include <cmath>
#include <limits>
#include <algorithm>

static bool correspond(const cv::Point2f& p, float x, float y) {
    return std::abs(p.x-x)<0.02f && std::abs(p.y-y)<0.02f;
}
static bool estPointTest(const cv::Point2f& p) {
    static const std::vector<cv::Point2f> test={
        {52.5f,0.f},{52.5f,34.f},{52.5f,68.f},
        {11.f,0.f},{11.f,68.f},
        {27.5f,0.f},{27.5f,68.f},
        {77.5f,0.f},{77.5f,68.f},
        {94.f,0.f},{94.f,68.f}
    };
    for(const auto& t:test) if(correspond(p,t.x,t.y))return true;
    return false;
}
static bool lireCoordonnees(const std::string& cle,cv::Point2f& p) {
    std::string s=cle;
    for(char& c:s) if(c=='('||c==')'||c==','||c==';')c=' ';
    std::istringstream in(s);
    return static_cast<bool>(in>>p.x>>p.y);
}
static bool chargerKeypoints(const std::string& chemin,
                           std::vector<cv::Point2f>& terrain,
                           std::vector<cv::Point2f>& pixels) {
    cv::FileStorage fs(chemin,cv::FileStorage::READ);
    if(!fs.isOpened())return false;
    cv::FileNode racine=fs.root();
    for(auto it=racine.begin();it!=racine.end();++it) {
        cv::Point2f X,uv;
        if(!lireCoordonnees((*it).name(),X))continue;
        if((*it).isSeq()&&(*it).size()>=2) {
            auto el=(*it).begin();
            uv.x=static_cast<float>((double)(*el)); ++el;
            uv.y=static_cast<float>((double)(*el));
            terrain.push_back(X);
            pixels.push_back(uv);
        }
    }
    return terrain.size()==pixels.size()&&terrain.size()>=15;
}
static double evaluer(const std::vector<cv::Point2f>& terrainTest,
                     const std::vector<cv::Point2f>& pixelsCorrigesTest,
                     const cv::Mat& HversImage,
                     bool details) {
    if(HversImage.empty()||terrainTest.empty())return std::numeric_limits<double>::quiet_NaN();
    std::vector<cv::Point2f> estimations;
    cv::perspectiveTransform(pixelsCorrigesTest,estimations,HversImage.inv());
    double somme=0;
    for(size_t i=0;i<estimations.size();++i) {
        double erreur=cv::norm(estimations[i]-terrainTest[i]);
        somme+=erreur*erreur;
        if(details)std::cout<<"Attendu : "<<terrainTest[i]<<" | Estime : "
                            <<estimations[i]<<" | erreur : "<<erreur<<" m\n";
    }
    return std::sqrt(somme/estimations.size());
}
int main(int argc,char** argv) {
    std::string videoPath=argc>1?argv[1]:"data/videos/YN_VIDEO.mp4";
    std::string jsonPath=argc>2?argv[2]:"data/Calibration/117093_keypoints.json";
    cv::VideoCapture video(videoPath);
    cv::Mat frame;
    if(!video.isOpened()||!video.read(frame)) {
        std::cerr<<"Erreur : video manquante : "<<videoPath<<"\n";return 1;
    }
    std::vector<cv::Point2f> monde,pixels;
    if(!chargerKeypoints(jsonPath,monde,pixels)) {
        std::cerr<<"Erreur : JSON des keypoints manquant/invalide : "<<jsonPath<<"\n";return 1;
    }
    std::vector<cv::Point2f> mondeTrain,pxTrain,mondeTest,pxTest;
    std::vector<cv::Point3f> monde3D;
    for(size_t i=0;i<monde.size();++i) {
        if(estPointTest(monde[i])) {mondeTest.push_back(monde[i]);pxTest.push_back(pixels[i]);}
        else {mondeTrain.push_back(monde[i]);pxTrain.push_back(pixels[i]);monde3D.emplace_back(monde[i].x,monde[i].y,0.f);}
    }
    std::cout<<"Points charges : "<<monde.size()<<" | entrainement : "
             <<mondeTrain.size()<<" | validation : "<<mondeTest.size()<<"\n";
    if(mondeTrain.size()<15 || mondeTest.size()!=11) {
        std::cerr<<"Jeu de 11 points de validation non retrouve : verifier JSON/format.\n";return 1;
    }
    cv::Mat K=cv::Mat::zeros(3,3,CV_64F),D=cv::Mat::zeros(4,1,CV_64F);
    int flags=cv::fisheye::CALIB_RECOMPUTE_EXTRINSIC|
              cv::fisheye::CALIB_FIX_SKEW|cv::fisheye::CALIB_CHECK_COND|
              cv::fisheye::CALIB_FIX_K3|cv::fisheye::CALIB_FIX_K4;
    cv::TermCriteria criteria(cv::TermCriteria::COUNT|cv::TermCriteria::EPS,100,1e-6);
    double rms=0;
    std::vector<cv::Mat> rvecs,tvecs;
    try {
        rms=cv::fisheye::calibrate(std::vector<std::vector<cv::Point3f>>{monde3D},
                                  std::vector<std::vector<cv::Point2f>>{pxTrain},
                                  frame.size(),K,D,rvecs,tvecs,flags,criteria);
    }catch(const cv::Exception& e){
        std::cerr<<"Echec calibration fisheye : "<<e.what()<<"\n";return 1;
    }
    
    cv::Mat Knew;

cv::fisheye::estimateNewCameraMatrixForUndistortRectify(
    K,
    D,
    frame.size(),
    cv::Matx33d::eye(),
    Knew,
    1.0
);

    std::vector<cv::Point2f> correctedTrain,correctedTest;
    cv::fisheye::undistortPoints(pxTrain,correctedTrain,K,D,cv::Matx33d::eye(),Knew);
    cv::fisheye::undistortPoints(pxTest,correctedTest,K,D,cv::Matx33d::eye(),Knew);
    cv::Mat inliers;
    cv::Mat HterrainVersImage=cv::findHomography(mondeTrain,correctedTrain,cv::RANSAC,20.0,inliers);
    if(HterrainVersImage.empty()) {std::cerr<<"Homographie introuvable\n";return 1;}
    std::cout<<"RMS fisheye : "<<rms<<" pixels\n";
    std::cout<<"Inliers RANSAC : "<<cv::countNonZero(inliers)<<"/"<<mondeTrain.size()<<"\n";
    std::cout<<"--- TEST SUR 11 POINTS EXCLUS DE LA CALIBRATION ---\n";
    double rmse=evaluer(mondeTest,correctedTest,HterrainVersImage,true);
    std::cout<<"RMSE de validation (m) : "<<rmse<<"\n";
    cv::Mat Hsans=cv::findHomography(mondeTrain,correctedTrain,0);
    std::cout<<"RMSE sans RANSAC (m) : "
             <<evaluer(mondeTest,correctedTest,Hsans,false)<<"\n";
    cv::FileStorage fs("calibration_soccertrack.yml",cv::FileStorage::WRITE);
    if(!fs.isOpened()) {std::cerr<<"Erreur ecriture YAML\n";return 1;}
    fs<<"K"<<K<<"D"<<D<<"Knew"<<Knew;
    fs<<"H_terrain_vers_image_corrigee"<<HterrainVersImage;
    fs<<"H_image_corrigee_vers_terrain"<<HterrainVersImage.inv();
    fs<<"largeur_image"<<frame.cols<<"hauteur_image"<<frame.rows;
    fs<<"rms_pixels"<<rms;
    fs.release();
    std::cout<<"Fichier cree : calibration_soccertrack.yml\n";
    cv::Mat map1,map2,imageCorrigee;
    cv::fisheye::initUndistortRectifyMap(K,D,cv::Matx33d::eye(),Knew,
        frame.size(),CV_16SC2,map1,map2);
    cv::remap(frame,imageCorrigee,map1,map2,cv::INTER_LINEAR);
    cv::Mat apercu;
    cv::resize(imageCorrigee,apercu,cv::Size(1400,cvRound(1400.0*frame.rows/frame.cols)));
    cv::imshow("SoccerTrack - correction fisheye",apercu);
    cv::waitKey(0);
    cv::destroyAllWindows();
    return 0;
}
