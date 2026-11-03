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

include_directories(${OpenCV_INCLUDE_DIRS})

# 1. Programme principal : multipistage (où nous travaillons sur le masque)
add_executable(multipistage src/main.cpp)
target_link_libraries(multipistage ${OpenCV_LIBS})

# 2. premier programme de test conservé : initiation_opencv
add_executable(initiation_opencv src/initiation_opencv.cpp)
target_link_libraries(initiation_opencv ${OpenCV_LIBS})

# 3. Programme de détourage automatique du terrain panoramique : detourage_terrain
add_executable(detourage_terrain src/detourage_terrain.cpp)
target_link_libraries(detourage_terrain ${OpenCV_LIBS})

# 4. Programme de detection des pieds des joueurs : detection_pieds
add_executable(detection_pieds src/detection_pieds.cpp)
target_link_libraries(detection_pieds ${OpenCV_LIBS})
