// Compile the same sensor integration and failure cases for Solo in the
// normal host-regression runner as well as Duo.
#define OQ_TOPOLOGY_DUO 0
#include "oq_heating_capacity_runtime_test.cpp"
