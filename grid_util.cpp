#include <iostream>
#include <string>
#include "vec.h"

// Utilty app to compute a gridding given a number of cells
// to build the grid with
int main(int argc, char **argv) {
  vec3<int> grid = compute_grid(std::stoi(argv[1]));
  std::cout << grid.x << " " << grid.y << " " << grid.z << "\n";
  return 0;
}

