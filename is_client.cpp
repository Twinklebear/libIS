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

#include "is_client.h"
#include <chrono>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
#include <arpa/inet.h>
#include <mpi.h>
#include <netdb.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "intercomm.h"
#include "is_command.h"

namespace is {
namespace client {

    static bool LIBIS_LOGGING = false;
    static std::string LIBIS_LOG_OUTPUT;
    static void check_logging_wanted()
    {
        const char *log_var = getenv("LIBIS_LOG_OUTPUT");
        if (log_var) {
            LIBIS_LOG_OUTPUT = log_var;
            LIBIS_LOGGING = true;
        }
    }

    struct SimulationConnection {
        MPI_Comm simComm, ownComm;
        std::shared_ptr<InterComm> intercomm = nullptr;
        int rank, size, clientIntraCommRoot;
        int simSocket, simPort;
        int simQuit = 0;
        std::string myPortName, simServer;

        std::FILE *log = nullptr;

        SimulationConnection(MPI_Comm com, const std::string &simServer, const int port);
        SimulationConnection(MPI_Comm com, MPI_Comm sim);
        ~SimulationConnection();
        std::vector<SimState> query();

    private:
        void connectSim();
        int sendCommand(const COMMAND cmd);
        void disconnect();
    };
    SimulationConnection::SimulationConnection(MPI_Comm com,
                                               const std::string &simServer,
                                               const int port)
        : simComm(MPI_COMM_NULL),
          ownComm(MPI_COMM_NULL),
          rank(-1),
          size(-1),
          clientIntraCommRoot(0),
          simSocket(-1),
          simPort(port),
          simServer(simServer)
    {
        MPI_Comm_dup(com, &ownComm);
        MPI_Comm_size(ownComm, &size);
        MPI_Comm_rank(ownComm, &rank);
        check_logging_wanted();
        if (LIBIS_LOGGING) {
            log = std::fopen(LIBIS_LOG_OUTPUT.c_str(), "wa");
        }
        connectSim();
    }
    SimulationConnection::SimulationConnection(MPI_Comm com, MPI_Comm sim)
        : simComm(MPI_COMM_NULL),
          ownComm(MPI_COMM_NULL),
          rank(-1),
          size(-1),
          clientIntraCommRoot(0),
          simSocket(-1),
          simPort(std::numeric_limits<uint16_t>::max())
    {
        MPI_Comm_dup(com, &ownComm);
        MPI_Comm_size(ownComm, &size);
        MPI_Comm_rank(ownComm, &rank);
        check_logging_wanted();
        if (LIBIS_LOGGING) {
            log = std::fopen(LIBIS_LOG_OUTPUT.c_str(), "wa");
        }

        simComm = sim;
        int isInterComm = 0;
        MPI_Comm_test_inter(simComm, &isInterComm);
        /*
           TODO
        if (isInterComm) {
            MPI_Comm_remote_size(simComm, &simSize);
        } else {
            MPI_Comm_size(simComm, &simSize);
            if (rank == 0) {
                MPI_Comm_rank(simComm, &clientIntraCommRoot);
            }
            MPI_Bcast(&clientIntraCommRoot, 1, MPI_INT, 0, ownComm);
        }
        */
        std::cout << "WILL TODO UPDATE THIS!\n";
    }
    SimulationConnection::~SimulationConnection()
    {
        if (!simQuit) {
            disconnect();
        }
        if (log) {
            std::fclose(log);
        }
        MPI_Comm_free(&ownComm);
    }
    std::vector<SimState> SimulationConnection::query()
    {
        if (sendCommand(QUERY)) {
            return std::vector<SimState>{};
        }

        int correctedSimSize = intercomm->remoteSize();
        // TODO: This is assuming the use existing comm mode is running in mpi
        // multi-program launch mode.
        if (clientIntraCommRoot != 0) {
            correctedSimSize = clientIntraCommRoot;
        }
        const int simsPerClient = correctedSimSize / size;
        int mySims = simsPerClient;
        int extraOffset = 0;
        if (correctedSimSize % size != 0) {
            extraOffset = std::min(rank, (correctedSimSize % size));
            if ((correctedSimSize % size) - rank > 0) {
                mySims += 1;
            }
        }

        // TODO: Logging should dump to individual files for each rank

        // Currently we assume an M:N mapping of sim ranks to client ranks,
        // where M >= N. As such each simulation's data is assigned to the
        // corresponding rank # on the client side. So client 0 gets data
        // from sim rank 0, and so on. In the case of M > N each client will
        // recv M/N regions, merging these regions is left as an optional operation
        // for the user.
        std::vector<SimState> regions(mySims, SimState());
        using namespace std::chrono;
        size_t bytes_transferred = 0;
        size_t transfer_time = 0;
        for (size_t i = 0; i < regions.size(); ++i) {
            const int regionId = i + simsPerClient * rank + extraOffset;
            SimState &r = regions[i];

            auto start_transfer = high_resolution_clock::now();
            // Send over a ping the the simulation rank to have it send us the data
            intercomm->send(&rank, sizeof(int), regionId);

            // Recieve the header telling us about the simulation state
            SimStateHeader header;
            intercomm->recv(&header, sizeof(SimStateHeader), regionId);

            r.world = header.world;
            r.local = header.local;
            r.ghost = header.ghost;
            r.simRank = header.simRank;
            bytes_transferred += sizeof(SimStateHeader);

            for (uint64_t f = 0; f < header.numFields; ++f) {
                Field field = Field::recv(intercomm, regionId);
                bytes_transferred += field.array->numBytes();

                r.fields[field.name] = field;
            }
            if (header.hasParticles) {
                r.particles = Particles::recv(intercomm, regionId);
                bytes_transferred += r.particles.array->numBytes();
            }

            auto end_transfer = high_resolution_clock::now();
            transfer_time += duration_cast<nanoseconds>(end_transfer - start_transfer).count();
        }

        if (LIBIS_LOGGING) {
            std::vector<size_t> results(size * 2, 0);
            std::array<size_t, 2> send_results = {transfer_time, bytes_transferred};
            MPI_Gather(send_results.data(),
                       2 * sizeof(size_t),
                       MPI_BYTE,
                       results.data(),
                       2 * sizeof(size_t),
                       MPI_BYTE,
                       0,
                       ownComm);
            if (rank == 0) {
                std::fprintf(log, "#------#\nOn %d nodes, with %d sims/client\n"
                        "libIS transfer times: [", size, simsPerClient);
                for (size_t i = 0; i < results.size(); i += 2) {
                    std::fprintf(log, " %luns", results[i]);
                }
                std::fprintf(log, " ]\nlibIS bytes transferred: [");
                for (size_t i = 1; i < results.size(); i += 2) {
                    std::fprintf(log, " %lub", results[i]);
                }
                std::fprintf(log, " ]\n");
            }
        }

        MPI_Barrier(ownComm);
        return regions;
    }
    void SimulationConnection::connectSim()
    {
        intercomm = InterComm::listen(ownComm);
        myPortName = intercomm->portName();
        sendCommand(CONNECT);
        intercomm->accept(ownComm);

        // Recieve command back from rank 0 telling us to continue or quit
        if (rank == 0) {
            intercomm->recv(&simQuit, sizeof(int), 0);
        }
        MPI_Bcast(&simQuit, 1, MPI_INT, 0, ownComm);
    }
    int SimulationConnection::sendCommand(const COMMAND cmd)
    {
        // Sending a command consists of:
        // 1. sending our MPI port name to identify ourself (length, then string)
        // 2. sending the command as an int
        if (rank == 0) {
            if (cmd == CONNECT) {
                std::cout << "Sending command " << cmd << " over socket\n" << std::flush;
                simSocket = socket(AF_INET, SOCK_STREAM, 0);
                if (simSocket < 0) {
                    throw std::runtime_error("Failed to make socket");
                }

                struct hostent *server = gethostbyname(simServer.c_str());
                if (!server) {
                    throw std::runtime_error("Lookup failed for simulation server " + simServer);
                }

                struct sockaddr_in servAddr;
                std::memset(&servAddr, 0, sizeof(servAddr));
                servAddr.sin_family = AF_INET;
                servAddr.sin_port = htons(simPort);
                std::memcpy(&servAddr.sin_addr.s_addr, server->h_addr, server->h_length);
                socklen_t len = sizeof(servAddr);

                if (connect(simSocket, (struct sockaddr *)&servAddr, len) < 0) {
                    throw std::runtime_error("Failed to connect to sim socket");
                }

                // We also send the null-terminator
                const int portLen = myPortName.size() + 1;
                if (send(simSocket, &portLen, sizeof(portLen), 0) != sizeof(portLen)) {
                    throw std::runtime_error("Failed to send port name length");
                }
                if (send(simSocket, myPortName.c_str(), portLen, 0) != portLen) {
                    throw std::runtime_error("Failed to send port name");
                }
                const int cmdVal = cmd;
                if (send(simSocket, &cmdVal, sizeof(cmdVal), 0) != sizeof(cmdVal)) {
                    throw std::runtime_error("Failed to send command");
                }
                close(simSocket);
            } else {
                int cmdVal = cmd;
                intercomm->send(&cmdVal, sizeof(int), 0);

                // Recieve command back from rank 0 telling us to continue or quit
                intercomm->recv(&simQuit, sizeof(int), 0);
            }
        }
        MPI_Barrier(ownComm);

        MPI_Bcast(&simQuit, 1, MPI_INT, 0, ownComm);
        return simQuit;
    }
    void SimulationConnection::disconnect()
    {
        sendCommand(DISCONNECT);
        intercomm = nullptr;
    }

    static std::unique_ptr<SimulationConnection> sim;

    void connect(const std::string &simServer, const int port, MPI_Comm ownComm, bool *sim_quit)
    {
        sim = std::unique_ptr<SimulationConnection>(
            new SimulationConnection(ownComm, simServer, port));
        if (sim->simQuit) {
            sim = nullptr;
            if (sim_quit) {
                *sim_quit = true;
            }
        }
    }

    void connectWithExisting(MPI_Comm ownComm, MPI_Comm simComm, bool *sim_quit)
    {
        sim = std::unique_ptr<SimulationConnection>(new SimulationConnection(ownComm, simComm));
        if (sim->simQuit) {
            sim = nullptr;
            if (sim_quit) {
                *sim_quit = true;
            }
        }
    }

    std::vector<SimState> query(bool *sim_quit)
    {
        auto res = sim->query();
        if (sim->simQuit) {
            sim = nullptr;
            if (sim_quit) {
                *sim_quit = true;
            }
        }
        return res;
    }

    std::future<std::vector<SimState>> query_async(bool *sim_quit)
    {
        std::future<std::vector<SimState>> future =
            std::async(std::launch::async, [&]() { return query(sim_quit); });
        return future;
    }

    void disconnect()
    {
        sim = nullptr;
    }

    bool sim_connected()
    {
        return sim != nullptr;
    }
}
}
