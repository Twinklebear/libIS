#pragma once

#include <memory>
#include <array>
#include <unordered_map>
#include <vector>
#include <mpi.h>
#include "is_common.h"
#include "intercomm.h"

namespace is {

/* An abstract array interface, which may refer to
 * an array of data we own or are borrowing.
 */
struct Array {
	uint64_t elemStride;

	Array(uint64_t elemStride);
	virtual ~Array();
	virtual void* data() = 0;
	virtual const void* data() const = 0;
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
	void* data() override;
	const void* data() const override;
	size_t numBytes() const override;
};

/* An array borrowed by this object, the array will not be free'd when
 * the object goes out of scope.
 */
struct BorrowedArray : Array {
	void *array;
	uint64_t arrayBytes;

	BorrowedArray(void *array, const uint64_t arrayBytes,
			const uint64_t elemStride);
	void* data() override;
	const void* data() const override;
	size_t numBytes() const override;
};

/* A 3D regular grid field of data in the simulation
 */
struct Field {
	std::string name;
	libISDType dataType;
	std::array<uint64_t, 3> dims;
	// The field data
	std::shared_ptr<Array> array;

	Field();
	Field(const std::string &name, libISDType type, const uint64_t dims[3],
			std::shared_ptr<Array> &array);

	void send(std::shared_ptr<InterComm> &intercomm, const int rank) const;
	static Field recv(std::shared_ptr<InterComm> &intercomm, const int rank);
};

/* An array of particles in the simulation, along with any ghost particles
 */
struct Particles {
	uint64_t numParticles, numGhost;
	// The particle data
	std::shared_ptr<Array> array;

	Particles();
	Particles(uint64_t numParticles, uint64_t numGhost,
			std::shared_ptr<Array> &array);

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
	std::unordered_map<std::string, Field> fields;
	Particles particles;

	std::vector<std::string> fieldNames() const;
};

#pragma pack(1)
struct SimStateHeader {
	libISBox3f world, local, ghost;
	uint32_t simRank;
	uint64_t numFields;
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

