// ======================================================================== //
// Copyright 2018 Intel Corporation                                         //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#include <memory>
#include <fstream>
#include <thread>
#include <chrono>
#include <limits>
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <stdlib.h>
#include <mpi.h>
#include "is_render.h"
#include "is_command.h"

namespace is {
namespace render {

static bool LIBIS_LOGGING = false;
static std::string LIBIS_LOG_OUTPUT;
static void check_logging_wanted() {
	const char *log_var = getenv("LIBIS_LOG_OUTPUT");
	if (log_var) {
		LIBIS_LOG_OUTPUT = log_var;
		LIBIS_LOGGING = true;
	}
}

struct SimulationConnection {
	MPI_Comm simComm, ownComm;
	int simSize;
	int rank, size, clientIntraCommRoot;
	int simSocket, simPort;
	std::string myPortName, simServer;

	std::shared_ptr<std::ofstream> log;

	SimulationConnection(MPI_Comm com, const std::string &simServer,
			const int port);
	SimulationConnection(MPI_Comm com, MPI_Comm sim);
	~SimulationConnection();
	std::vector<SimState> query();

private:
	void connectSim();
	void sendCommand(const COMMAND cmd);
	void disconnect();
};
SimulationConnection::SimulationConnection(MPI_Comm com, const std::string &simServer,
		const int port)
	: simComm(MPI_COMM_NULL),
	ownComm(MPI_COMM_NULL),
	simSize(-1),
	rank(-1),
	size(-1),
	clientIntraCommRoot(0),
	simSocket(-1),
	simPort(port),
	simServer(simServer)
{
	MPI_Comm_dup(com, &ownComm);
	MPI_Comm_size(ownComm, &size);
	MPI_Comm_rank(ownComm, &rank);
	check_logging_wanted();
	if (LIBIS_LOGGING) {
		log = std::make_shared<std::ofstream>(LIBIS_LOG_OUTPUT.c_str());
	}
	connectSim();
}
SimulationConnection::SimulationConnection(MPI_Comm com, MPI_Comm sim)
	: simComm(MPI_COMM_NULL),
	ownComm(MPI_COMM_NULL),
	simSize(-1),
	rank(-1),
	size(-1),
	clientIntraCommRoot(0),
	simSocket(-1),
	simPort(std::numeric_limits<uint16_t>::max())
{
	MPI_Comm_dup(com, &ownComm);
	MPI_Comm_size(ownComm, &size);
	MPI_Comm_rank(ownComm, &rank);
	check_logging_wanted();
	if (LIBIS_LOGGING) {
		log = std::make_shared<std::ofstream>(LIBIS_LOG_OUTPUT.c_str());
	}

	simComm = sim;
	int isInterComm = 0;
	MPI_Comm_test_inter(simComm, &isInterComm);
	if (isInterComm) {
		MPI_Comm_remote_size(simComm, &simSize);
	} else {
		MPI_Comm_size(simComm, &simSize);
		if (rank == 0) {
			MPI_Comm_rank(simComm, &clientIntraCommRoot);
		}
		MPI_Bcast(&clientIntraCommRoot, 1, MPI_INT, 0, ownComm);
	}
}
SimulationConnection::~SimulationConnection() {
	disconnect();
	MPI_Comm_free(&ownComm);
}
std::vector<SimState> SimulationConnection::query() {
	sendCommand(QUERY);

	int correctedSimSize = simSize;
	// TODO: This is assuming the use existing comm mode is running in mpi
	// multi-program launch mode.
	if (clientIntraCommRoot != 0) {
		correctedSimSize = clientIntraCommRoot;
	}
	const int simsPerClient = correctedSimSize / size;
	int mySims = simsPerClient;
	int extraOffset = 0;
	if (correctedSimSize % size != 0) {
		extraOffset = std::min(rank, (correctedSimSize % size));
		if ((correctedSimSize % size) - rank > 0) {
			mySims += 1;
		}
	}

	// TODO: Logging should dump to individual files for each rank

	// Currently we assume an M:N mapping of sim ranks to client ranks,
	// where M >= N. As such each simulation's data is assigned to the
	// corresponding rank # on the client side. So client 0 gets data
	// from sim rank 0, and so on. In the case of M > N each client will
	// recv M/N regions, merging these regions is left as an optional operation
	// for the user.
	std::vector<SimState> regions(mySims, SimState());
	using namespace std::chrono;
	size_t bytes_transferred = 0;
	size_t transfer_time = 0;
	for (size_t i = 0; i < regions.size(); ++i) {
		const int regionId = i + simsPerClient * rank + extraOffset;
		SimState &r = regions[i];

		auto start_transfer = high_resolution_clock::now();
		// Send over a ping the the simulation rank to have it send us the data
		MPI_Send(&rank, 1, MPI_INT, regionId, 4503, simComm);

		// Recieve the header telling us about the simulation state
		SimStateHeader header;
		MPI_Recv(&header, sizeof(SimStateHeader), MPI_BYTE, regionId, 4503, simComm,
				MPI_STATUS_IGNORE);

		r.world = header.world;
		r.local = header.local;
		r.ghost = header.ghost;
		r.simRank = header.simRank;
		bytes_transferred += sizeof(SimStateHeader);

		for (uint64_t f = 0; f < header.numFields; ++f) {
			Field field = Field::recv(simComm, regionId, 4503);
			bytes_transferred += field.array->numBytes();

			r.fields[field.name] = field;
		}
		if (header.hasParticles) {
			r.particles = Particles::recv(simComm, regionId, 4503);
			bytes_transferred += r.particles.array->numBytes();
		}

		auto end_transfer = high_resolution_clock::now();
		transfer_time += duration_cast<nanoseconds>(end_transfer - start_transfer).count();
	}

	if (LIBIS_LOGGING) {
		std::vector<size_t> results(size * 2, 0);
		std::array<size_t, 2> send_results = {transfer_time, bytes_transferred};
		MPI_Gather(send_results.data(), 2 * sizeof(size_t), MPI_BYTE,
				results.data(), 2 * sizeof(size_t), MPI_BYTE, 0, ownComm);
		if (rank == 0) {
			*log << "#------#\nOn " << size << " nodes, with "
				<< simsPerClient << " sims/client\nlibIS transfer times: [ ";
			for (size_t i = 0; i < results.size(); i += 2) {
				*log << results[i] << "ns ";
			}
			*log << " ]\nlibIS bytes transferred: [ ";
			for (size_t i = 1; i < results.size(); i += 2) {
				*log << results[i] << "b ";
			}
			*log << " ]" << std::endl;
		}
	}

	MPI_Barrier(ownComm);
	return regions;
}
void SimulationConnection::connectSim() {
	char mpiPortName[MPI_MAX_PORT_NAME + 1] = {0};
	if (rank == 0) {
		MPI_Open_port(MPI_INFO_NULL, mpiPortName);
		myPortName = mpiPortName;

		std::cout << "Sending connect cmd, got MPI port name from open '"
			<< mpiPortName << "'" << std::endl;
	}
	MPI_Bcast(mpiPortName, MPI_MAX_PORT_NAME + 1, MPI_BYTE, 0, MPI_COMM_WORLD);
	sendCommand(CONNECT);

	MPI_Comm_accept(mpiPortName, MPI_INFO_NULL, 0, ownComm, &simComm);
	MPI_Comm_set_errhandler(simComm, MPI_ERRORS_RETURN);

	MPI_Comm_remote_size(simComm, &simSize);
}
void SimulationConnection::sendCommand(const COMMAND cmd) {
	// Sending a command consists of:
	// 1. sending our MPI port name to identify ourself (length, then string)
	// 2. sending the command as an int
	if (rank == 0) {
		if (simComm == MPI_COMM_NULL) {
			std::cout << "Sending command " << cmd << " over socket\n" << std::flush;
			simSocket = socket(AF_INET, SOCK_STREAM, 0);
			if (simSocket < 0) {
				throw std::runtime_error("Failed to make socket");
			}

			struct hostent *server = gethostbyname(simServer.c_str());
			if (!server) {
				throw std::runtime_error("Lookup failed for simulation server " + simServer);
			}

			struct sockaddr_in servAddr;
			std::memset(&servAddr, 0, sizeof(servAddr));
			servAddr.sin_family = AF_INET;
			servAddr.sin_port = htons(simPort);
			std::memcpy(&servAddr.sin_addr.s_addr, server->h_addr, server->h_length);
			socklen_t len = sizeof(servAddr);

			if (connect(simSocket, (struct sockaddr*)&servAddr, len) < 0) {
				throw std::runtime_error("Failed to connect to sim socket");
			}

			// We also send the null-terminator
			const int portLen = myPortName.size() + 1;
			if (send(simSocket, &portLen, sizeof(portLen), 0) != sizeof(portLen)) {
				throw std::runtime_error("Failed to send port name length");
			}
			if (send(simSocket, myPortName.c_str(), portLen, 0) != portLen) {
				throw std::runtime_error("Failed to send port name");
			}
			const int cmdVal = cmd;
			if (send(simSocket, &cmdVal, sizeof(cmdVal), 0) != sizeof(cmdVal)) {
				throw std::runtime_error("Failed to send command");
			}
			close(simSocket);
		} else {
			const int cmdVal = cmd;
			MPI_Send(&cmdVal, 1, MPI_INT, 0, 4505, simComm);
		}
	}
	MPI_Barrier(ownComm);
}
void SimulationConnection::disconnect() {
	sendCommand(DISCONNECT);

	int isInterComm = 0;
	MPI_Comm_test_inter(simComm, &isInterComm);
	if (isInterComm) {
		MPI_Comm_disconnect(&simComm);
	} else {
		MPI_Barrier(simComm);
		simComm = MPI_COMM_NULL;
	}
}

static std::unique_ptr<SimulationConnection> sim;

void connect(const std::string &simServer, const int port, MPI_Comm ownComm) {
	sim = std::unique_ptr<SimulationConnection>(new SimulationConnection(ownComm, simServer, port));
}
void connectWithExisting(MPI_Comm ownComm, MPI_Comm simComm) {
	sim = std::unique_ptr<SimulationConnection>(new SimulationConnection(ownComm, simComm));
}
std::vector<SimState> query() {
	return sim->query();
}
void disconnect() {
	sim = nullptr;
}

}
}

