#include "is_simstate.h"
#include "intercomm.h"
#include "is_buffering.h"

using namespace is;

Array::Array(uint64_t elemStride) : elemStride(elemStride) {}
Array::~Array() {}

size_t Array::size() const
{
    return numBytes() / elemStride;
}
size_t Array::stride() const
{
    return elemStride;
}

OwnedArray::OwnedArray(const uint64_t arrayBytes, const uint64_t elemStride)
    : Array(elemStride), array(arrayBytes, 0)
{
}
void *OwnedArray::data()
{
    return array.data();
}
const void *OwnedArray::data() const
{
    return array.data();
}
size_t OwnedArray::numBytes() const
{
    return array.size();
}

BorrowedArray::BorrowedArray(void *array, const uint64_t arrayBytes, const uint64_t elemStride)
    : Array(elemStride), array(array), arrayBytes(arrayBytes)
{
}
void *BorrowedArray::data()
{
    return array;
}
const void *BorrowedArray::data() const
{
    return array;
}
size_t BorrowedArray::numBytes() const
{
    return arrayBytes;
}

Field::Field() : dataType(INVALID), dims({0, 0, 0}) {}
Field::Field(const std::string &name,
             libISDType type,
             const uint64_t dims[3],
             std::shared_ptr<Array> &array)
    : name(name), dataType(type), dims({dims[0], dims[1], dims[2]}), array(array)
{
}
void Field::send(std::shared_ptr<InterComm> &intercomm, const int rank) const
{
    WriteBuffer header;
    header << name << (uint32_t)dataType << dims << array->numBytes() << array->stride();
    uint64_t headerSize = header.size();
    intercomm->send(&headerSize, sizeof(uint64_t), rank);
    intercomm->send(header.data(), header.size(), rank);
    intercomm->send(array->data(), array->numBytes(), rank);
}
Field Field::recv(std::shared_ptr<InterComm> &intercomm, const int rank)
{
    uint64_t headerSize = 0;
    intercomm->recv(&headerSize, sizeof(uint64_t), rank);

    std::vector<char> headerBuf(headerSize, 0);
    intercomm->recv(headerBuf.data(), headerSize, rank);
    ReadBuffer header(headerBuf);

    Field field;
    uint32_t dtype;
    uint64_t fieldBytes, elemStride;
    header >> field.name >> dtype >> field.dims >> fieldBytes >> elemStride;
    field.dataType = (libISDType)dtype;

    field.array = std::make_shared<OwnedArray>(fieldBytes, elemStride);
    intercomm->recv(field.array->data(), field.array->numBytes(), rank);
    return field;
}

Particles::Particles() : numParticles(0), numGhost(0) {}
Particles::Particles(uint64_t numParticles, uint64_t numGhost, std::shared_ptr<Array> &array)
    : numParticles(numParticles), numGhost(numGhost), array(array)
{
}
void Particles::send(std::shared_ptr<InterComm> &intercomm, const int rank) const
{
    WriteBuffer header;
    header << numParticles << numGhost << array->numBytes() << array->stride();
    const uint64_t headerSize = 4 * sizeof(uint64_t);
    intercomm->send(header.data(), header.size(), rank);
    intercomm->send(array->data(), array->numBytes(), rank);
}
Particles Particles::recv(std::shared_ptr<InterComm> &intercomm, const int rank)
{
    const uint64_t headerSize = 4 * sizeof(uint64_t);
    std::vector<char> headerBuf(headerSize, 0);
    intercomm->recv(headerBuf.data(), headerBuf.size(), rank);
    ReadBuffer header(headerBuf);

    Particles particles;
    uint64_t particleBytes, elemStride;
    header >> particles.numParticles >> particles.numGhost >> particleBytes >> elemStride;

    particles.array = std::make_shared<OwnedArray>(particleBytes, elemStride);
    intercomm->recv(particles.array->data(), particles.array->numBytes(), rank);
    return particles;
}

std::vector<std::string> SimState::fieldNames() const
{
    std::vector<std::string> names;
    names.reserve(fields.size());
    for (const auto &f : fields) {
        names.push_back(f.first);
    }
    return names;
}

SimStateHeader::SimStateHeader() : simRank(uint32_t(-1)), numFields(0), hasParticles(0) {}
SimStateHeader::SimStateHeader(const SimState *state)
    : world(state->world),
      local(state->local),
      ghost(state->ghost),
      simRank(state->simRank),
      numFields(state->fields.size()),
      hasParticles(state->particles.numParticles)
{
}

libISSimState::libISSimState(SimState *state) : state(state) {}
libISSimState::~libISSimState()
{
    delete state;
}
