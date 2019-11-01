cmake_minimum_required(VERSION 2.8)

project(p3dv)

SET(CMAKE_CXX_FLAGS "-O3 -std=c++11 -w -fopenmp -pthread")
SET(CMAKE_C_FLAGS "-O3 -std=c++11 -fopenmp -pthread")

#SET(CMAKE_BUILD_TYPE "Debug") 
#SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g -ggdb")
SET(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall")

SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
SET(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

#list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

#GLOG 
#find_package(glog REQUIRED)

#GFLAGS
#Require the FindGflags.cmake (reference: ceres_solver) in the /cmake folder
#find_package(Gflags)

#PCL
FIND_PACKAGE(PCL REQUIRED)
INCLUDE_DIRECTORIES(${PCL_INCLUDE_DIRS})
LINK_DIRECTORIES(${PCL_LIBRARY_DIRS})
#ADD_DEFINITIONS(${PCL_DEFINITIONS})
LIST(REMOVE_ITEM PCL_LIBRARIES "vtkproj4")

#OpenCV
FIND_PACKAGE(OpenCV REQUIRED)
INCLUDE_DIRECTORIES(${OpenCV_INCLUDE_DIRS})

# head
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/include)
AUX_SOURCE_DIRECTORY(${PROJECT_SOURCE_DIR}/src SRC_LIST)
message("SRC_LIST is " ${SRC_LIST})

#SET(DEP_LIBS ${DEP_LIBS} ${OpenCV_LIBS} /usr/local/lib/libproj.so glog::glog ${GFLAGS_LIBRARIES})
SET(DEP_LIBS ${DEP_LIBS} ${OpenCV_LIBS})

# SFM test
add_executable(sfm ${PROJECT_SOURCE_DIR}/src/sfm.cpp ${SRC_LIST})
target_link_libraries(sfm ${DEP_LIBS})

# add_executable(lo_test_apollo ${PROJECT_SOURCE_DIR}/test/lidar_odometry_test_apollo.cpp ${SRC_LIST})
# target_link_libraries(lo_test_apollo ${DEP_LIBS})