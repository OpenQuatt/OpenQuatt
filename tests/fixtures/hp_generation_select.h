#pragma once

#include <string>

class FakeGenerationSelect {
 public:
  bool has_state() const { return has_state_; }
  const std::string& current_option() const { return option_; }

  bool has_state_{true};
  std::string option_{"V1.5"};
};

inline FakeGenerationSelect hp_generation;
