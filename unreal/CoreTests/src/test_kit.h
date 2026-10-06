// Fourfold core tests - the data-only test kit of test_core_verbs.gd (one move per verb, sub-elements 1-3).
#pragma once

namespace fft {
// Binds the test kit's moves over sub-elements 1-3 and adds its rules. Call inside SimHarness::begin_scope().
void register_test_kit();
}  // namespace fft
