// Affichage des keypoints JSON sur une video panoramique.
#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <string>
int main(int argc,char**argv){
    std::string video=argc>1?argv[1]:"data/videos/YN_VIDEO.mp4";
    std::string json=argc>2?argv[2]:"data/Calibration/117093_keypoints.json";
    cv::VideoCapture cap(video);
    cv::Mat frame;
    if(!cap.isOpened()||!cap.read(frame)){std::cerr<<"Video absente\n";return 1;}
    cv::FileStorage fs(json,cv::FileStorage::READ);
    if(!fs.isOpened()){std::cerr<<"Keypoints absents\n";return 1;}
    int total=0,dansImage=0;
    for(auto it=fs.root().begin();it!=fs.root().end();++it){
        if(!(*it).isSeq()||(*it).size()<2)continue;
        auto el=(*it).begin();double x=(double)(*el);++el;double y=(double)(*el);
        total++;
        if(x>=0&&x<frame.cols&&y>=0&&y<frame.rows){
            cv::circle(frame,cv::Point(cvRound(x),cvRound(y)),12,cv::Scalar(0,0,255),-1);
            dansImage++;
        }
    }
    std::cout<<"Points charges : "<<total<<" | Points dans l'image : "<<dansImage<<"\n";
    cv::Mat apercu;cv::resize(frame,apercu,cv::Size(1400,cvRound(1400.0*frame.rows/frame.cols)));
    cv::imshow("Verification keypoints",apercu);cv::waitKey(0);
    return 0;
}
