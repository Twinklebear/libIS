#include <stdexcept>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>
#include "is_buffering.h"
#include "intercomm.h"

bool mpi_open_port_available() {
#ifdef LIBIS_FORCE_SOCKET_INTERCOMM
	return false;
#else
	// TODO: Maybe get the errhandler and restore it after instead of setting back to fatal?
	MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_RETURN);
	char mpiPortName[MPI_MAX_PORT_NAME + 1] = {0};
	int ret = MPI_Open_port(MPI_INFO_NULL, mpiPortName);
	if (ret != 0) {
		return false;
	}
	MPI_Close_port(mpiPortName);
	MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_ARE_FATAL);
	return true;
#endif
}

std::shared_ptr<InterComm> InterComm::listen(MPI_Comm ownComm) {
	if (mpi_open_port_available()) {
		return MPIInterComm::listen(ownComm);
	}
	return SocketInterComm::listen(ownComm);
}
std::shared_ptr<InterComm> InterComm::connect(const std::string &host, MPI_Comm ownComm) {
	if (mpi_open_port_available()) {
		return MPIInterComm::connect(host, ownComm);
	}
	return SocketInterComm::connect(host, ownComm);
}

MPIInterComm::~MPIInterComm() {
	MPI_Comm_disconnect(&comm);
}

std::shared_ptr<MPIInterComm> MPIInterComm::listen(MPI_Comm ownComm) {
	auto interComm = std::make_shared<MPIInterComm>();

	int rank = 0;
	MPI_Comm_rank(ownComm, &rank);

	char mpiPortName[MPI_MAX_PORT_NAME + 1] = {0};
	if (rank == 0) {
		MPI_Open_port(MPI_INFO_NULL, mpiPortName);

		std::cout << "Sending connect cmd, got MPI port name from open '"
			<< mpiPortName << "'" << std::endl;
	}
	MPI_Bcast(mpiPortName, MPI_MAX_PORT_NAME + 1, MPI_BYTE, 0, ownComm);
	interComm->mpiPortName = mpiPortName;
	return interComm;
}

std::shared_ptr<MPIInterComm> MPIInterComm::connect(const std::string &mpiPort, MPI_Comm ownComm) {
	auto interComm = std::make_shared<MPIInterComm>();
	MPI_Comm_connect(const_cast<char*>(mpiPort.c_str()), MPI_INFO_NULL, 0, ownComm, &interComm->comm);
	MPI_Comm_set_errhandler(interComm->comm, MPI_ERRORS_RETURN);
	MPI_Comm_remote_size(interComm->comm, &interComm->remSize);
	return interComm;
}

void MPIInterComm::accept(MPI_Comm ownComm) {
	if (mpiPortName.empty()) {
		throw std::runtime_error("Cannot accept on non-listening MPIInterComm!");
	}
	MPI_Comm_accept(mpiPortName.c_str(), MPI_INFO_NULL, 0, ownComm, &comm);
	MPI_Comm_set_errhandler(comm, MPI_ERRORS_RETURN);

	MPI_Comm_remote_size(comm, &remSize);
}

void MPIInterComm::send(void *data, size_t size, int rank) {
	MPI_Send(data, size, MPI_BYTE, rank, 0, comm);
}

void MPIInterComm::recv(void *data, size_t size, int rank) {
	MPI_Recv(data, size, MPI_BYTE, rank, 0, comm, MPI_STATUS_IGNORE);
}

bool MPIInterComm::probe(int rank) {
	int flag = 0;
	MPI_Iprobe(rank, 0, comm, &flag, MPI_STATUS_IGNORE);
	return flag != 0;
}

int MPIInterComm::probeAll() {
	// Note: explicitly not using Iprobe w/ MPI_ANY_SOURCE b/c it segfaults
	// on Intel MPI?
	for (int i = 0; i < remSize; ++i) {
		if (probe(i)) {
			return i;
		}
	}
	return -1;
}

size_t MPIInterComm::remoteSize() {
	return remSize;
}

const std::string& MPIInterComm::portName() {
	return mpiPortName;
}

SocketInterComm::~SocketInterComm() {
	if (listenSocket != -1) {
		close(listenSocket);
	}
	for (auto &s : sockets) {
		close(s);
	}
}

std::shared_ptr<SocketInterComm> SocketInterComm::listen(MPI_Comm ownComm) {
	auto intercomm = std::make_shared<SocketInterComm>();

	intercomm->listenSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (intercomm->listenSocket == -1) {
		throw std::runtime_error("Failed to open listen socket");
	}

	struct sockaddr_in servAddr;
	std::memset(&servAddr, 0, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_port = 0;
	servAddr.sin_addr.s_addr = INADDR_ANY;

	if (bind(intercomm->listenSocket, (struct sockaddr*)&servAddr, sizeof(servAddr)) < 0) {
		throw std::runtime_error("Failed to bind socket");
	}

	if (::listen(intercomm->listenSocket, 8) < 0) {
		throw std::runtime_error("Failed to listen on socket");
	}

	// We don't specify a port, so now find which port we're listening on
	std::memset(&servAddr, 0, sizeof(servAddr));
	socklen_t socklen = sizeof(servAddr);
	getsockname(intercomm->listenSocket, (struct sockaddr*)&servAddr, &socklen);
	intercomm->listenPort = ntohs(servAddr.sin_port);

	char hostname[256] = {0};
	gethostname(hostname, 255);
	intercomm->hostPortName = std::string(hostname) + ":" + std::to_string(intercomm->listenPort);
	std::cout << "client listening info: " << intercomm->portName() << "\n" << std::flush;

	return intercomm;
}

void parseHost(const std::string &hostport, std::string &host, int &port) {
	auto fnd = hostport.find(':');
	if (fnd == std::string::npos) {
		throw std::runtime_error("failed to parse host: " + hostport);
	}
	host = hostport.substr(0, fnd);
	port = std::stoi(hostport.substr(fnd + 1));
}


std::shared_ptr<SocketInterComm> SocketInterComm::connect(const std::string &host, MPI_Comm ownComm) {
	auto intercomm = std::make_shared<SocketInterComm>();

	int rank = 0;
	int worldSize = 0;
	MPI_Comm_rank(ownComm, &rank);
	MPI_Comm_size(ownComm, &worldSize);

	// The host we get back is the name and port for rank 0, so first all clients connect to rank 0
	// which sends back the info about the other ranks to connect to and establish the group 
	// Each connecting client will tell us what rank it is, and we tell it which rank we are
	// so that the socket can be placed in the right index in the vector of sockets

	std::vector<std::string> remoteHosts;
	{
		intercomm->sockets.push_back(socket(AF_INET, SOCK_STREAM, 0));
		if (intercomm->sockets[0] == -1) {
			std::cout << "Failed to make socket\n";
			throw std::runtime_error("failed to make socket");
		}

		std::string servername;
		int port;
		parseHost(host, servername, port);
		std::cout << "host: '" << servername << "'\n";
		std::cout << "port: " << port << "\n";

		struct hostent *server = gethostbyname(servername.c_str());
		if (!server) {
			throw std::runtime_error("Lookup failed for remote " + host);
		}

		struct sockaddr_in servAddr = {0};
		servAddr.sin_family = AF_INET;
		servAddr.sin_port = htons(port);
		std::memcpy(&servAddr.sin_addr.s_addr, server->h_addr, server->h_length);
		if (::connect(intercomm->sockets[0], (struct sockaddr*)&servAddr, sizeof(servAddr)) < 0) {
			perror("failed to connect to rank 0");
			std::cout << std::flush;
			throw std::runtime_error("Failed to connect to remote rank 0");
		}

		// Now send rank 0 of the remote our rank, and get back the list of other remotes to connect to
		::send(intercomm->sockets[0], &rank, sizeof(int), 0);
		// Rank 0 also sends the world size
		if (rank == 0) {
			::send(intercomm->sockets[0], &worldSize, sizeof(int), 0);
		}
		uint64_t bufSize = 0;
		::recv(intercomm->sockets[0], &bufSize, sizeof(uint64_t), 0);
		std::vector<char> recvbuf(bufSize, 0);
		::recv(intercomm->sockets[0], recvbuf.data(), recvbuf.size(), 0);

		is::ReadBuffer readbuf(recvbuf);
		readbuf >> remoteHosts;
	}

	std::cout << "Rank " << rank << " has " << remoteHosts.size() << " other hosts to connect to\n";

	for (const auto &r : remoteHosts) {
		std::cout << "rank " << rank << " connecting to remote " << r << "\n" << std::flush;

		intercomm->sockets.push_back(socket(AF_INET, SOCK_STREAM, 0));
		std::string servername;
		int port;
		parseHost(r, servername, port);

		struct hostent *server = gethostbyname(servername.c_str());
		if (!server) {
			throw std::runtime_error("Lookup failed for remote " + r);
		}

		struct sockaddr_in servAddr = {0};
		servAddr.sin_family = AF_INET;
		servAddr.sin_port = htons(port);
		std::memcpy(&servAddr.sin_addr.s_addr, server->h_addr, server->h_length);
		if (::connect(intercomm->sockets.back(), (struct sockaddr*)&servAddr, sizeof(servAddr)) < 0) {
			throw std::runtime_error("Failed to connect to remote host " + r);
		}

		// Now send it our rank so it knows who we are
		::send(intercomm->sockets.back(), &rank, sizeof(int), 0);
	}

	return intercomm;
}

void SocketInterComm::accept(MPI_Comm ownComm) {
	// First all clients connect to rank 0
	// which sends back the info about the other ranks to connect to and establish the group 
	// Each connecting client will tell us what rank it is, and we tell it which rank we are
	// so that the socket can be placed in the right index in the vector of sockets

	int rank = 0;
	int worldSize = 0;
	MPI_Comm_rank(ownComm, &rank);
	MPI_Comm_size(ownComm, &worldSize);

	std::vector<std::string> hosts;
	// Collect the hostport info from the other ranks
	for (int i = 1; i < worldSize; ++i) {
		if (rank == 0) {
			uint64_t size = 0;
			MPI_Recv(&size, sizeof(uint64_t), MPI_BYTE, i, 0, ownComm, MPI_STATUS_IGNORE);
			std::vector<char> buf(size + 1, '\0');
			MPI_Recv(buf.data(), size, MPI_BYTE, i, 0, ownComm, MPI_STATUS_IGNORE);
			std::string host = buf.data();
			std::cout << "Got hostport " << host << " for rank " << i << "\n";
			hosts.push_back(host);
		} else if (rank == i) {
			uint64_t size = hostPortName.size();
			MPI_Send(&size, sizeof(uint64_t), MPI_BYTE, 0, 0, ownComm);
			MPI_Send(hostPortName.data(), size, MPI_BYTE, 0, 0, ownComm);
		}
	}

	is::WriteBuffer hostsbuf;
	hostsbuf << hosts;
	std::cout << "hostsbuf size: " << hostsbuf.size() << "\n";

	// A map of rank id to remote socket
	std::unordered_map<int, int> remotes;
	if (rank == 0) {
		int nconnected = 0;
		int nexpected = -1;
		while (nconnected != nexpected) {
			std::cout << "accepting on rank 0 " << std::endl;
			struct sockaddr_in addr = {0};
			socklen_t len = sizeof(addr);
			int accepted = ::accept(listenSocket, (struct sockaddr*)&addr, &len);
			if (accepted == -1) {
				perror("accepting on rank 0");
			}
			// Get info about which rank this is which is connecting to us
			int remoteRank = 0;
			::recv(accepted, &remoteRank, sizeof(int), 0);
			++nconnected;

			std::cout << "Rank 0 got connection from rank: " << remoteRank << "\n";
			remotes[remoteRank] = accepted;

			// Remote rank 0 will tell us how many other ranks to expect to connect
			if (remoteRank == 0) {
				::recv(accepted, &nexpected, sizeof(int), 0);
				std::cout << "Expecting connections from " << nexpected << " remotes\n";
				MPI_Bcast(&nexpected, 1, MPI_INT, 0, ownComm);
			}

			// Send back the list of other ranks the remote should connect to
			uint64_t bufSize = hostsbuf.size();
			::send(accepted, &bufSize, sizeof(uint64_t), 0);
			::send(accepted, hostsbuf.data(), hostsbuf.size(), 0);
		}
	} else {
		int nconnected = 0;
		int nexpected = 0;
		MPI_Bcast(&nexpected, 1, MPI_INT, 0, ownComm);
		while (nconnected != nexpected) {
			struct sockaddr_in addr = {0};
			socklen_t len = sizeof(addr);
			int accepted = ::accept(listenSocket, (struct sockaddr*)&addr, &len);
			if (accepted == -1) {
				perror("accepting on other rank");
			}
			// Get info about which rank this is which is connecting to us
			int remoteRank = 0;
			::recv(accepted, &remoteRank, sizeof(int), 0);
			++nconnected;

			std::cout << "Rank " << rank << " got connection from rank: " << remoteRank << "\n";
			remotes[remoteRank] = accepted;
		}
	}

	// Take the list of remotes and fill out the vector w/ the sockets
	sockets.resize(remotes.size());
	for (const auto &r : remotes) {
		sockets[r.first] = r.second;
	}
}

void SocketInterComm::send(void *data, size_t size, int rank) {
	uint8_t *b = reinterpret_cast<uint8_t*>(data);
	size_t nsent = 0;
	while (nsent != size) {
		int ret = ::send(sockets[rank], b + nsent, size - nsent, 0);
		if (ret < 0) {
			perror("send error");
		}
		nsent += ret;
	}
}

void SocketInterComm::recv(void *data, size_t size, int rank) {
	uint8_t *b = reinterpret_cast<uint8_t*>(data);
	size_t nrecv = 0;
	while (nrecv != size) {
		int ret = ::recv(sockets[rank], b + nrecv, size - nrecv, 0);
		if (ret < 0) {
			perror("recv error");
		}
		nrecv += ret;
	}
}

bool SocketInterComm::probe(int rank) {
	pollfd p = {0};
	p.fd = sockets[rank];
	p.events = POLLIN;
	int nready = poll(&p, 1, 0);
	return nready == 1 && (p.revents & POLLIN);
}

int SocketInterComm::probeAll() {
	std::vector<pollfd> pollfds;
	for (const auto &r : sockets) {
		pollfd p = {0};
		p.fd = r;
		p.events = POLLIN;
		pollfds.push_back(p);
	}
	int nready = poll(pollfds.data(), pollfds.size(), 0);
	if (nready == 0) {
		return -1;
	}

	// If some are ready we just find the first which is ready and return that rank
	for (size_t i = 0; i < pollfds.size(); ++i) {
		if (pollfds[i].revents & POLLIN) {
			return i;
		}
	}
	return -1;
}

size_t SocketInterComm::remoteSize() {
	return sockets.size();
}

const std::string& SocketInterComm::portName() {
	return hostPortName;
}

