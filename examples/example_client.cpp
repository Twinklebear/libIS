#include <chrono>
#include <iostream>
#include <cstring>
#include <thread>
#include <random>
#include <vector>
#include <mpi.h>
#include "libIS/is_client.h"

/* The example client connects to the simulation and
 * prints out the meta-data about its particles, fields
 * and bounds, then disconnects.
 */

int main(int ac, char **av) {
	MPI_Init(&ac, &av);

	int rank = 0;
	int world_size = 0;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &world_size);

	std::cout << "#client rank " << rank << "/" << world_size << "\n";

	std::string server;
	int port = -1;
	for (int i = 1; i < ac; ++i) {
		if (std::strcmp(av[i], "-server") == 0) {
			server = av[++i];
		} else if (std::strcmp(av[i], "-port") == 0) {
			port = std::atoi(av[++i]);
		}
	}
	if (server.empty() || port < 0) {
		std::cerr << "Usage: " << av[0] << " -server <server> -port <port>\n";
		return 1;
	}

	// Connect to the simulation
	is::client::connect(server, port, MPI_COMM_WORLD);

	for (int j = 0; j < 10; ++j) {
		// Query the data from the simulation
		// Some fake load balancing based on rank
		auto regions = is::client::query(rank == 0 ? 1.f : 4.f);

		for (int i = 0; i < world_size; ++i) {
			if (rank == i) {
				std::cout << "Rank " << rank << " has " << regions.size() << " regions\n";
				// For each region we received, print out its data
				for (const auto &r : regions) {
					std::cout << "region has " << r.particles.numParticles
						<< " particles and " << r.fields.size() << " fields\n";
					std::cout << "Fields: {";
					for (const auto &f : r.fields) {
						std::cout << "(" << f.first << ", ";
						if (f.second.dataType == UINT8) {
							std::cout << "uint8";
						} else if (f.second.dataType == FLOAT) {
							std::cout << "float";
						} else if (f.second.dataType == DOUBLE) {
							std::cout << "double";
						} else {
							std::cout << "INVALID!";
						}
						std::cout << ", [" << f.second.dims[0] << ", "
							<< f.second.dims[1] << ", " << f.second.dims[2]
							<< "]), ";
					}
					std::cout << "}\n";
				}
				std::cout << "--------" << std::endl;
			}
			MPI_Barrier(MPI_COMM_WORLD);
		}
	}

	is::client::disconnect();
	MPI_Finalize();
	return 0;
}

