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
# Module commun : les deux programmes utilisent exactement les memes fonctions.
add_library(traitements_image STATIC src/traitements_image.cpp)
target_include_directories(traitements_image PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(traitements_image PUBLIC ${OpenCV_LIBS})

add_executable(detourage_terrain src/detourage_terrain.cpp)
target_link_libraries(detourage_terrain PRIVATE traitements_image)

# Programme de verification mathematique, sans fenetres ni video par defaut.
add_executable(test_traitements src/test_traitements.cpp)
target_link_libraries(test_traitements PRIVATE traitements_image)

enable_testing()
add_test(NAME traitements_mathematiques COMMAND test_traitements)

# Les commentaires et textes francais sont en UTF-8 sous Visual Studio.
if(MSVC)
    foreach(cible traitements_image detourage_terrain test_traitements)
        target_compile_options(${cible} PRIVATE /EHsc /utf-8)
    endforeach()
endif()


