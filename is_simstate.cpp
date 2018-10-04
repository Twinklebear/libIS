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

#include "is_buffering.h"
#include "is_simstate.h"

using namespace is;

Array::Array(uint64_t elemStride) : elemStride(elemStride) {}
Array::~Array() {}

size_t Array::size() const {
	return numBytes() / elemStride;
}
size_t Array::stride() const {
	return elemStride;
}

OwnedArray::OwnedArray(const uint64_t arrayBytes, const uint64_t elemStride)
	: Array(elemStride), array(arrayBytes, 0)
{}
void* OwnedArray::data() {
	return array.data();
}
const void* OwnedArray::data() const {
	return array.data();
}
size_t OwnedArray::numBytes() const {
	return array.size();
}

BorrowedArray::BorrowedArray(void *array, const uint64_t arrayBytes,
		const uint64_t elemStride)
	: Array(elemStride), array(array), arrayBytes(arrayBytes)
{}
void* BorrowedArray::data() {
	return array;
}
const void* BorrowedArray::data() const {
	return array;
}
size_t BorrowedArray::numBytes() const {
	return arrayBytes;
}

Field::Field() : dataType(INVALID), dims({0, 0, 0}) {}
Field::Field(const std::string &name, libISDType type, const uint64_t dims[3],
		std::shared_ptr<Array> &array)
	: name(name), dataType(type), dims({dims[0], dims[1], dims[2]}), array(array)
{}
void Field::send(MPI_Comm comm, const int rank, const int tag) const {
	WriteBuffer header;
	header << name << (uint32_t)dataType << dims
		<< array->numBytes() << array->stride();
	MPI_Send(header.data(), header.size(), MPI_BYTE, rank, tag, comm);
	MPI_Send(array->data(), array->numBytes(), MPI_BYTE, rank, tag, comm);
}
Field Field::recv(MPI_Comm comm, const int rank, const int tag) {
	MPI_Status status;
	MPI_Probe(rank, tag, comm, &status);
	int headerSize = 0;
	MPI_Get_count(&status, MPI_BYTE, &headerSize);

	std::vector<char> headerBuf(headerSize, 0);
	MPI_Recv(headerBuf.data(), headerSize, MPI_BYTE, rank, tag, comm, MPI_STATUS_IGNORE);
	ReadBuffer header(headerBuf);

	Field field;
	uint32_t dtype;
	uint64_t fieldBytes, elemStride;
	header >> field.name >> dtype >> field.dims >> fieldBytes >> elemStride;
	field.dataType = (libISDType)dtype;

	field.array = std::make_shared<OwnedArray>(fieldBytes, elemStride);
	MPI_Recv(field.array->data(), field.array->numBytes(), MPI_BYTE,
			rank, tag, comm, MPI_STATUS_IGNORE);

	return field;
}

Particles::Particles() : numParticles(0), numGhost(0) {}
Particles::Particles(uint64_t numParticles, uint64_t numGhost,
		std::shared_ptr<Array> &array)
	: numParticles(numParticles), numGhost(numGhost), array(array)
{}
void Particles::send(MPI_Comm comm, const int rank, const int tag) const {
	WriteBuffer header;
	header << numParticles << numGhost << array->numBytes() << array->stride();
	const uint64_t headerSize = 4 * sizeof(uint64_t);
	MPI_Send(header.data(), header.size(), MPI_BYTE, rank, tag, comm);
	MPI_Send(array->data(), array->numBytes(), MPI_BYTE, rank, tag, comm);
}
Particles Particles::recv(MPI_Comm comm, const int rank, const int tag) {
	const uint64_t headerSize = 4 * sizeof(uint64_t);
	std::vector<char> headerBuf(headerSize, 0);
	MPI_Recv(headerBuf.data(), headerSize, MPI_BYTE, rank, tag, comm, MPI_STATUS_IGNORE);
	ReadBuffer header(headerBuf);

	Particles particles;
	uint64_t particleBytes, elemStride;
	header >> particles.numParticles >> particles.numGhost >> particleBytes >> elemStride;

	particles.array = std::make_shared<OwnedArray>(particleBytes, elemStride);
	MPI_Recv(particles.array->data(), particles.array->numBytes(), MPI_BYTE,
			rank, tag, comm, MPI_STATUS_IGNORE);
	return particles;
}

std::vector<std::string> SimState::fieldNames() const {
	std::vector<std::string> names;
	names.reserve(fields.size());
	for (const auto &f : fields) {
		names.push_back(f.first);
	}
	return names;
}

SimStateHeader::SimStateHeader()
	: simRank(uint32_t(-1)),
	numFields(0),
	hasParticles(0)
{}
SimStateHeader::SimStateHeader(const SimState *state)
	: world(state->world),
	local(state->local),
	ghost(state->ghost),
	simRank(state->simRank),
	numFields(state->fields.size()),
	hasParticles(state->particles.numParticles > 0)
{}

libISSimState::libISSimState(SimState *state) : state(state){}
libISSimState::~libISSimState() { delete state; }

