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
find_package(libIS REQUIRED)
find_package(MPI REQUIRED)

option(BUILD_LAMMPS_EXAMPLE "Build the LAMMPS example" OFF)

add_executable(example_sim example_sim.cpp)
target_link_libraries(example_sim PRIVATE is_sim ${MPI_CXX_LIBRARIES})
target_include_directories(example_sim PRIVATE
	$<BUILD_INTERFACE:${MPI_CXX_INCLUDE_PATH}>)

add_executable(example_client example_client.cpp)
target_link_libraries(example_client PRIVATE is_client ${MPI_CXX_LIBRARIES})
target_include_directories(example_client PRIVATE
	$<BUILD_INTERFACE:${MPI_CXX_INCLUDE_PATH}>)

if (BUILD_LAMMPS_EXAMPLE)
	if (NOT LAMMPS_DIR)
		message(FATAL_ERROR "To build LAMMPS driver you must set -DLAMMPS_DIR=<root of lammps dir>")
	endif()

	include_directories(${LAMMPS_DIR}/src)

	add_executable(lammps_example lammps_example.cpp)
	target_link_libraries(lammps_example PRIVATE
		is_sim
		${MPI_CXX_LIBRARIES}
		${LAMMPS_DIR}/src/liblammps_mpi.a)
	target_include_directories(lammps_example PRIVATE
		$<BUILD_INTERFACE:${MPI_CXX_INCLUDE_PATH}>
		$<BUILD_INTERFACE:${LAMMPS_DIR}/src>)
endif()

