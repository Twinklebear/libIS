# libIS

In situ data management layer for easily using in transit visualization.
An example video of the our viewer can be seen on [YouTube](https://youtu.be/YUH55CvPmxg),
the viewer code is available [here](https://github.com/ospray/ospray_senpai).
This is the code for the core library described in the paper, see
[the paper for more details](https://www.willusher.io/publications/libis-isav18).

Will Usher, Silvio Rizzi, Ingo Wald, Jefferson Amstutz, Joseph Insley,
Venkatram Vishwanath, Nicola Ferrier, Michael E. Papka, and Valerio Pascucci.
2018. libIS: A Lightweight Library for Flexible In Transit Visualization. In
*ISAV: In Situ Infrastructures for Enabling Extreme-Scale Analysis and Visualization (ISAV '18),
November 12, 2018, Dallas, TX, USA*. ACM, New York, NY, USA, 6 pages.
[https://doi.org/10.1145/3281464.3281466](https://doi.org/10.1145/3281464.3281466).

# Documentation

libIS is split into two libraries: one to integrate into the simulation (`is_sim`), and one
to write the in transit client (`is_client`).

A typical decoupled use case works
by having the simulation listen on a port for the client, which will request to
connect to the simulation by sending a message to the simulation rank 0
over TCP. The connection setup creates an MPI
intercommunicator, which is used for the rest of the communication
until the client requests to disconnect.

An existing communicator can also be used, and can take an MPI inter- or intra-communicator,
depending on how the application wants to run the client.

libIS supports M:N configurations, where M simulations ranks send to N clients
(assuming M >= N). However, libIS is agnostic to the underlying data being
transferred, and will not restructure the simulation data. Instead
each of the client ranks will receive M/N pieces of data, with any
remainder bricks assigned evenly among the clients.

## Building

libIS builds with CMake, and only depends on MPI. The library and headers will
be installed under the directory set as your `CMAKE_INSTALL_PREFIX`, or the
default install path.

```
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=./install/
make install
```

Applications using libIS can then find it via CMake, using the installed CMake
config files under `<install_prefix>/lib/cmake/libIS/`. See
the [examples/](examples/) for an example simulation and client, and use
from CMake.
After building and installing libIS, the examples can be built:

```
cd examples
mkdir build
cd build
cmake .. -DlibIS_DIR=<install_prefix>/lib/cmake/libIS/
make
```

## Usage from CMake

libIS exports CMake3 targets, in the provided export file, so to link the libraries
and include the headers you only need the following:

```cmake
find_package(libIS REQUIRED)
target_link_libraries(your_sim is_sim)

target_link_libraries(your_client is_client)
```

## Simulation-side Library is\_sim

The simulation side library provides a non-blocking C API, to allow easier integration
into a range of simulations.
Simulations communicate their data to libIS by creating and filling out a
`libISSimState` object. This object contains metadata about the
simulation bounds, fields, data types, particles, and pointers to the underlying
arrays.

A simulation using libIS should first initialize MPI, then call
one of the `libISInit` methods. After setting up the simulation state
object, the simulation should call `libISProcess` each time a timestep
is ready to send to clients. Before exiting, the simulation should call
`libISFinalize`. All libIS calls are collectives, and thus should be called
by all ranks.


### Initialization and Finalization

```c
void libISInit(MPI_Comm simWorld, const int port)
```
Initialize the MPI communicator for a socket connect-disconnect configuration.
On rank 0 a background thread is launched to listen for client connection requests
on the specified port. Clients should call `is::client::connect` to
connect to a simulation with this approach. 

- `simWorld`: the simulation's `MPI_COMM_WORLD`, or equivalent if not
	running on all ranks in world.
- `port`: the port that libIS should listen for client connections on

```c
void libISInitWithExisting(MPI_Comm simWorld, MPI_Comm clientComm);
```
Initialize the library to use a previously established MPI inter-
or intra-communicator with the client. Clients should call
`is::client::connectWithExisting` to connect to a simulation with this
approach.

- `simWorld`: the simulation's `MPI_COMM_WORLD`, or equivalent if not
	running on all ranks in world.
- `clientComm`: the previously setup communicator with the clients. This
	should be the same comm as passed to `simComm` when the client
	calls `is::client::connectWithExisting`.

```c
void libISFinalize(void)
```
Shutdown the library, cleans up MPI communicator and terminates
the background listening thread if one was launched.

### Specifying Simulation Data

```c
libISSimState* libISMakeSimState(void);
void libISFreeSimState(libISSimState *state);
```
Create and free a simulation state object. Typically only
one is required, which can then be updated with the simulation state
by either specifying the meta-data and simulation data pointers
once up front, or, if the simulation uses different buffers
between timesteps, by re-specifying the changed fields each
timestep to overwrite the previous meta-data and pointer.

```c
typedef struct {
	float x, y, z;
} libISVec3f;

typedef struct {
	libISVec3f min, max;
} libISBox3f;

libISBox3f libISMakeBox3f();
void libISBoxExtend(libISBox3f *box, libISVec3f *v);

void libISSetWorldBounds(libISSimState *state, const libISBox3f box);
void libISSetLocalBounds(libISSimState *state, const libISBox3f box);
void libISSetGhostBounds(libISSimState *state, const libISBox3f box);
```
Set the simulation world domain bounds (the global domain), the
local bounds of data owned by this process, and any additional ghost
bounds, for ghost regions on the process. The box is specified
by making a new empty box, and extending it to hold the desired bounds
by calling extend with the upper and lower corners.


```c
typedef enum {
	UINT8,
	FLOAT,
	DOUBLE,
	INVALID,
} libISDType;
void libISSetField(libISSimState *state, const char *fieldName,
    const uint64_t dimensions[3], const libISDType type, const void *data);
```
Set or update a regular grid field which will be sent to clients querying data. The
pointer is shared with the simulation, when a client connects and requests
data it will be sent as a copy over MPI.

- `fieldName`: name of field to create, if `fieldName` has been specified before
	the existing entry will be updated.
- `dimensions`: XYZ dimensions of the grid
- `type`: type of data in the field, one of `UINT8`, `FLOAT` or `DOUBLE`
- `data`: the pointer to the field data, shared with the simulation.

```c
void libISSetParticles(libISSimState *state, const uint64_t numParticles,
    const uint64_t numGhostParticles, const uint64_t particleStride, const void *data);
```
Set or update the particle data which will be sent to clients querying data. The
pointer is shared with the simulation, when a client connects and requests
data it will be sent as a copy over MPI. The particle array should be organized
as follows: `[local particles..., ghost particles...]`, with the particles stored
in an array of structures layout.

- `numParticles`: the number of local particles in the array
- `numGhostParticles`: the number of ghost particles in the array. The ghost particles
	should come after the local particles in the array. This parameter can be 0
- `particleStride`: the size of an individual particle struct.
- `data`: the pointer to the particle data, shared with the simulation.

```c
void libISProcess(const libISSimState *state);
```
Send the simulation data out for processing to the client, if one has
connected and requested a timestep. This method also processes other
client commands, i.e., connecting and disconnecting.
If no client requested a command to run, this returns immediately.

## Client-side Library is\_client

The client library provides a blocking C++ API, to simplify
the implementation of visualization clients. If a non-blocking API
is desired, the libIS calls can be pushed on to a background thread
(see e.g., [ospray\_senpai's QueryTask](https://github.com/ospray/ospray_senpai/blob/master/query_task.h)).

### Initialization and Finalization

```c++
void is::client::connect(const std::string &simServer,
    const int port, MPI_Comm ownComm)
```
Connect to the simulation integrated with libIS which is listening
for clients on a socket. This is the matching connection call
for `libISInit`

- `simServer`: the hostname of rank 0 of the simulation, which is listening
	for client connections
- `port`: the port the simulation is listening on, which was specified
	in `libISInit`
- `ownComm`: the client's `MPI_COMM_WORLD`, or equivalent if running on
	a subset of ranks in the world.

```c++
void connectWithExisting(MPI_Comm ownComm, MPI_Comm simComm);
```
Connect to a simulation over the previously established MPI communicator.
This is the matching connection call for `libISInitWithExisting`

- `ownComm`: the client's `MPI_COMM_WORLD`, or equivalent if running on
	a subset of ranks in the world.
- `simComm`: the previously setup communicator with the simulation. This
	should be the same comm as was passed to `clientComm` when
	`libISInitWithExisting` was called on the simulation.

```c++
void is::client::disconnect()
```
Disconnect the client from the simulation, closing the
intercommunicator (if using `is::client::connect`) and shutting down the library.

### Querying Data from the Simulation

```c++
std::vector<is::SimState> is::client::query();
```
Query our region in the next timestep from the simulation.
This call will block until the simulation responds with
the data. To allow for M > N configurations this will
return a vector of simulation state data, with one entry
per-simulation assigned to this client rank.

```c++
struct SimState {
    libISBox3f world, local, ghost;
    // The rank we received this data from
    int simRank;
    // Regular 3D grid fields, indexed by the field name
    std::unordered_map<std::string, Field> fields;
    // Particle data on the rank, if any
    Particles particles;

    std::vector<std::string> fieldNames() const;
};
```
Each `SimState` returned corresponds to a sub-domain of the simulation,
on some rank and will contain the field and particle data
set by that rank.

```c++
struct Field {
    std::string name;
    libISDType dataType;
    // X, Y, Z dimensions of the field
    std::array<uint64_t, 3> dims;
    // The field data
    std::shared_ptr<Array> array;
};
```
A 3D regular grid field of data in the simulation.

```c++
struct Particles {
    uint64_t numParticles, numGhost;
    // The particle data
    std::shared_ptr<Array> array;
};
```
An array of particles in the simulation, along with any ghost particles.


```c++
struct Array {
    virtual void* data() = 0;
    virtual const void* data() const = 0;
    virtual size_t numBytes() const = 0;

    size_t size() const;
    size_t stride() const;
};
```
An abstract array interface, which may refer to an array of data we own or are borrowing.
Contains information about the size of the array (in elements), and the stride (in bytes)
between each element in the array. For example, the array in the `Particles`
object will have the stride set to the size of a particle. The type information
is kept separately, either on the `Field` structure, or is assumed to be known
by the client in the case of the `Particles`.

## Running the Examples

The example simulation and client show how to setup a decoupled,
in transit use case with libIS. The simulation listens on
port 29374 for the client, who will connect and print out the metadata
about the simulation's data, then disconnect.

In one terminal, run the simulation:
```
mpirun -n 2 ./example_sim
```

In a second terminal, run the client and connect to the simulation:
```
mpirun -n 1 ./example_client -server localhost -port 29374
```

The simulation pretends to work by sleeping every 2 seconds
for each timestep. After launching the client you should
see it connect and print out the meta-data, and exit.

