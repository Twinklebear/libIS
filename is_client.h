#pragma once

#include <future>
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
 *
 * load is the local time or "load" on this rank. Usually "load" would be
 * compute time, though it isn't required to be compute time.
 * Higher load values should correspond to the workload on this
 * rank being higher than others, while lower values correspond to a
 * lower workload. If all ranks specify the same value, the load is assumed
 * to be evenly distributed.
 */
std::vector<SimState> query(float load = 1.f);

/* Asynchronously query our region in the next timestep from the simulation.
 * This call will block until the simulation responds with
 * the data.
 */
std::future<std::vector<SimState>> queryAsync(float load = 1.f);

/* Disconnect the client from the simulation, closing the
 * intercommunicator and shutting down the library.
 */
void disconnect();

}
}

