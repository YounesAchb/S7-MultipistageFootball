#pragma once
#include <opencv2/opencv.hpp>
#include <cmath>
#include <string>
#include <vector>

// Reconstruction du module de projection panoramique utilisé en novembre 2026.
class ProjectionTerrain {
private:
    cv::Mat K, D, Knew, H_imageVersTerrain;
    int largeur = 0, hauteur = 0;
public:
    bool charger(const std::string& chemin) {
        cv::FileStorage fs(chemin, cv::FileStorage::READ);
        if (!fs.isOpened()) return false;
        fs["K"] >> K;
        fs["D"] >> D;
        fs["Knew"] >> Knew;
        fs["H_image_corrigee_vers_terrain"] >> H_imageVersTerrain;
        fs["largeur_image"] >> largeur;
        fs["hauteur_image"] >> hauteur;
        return !K.empty() && !D.empty() && !Knew.empty() &&
               !H_imageVersTerrain.empty() && largeur > 0 && hauteur > 0;
    }
    bool projeter(const cv::Point2f& pixel, cv::Point2f& positionTerrain) const {
        if (K.empty() || H_imageVersTerrain.empty()) return false;
        if (pixel.x < 0 || pixel.x >= largeur || pixel.y < 0 || pixel.y >= hauteur) return false;
        std::vector<cv::Point2f> original = {pixel}, corrige, terrain;
        cv::fisheye::undistortPoints(original, corrige, K, D, cv::Matx33d::eye(), Knew);
        cv::perspectiveTransform(corrige, terrain, H_imageVersTerrain);
        if (terrain.empty()) return false;
        positionTerrain = terrain[0];
        return std::isfinite(positionTerrain.x) && std::isfinite(positionTerrain.y);
    }
};
