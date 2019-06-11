#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <memory>
#include <mpi.h>

class InterComm {
public:
	// Send some data to a rank on the other end
	virtual void send(void *data, size_t size, int rank) = 0;
	// Recv data from some rank into the provided buffer
	virtual void recv(std::vector<uint8_t> &data, size_t size, int rank) = 0;
	// See if data is available to be received from some rank
	virtual bool probe(int rank) = 0;

	virtual size_t remoteSize() = 0;
	virtual const std::string& portName() = 0;
};

class MPIInterComm : public InterComm {
	MPI_Comm comm;
	int remSize;
	std::string mpiPortName;

public:
	MPIInterComm();
	static std::shared_ptr<MPIInterComm> listen(MPI_Comm ownComm);
	static std::shared_ptr<MPIInterComm> connect(const std::string &mpiPort, MPI_Comm ownComm);
	void accept(MPI_Comm ownComm);
	void send(void *data, size_t size, int rank) override;
	void recv(std::vector<uint8_t> &data, size_t size, int rank) override;
	bool probe(int rank);

	size_t remoteSize() override;
	const std::string& portName() override; 
};

/*
class SocketInterComm : public InterComm {
	std::vector<int> sockets;
	std::string hostPortName;

public:
	static std::shared_ptr<SocketInterComm> listen(MPI_Comm ownComm);
	static std::shared_ptr<SocketInterComm> connect(const std::string &host, MPI_Comm ownComm);
	void accept(MPI_Comm ownComm);
	void send(void *data, size_t size, int rank) override;
	void recv(std::vector<uint8_t> &data, size_t size, int rank) override;

	size_t remoteSize() override;
	const std::string& portName() override;
};
*/

