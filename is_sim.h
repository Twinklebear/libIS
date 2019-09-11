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

#pragma once

#include <mpi.h>
#include "is_common.h"

/* Simulation side library for libIS. Provides methods
 * for initializing and shutting down the server and
 * making the new timestep available for processing
 */

/* The simulation state is an opaque handle to manage
 * the different fields and particle data pointers set by
 * the simulation, along with information about the local, ghost
 * and global bounds
 */
typedef struct libISSimState libISSimState;

/* Initialize the MPI communicator in the library and on rank 0
 * spawn a background thread watching for client connections on
 * a socket. After opening the socket we print out the hostname
 * and listen on the port number specified.
 */
extern "C" void libISInit(MPI_Comm simWorld, const int port);
/* Initialize the library to use the already established
 * communicator with the client processes.
 */
extern "C" void libISInitWithExisting(MPI_Comm simWorld, MPI_Comm clientComm);
/* Shutdown the library, cleans up MPI communicator and terminates
 * the background listening thread.
 */
extern "C" void libISFinalize(void);
/* Send the data out for processing to the client, if one has
 * connected since the last call to process.
 */
extern "C" void libISProcess(const libISSimState *state);

extern "C" libISSimState *libISMakeSimState(void);
extern "C" void libISFreeSimState(libISSimState *state);

/* Set the world bounds of the entire simulation
 */
extern "C" void libISSetWorldBounds(libISSimState *state, const libISBox3f box);
/* Set the local bounds of simulation data on this rank
 */
extern "C" void libISSetLocalBounds(libISSimState *state, const libISBox3f box);
/* Set the ghost bounds of simulation data on this rank
 */
extern "C" void libISSetGhostBounds(libISSimState *state, const libISBox3f box);

/* Set or update a regular grid field which will be sent to clients querying data. The
 * pointer is shared with the simulation, when a client connects and requests
 * data it will be sent as a copy over MPI.
 *
 * fieldName: name of field to create or update
 * dimensions: XYZ dimensions of the grid
 * type: type of data in the field, one of UINT8, FLOAT or DOUBLE
 * data: pointer to the field data, will be shared with the simulation with a copy
 *       sent to clients during processing.
 */
extern "C" void libISSetField(libISSimState *state,
                              const char *fieldName,
                              const uint64_t dimensions[3],
                              const libISDType type,
                              const void *data);

/* Set or update the particle data which will be sent to clients querying data. The
 * pointer is shared with the simulation, when a client connects and requests
 * data it will be sent as a copy over MPI. The particle array should be organized
 * as follows: [local particles..., ghost particles...], with the particles stored
 * in an array of structures layout.
 *
 * numParticles: number of local particles in the array
 * numGhostParticles: number of ghost particles in the array. The ghost particles
 *                    should come after the local particles in the array.
 * particleStride: the size of an individual particle struct.
 * data: pointer to the particle data, will be shared with the simulation with a copy
 *       sent to clients during processing.
 */
extern "C" void libISSetParticles(libISSimState *state,
                                  const uint64_t numParticles,
                                  const uint64_t numGhostParticles,
                                  const uint64_t particleStride,
                                  const void *data);
