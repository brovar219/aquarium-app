#pragma once

#include "model.hpp"

namespace aq {

/** JSON-серіалізація знімку стану (malloc — звільняти `free()`). */
char* state_to_json_malloc(const DeviceState& s);

}  // namespace aq
