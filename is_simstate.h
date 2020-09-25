#pragma once

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <mpi.h>
#include "intercomm.h"
#include "is_common.h"

namespace is {

/* An abstract array interface, which may refer to
 * an array of data we own or are borrowing.
 */
struct Array {
    uint64_t elemStride;

    Array(uint64_t elemStride);
    virtual ~Array();
    virtual void *data() = 0;
    virtual const void *data() const = 0;
    virtual size_t numBytes() const = 0;

    size_t size() const;
    size_t stride() const;
};

/* An array owned by this object, the array will be free'd when
 * the object goes out of scope
 */
struct OwnedArray : Array {
    std::vector<char> array;

    OwnedArray(const uint64_t arrayBytes, const uint64_t elemStride);
    void *data() override;
    const void *data() const override;
    size_t numBytes() const override;
};

/* An array borrowed by this object, the array will not be free'd when
 * the object goes out of scope.
 */
struct BorrowedArray : Array {
    void *array;
    uint64_t arrayBytes;

    BorrowedArray(void *array, const uint64_t arrayBytes, const uint64_t elemStride);
    void *data() override;
    const void *data() const override;
    size_t numBytes() const override;
};

/* A 1D or 3D buffer of data corresponding to arbitrary buffers of data,
 * or 3D grid fields
 */
struct Buffer {
    std::string name;
    libISDType dataType;
    std::array<uint64_t, 3> dims;
    // The data
    std::shared_ptr<Array> array;

    Buffer();
    Buffer(const std::string &name,
           libISDType type,
           const uint64_t dims[3],
           std::shared_ptr<Array> &array);

    Buffer(const std::string &name, const uint64_t size, std::shared_ptr<Array> &array);

    void send(std::shared_ptr<InterComm> &intercomm, const int rank) const;
    static Buffer recv(std::shared_ptr<InterComm> &intercomm, const int rank);
};

/* An array of particles in the simulation, along with any ghost particles
 */
struct Particles {
    uint64_t numParticles, numGhost;
    // The particle data
    std::shared_ptr<Array> array;

    Particles();
    Particles(uint64_t numParticles, uint64_t numGhost, std::shared_ptr<Array> &array);

    void send(std::shared_ptr<InterComm> &intercomm, const int rank) const;
    static Particles recv(std::shared_ptr<InterComm> &intercomm, const int rank);
};

/* A region is a box of space containing some particle
 * data and volume data returned to us by the simulation running on
 * some remote set of nodes. The bounds should be used
 * (optionally with particle radius extensions) as the
 * region for the distributed model, if rendering
 * transpararent geometry.
 */
struct SimState {
    libISBox3f world, local, ghost;
    // The rank we received this data from
    int simRank;
    std::unordered_map<std::string, Buffer> buffers;
    Particles particles;

    std::vector<std::string> bufferNames() const;
};

#pragma pack(1)
struct SimStateHeader {
    libISBox3f world, local, ghost;
    uint32_t simRank;
    uint64_t numBuffers;
    uint32_t hasParticles;

    SimStateHeader();
    SimStateHeader(const SimState *state);
};
}

struct libISSimState {
    is::SimState *state;

    libISSimState(is::SimState *state);
    ~libISSimState();
};
