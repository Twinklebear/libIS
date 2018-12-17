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

#include <vector>
#include <mpi.h>
#include "is_common.h"
#include "is_simstate.h"

/* Client side library for libIS. Provides methods
 * for querying a region of data back from a simulation
 */

namespace is {
namespace client {

/* Connect to the simulation running libis_sim on the host
 * and port specified.
 */
void connect(const std::string &simServer, const int port, MPI_Comm ownComm);
/* Connect to a simulation over an already established MPI communicator
 */
void connectWithExisting(MPI_Comm ownComm, MPI_Comm simComm);
/* Query our region in the next timestep from the simulation.
 * This call will block until the simulation responds with
 * the data.
 */
std::vector<SimState> query();
/* Disconnect the client from the simulation, closing the
 * intercommunicator and shutting down the library.
 */
void disconnect();

}
}

