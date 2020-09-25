#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <thread>
#include <vector>
#include <mpi.h>
#include "json.hpp"
#include "libIS/is_sim.h"
#include "libIS/vec.h"

/* The volume example simulation loads a RAW volume file
 * which it then sends to clients each timestep.
 */

using json = nlohmann::json;
using vec3i = vec3<int>;
using vec3f = vec3<float>;

struct box3f {
    vec3f lower;
    vec3f upper;

    box3f()
        : lower(std::numeric_limits<float>::infinity()),
          upper(-std::numeric_limits<float>::infinity())
    {
    }

    box3f(const vec3f &lo, const vec3f &hi) : lower(lo), upper(hi) {}
};

struct VolumeBrick {
    box3f bounds;
    vec3i dims;
    libISDType dtype;
    std::shared_ptr<std::vector<uint8_t>> voxel_data;
};

VolumeBrick load_volume_brick(json &config, const int mpi_rank, const int mpi_size);

template <typename T>
inline vec3<T> get_vec3(const json &j)
{
    vec3<T> v;
    for (size_t i = 0; i < 3; ++i) {
        v[i] = j[i].get<T>();
    }
    return v;
}

int main(int ac, char **av)
{
    MPI_Init(&ac, &av);

    json config;
    bool quiet = false;
    int N_STEPS = 10;
    for (int i = 1; i < ac; ++i) {
        if (std::strcmp(av[i], "-n") == 0) {
            N_STEPS = std::atoi(av[++i]);
        } else if (std::strcmp(av[i], "-quiet") == 0) {
            quiet = true;
        } else {
            std::ifstream cfg_file(av[i]);
            if (!cfg_file) {
                std::cerr << "[error]: Failed to open config file " << av[i] << "\n";
                throw std::runtime_error("Failed to open input config file");
            }
            cfg_file >> config;
        }
    }

    if (config.is_null()) {
        std::cerr
            << "[error]: A config file must be provided. Use the fetch_scivis.py script\n";
        throw std::runtime_error("No config file provided");
    }

    MPI_Comm sim_comm = MPI_COMM_WORLD;
    int rank = 0;
    MPI_Comm_rank(sim_comm, &rank);
    int size = 0;
    MPI_Comm_size(sim_comm, &size);
    std::cout << "#sim rank " << rank << "/" << size << "\n";

    std::cout << "Now listening for client connection on port 29374" << std::endl;
    libISInit(MPI_COMM_WORLD, 29374);

    VolumeBrick brick = load_volume_brick(config, rank, size);
    const vec3f volume_bounds =
        get_vec3<float>(config["size"]) * get_vec3<float>(config["spacing"]);

    std::cout << "spacing: " << get_vec3<float>(config["spacing"]) << "\n";
    libISSimState *libis_state = libISMakeSimState();

    libISVec3f world_min{0.f, 0.f, 0.f};
    libISVec3f world_max{volume_bounds.x, volume_bounds.y, volume_bounds.z};
    libISBox3f world_bounds = libISMakeBox3f();
    libISBoxExtend(&world_bounds, &world_min);
    libISBoxExtend(&world_bounds, &world_max);
    libISSetWorldBounds(libis_state, world_bounds);

    libISBox3f local_bounds = libISMakeBox3f();
    libISVec3f box_min{brick.bounds.lower.x, brick.bounds.lower.y, brick.bounds.lower.z};
    libISVec3f box_max{brick.bounds.upper.x, brick.bounds.upper.y, brick.bounds.upper.z};
    libISBoxExtend(&local_bounds, &box_min);
    libISBoxExtend(&local_bounds, &box_max);
    libISSetLocalBounds(libis_state, local_bounds);
    libISSetGhostBounds(libis_state, local_bounds);

    // Setup the shared pointers to our field data
    const std::array<uint64_t, 3> field_dims = {brick.dims.x, brick.dims.y, brick.dims.z};
    libISSetField(
        libis_state, "field_one", field_dims.data(), brick.dtype, brick.voxel_data->data());

    for (int i = 0; i < N_STEPS; ++i) {
        MPI_Barrier(sim_comm);
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

std::shared_ptr<std::vector<uint8_t>> load_raw_volume(const std::string &file,
                                                      const std::string &dtype,
                                                      const vec3i &vol_dims,
                                                      const vec3i &brick_dims,
                                                      const vec3i &brick_offset);

VolumeBrick load_volume_brick(json &config, const int mpi_rank, const int mpi_size)
{
    using namespace std::chrono;
    VolumeBrick brick;

    const std::string volume_file = config["volume"].get<std::string>();
    const vec3i volume_dims = get_vec3<int>(config["size"]);
    const vec3f spacing = get_vec3<float>(config["spacing"]);
    const vec3i grid = compute_grid(mpi_size);
    const vec3i brick_id(
        mpi_rank % grid.x, (mpi_rank / grid.x) % grid.y, mpi_rank / (grid.x * grid.y));

    brick.dims = volume_dims / grid;

    const vec3i brick_lower = brick_id * brick.dims;
    const vec3i brick_upper = brick_id * brick.dims + brick.dims;

    brick.bounds = box3f(vec3f(brick_lower) * spacing, vec3f(brick_upper) * spacing);

    const std::string voxel_type = config["type"].get<std::string>();
    // TODO: may need uint16 to test non-uniform size volume?
    if (voxel_type == "uint8") {
        brick.dtype = UINT8;
    } else if (voxel_type == "float32") {
        brick.dtype = FLOAT;
    } else if (voxel_type == "float64") {
        brick.dtype = DOUBLE;
    } else {
        std::cerr << "[error]: Unsupported voxel type: " + voxel_type;
        throw std::runtime_error("[error]: Unsupported voxel type: " + voxel_type);
    }

    const size_t n_voxels = size_t(brick.dims.x) * size_t(brick.dims.y) * size_t(brick.dims.z);

    auto start = high_resolution_clock::now();

    brick.voxel_data =
        load_raw_volume(volume_file, voxel_type, volume_dims, brick.dims, brick_lower);

    auto end = high_resolution_clock::now();
    if (mpi_rank == 0) {
        std::cout << "Loading volume brick took "
                  << duration_cast<milliseconds>(end - start).count() << "ms\n";
    }

    return brick;
}

std::shared_ptr<std::vector<uint8_t>> load_raw_volume(const std::string &file,
                                                      const std::string &dtype,
                                                      const vec3i &vol_dims,
                                                      const vec3i &brick_dims,
                                                      const vec3i &brick_offset)
{
    size_t voxel_size = 0;
    MPI_Datatype voxel_type;
    if (dtype == "uint8") {
        voxel_type = MPI_UNSIGNED_CHAR;
        voxel_size = 1;
    } else if (dtype == "uint16") {
        voxel_type = MPI_UNSIGNED_SHORT;
        voxel_size = 2;
    } else if (dtype == "float32") {
        voxel_type = MPI_FLOAT;
        voxel_size = 4;
    } else if (dtype == "float64") {
        voxel_type = MPI_DOUBLE;
        voxel_size = 8;
    } else {
        throw std::runtime_error("Unrecognized voxel type " + dtype);
    }

    const size_t n_voxels = size_t(brick_dims.x) * size_t(brick_dims.y) * size_t(brick_dims.z);
    auto voxel_data = std::make_shared<std::vector<uint8_t>>(n_voxels * voxel_size, 0);

    // MPI still uses 32-bit signed ints for counts of objects, so we have to split reads
    // of large data up so the count doesn't overflow. This assumes each X-Y slice is within
    // that size limit and reads chunks
    // It also seems like that size limit is based on bytes, not # of elements
    const size_t elements_per_read = std::numeric_limits<int32_t>::max() / voxel_size;
    const size_t n_chunks =
        n_voxels / elements_per_read + (n_voxels % elements_per_read > 0 ? 1 : 0);
    const size_t chunk_thickness = brick_dims.z / n_chunks;

    MPI_File file_handle;
    auto rc = MPI_File_open(
        MPI_COMM_WORLD, file.c_str(), MPI_MODE_RDONLY, MPI_INFO_NULL, &file_handle);
    if (rc != MPI_SUCCESS) {
        std::cerr << "[error]: Failed to open file " << file << "\n";
        throw std::runtime_error("Failed to open " + file);
    }
    for (size_t i = 0; i < n_chunks; ++i) {
        const vec3i chunk_offset(
            brick_offset.x, brick_offset.y, brick_offset.z + i * chunk_thickness);
        vec3i chunk_dims = vec3i(brick_dims.x, brick_dims.y, chunk_thickness);
        if (i * chunk_thickness + chunk_thickness >= brick_dims.z) {
            chunk_dims.z = brick_dims.z - i * chunk_thickness;
        }
        const size_t byte_offset = i * size_t(chunk_thickness) * size_t(brick_dims.y) *
                                   size_t(brick_dims.x) * voxel_size;
        const int chunk_voxels = chunk_dims.x * chunk_dims.y * chunk_dims.z;

        MPI_Datatype brick_type;
        MPI_Type_create_subarray(3,
                                 &vol_dims.x,
                                 &chunk_dims.x,
                                 &chunk_offset.x,
                                 MPI_ORDER_FORTRAN,
                                 voxel_type,
                                 &brick_type);
        MPI_Type_commit(&brick_type);

        MPI_File_set_view(file_handle, 0, voxel_type, brick_type, "native", MPI_INFO_NULL);
        rc = MPI_File_read_all(file_handle,
                               voxel_data->data() + byte_offset,
                               chunk_voxels,
                               voxel_type,
                               MPI_STATUS_IGNORE);
        if (rc != MPI_SUCCESS) {
            std::cerr << "[error]: Failed to read all voxels from file.\n";
            throw std::runtime_error("Failed to read all voxels from file");
        }
        MPI_Type_free(&brick_type);
    }
    MPI_File_close(&file_handle);
    return voxel_data;
}
