#include "model.hpp"

#include <cstring>

namespace aq {

// Index = numeric value of LightProgram enum.
// Values 1 & 2 (old GROW_ONLY / VIEW_ONLY) map to "plants_pro" for API.
static const char* kPrograms[] = {
    "plants_pro",    // 0
    "plants_pro",    // 1 (legacy GROW_ONLY)
    "plants_pro",    // 2 (legacy VIEW_ONLY)
    "fluval_classic",// 3
    "bright_day",    // 4
    "custom",        // 5
};
static constexpr int kNumPrograms = 6;

std::string light_program_to_api(LightProgram p) {
  const int i = static_cast<int>(p);
  if (i >= 0 && i < kNumPrograms) return kPrograms[i];
  return "plants_pro";
}

bool light_program_from_api(const char* s, LightProgram& out) {
  if (strcmp(s, "plants_pro") == 0)     { out = LightProgram::PLANTS_PRO;     return true; }
  if (strcmp(s, "fluval_classic") == 0) { out = LightProgram::FLUVAL_CLASSIC; return true; }
  if (strcmp(s, "bright_day") == 0)     { out = LightProgram::BRIGHT_DAY;     return true; }
  if (strcmp(s, "custom") == 0)         { out = LightProgram::CUSTOM;         return true; }
  return false;
}

std::string operation_mode_to_api(OperationMode m) {
  return m == OperationMode::MANUAL ? "manual" : "auto_24h";
}

bool operation_mode_from_api(const char* s, OperationMode& out) {
  if (strcmp(s, "manual") == 0)   { out = OperationMode::MANUAL;   return true; }
  if (strcmp(s, "auto_24h") == 0) { out = OperationMode::AUTO_24H; return true; }
  return false;
}

}  // namespace aq
