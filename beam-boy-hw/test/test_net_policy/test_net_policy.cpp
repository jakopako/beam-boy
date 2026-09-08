// Tests for the credential-retention policy.
//
// The rule this covers is small but easy to get wrong in a way that is painful
// and invisible: erase too eagerly and a user who walks out of range comes home
// to a console that has forgotten their WiFi; erase too reluctantly and a
// mistyped password leaves a Connect entry that can never succeed. Neither
// shows up as a crash, and both are tedious to reproduce on hardware -- you
// need a real AP and a deliberately wrong key. Here the whole truth table runs
// in microseconds.

#include <unity.h>

#include "core/net_policy.h"

using namespace beamboy;

namespace {

// The case that motivated the whole change: credentials typed into the captive
// portal, rejected on their first use. Keeping them would strand the user on a
// Connect entry that cannot work.
void test_first_attempt_with_a_rejected_password_is_discarded() {
  TEST_ASSERT_TRUE(shouldDiscardCredentials(FailReason::kBadPassword, true));
}

// The counterweight. A network that has worked before is assumed good; if the
// AP now rejects the key the user changed it on the router, and silently
// wiping the console's copy would be a surprising thing for it to do on its
// own. Forget remains the deliberate way out.
void test_established_credentials_survive_a_rejection() {
  TEST_ASSERT_FALSE(shouldDiscardCredentials(FailReason::kBadPassword, false));
}

// The deliberate asymmetry, and the one most likely to be "simplified" later by
// someone who reads kNotFound as "the SSID is wrong". It is not: an AP that was
// never heard looks identical whether the name is misspelled or the user is
// simply somewhere else. Out of range is far more common, so this must be kept
// even on a brand-new, never-proven network.
void test_a_network_that_was_not_heard_is_kept_even_when_unproven() {
  TEST_ASSERT_FALSE(shouldDiscardCredentials(FailReason::kNotFound, true));
}

// No verdict arrived before the deadline, so there is nothing to act on. A
// congested band or a slow router must not cost the user their configuration.
void test_a_timeout_is_never_grounds_for_discarding() {
  TEST_ASSERT_FALSE(shouldDiscardCredentials(FailReason::kTimeout, true));
  TEST_ASSERT_FALSE(shouldDiscardCredentials(FailReason::kTimeout, false));
}

// kLost means we were associated a moment ago, which is positive proof the
// credentials are good. Discarding here would be self-contradictory.
void test_losing_an_established_link_never_discards() {
  TEST_ASSERT_FALSE(shouldDiscardCredentials(FailReason::kLost, true));
  TEST_ASSERT_FALSE(shouldDiscardCredentials(FailReason::kLost, false));
}

// Guards the shape of the rule rather than one row of it: exactly one of the
// ten (reason, unproven) combinations may discard. If someone later widens the
// condition -- say, adding kNotFound "because a typo should be cleaned up" --
// this fails even if they also update the row-specific test above.
void test_exactly_one_combination_discards() {
  const FailReason reasons[] = {
      FailReason::kNone,    FailReason::kBadPassword, FailReason::kNotFound,
      FailReason::kTimeout, FailReason::kLost,
  };

  int discards = 0;
  const bool unproven_states[] = {false, true};
  for (FailReason reason : reasons) {
    for (bool unproven : unproven_states) {
      if (shouldDiscardCredentials(reason, unproven)) discards++;
    }
  }

  TEST_ASSERT_EQUAL_INT(1, discards);
}

// kNone is not a failure at all. It reaches this function only if some future
// caller forgets to classify, and the safe response to "no information" is to
// change nothing.
void test_the_absence_of_a_failure_discards_nothing() {
  TEST_ASSERT_FALSE(shouldDiscardCredentials(FailReason::kNone, true));
  TEST_ASSERT_FALSE(shouldDiscardCredentials(FailReason::kNone, false));
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_attempt_with_a_rejected_password_is_discarded);
  RUN_TEST(test_established_credentials_survive_a_rejection);
  RUN_TEST(test_a_network_that_was_not_heard_is_kept_even_when_unproven);
  RUN_TEST(test_a_timeout_is_never_grounds_for_discarding);
  RUN_TEST(test_losing_an_established_link_never_discards);
  RUN_TEST(test_exactly_one_combination_discards);
  RUN_TEST(test_the_absence_of_a_failure_discards_nothing);
  return UNITY_END();
}
