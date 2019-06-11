## ======================================================================== ##
## Copyright 2018 Intel Corporation                                         ##
##                                                                          ##
## Licensed under the Apache License, Version 2.0 (the "License");          ##
## you may not use this file except in compliance with the License.         ##
## You may obtain a copy of the License at                                  ##
##                                                                          ##
##     http://www.apache.org/licenses/LICENSE-2.0                           ##
##                                                                          ##
## Unless required by applicable law or agreed to in writing, software      ##
## distributed under the License is distributed on an "AS IS" BASIS,        ##
## WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. ##
## See the License for the specific language governing permissions and      ##
## limitations under the License.                                           ##
## ======================================================================== ##

cmake_minimum_required(VERSION 3.4)
project(libIS)
include(CMakePackageConfigHelpers)

find_package(Threads REQUIRED)
find_package(MPI REQUIRED)

include_directories(${MPI_CXX_INCLUDE_PATH})
add_library(is_sim SHARED is_sim.cpp is_common.cpp is_simstate.cpp is_buffering.cpp intercomm.cpp)
target_link_libraries(is_sim PUBLIC ${MPI_CXX_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
target_include_directories(is_sim PUBLIC
	$<BUILD_INTERFACE:${libIS_BINARY_DIR}>
	$<BUILD_INTERFACE:${libIS_SOURCE_DIR}>
	$<BUILD_INTERFACE:${MPI_CXX_INCLUDE_PATH}>
	$<INSTALL_INTERFACE:${MPI_CXX_INCLUDE_PATH}>
	$<INSTALL_INTERFACE:include>
)

add_library(is_client SHARED is_client.cpp is_common.cpp is_simstate.cpp is_buffering.cpp intercomm.cpp)
target_link_libraries(is_client PUBLIC ${MPI_CXX_LIBRARIES})
target_include_directories(is_client PUBLIC
	$<BUILD_INTERFACE:${libIS_BINARY_DIR}>
	$<BUILD_INTERFACE:${libIS_SOURCE_DIR}>
	$<BUILD_INTERFACE:${MPI_CXX_INCLUDE_PATH}>
	$<INSTALL_INTERFACE:${MPI_CXX_INCLUDE_PATH}>
	$<INSTALL_INTERFACE:include>
)

add_executable(grid_util grid_util.cpp)

set_target_properties(is_sim is_client
	PROPERTIES
	CXX_STANDARD 11
	CXX_STANDARD_REQUIRED ON)

install(TARGETS is_sim is_client EXPORT libISConfig
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib
	RUNTIME DESTINATION bin
	INCLUDES DESTINATION include
)
install(FILES is_common.h is_client.h is_sim.h is_buffering.h is_simstate.h vec.h
	DESTINATION include/libIS
)
install(EXPORT libISConfig
	DESTINATION lib/cmake/libIS
)

