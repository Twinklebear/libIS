#include "intercomm.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "is_buffering.h"
#include "mac_sockets_defines.h"

const static int MPI_HACK_TAG = 29745;

bool mpi_open_port_available()
{
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

std::shared_ptr<InterComm> InterComm::listen(MPI_Comm ownComm)
{
    int rank = 0;
    MPI_Comm_rank(ownComm, &rank);
    if (mpi_open_port_available()) {
        if (rank == 0) {
            std::cout << "Using MPI\n";
        }
        return MPIInterComm::listen(ownComm);
    }
    if (rank == 0) {
        std::cout << "Using Sockets\n";
    }
    return SocketInterComm::listen(ownComm);
}
std::shared_ptr<InterComm> InterComm::connect(const std::string &host, MPI_Comm ownComm)
{
    int rank = 0;
    MPI_Comm_rank(ownComm, &rank);
    if (mpi_open_port_available()) {
        if (rank == 0) {
            std::cout << "Using MPI\n";
        }
        return MPIInterComm::connect(host, ownComm);
    }
    if (rank == 0) {
        std::cout << "Using Sockets\n";
    }
    return SocketInterComm::connect(host, ownComm);
}

MPIInterComm::~MPIInterComm()
{
    for (auto &req : activeProbes) {
        if (req != MPI_REQUEST_NULL) {
            MPI_Cancel(&req);
            MPI_Wait(&req, MPI_STATUS_IGNORE);
        }
    }
    // Even with all comm cancelled or completed, disconnect still hangs!? Wtf mpi..
    // MPI_Comm_disconnect(&comm);
    MPI_Comm_free(&comm);
}

std::shared_ptr<MPIInterComm> MPIInterComm::listen(MPI_Comm ownComm)
{
    auto interComm = std::make_shared<MPIInterComm>();

    int rank = 0;
    MPI_Comm_rank(ownComm, &rank);

    char mpiPortName[MPI_MAX_PORT_NAME + 1] = {0};
    if (rank == 0) {
        MPI_Open_port(MPI_INFO_NULL, mpiPortName);

        std::cout << "Sending connect cmd, got MPI port name from open '" << mpiPortName << "'"
                  << std::endl;
    }
    MPI_Bcast(mpiPortName, MPI_MAX_PORT_NAME + 1, MPI_BYTE, 0, ownComm);
    interComm->mpiPortName = mpiPortName;
    return interComm;
}

std::shared_ptr<MPIInterComm> MPIInterComm::connect(const std::string &mpiPort,
                                                    MPI_Comm ownComm)
{
    auto interComm = std::make_shared<MPIInterComm>();
    MPI_Comm_connect(
        const_cast<char *>(mpiPort.c_str()), MPI_INFO_NULL, 0, ownComm, &interComm->comm);
    MPI_Comm_set_errhandler(interComm->comm, MPI_ERRORS_ARE_FATAL);
    MPI_Comm_remote_size(interComm->comm, &interComm->remSize);

    interComm->activeProbes.resize(interComm->remSize, MPI_REQUEST_NULL);
    interComm->probeBuffers.resize(interComm->remSize, 0);
    interComm->haveProbeBuffer.resize(interComm->remSize, false);

    return interComm;
}

void MPIInterComm::accept(MPI_Comm ownComm)
{
    if (mpiPortName.empty()) {
        throw std::runtime_error("Cannot accept on non-listening MPIInterComm!");
    }
    MPI_Comm_accept(mpiPortName.c_str(), MPI_INFO_NULL, 0, ownComm, &comm);
    MPI_Comm_set_errhandler(comm, MPI_ERRORS_ARE_FATAL);
    MPI_Close_port(mpiPortName.c_str());

    MPI_Comm_remote_size(comm, &remSize);

    activeProbes.resize(remSize, MPI_REQUEST_NULL);
    probeBuffers.resize(remSize, 0);
    haveProbeBuffer.resize(remSize, false);
}

void MPIInterComm::send(void *data, size_t size, int rank)
{
    uint8_t *buf = static_cast<uint8_t *>(data);
    MPI_Send(buf, 1, MPI_BYTE, rank, MPI_HACK_TAG, comm);
    MPI_Send(buf + 1, size - 1, MPI_BYTE, rank, 0, comm);
}

void MPIInterComm::recv(void *data, size_t size, int rank)
{
    uint8_t *buf = static_cast<uint8_t *>(data);
    if (haveProbeBuffer[rank] || activeProbes[rank] != MPI_REQUEST_NULL) {
        if (activeProbes[rank] != MPI_REQUEST_NULL) {
            MPI_Wait(&activeProbes[rank], MPI_STATUS_IGNORE);
        }
        haveProbeBuffer[rank] = false;
        buf[0] = probeBuffers[rank];
        MPI_Recv(buf + 1, size - 1, MPI_BYTE, rank, 0, comm, MPI_STATUS_IGNORE);
    } else {
        MPI_Recv(buf, 1, MPI_BYTE, rank, MPI_HACK_TAG, comm, MPI_STATUS_IGNORE);
        MPI_Recv(buf + 1, size - 1, MPI_BYTE, rank, 0, comm, MPI_STATUS_IGNORE);
    }
}

bool MPIInterComm::probe(int rank)
{
    if (haveProbeBuffer[rank]) {
        std::cerr << "Error: calling probe again before recving invalidates MPI hack "
                     "state\n";
        throw std::runtime_error("Must recv after successful probe before re-probing");
    }

    if (activeProbes[rank] == MPI_REQUEST_NULL) {
        MPI_Irecv(
            &probeBuffers[rank], 1, MPI_BYTE, rank, MPI_HACK_TAG, comm, &activeProbes[rank]);
    }

    int flag = 0;
    MPI_Test(&activeProbes[rank], &flag, MPI_STATUS_IGNORE);
    if (flag) {
        haveProbeBuffer[rank] = true;
    }
    return flag != 0;
}

int MPIInterComm::probeAll()
{
    // Note: explicitly not using Iprobe w/ MPI_ANY_SOURCE b/c it segfaults
    // on Intel MPI?
    for (int i = 0; i < remSize; ++i) {
        if (probe(i)) {
            return i;
        }
    }
    return -1;
}

size_t MPIInterComm::remoteSize()
{
    return remSize;
}

const std::string &MPIInterComm::portName()
{
    return mpiPortName;
}

const static int SOCKET_MAX_SIMULTANEOUS_CONNECTIONS = 32;

SocketInterComm::~SocketInterComm()
{
    if (listenSocket != -1) {
        close(listenSocket);
    }
    for (auto &s : sockets) {
        close(s);
    }
}

std::shared_ptr<SocketInterComm> SocketInterComm::listen(MPI_Comm ownComm)
{
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

    if (bind(intercomm->listenSocket, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0) {
        throw std::runtime_error("Failed to bind socket");
    }

    if (::listen(intercomm->listenSocket, SOCKET_MAX_SIMULTANEOUS_CONNECTIONS) < 0) {
        throw std::runtime_error("Failed to listen on socket");
    }

    // We don't specify a port, so now find which port we're listening on
    std::memset(&servAddr, 0, sizeof(servAddr));
    socklen_t socklen = sizeof(servAddr);
    getsockname(intercomm->listenSocket, (struct sockaddr *)&servAddr, &socklen);
    intercomm->listenPort = ntohs(servAddr.sin_port);

    char hostname[256] = {0};
    gethostname(hostname, 255);
    intercomm->hostPortName =
        std::string(hostname) + ":" + std::to_string(intercomm->listenPort);
    std::cout << "client listening info: " << intercomm->portName() << "\n" << std::flush;

    return intercomm;
}

void parseHost(const std::string &hostport, std::string &host, int &port)
{
    auto fnd = hostport.find(':');
    if (fnd == std::string::npos) {
        throw std::runtime_error("failed to parse host: " + hostport);
    }
    host = hostport.substr(0, fnd);
    port = std::stoi(hostport.substr(fnd + 1));
}

void sendAll(int socket, void *data, size_t size)
{
    uint8_t *b = reinterpret_cast<uint8_t *>(data);
    size_t nsent = 0;
    while (nsent != size) {
        int ret = ::send(socket, b + nsent, size - nsent, 0);
        if (ret < 0) {
            perror("send error");
        }
        nsent += ret;
    }
}

void recvAll(int socket, void *data, size_t size)
{
    uint8_t *b = reinterpret_cast<uint8_t *>(data);
    size_t nrecv = 0;
    while (nrecv != size) {
        int ret = ::recv(socket, b + nrecv, size - nrecv, 0);
        if (ret < 0) {
            perror("recv error");
        }
        nrecv += ret;
    }
}

std::shared_ptr<SocketInterComm> SocketInterComm::connect(const std::string &host,
                                                          MPI_Comm ownComm)
{
    auto intercomm = std::make_shared<SocketInterComm>();

    int rank = 0;
    int worldSize = 0;
    MPI_Comm_rank(ownComm, &rank);
    MPI_Comm_size(ownComm, &worldSize);

    // The host we get back is the name and port for rank 0, so first all clients connect to
    // rank 0 which sends back the info about the other ranks to connect to and establish the
    // group Each connecting client will tell us what rank it is, and we tell it which rank we
    // are so that the socket can be placed in the right index in the vector of sockets

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

        struct hostent *server = gethostbyname(servername.c_str());
        if (!server) {
            throw std::runtime_error("Lookup failed for remote " + host);
        }

        // Rate limit our connections to rank 0
        for (int i = 0; i < worldSize; i += SOCKET_MAX_SIMULTANEOUS_CONNECTIONS) {
            if (rank >= i && rank < i + SOCKET_MAX_SIMULTANEOUS_CONNECTIONS) {
                struct sockaddr_in servAddr = {0};
                servAddr.sin_family = AF_INET;
                servAddr.sin_port = htons(port);
                std::memcpy(&servAddr.sin_addr.s_addr, server->h_addr, server->h_length);
                if (::connect(intercomm->sockets[0],
                              (struct sockaddr *)&servAddr,
                              sizeof(servAddr)) < 0) {
                    perror("failed to connect to rank 0");
                    std::cout << std::flush;
                    throw std::runtime_error("Failed to connect to remote rank 0");
                }

                // Now send rank 0 of the remote our rank, and get back the list of other
                // remotes to connect to
                sendAll(intercomm->sockets[0], &rank, sizeof(int));
                // Rank 0 also sends the world size
                if (rank == 0) {
                    sendAll(intercomm->sockets[0], &worldSize, sizeof(int));
                }
                uint64_t bufSize = 0;
                recvAll(intercomm->sockets[0], &bufSize, sizeof(uint64_t));
                std::vector<char> recvbuf(bufSize, 0);
                recvAll(intercomm->sockets[0], recvbuf.data(), bufSize);

                is::ReadBuffer readbuf(recvbuf);
                readbuf >> remoteHosts;
            }
            MPI_Barrier(ownComm);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // Rate limit our setup with the other hosts
    for (int i = 0; i < worldSize; i += SOCKET_MAX_SIMULTANEOUS_CONNECTIONS) {
        if (rank >= i && rank < i + SOCKET_MAX_SIMULTANEOUS_CONNECTIONS) {
            for (const auto &r : remoteHosts) {
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
                if (::connect(intercomm->sockets.back(),
                              (struct sockaddr *)&servAddr,
                              sizeof(servAddr)) < 0) {
                    throw std::runtime_error("Failed to connect to remote host " + r);
                }

                // Now send it our rank so it knows who we are
                sendAll(intercomm->sockets.back(), &rank, sizeof(int));
            }
        }
        MPI_Barrier(ownComm);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return intercomm;
}

void SocketInterComm::accept(MPI_Comm ownComm)
{
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
            hosts.push_back(host);
        } else if (rank == i) {
            uint64_t size = hostPortName.size();
            MPI_Send(&size, sizeof(uint64_t), MPI_BYTE, 0, 0, ownComm);
            MPI_Send(hostPortName.data(), size, MPI_BYTE, 0, 0, ownComm);
        }
    }

    is::WriteBuffer hostsbuf;
    hostsbuf << hosts;

    // A map of rank id to remote socket
    std::unordered_map<int, int> remotes;
    if (rank == 0) {
        int nconnected = 0;
        int nexpected = -1;
        while (nconnected != nexpected) {
            struct sockaddr_in addr = {0};
            socklen_t len = sizeof(addr);
            int accepted = ::accept(listenSocket, (struct sockaddr *)&addr, &len);
            if (accepted == -1) {
                perror("accepting on rank 0");
            }
            // Get info about which rank this is which is connecting to us
            int remoteRank = 0;
            recvAll(accepted, &remoteRank, sizeof(int));
            ++nconnected;

            remotes[remoteRank] = accepted;

            // Remote rank 0 will tell us how many other ranks to expect to connect
            if (remoteRank == 0) {
                recvAll(accepted, &nexpected, sizeof(int));
                MPI_Bcast(&nexpected, 1, MPI_INT, 0, ownComm);
            }

            // Send back the list of other ranks the remote should connect to
            uint64_t bufSize = hostsbuf.size();
            sendAll(accepted, &bufSize, sizeof(uint64_t));
            sendAll(accepted, hostsbuf.data(), hostsbuf.size());
        }
    } else {
        int nconnected = 0;
        int nexpected = 0;
        MPI_Bcast(&nexpected, 1, MPI_INT, 0, ownComm);
        while (nconnected != nexpected) {
            struct sockaddr_in addr = {0};
            socklen_t len = sizeof(addr);
            int accepted = ::accept(listenSocket, (struct sockaddr *)&addr, &len);
            if (accepted == -1) {
                perror("accepting on other rank");
            }
            // Get info about which rank this is which is connecting to us
            int remoteRank = 0;
            recvAll(accepted, &remoteRank, sizeof(int));
            ++nconnected;

            remotes[remoteRank] = accepted;
        }
    }

    // Take the list of remotes and fill out the vector w/ the sockets
    sockets.resize(remotes.size());
    for (const auto &r : remotes) {
        sockets[r.first] = r.second;
    }
}

void SocketInterComm::send(void *data, size_t size, int rank)
{
    sendAll(sockets[rank], data, size);
}

void SocketInterComm::recv(void *data, size_t size, int rank)
{
    recvAll(sockets[rank], data, size);
}

bool SocketInterComm::probe(int rank)
{
    pollfd p = {0};
    p.fd = sockets[rank];
    p.events = POLLIN;
    int nready = poll(&p, 1, 0);
    return nready == 1 && (p.revents & POLLIN);
}

int SocketInterComm::probeAll()
{
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

size_t SocketInterComm::remoteSize()
{
    return sockets.size();
}

const std::string &SocketInterComm::portName()
{
    return hostPortName;
}
