#include <memory>
#include <fstream>
#include <thread>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <limits>
#include <iostream>
#include <cstring>
#include <queue>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <stdlib.h>
#include <mpi.h>
#include "intercomm.h"
#include "is_client.h"
#include "is_command.h"

namespace is {
namespace client {

static bool LIBIS_LOGGING = false;
static std::string LIBIS_LOG_OUTPUT;
static void check_logging_wanted() {
	const char *log_var = getenv("LIBIS_LOG_OUTPUT");
	if (log_var) {
		LIBIS_LOG_OUTPUT = log_var;
		LIBIS_LOGGING = true;
	}
}

struct RankLoad {
	int rank = -1;
	float load = 0.f;

	RankLoad() = default;
	RankLoad(int rank, float load) : rank(rank), load(load) {}
};

bool operator<(const RankLoad &a, const RankLoad &b) {
	return a.load < b.load;
}

bool operator>(const RankLoad &a, const RankLoad &b) {
	return a.load > b.load;
}

struct SimulationConnection {
	MPI_Comm simComm, ownComm;
	std::shared_ptr<InterComm> intercomm = nullptr;
	int rank, size, clientIntraCommRoot;
	int simSocket, simPort;
	std::string myPortName, simServer;

	std::shared_ptr<std::ofstream> log;

	SimulationConnection(MPI_Comm com, const std::string &simServer,
			const int port);
	SimulationConnection(MPI_Comm com, MPI_Comm sim);
	~SimulationConnection();
	std::vector<SimState> query(float load = 1.f);

private:
	void connectSim();
	void sendCommand(const COMMAND cmd);
	void disconnect();
};
SimulationConnection::SimulationConnection(MPI_Comm com, const std::string &simServer,
		const int port)
	: simComm(MPI_COMM_NULL),
	ownComm(MPI_COMM_NULL),
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
	/*
	   TODO
	if (isInterComm) {
		MPI_Comm_remote_size(simComm, &simSize);
	} else {
		MPI_Comm_size(simComm, &simSize);
		if (rank == 0) {
			MPI_Comm_rank(simComm, &clientIntraCommRoot);
		}
		MPI_Bcast(&clientIntraCommRoot, 1, MPI_INT, 0, ownComm);
	}
	*/
	std::cout <<"WILL TODO UPDATE THIS!\n";
}
SimulationConnection::~SimulationConnection() {
	disconnect();
	MPI_Comm_free(&ownComm);
}
std::vector<SimState> SimulationConnection::query(float load) {
	sendCommand(QUERY);

	int correctedSimSize = intercomm->remoteSize();
	// TODO: This is assuming the use existing comm mode is running in mpi
	// multi-program launch mode.
	if (clientIntraCommRoot != 0) {
		correctedSimSize = clientIntraCommRoot;
	}

	// Determine the total "load" to compute the relative load of each rank
	// TODO: Not sure if communication/parallelism is better with one rank getting
	// all the loads and sending out the assignments, or an all-to-all step of sending
	// the loads to all ranks and then each can compute the assignments and use that
	// to find its own work.
	std::vector<float> rankLoads(size, 0.f);
	MPI_Allgather(&load, 1, MPI_FLOAT, rankLoads.data(), 1, MPI_FLOAT, MPI_COMM_WORLD);

	const float totalLoad = std::accumulate(rankLoads.begin(), rankLoads.end(), 0.f);

	// Since we floor the assignments, we may not initially assign all simulation ranks 
	float minLoad = *std::min_element(rankLoads.begin(), rankLoads.end());
	if (minLoad == 0.f) {
		minLoad = std::numeric_limits<float>::infinity();
		for (const auto &f : rankLoads) {
			if (f > 0.f) {
				minLoad = std::min(f, minLoad);
			}
		}
	}

	std::priority_queue<RankLoad, std::vector<RankLoad>, std::greater<RankLoad>> assignQueue;
	for (size_t i = 0; i < rankLoads.size(); ++i) {
		assignQueue.push(RankLoad(i, rankLoads[i]));
	}

	// Find the most underloaded rank from the previous task run and assign it a task
	std::vector<int> clientsPerRank(size, 1);
	for (int i = 0; i < correctedSimSize - clientsPerRank.size(); ++i) {
		RankLoad r = assignQueue.top();
		assignQueue.pop();

		clientsPerRank[r.rank]++;

		// Count the work of a single "element" as the minimum load reported on any rank
		r.load += minLoad;
		assignQueue.push(r);
	}

	const int simOffset = std::accumulate(clientsPerRank.begin(), clientsPerRank.begin() + rank, 0);

	std::cout << "rank: " << rank << " with relativeLoad " << load / totalLoad
		<< " will take " << clientsPerRank[rank] << " sims, starting at offset "
		<< simOffset << "\n";

	// Currently we assume an M:N mapping of sim ranks to client ranks,
	// where M >= N. As such each simulation's data is assigned to the
	// corresponding rank # on the client side. So client 0 gets data
	// from sim rank 0, and so on. In the case of M > N each client will
	// recv M/N regions, merging these regions is left as an optional operation
	// for the user.
	std::vector<SimState> regions(clientsPerRank[rank], SimState());
	using namespace std::chrono;
	size_t bytes_transferred = 0;
	size_t transfer_time = 0;
	for (size_t i = 0; i < regions.size(); ++i) {
		const int regionId = i + simOffset;
		SimState &r = regions[i];

		auto start_transfer = high_resolution_clock::now();
		// Send over a ping the the simulation rank to have it send us the data
		intercomm->send(&rank, sizeof(int), regionId);

		// Recieve the header telling us about the simulation state
		SimStateHeader header;
		intercomm->recv(&header, sizeof(SimStateHeader), regionId);

		r.world = header.world;
		r.local = header.local;
		r.ghost = header.ghost;
		r.simRank = header.simRank;
		bytes_transferred += sizeof(SimStateHeader);

		for (uint64_t f = 0; f < header.numFields; ++f) {
			Field field = Field::recv(intercomm, regionId);
			bytes_transferred += field.array->numBytes();

			r.fields[field.name] = field;
		}
		if (header.hasParticles) {
			r.particles = Particles::recv(intercomm, regionId);
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
				<< clientsPerRank[rank] << " sims\nlibIS transfer times: [ ";
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
	intercomm = InterComm::listen(ownComm);
	myPortName = intercomm->portName();
	sendCommand(CONNECT);
	intercomm->accept(ownComm);
}
void SimulationConnection::sendCommand(const COMMAND cmd) {
	// Sending a command consists of:
	// 1. sending our MPI port name to identify ourself (length, then string)
	// 2. sending the command as an int
	if (rank == 0) {
		if (cmd == CONNECT) {
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
			int cmdVal = cmd;
			intercomm->send(&cmdVal, sizeof(int), 0);
		}
	}
	MPI_Barrier(ownComm);
}
void SimulationConnection::disconnect() {
	sendCommand(DISCONNECT);
	intercomm = nullptr;
}

static std::unique_ptr<SimulationConnection> sim;

void connect(const std::string &simServer, const int port, MPI_Comm ownComm) {
	sim = std::unique_ptr<SimulationConnection>(new SimulationConnection(ownComm, simServer, port));
}
void connectWithExisting(MPI_Comm ownComm, MPI_Comm simComm) {
	sim = std::unique_ptr<SimulationConnection>(new SimulationConnection(ownComm, simComm));
}
std::vector<SimState> query(float load) {
	return sim->query(load);
}
std::future<std::vector<SimState>> queryAsync(float load) {
	std::future<std::vector<SimState>> future =
		std::async(std::launch::async, [=](){
			return query(load);
		});
	return future;
}
void disconnect() {
	sim = nullptr;
}

}
}

