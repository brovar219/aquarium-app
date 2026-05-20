#include "model.hpp"

#include <cstring>

namespace aq {

static const char* kPrograms[] = {"plants_pro", "grow_only", "view_only", "fluval_classic",
                                  "bright_day"};

std::string light_program_to_api(LightProgram p) {
  const int i = static_cast<int>(p);
  if (i >= 0 && i < 5) {
    return kPrograms[i];
  }
  return "plants_pro";
}

bool light_program_from_api(const char* s, LightProgram& out) {
  for (int i = 0; i < 5; ++i) {
    if (strcmp(s, kPrograms[i]) == 0) {
      out = static_cast<LightProgram>(i);
      return true;
    }
  }
  return false;
}

std::string operation_mode_to_api(OperationMode m) {
  return m == OperationMode::MANUAL ? "manual" : "auto_24h";
}

bool operation_mode_from_api(const char* s, OperationMode& out) {
  if (strcmp(s, "manual") == 0) {
    out = OperationMode::MANUAL;
    return true;
  }
  if (strcmp(s, "auto_24h") == 0) {
    out = OperationMode::AUTO_24H;
    return true;
  }
  return false;
}

}  // namespace aq
