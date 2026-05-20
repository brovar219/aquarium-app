#pragma once

#include "interfaces.hpp"

namespace aq {

class NvsSettingsStore final : public ISettingsStore {
 public:
  bool load(PersistedSettings& out) override;
  bool save(const PersistedSettings& in) override;
};

}  // namespace aq
