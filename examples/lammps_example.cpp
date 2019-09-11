#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <mpi.h>
#include "domain.h"
#include "libIS/is_sim.h"
#include "library.h"

#include "fix.h"
#include "fix_external.h"
#include "lammps.h"
#include "modify.h"

using namespace LAMMPS_NS;

struct Info {
    int rank;
    LAMMPS *lmp;
};

struct Particle {
    float x, y, z;
    int type;

    Particle(float x, float y, float z, int type) : x(x), y(y), z(z), type(type) {}
};

void mycallback(void *ptr, bigint ntimestep, int nlocal, int *id, double **x, double **f)
{
    Info *info = (Info *)ptr;

    libISBox3f bounds = libISMakeBox3f();
    bounds.min.x = info->lmp->domain->sublo[0];
    bounds.max.x = info->lmp->domain->subhi[0];
    bounds.min.y = info->lmp->domain->sublo[1];
    bounds.max.y = info->lmp->domain->subhi[1];
    bounds.min.z = info->lmp->domain->sublo[2];
    bounds.max.z = info->lmp->domain->subhi[2];

    int *types = (int *)lammps_extract_atom(info->lmp, "type");

    int nghost = *(int *)lammps_extract_global(info->lmp, "nghost");

    // The arrays we get through extract atom include the ghost atoms as well
    double **pos = (double **)lammps_extract_atom(info->lmp, "x");

    libISSimState *state = libISMakeSimState();
    libISSetLocalBounds(state, bounds);
    libISSetGhostBounds(state, bounds);

    // LAMMPS uses doubles for particle positions, so we need to convert to float
    std::vector<Particle> atoms;
    for (int i = 0; i < nlocal + nghost; ++i) {
        float x = static_cast<float>((*pos)[i * 3]);
        float y = static_cast<float>((*pos)[i * 3 + 1]);
        float z = static_cast<float>((*pos)[i * 3 + 2]);
        int type = types[i];
        atoms.emplace_back(x, y, z, type);
    }

    libISSetParticles(state, nlocal, nghost, sizeof(Particle), atoms.data());
    libISProcess(state);

    libISFreeSimState(state);
}

const static std::string USAGE =
    "Usage: ./driver <lammps input> [options] [-lmp <lammps args>]\n"
    "Options:\n"
    "  -h                  Print this help text\n"
    "  -n <int>            Specify the number of steps to simulate for. Default is 10000\n"
    "  -lmp <lammps args>  Pass the list of arguments <args> to LAMMPS as if they were\n"
    "                      command line args to LAMMPS. This must be the last argument, all\n"
    "                      following arguments will be passed to lammps.";

int main(int argc, char **argv)
{
    std::vector<std::string> args(argv, argv + argc);
    if (args.size() < 2) {
        std::cout << USAGE << "\n";
        return 1;
    }
    std::string lammps_input = args[1];

    // Additional args the user may be passing to lammps
    std::vector<char *> lammps_args(1, argv[0]);

    size_t sim_steps = 10000;
    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "-n") {
            sim_steps = std::stoull(args[++i]);
        } else if (args[i] == "-h") {
            std::cout << USAGE << "\n";
            return 0;
        } else if (args[i] == "-lmp") {
            ++i;
            for (; i < args.size(); ++i) {
                lammps_args.push_back(&args[i][0]);
            }
            break;
        }
    }

    MPI_Init(&argc, &argv);

    MPI_Comm simComm = MPI_COMM_WORLD;

    int rank;
    int worldSize;
    MPI_Comm_rank(simComm, &rank);
    MPI_Comm_size(simComm, &worldSize);

    const int simPort = 29374;
    libISInit(simComm, simPort);

    LAMMPS *lammps;
    lammps_open(lammps_args.size(), lammps_args.data(), simComm, (void **)&lammps);

    /* run the input script thru LAMMPS one line at a time until end-of-file
       driver proc 0 reads a line, Bcasts it to all procs
       (could just send it to proc 0 of comm_lammps and let it Bcast)
       all LAMMPS procs call lammps_command() on the line */

    if (rank == 0) {
        std::cout << "Loading lammps input: '" << lammps_input << "'\n";
        std::ifstream input(lammps_input.c_str());
        for (std::string line; std::getline(input, line);) {
            int len = line.size();
            // Skip empty lines
            if (len == 0) {
                continue;
            }
            MPI_Bcast(&len, 1, MPI_INT, 0, simComm);
            MPI_Bcast(&line[0], len, MPI_CHAR, 0, simComm);
            lammps_command(lammps, &line[0]);
        }
        // Bcast out we're done with the file
        int len = 0;
        MPI_Bcast(&len, 1, MPI_INT, 0, simComm);
    } else {
        while (true) {
            int len = 0;
            MPI_Bcast(&len, 1, MPI_INT, 0, simComm);
            if (len == 0) {
                break;
            } else {
                std::vector<char> line(len + 1, '\0');
                MPI_Bcast(line.data(), len, MPI_CHAR, 0, simComm);
                lammps_command(lammps, line.data());
            }
        }
    }

    // Setup the fix external callback
    Info info;
    info.rank = rank;
    info.lmp = lammps;

    int ifix = lammps->modify->find_fix_by_style("external");
    // If there's no external fix, install one we can use
    if (ifix == -1) {
        lammps_command(lammps, "fix _driver_gen_ext all external pf/callback 1 1");
        ifix = lammps->modify->find_fix_by_style("external");
    }
    FixExternal *fix = (FixExternal *)lammps->modify->fix[ifix];
    fix->set_callback(mycallback, &info);

    std::shared_ptr<std::ofstream> log;
    if (getenv("LIBIS_LAMMPS_LOG_OUTPUT")) {
        log = std::make_shared<std::ofstream>(getenv("LIBIS_LAMMPS_LOG_OUTPUT"));
    }
    // run for a number of steps
    for (size_t i = 0; i < sim_steps; ++i) {
        using namespace std::chrono;

        auto start = high_resolution_clock::now();
        lammps_command(lammps, "run 1");
        auto end = high_resolution_clock::now();
        if (rank == 0 && log) {
            *log << "LAMMPS step took: " << duration_cast<milliseconds>(end - start).count()
                 << "ms\n"
                 << std::flush;
        }
    }
    lammps_close(lammps);
    std::cout << "Simulation complete\n";

    libISFinalize();

    MPI_Finalize();
    return 0;
}
