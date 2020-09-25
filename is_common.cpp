#include <limits>
#include <cassert>
#include <algorithm>
#include "is_common.h"
#include "is_simstate.h"

using namespace is;

extern "C" libISBox3f libISMakeBox3f() {
	return libISBox3f{std::numeric_limits<float>::infinity(),
		std::numeric_limits<float>::infinity(),
		std::numeric_limits<float>::infinity(),
		-std::numeric_limits<float>::infinity(),
		-std::numeric_limits<float>::infinity(),
		-std::numeric_limits<float>::infinity()
	};
}
extern "C" void libISBoxExtend(libISBox3f *box, libISVec3f *v) {
	box->min.x = std::min(box->min.x, v->x);
	box->min.y = std::min(box->min.y, v->y);
	box->min.z = std::min(box->min.z, v->z);

	box->max.x = std::max(box->max.x, v->x);
	box->max.y = std::max(box->max.y, v->y);
	box->max.z = std::max(box->max.z, v->z);
}

