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

#include "is_common.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include "is_simstate.h"

using namespace is;

extern "C" libISBox3f libISMakeBox3f()
{
    return libISBox3f{std::numeric_limits<float>::infinity(),
                      std::numeric_limits<float>::infinity(),
                      std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity()};
}
extern "C" void libISBoxExtend(libISBox3f *box, libISVec3f *v)
{
    box->min.x = std::min(box->min.x, v->x);
    box->min.y = std::min(box->min.y, v->y);
    box->min.z = std::min(box->min.z, v->z);

    box->max.x = std::max(box->max.x, v->x);
    box->max.y = std::max(box->max.y, v->y);
    box->max.z = std::max(box->max.z, v->z);
}
