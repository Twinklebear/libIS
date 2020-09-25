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
    void connect(const std::string &simServer,
                 const int port,
                 MPI_Comm ownComm,
                 bool *sim_quit = nullptr);

    /* Connect to a simulation over an already established MPI communicator
     */
    void connectWithExisting(MPI_Comm ownComm, MPI_Comm simComm, bool *sim_quit = nullptr);

    /* Query our region in the next timestep from the simulation.
     * This call will block until the simulation responds with
     * the data.
     */
    std::vector<SimState> query(bool *sim_quit = nullptr);

    /* Asynchronously query our region in the next timestep from the simulation.
     * This call will block until the simulation responds with
     * the data.
     */
    std::future<std::vector<SimState>> query_async(bool *sim_quit = nullptr);

    /* Disconnect the client from the simulation, closing the
     * intercommunicator and shutting down the library.
     */
    void disconnect();

    /* Check if the simulation is still connected, returns false if the
     * simulation was not connected to or has quit (thus forcing the client
     * to disconnect
     */
    bool sim_connected();
}
}
