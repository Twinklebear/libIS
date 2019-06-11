#include <stdexcept>
#include <iostream>
#include "intercomm.h"

MPIInterComm::MPIInterComm() : comm(MPI_COMM_NULL), remSize(0) {}

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
	MPI_Send(data, size, MPI_BYTE, rank, 4502, comm);
}

void MPIInterComm::recv(std::vector<uint8_t> &data, size_t size, int rank) {
	MPI_Recv(data.data(), size, MPI_BYTE, rank, 4502, comm, MPI_STATUS_IGNORE);
}

bool MPIInterComm::probe(int rank) {
	int flag = 0;
	MPI_Iprobe(rank, 4502, comm, &flag, MPI_STATUS_IGNORE);
	return flag != 0;
}

size_t MPIInterComm::remoteSize() {
	return remSize;
}

const std::string& MPIInterComm::portName() {
	return mpiPortName;
}

