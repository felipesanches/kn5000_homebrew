/* KN7000 shim (src/kn7/pt2_kn7.h): pt2-clone's high-resolution timer is not used. */
#pragma once
#include <stdint.h>
typedef struct hpc_t { uint64_t dummy; } hpc_t;
