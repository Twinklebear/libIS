#pragma once

#include <stdint.h>

typedef struct {
	float x, y, z;
} libISVec3f;

typedef struct {
	libISVec3f min, max;
} libISBox3f;

typedef enum {
	UINT8,
	FLOAT,
	DOUBLE,
	INVALID,
} libISDType;

extern "C" libISBox3f libISMakeBox3f();
extern "C" void libISBoxExtend(libISBox3f *box, libISVec3f *v);

