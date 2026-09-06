#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/sources/oq_resolved_learning_source.h"

using namespace oq_sources;

void test_optional_receipt_replaces_missing_and_invalid_fields() {
  RawFloatReceipt receipt;
  observe_optional(receipt, true, 19.5f, 100U);
  assert(receipt.received && receipt.valid && receipt.value == 19.5f && receipt.received_ms == 100U);

  observe_optional(receipt, false, 0.0f, 200U);
  assert(receipt.received && !receipt.valid && isnan(receipt.value) && receipt.received_ms == 200U);

  observe_optional(receipt, true, NAN, 300U);
  assert(receipt.received && !receipt.valid && isnan(receipt.value) && receipt.received_ms == 300U);
}

void test_configuration_generation_observes_a_b_a() {
  SourceConfigurationGeneration owner;
  const SourceConfigurationKey a{1U, 2U, 3U, 10U};
  const SourceConfigurationKey b{1U, 2U, 3U, 11U};

  assert(owner.observe(a) == 1U);
  assert(owner.observe(a) == 1U);
  assert(owner.observe(b) == 2U);
  assert(owner.observe(a) == 3U);
  assert(next_configuration_generation(UINT32_MAX - 1U) == UINT32_MAX);
  assert(next_configuration_generation(UINT32_MAX) == 0U);
}

void test_resolution_generation_observes_route_a_b_a_between_learner_ticks() {
  SourceConfigurationGeneration owner;
  const SourceConfigurationKey configuration{2U, 0U, 0U, 0U};
  owner.observe(configuration);
  ResolvedLearningSource source;
  source.route = LearningSourceRoute::HP1_FLOW;
  source.provenance = LearningSourceProvenance::PHYSICAL_RECEIPT;
  const uint32_t first_a = owner.observe_resolution(source);

  source.route = LearningSourceRoute::SYNTHESIZED_ZERO_FLOW;
  source.provenance = LearningSourceProvenance::SYNTHESIZED;
  const uint32_t b = owner.observe_resolution(source);

  source.route = LearningSourceRoute::HP1_FLOW;
  source.provenance = LearningSourceProvenance::PHYSICAL_RECEIPT;
  const uint32_t second_a = owner.observe_resolution(source);
  assert(first_a < b && b < second_a);
  assert(owner.observe_resolution(source) == second_a);
}

void test_physical_source_uses_value_paired_with_receipt_time() {
  RawFloatReceipt receipt;
  receipt.observe(725.0f, static_cast<uint64_t>(UINT32_MAX) + 25U, true);
  const auto source = physical_source(LearningSourceRoute::HP2_FLOW, receipt, 7U);
  assert(source.valid);
  assert(source.value == 725.0f);
  assert(source.route == LearningSourceRoute::HP2_FLOW);
  assert(source.receipt.received_ms == static_cast<uint64_t>(UINT32_MAX) + 25U);
  assert(source.configuration_generation == 7U);
  assert(source.provenance == LearningSourceProvenance::PHYSICAL_RECEIPT);

  const auto unselected = physical_source(LearningSourceRoute::HP2_FLOW, receipt, 7U, false);
  assert(!unselected.valid && isnan(unselected.value));
  assert(unselected.provenance == LearningSourceProvenance::UNKNOWN);
}

void test_unsupported_and_synthesized_values_never_gain_receipts() {
  const auto aggregate = unsupported_source(800.0f, true, LearningSourceRoute::FLOW_AGGREGATE, 2U);
  assert(aggregate.valid && aggregate.value == 800.0f);
  assert(!aggregate.receipt.received && !aggregate.receipt.valid);
  assert(aggregate.provenance == LearningSourceProvenance::UNSUPPORTED);

  const auto zero = unsupported_source(0.0f, true, LearningSourceRoute::SYNTHESIZED_ZERO_FLOW, 3U,
                                       LearningSourceProvenance::SYNTHESIZED);
  assert(zero.valid && zero.value == 0.0f);
  assert(!zero.receipt.received && !zero.receipt.valid);
  assert(zero.provenance == LearningSourceProvenance::SYNTHESIZED);
}

void test_composite_keeps_both_physical_receipts_but_stays_unsupported() {
  RawFloatReceipt hp1;
  RawFloatReceipt hp2;
  hp1.observe(700.0f, 100U, true);
  hp2.observe(900.0f, 110U, true);
  const SourceConfigurationKey configuration{2U, 0U, 0U, 0U};
  const auto source = unsupported_composite_source(800.0f, true, LearningSourceRoute::FLOW_AGGREGATE,
                                                   LearningSourceRoute::HP1_FLOW, LearningSourceRoute::HP2_FLOW, hp1,
                                                   hp2, LearningCompositeOperation::ARITHMETIC_MEAN, configuration, 4U);
  assert(source.valid && source.value == 800.0f);
  assert(source.component_route == LearningSourceRoute::HP1_FLOW);
  assert(source.secondary_route == LearningSourceRoute::HP2_FLOW);
  assert(source.receipt.value == 700.0f && source.secondary_receipt.value == 900.0f);
  assert(source.composite_operation == LearningCompositeOperation::ARITHMETIC_MEAN);
  assert(source.provenance == LearningSourceProvenance::UNSUPPORTED);
  assert(same_configuration(source.configuration, configuration));
}

void test_cached_sources_fail_closed_when_current_receipt_is_revoked() {
  RawFloatReceipt hp1;
  RawFloatReceipt hp2;
  hp1.observe(700.0f, 100U, true);
  hp2.observe(900.0f, 110U, true);

  const auto direct = physical_source(LearningSourceRoute::HP1_FLOW, hp1, 4U);
  assert(validate_current_receipts(direct, hp1).valid);
  auto revoked = hp1;
  revoked.invalidate();
  const auto invalid_direct = validate_current_receipts(direct, revoked);
  assert(!invalid_direct.valid && isnan(invalid_direct.value));
  assert(invalid_direct.provenance == LearningSourceProvenance::UNKNOWN);

  const auto composite = unsupported_composite_source(800.0f, true, LearningSourceRoute::FLOW_AGGREGATE,
                                                      LearningSourceRoute::HP1_FLOW, LearningSourceRoute::HP2_FLOW, hp1,
                                                      hp2, LearningCompositeOperation::ARITHMETIC_MEAN, {}, 5U);
  assert(validate_current_receipts(composite, hp1, hp2).valid);
  hp2.invalidate();
  const auto invalid_composite = validate_current_receipts(composite, hp1, hp2);
  assert(!invalid_composite.valid && isnan(invalid_composite.value));
  assert(invalid_composite.provenance == LearningSourceProvenance::UNKNOWN);
  assert(!invalid_composite.secondary_receipt.valid);
}

void test_direct_route_requires_one_unique_matching_receipt() {
  RawFloatReceipt hp1;
  RawFloatReceipt hp2;
  hp1.observe(4.0f, 100U, true);
  hp2.observe(6.0f, 110U, true);
  assert(uniquely_matching_receipt_route(4.0f, true, LearningSourceRoute::HP1_OUTSIDE, hp1,
                                         LearningSourceRoute::HP2_OUTSIDE, hp2) == LearningSourceRoute::HP1_OUTSIDE);
  assert(uniquely_matching_receipt_route(6.0f, true, LearningSourceRoute::HP1_OUTSIDE, hp1,
                                         LearningSourceRoute::HP2_OUTSIDE, hp2) == LearningSourceRoute::HP2_OUTSIDE);

  hp2.observe(4.0f, 120U, true);
  assert(uniquely_matching_receipt_route(4.0f, true, LearningSourceRoute::HP1_OUTSIDE, hp1,
                                         LearningSourceRoute::HP2_OUTSIDE, hp2) == LearningSourceRoute::NONE);
  hp1.invalidate();
  assert(uniquely_matching_receipt_route(4.0f, true, LearningSourceRoute::HP1_OUTSIDE, hp1,
                                         LearningSourceRoute::HP2_OUTSIDE, hp2) == LearningSourceRoute::HP2_OUTSIDE);
}

int main() {
  test_optional_receipt_replaces_missing_and_invalid_fields();
  test_configuration_generation_observes_a_b_a();
  test_resolution_generation_observes_route_a_b_a_between_learner_ticks();
  test_physical_source_uses_value_paired_with_receipt_time();
  test_unsupported_and_synthesized_values_never_gain_receipts();
  test_composite_keeps_both_physical_receipts_but_stays_unsupported();
  test_cached_sources_fail_closed_when_current_receipt_is_revoked();
  test_direct_route_requires_one_unique_matching_receipt();
  return 0;
}
