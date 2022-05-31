cmake_minimum_required(VERSION 2.8)
project(ydlidar_sdk_gs2 C CXX)

#add_definitions(-std=c++11) # Use C++11
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
include_directories(include)
include_directories(src)
set(CMAKE_BUILD_TYPE Release)

IF (WIN32)
FILE(GLOB SDK_SRC 
  "src/*.cpp"
  "src/*.c"
  "src/*.h"
  "src/impl/windows/*.cpp"
  "src/impl/windows/*.h"
)
	
ELSE()
FILE(GLOB SDK_SRC 
  "src/*.cpp"
  "src/*.c"
  "src/*.h"
  "src/impl/unix/*.cpp"
  "src/impl/unix/*.h"
)

	
ENDIF()

add_subdirectory(samples)

add_library(${PROJECT_NAME} SHARED ${SDK_SRC})
IF (WIN32)
target_link_libraries(${PROJECT_NAME} setupapi Winmm)
ELSE()
target_link_libraries(${PROJECT_NAME} rt pthread)
ENDIF()                    
