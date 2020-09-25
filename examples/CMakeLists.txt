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
	add_executable(lammps_example lammps_example.cpp)

	find_path(LAMMPS_INCLUDE_DIR lammps.h
		PATHS ${LAMMPS_DIR})

	find_library(LAMMPS_LIBRARY lammps_omp
		PATHS ${LAMMPS_DIR})

	if (NOT LAMMPS_LIBRARY)
		message(WARNING "OpenMP LAMMPS library not found, searching for MPI LAMMPS")
		find_library(LAMMPS_LIBRARY lammps_mpi
			HINTS ${LAMMPS_DIR})
	else()
		find_package(OpenMP REQUIRED)

		target_link_libraries(lammps_example PRIVATE
			OpenMP::OpenMP_CXX)
	endif()

	if (NOT LAMMPS_INCLUDE_DIR)
		message(FATAL_ERROR "To build LAMMPS driver you must set -DLAMMPS_DIR=<root of lammps dir>")
	endif()

	if (NOT LAMMPS_LIBRARY)
		message(FATAL_ERROR "To build LAMMPS driver you must have lammps built with MPI or OMP")
	endif()

	target_link_libraries(lammps_example PRIVATE
		is_sim
		${MPI_CXX_LIBRARIES}
		${LAMMPS_LIBRARY})

	target_include_directories(lammps_example PRIVATE
		$<BUILD_INTERFACE:${MPI_CXX_INCLUDE_PATH}>
		$<BUILD_INTERFACE:${LAMMPS_INCLUDE_DIR}>)
endif()

