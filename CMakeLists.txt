cmake_minimum_required(VERSION 3.10)
project(multipistage CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Détection automatique sous macOS (Homebrew) si besoin
if(APPLE AND EXISTS "/opt/homebrew/opt/opencv")
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/opencv")
endif()

# Recherche automatique d'OpenCV (fonctionne sur Mac, Windows et Linux)
find_package(OpenCV REQUIRED)

include_directories(${OpenCV_INCLUDE_DIRS} include)

# --- Cibles Vue Panoramique (conservées) ---
add_executable(multipistage src/vue_panoramique/main.cpp)
target_link_libraries(multipistage ${OpenCV_LIBS})

add_executable(initiation_opencv src/vue_panoramique/initiation_opencv.cpp)
target_link_libraries(initiation_opencv ${OpenCV_LIBS})

add_executable(detourage_terrain src/vue_panoramique/detourage_terrain.cpp)
target_link_libraries(detourage_terrain ${OpenCV_LIBS})

add_executable(detection_pieds src/vue_panoramique/detection_pieds.cpp)
target_link_libraries(detection_pieds ${OpenCV_LIBS})

add_executable(suivi_reidentification src/vue_panoramique/suivi_reidentification.cpp)
target_link_libraries(suivi_reidentification ${OpenCV_LIBS})

# --- Cibles Vue de Haut / Zénithale ---
# 6. Programme de tracking universel vue du dessus : tracking_vue_haut
add_executable(tracking_vue_haut src/vue_haut/tracking_vue_haut.cpp)
target_link_libraries(tracking_vue_haut ${OpenCV_LIBS})

# 7. Programme de test et simulation de l'IP de normalisation d'image FPGA : test_normalisation_fpga
add_executable(test_normalisation_fpga src/fpga/test_normalisation_fpga.cpp)
target_link_libraries(test_normalisation_fpga ${OpenCV_LIBS})


# ===== Calibration et homographie - Badr =====

add_executable(calibration_vue_haut
    src/calibration_vue_haut.cpp
)
target_link_libraries(calibration_vue_haut PRIVATE ${OpenCV_LIBS})

add_executable(calibration_auto_vue_haut
    src/calibration_auto_vue_haut.cpp
)
target_link_libraries(calibration_auto_vue_haut PRIVATE ${OpenCV_LIBS})

add_executable(calibration_soccertrack
    src/calibration_soccertrack.cpp
)
target_link_libraries(calibration_soccertrack PRIVATE ${OpenCV_LIBS})

add_executable(test_projection
    src/test_projection.cpp
)
target_link_libraries(test_projection PRIVATE ${OpenCV_LIBS})

add_executable(verification_keypoints
    src/verification_keypoints.cpp
)
target_link_libraries(verification_keypoints PRIVATE ${OpenCV_LIBS})