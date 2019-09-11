#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <mpi.h>

class InterComm {
public:
    static std::shared_ptr<InterComm> listen(MPI_Comm ownComm);
    static std::shared_ptr<InterComm> connect(const std::string &host, MPI_Comm ownComm);

    virtual void accept(MPI_Comm ownComm) = 0;
    // Send some data to a rank on the other end
    virtual void send(void *data, size_t size, int rank) = 0;
    // Recv data from some rank into the provided buffer
    virtual void recv(void *data, size_t size, int rank) = 0;
    // See if data is available to be received from some rank
    virtual bool probe(int rank) = 0;
    // See if data is available to received from any rank, returns -1 if none
    virtual int probeAll() = 0;

    virtual size_t remoteSize() = 0;
    virtual const std::string &portName() = 0;
};

// TODO: Also allow supporting use of an intracomm
class MPIInterComm : public InterComm {
    MPI_Comm comm = MPI_COMM_NULL;
    int remSize = -1;
    std::string mpiPortName;

public:
    MPIInterComm() = default;
    ~MPIInterComm();
    static std::shared_ptr<MPIInterComm> listen(MPI_Comm ownComm);
    static std::shared_ptr<MPIInterComm> connect(const std::string &mpiPort, MPI_Comm ownComm);
    void accept(MPI_Comm ownComm) override;
    void send(void *data, size_t size, int rank) override;
    void recv(void *data, size_t size, int rank) override;
    bool probe(int rank) override;
    int probeAll() override;

    size_t remoteSize() override;
    const std::string &portName() override;
};

class SocketInterComm : public InterComm {
    int listenSocket = -1;
    int listenPort = 0;
    std::vector<int> sockets;
    std::string hostPortName;

public:
    SocketInterComm() = default;
    ~SocketInterComm();
    static std::shared_ptr<SocketInterComm> listen(MPI_Comm ownComm);
    static std::shared_ptr<SocketInterComm> connect(const std::string &host, MPI_Comm ownComm);
    void accept(MPI_Comm ownComm) override;
    void send(void *data, size_t size, int rank) override;
    void recv(void *data, size_t size, int rank) override;
    bool probe(int rank) override;
    int probeAll() override;

    size_t remoteSize() override;
    const std::string &portName() override;
};
