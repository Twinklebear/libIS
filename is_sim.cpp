#include <atomic>
#include <cassert>
#include <limits>
#include <iostream>
#include <chrono>
#include <mutex>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <thread>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <mpi.h>
#include "intercomm.h"
#include "is_sim.h"
#include "is_command.h"
#include "is_simstate.h"
#include "is_buffering.h"

namespace is {
namespace sim {

static uint16_t LISTEN_PORT = 29374;

struct ConnectionManager {
	MPI_Comm simComm, clientComm;
	std::shared_ptr<InterComm> intercomm = nullptr;
	std::string clientPort;
	int clientCommand, incomingCommand;
	int simSize, simRank;

	std::mutex mutex;
	bool newQuery;
	std::atomic<bool> exitThread;
	std::thread listenerThread;

	ConnectionManager(MPI_Comm sim);
	ConnectionManager(MPI_Comm sim, MPI_Comm client);
	~ConnectionManager();
	void listenForClient();
	void process(const SimState *state);

private:
	void connectClient();
	void disconnectClient();
	void handleQuery(const SimState *state);

	MPI_Request newCommand;
};

ConnectionManager::ConnectionManager(MPI_Comm sim) : simComm(MPI_COMM_NULL),
	clientComm(MPI_COMM_NULL), clientCommand(INVALID), simSize(-1), simRank(-1),
	newQuery(false), exitThread(false), newCommand(MPI_REQUEST_NULL)
{
	MPI_Comm_dup(sim, &simComm);
	MPI_Comm_size(simComm, &simSize);
	MPI_Comm_rank(simComm, &simRank);
	if (simRank == 0) {
		listenerThread = std::thread([&](){ listenForClient(); });
	}
}
ConnectionManager::ConnectionManager(MPI_Comm sim, MPI_Comm client)
	: simComm(MPI_COMM_NULL), clientComm(MPI_COMM_NULL), clientCommand(INVALID),
	simSize(-1), simRank(-1), newQuery(false), exitThread(true),
	newCommand(MPI_REQUEST_NULL)
{
	MPI_Comm_dup(sim, &simComm);
	MPI_Comm_size(simComm, &simSize);
	MPI_Comm_rank(simComm, &simRank);

	clientComm = client;
	int isInterComm = 0;
	MPI_Comm_test_inter(clientComm, &isInterComm);
	/* TODO
	if (isInterComm) {
		MPI_Comm_remote_size(clientComm, &clientSize);
	} else {
		MPI_Comm_size(clientComm, &clientSize);
	}
	*/
	std::cout << "WILL TODO UPDATE\n";
}
ConnectionManager::~ConnectionManager() {
	MPI_Comm_free(&simComm);
	if (listenerThread.joinable()) {
		exitThread = true;
		listenerThread.join();
	}
}
void ConnectionManager::listenForClient() {
	const int listenSocket = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (listenSocket < 0) {
		throw std::runtime_error("Failed to create socket");
	}
	{
		int flag = true;
		setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int));
	}

	struct sockaddr_in servAddr;
	std::memset(&servAddr, 0, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(LISTEN_PORT);
	servAddr.sin_addr.s_addr = INADDR_ANY;

	if (bind(listenSocket, (struct sockaddr*)&servAddr, sizeof(servAddr)) < 0) {
		throw std::runtime_error("Binding to port failed");
	}
	if (listen(listenSocket, 4) < 0) {
		throw std::runtime_error("Listening on socket failed");
	}

	char hostname[1024] = {0};
	gethostname(hostname, 1023);

	std::cout << "is_sim: now listening for connections on "
		<< hostname << ":" << LISTEN_PORT << std::endl;

	while (!exitThread) {
		struct sockaddr_in addr;
		socklen_t len = sizeof(addr);
		int accepted = -1;
		while (!exitThread) {
			accepted = accept(listenSocket, (struct sockaddr*)&addr, &len);
			if (accepted < 0) {
				if (errno == EWOULDBLOCK || errno == EAGAIN) {
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				} else {
					throw std::runtime_error("Failed to accept connection");
				}
			} else {
				break;
			}
		}
		if (exitThread) {
			break;
		}

		char portName[MPI_MAX_PORT_NAME + 1] = {0};
		// Get the size of the comm name being sent to us
		int portNameLen = -1;
		if (recv(accepted, &portNameLen, sizeof(portNameLen), MSG_NOSIGNAL)
				!= sizeof(portNameLen))
		{
			std::cout << "#is_sim: error in reading incoming connection\n";
			close(accepted);
			continue;
		}
		// Now recieve the port name
		if (recv(accepted, portName, portNameLen, MSG_NOSIGNAL) != portNameLen) {
			std::cout << "#is_sim: error in reading incoming connection\n";
			close(accepted);
			continue;
		}
		std::cout << "Connecting to client on " << portName << "\n" << std::flush;
		int cmd = INVALID;
		// Finally, receive the command
		if (recv(accepted, &cmd, sizeof(cmd), MSG_NOSIGNAL) != sizeof(cmd)) {
			std::cout << "#is_sim: error in reading incoming connection\n";
			close(accepted);
			continue;
		}

		close(accepted);

		std::lock_guard<std::mutex> lock(mutex);
		if (intercomm == nullptr) {
			clientPort = portName;
		}
		clientCommand = cmd;
		newQuery = true;
		std::cout << "#is_sim: Communicating with " << portName << " got command "
			<< clientCommand << "\n" << std::flush;
		break;
	}
	close(listenSocket);
}
void ConnectionManager::process(const SimState *state) {
	std::lock_guard<std::mutex> lock(mutex);

	int haveQuery = newQuery ? 1 : 0;
	// Asynchronously check for client commands if we've got a client connected
	// TODO: Re-add intracomm support
	if (!haveQuery && simRank == 0 && intercomm != nullptr) {
		haveQuery = intercomm->probe(0) ? 1 : 0;
		if (haveQuery) {
			intercomm->recv(&clientCommand, sizeof(clientCommand), 0);
		}
	}

	MPI_Bcast(&haveQuery, 1, MPI_INT, 0, simComm);
	if (!haveQuery) {
		return;
	}

	MPI_Bcast(&clientCommand, 1, MPI_INT, 0, simComm);

	if (simRank == 0) {
		std::cout << "We have a query, command: "
			<< clientCommand << "\n" << std::flush;
	}

	switch (clientCommand) {
		case CONNECT: connectClient(); break;
		case QUERY: handleQuery(state); break;
		case DISCONNECT: disconnectClient(); break;
		default: throw std::runtime_error("Invalid or unknown client command!");
	}
	clientCommand = -1;
	newQuery = false;
}
void ConnectionManager::connectClient() {
	if (intercomm != nullptr) {
		throw std::runtime_error("libIS_sim error: Attempt to connect client "
				"while already connected");
	}
	if (simRank == 0) {
		WriteBuffer writebuf;
		writebuf << clientPort;
		uint64_t bufsize = writebuf.size();
		MPI_Bcast(&bufsize, sizeof(uint64_t), MPI_BYTE, 0, simComm);
		MPI_Bcast(writebuf.data(), writebuf.size(), MPI_BYTE, 0, simComm);
	} else {
		uint64_t bufsize = 0;
		MPI_Bcast(&bufsize, sizeof(uint64_t), MPI_BYTE, 0, simComm);
		std::vector<char> buf(bufsize, 0);
		MPI_Bcast(buf.data(), buf.size(), MPI_BYTE, 0, simComm);

		ReadBuffer readbuf(buf);
		readbuf >> clientPort;
	}
	intercomm = InterComm::connect(clientPort, simComm);
}
void ConnectionManager::disconnectClient() {
	intercomm = nullptr;

	// Watch for new clients again
	if (simRank == 0) {
		if (listenerThread.joinable()) {
			listenerThread.join();
			listenerThread = std::thread([&](){ listenForClient(); });
		}
	}
	/*
	 * REMAINING TODO for handling intracomm now
	if (clientComm != MPI_COMM_NULL) {
		int isInterComm = 0;
		MPI_Comm_test_inter(clientComm, &isInterComm);
		if (isInterComm) {
			MPI_Comm_disconnect(&clientComm);
			// Watch for new clients again
			if (simRank == 0) {
				if (listenerThread.joinable()) {
					listenerThread.join();
				}
				listenerThread = std::thread([&](){ listenForClient(); });
			}
		} else {
			MPI_Barrier(clientComm);
			clientComm = MPI_COMM_NULL;
		}
	}
	*/
}
void ConnectionManager::handleQuery(const SimState *state) {
	// Wait for the client who will recieve our data to contact us
	int myClient = -1;
	do {
		myClient = intercomm->probeAll();
	} while (myClient == -1);

	// Recv the int ping the client sent us
	int clientPing = 0;
	intercomm->recv(&clientPing, sizeof(clientPing), myClient);

	SimStateHeader header(state);
	// Send the world, local and ghost bounds, and the simRank
	intercomm->send(&header, sizeof(SimStateHeader), myClient);

	// TODO: Have the client tell us what it wants instead of sending over everything
	for (const auto &field : state->fields) {
		field.second.send(intercomm, myClient);
	}
	if (header.hasParticles) {
		state->particles.send(intercomm, myClient);
	}
}

uint64_t dtypeStride(libISDType t) {
	switch (t) {
		case UINT8: return 1;
		case FLOAT: return 4;
		case DOUBLE: return 8;
	};
	assert(false);
	return 0;
}

}
}

static std::unique_ptr<is::sim::ConnectionManager> manager;


extern "C" void libISInit(MPI_Comm sim, const int port) {
	using namespace is::sim;
	LISTEN_PORT = port;
	manager = std::unique_ptr<ConnectionManager>(new ConnectionManager(sim));
}
extern "C" void libISInitWithExisting(MPI_Comm simWorld, MPI_Comm clientComm) {
	using namespace is::sim;
	LISTEN_PORT = std::numeric_limits<uint16_t>::max();
	manager = std::unique_ptr<ConnectionManager>(new ConnectionManager(simWorld, clientComm));
}
extern "C" void libISFinalize() {
	manager = nullptr;
}
extern "C" void libISProcess(const libISSimState *state) {
	manager->process(state->state);
}

extern "C" libISSimState* libISMakeSimState(void) {
	if (!manager) {
		return NULL;
	}
	libISSimState *s = new libISSimState(new is::SimState);
	s->state->world = libISMakeBox3f();
	s->state->local = libISMakeBox3f();
	s->state->ghost = libISMakeBox3f();
	s->state->simRank = manager->simRank;
	return s;
}
extern "C" void libISFreeSimState(libISSimState *s) {
	delete s;
}

extern "C" void libISSetWorldBounds(libISSimState *s, const libISBox3f box) {
	s->state->world = box;
}
extern "C" void libISSetLocalBounds(libISSimState *s, const libISBox3f box) {
	s->state->local = box;
}
extern "C" void libISSetGhostBounds(libISSimState *s, const libISBox3f box) {
	s->state->ghost = box;
}

extern "C" void libISSetField(libISSimState *s, const char *fieldName,
		const uint64_t dimensions[3], const libISDType type, const void *data)
{
	using namespace is;
	const uint64_t elemStride = sim::dtypeStride(type);
	std::shared_ptr<Array> array = std::make_shared<BorrowedArray>(const_cast<void*>(data),
			dimensions[0] * dimensions[1] * dimensions[2] * elemStride, elemStride);
	s->state->fields[fieldName] = Field(fieldName, type, dimensions, array);
}
extern "C" void libISSetParticles(libISSimState *s, const uint64_t numParticles,
		const uint64_t numGhostParticles, const uint64_t particleStride, const void *data)
{
	using namespace is;
	std::shared_ptr<Array> array = std::make_shared<BorrowedArray>(const_cast<void*>(data),
			(numParticles + numGhostParticles) * particleStride, particleStride);
	s->state->particles = Particles(numParticles, numGhostParticles, array);
}

