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

#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <thread>
#include <vector>
#include <mpi.h>
#include "libIS/is_sim.h"
#include "libIS/vec.h"

/* The example simulation generates a random set
 * of particles on each rank and "simulates" them with
 * a random velocity each timestep. The simulation also
 * sends some example 3D grid fields which contain constant data
 */

struct Particle {
    vec3<float> pos;
    int attrib;
};

int rank, size;
vec3<int> grid, brick_id;
std::vector<Particle> particle;
const float speed = .01f;
size_t NUM_PARTICLES = 2000;
int N_STEPS = 40;
std::mt19937 rng;
libISBox3f bounds;

std::vector<float> field_one;
std::vector<uint8_t> field_two;
const std::array<uint64_t, 3> field_dims({64, 64, 64});

void init();
void step();

int main(int ac, char **av)
{
    MPI_Init(&ac, &av);

    bool quiet = false;
    for (int i = 0; i < ac; ++i) {
        if (std::strcmp(av[i], "-n") == 0) {
            N_STEPS = std::atoi(av[++i]);
        } else if (std::strcmp(av[i], "-particles") == 0) {
            NUM_PARTICLES = std::atoi(av[++i]);
        } else if (std::strcmp(av[i], "-quiet") == 0) {
            quiet = true;
        }
    }

    MPI_Comm sim_comm = MPI_COMM_WORLD;
    MPI_Comm_rank(sim_comm, &rank);
    MPI_Comm_size(sim_comm, &size);
    std::cout << "#sim rank " << rank << "/" << size << "\n";

    grid = compute_grid(size);
    brick_id = vec3<int>(rank % grid.x, (rank / grid.x) % grid.y, rank / (grid.x * grid.y));

    std::cout << "Waiting for client connection on port 29374" << std::endl;
    libISInit(MPI_COMM_WORLD, 29374);

    rng = std::mt19937(std::random_device()());
    init();

    libISSimState *libis_state = libISMakeSimState();

    libISVec3f world_min{0.f, 0.f, 0.f};
    libISVec3f world_max{grid.x, grid.y, grid.z};
    libISBox3f world_bounds = libISMakeBox3f();
    libISBoxExtend(&world_bounds, &world_min);
    libISBoxExtend(&world_bounds, &world_max);
    libISSetWorldBounds(libis_state, world_bounds);

    {
        bounds = libISMakeBox3f();
        libISVec3f box_min{brick_id.x, brick_id.y, brick_id.z};
        libISVec3f box_max{brick_id.x + 1.f, brick_id.y + 1.f, brick_id.z + 1.f};
        libISBoxExtend(&bounds, &box_min);
        libISBoxExtend(&bounds, &box_max);
    }
    libISSetLocalBounds(libis_state, bounds);
    libISSetGhostBounds(libis_state, bounds);

    // Setup the shared pointers to our particle and field data
    libISSetParticles(libis_state, NUM_PARTICLES, 0, sizeof(Particle), particle.data());
    libISSetField(libis_state, "field_one", field_dims.data(), FLOAT, field_one.data());
    libISSetField(libis_state, "field_two", field_dims.data(), UINT8, field_two.data());

    for (int i = 0; i < N_STEPS; ++i) {
        MPI_Barrier(sim_comm);
        step();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (rank == 0 && !quiet) {
            std::cout << "Timestep " << i << "\n";
        }
        // Send data to clients or process commands each timestep
        libISProcess(libis_state);
    }

    libISFreeSimState(libis_state);
    libISFinalize();
    MPI_Finalize();
    return 0;
}
void init()
{
    std::uniform_real_distribution<float> distrib;
    for (size_t i = 0; i < NUM_PARTICLES; ++i) {
        Particle v;
        v.pos = vec3<float>(distrib(rng), distrib(rng), distrib(rng)) + vec3<float>(brick_id);
        v.attrib = rank;
        particle.push_back(v);
    }

    // Setup the testing fields. Field one is filled with random data in [0, 1]
    // Field two is filled with our rank number
    field_one.resize(field_dims[0] * field_dims[1] * field_dims[2]);
    field_two.resize(field_dims[0] * field_dims[1] * field_dims[2], uint8_t(rank));

    for (size_t k = 0; k < field_dims[2]; ++k) {
        for (size_t j = 0; j < field_dims[1]; ++j) {
            for (size_t i = 0; i < field_dims[0]; ++i) {
                const vec3<float> voxel_pos = vec3<float>(i + brick_id.x * field_dims[0],
                                                          j + brick_id.y * field_dims[1],
                                                          k + brick_id.z * field_dims[2]);
                const size_t voxel_id = (k * field_dims[1] + j) * field_dims[0] + i;
                field_one[voxel_id] = voxel_pos.length();
            }
        }
    }
}
void step()
{
    std::uniform_real_distribution<float> distrib(-1.f, 1.f);
    // TODO: This should clamp them in the local bounds
    for (auto &p : particle) {
        p.pos += speed * vec3<float>(distrib(rng), distrib(rng), distrib(rng));
    }
}
