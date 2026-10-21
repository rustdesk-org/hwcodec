#include "common.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <deque>
#include <dxgi.h>
#include <string>
#include <windows.h>

namespace {
int elapsed, calls, sleeps, sleep_ms, get_data_ms, reply_at;
int reply_call;
bool watchdog_hit;
struct QueryClock {
  static std::chrono::milliseconds now() {
    return std::chrono::milliseconds(elapsed);
  }
};
namespace util {
inline auto now() { return QueryClock::now(); }
inline int64_t elapsed_ms(std::chrono::milliseconds start) {
  return (now() - start).count();
}
} // namespace util
struct Reply {
  HRESULT hr;
  BOOL complete;
};
std::deque<Reply> replies;
Reply pending, timed_reply;
struct TestContext {
  HRESULT GetData(void *, void *data, UINT size, UINT flags) {
    assert(size == sizeof(BOOL) && flags == 0);
    ++calls;
    // Stop old revisions that keep polling when the injected clock is frozen.
    if (calls > 1200) {
      watchdog_hit = true;
      return E_ABORT;
    }
    elapsed += get_data_ms;
    const bool transitioned = (reply_at >= 0 && elapsed >= reply_at) ||
                              (reply_call >= 0 && calls >= reply_call);
    Reply reply = transitioned ? timed_reply : pending;
    if (!replies.empty()) {
      reply = replies.front();
      replies.pop_front();
    }
    *static_cast<BOOL *>(data) = reply.complete;
    return reply.hr;
  }
};
struct TestQuery {
  void *Get() { return nullptr; }
};
class NativeDevice {
public:
  TestContext *context_;
  TestQuery query_;
  bool Query();
};
void test_sleep(DWORD ms) {
  assert(ms == 1);
  ++sleeps;
  elapsed += sleep_ms;
}
} // namespace
#define Sleep test_sleep
#define LOG_ERROR(message) ((void)(message))
// Exact production method body, extracted by run.ps1; only dependencies vary.
#include "query.inc"
#undef Sleep
#undef LOG_ERROR

int main() {
  int failures = 0;
  auto check = [&](const char *name, std::initializer_list<Reply> sequence,
                   Reply fallback, int call_ms, bool expected,
                   int expected_calls, int expected_ms, int expected_sleeps,
                   int transition_ms = -1, Reply transition = {S_OK, TRUE},
                   int sleep_duration_ms = 16, int transition_call = -1) {
    elapsed = calls = sleeps = 0;
    watchdog_hit = false;
    sleep_ms = sleep_duration_ms;
    get_data_ms = call_ms;
    reply_at = transition_ms;
    reply_call = transition_call;
    timed_reply = transition;
    replies = sequence;
    pending = fallback;
    TestContext context;
    NativeDevice device;
    device.context_ = &context;
    bool ret = device.Query();
    bool pass = ret == expected && calls == expected_calls &&
                elapsed == expected_ms && sleeps == expected_sleeps &&
                !watchdog_hit;
    std::printf("%s %s ret=%d polls=%d elapsed_ms=%d sleeps=%d watchdog=%d\n",
                pass ? "PASS" : "FAIL", name, ret, calls, elapsed, sleeps,
                watchdog_hit);
    if (!pass)
      ++failures;
  };
  check("immediate-completion", {{S_OK, TRUE}}, {S_FALSE, FALSE}, 0, true, 1, 0,
        0);
  check("pending-then-complete",
        {{S_FALSE, FALSE}, {S_FALSE, FALSE}, {S_OK, TRUE}}, {S_FALSE, FALSE}, 0,
        true, 3, 0, 0);
  check("device-removed", {}, {DXGI_ERROR_DEVICE_REMOVED, FALSE}, 0, false, 1,
        0, 0);
  check("hard-error", {}, {E_FAIL, FALSE}, 0, false, 1, 0, 0);
  check("pending-timeout", {}, {S_FALSE, FALSE}, 0, false, 164, 1008, 63);
  check("false-event-timeout", {}, {S_OK, FALSE}, 0, false, 164, 1008, 63);
  check("pending-data-is-not-completion", {}, {S_FALSE, TRUE}, 0, false, 164,
        1008, 63);
  check("slow-get-data-timeout", {}, {S_FALSE, FALSE}, 200, false, 5, 1000, 0);
  check("completion-before-final-sleep", {}, {S_FALSE, FALSE}, 0, true, 163,
        992, 62, 992);
  check("completion-during-final-sleep", {}, {S_FALSE, FALSE}, 0, true, 164,
        1008, 63, 999);
  check("completion-at-deadline", {}, {S_FALSE, FALSE}, 0, true, 164, 1008,
        63, 1000);
  check("device-removed-during-final-sleep", {}, {S_FALSE, FALSE}, 0, false,
        164, 1008, 63, 999, {DXGI_ERROR_DEVICE_REMOVED, FALSE});
  check("completion-after-final-poll", {}, {S_FALSE, FALSE}, 0, false, 164,
        1008, 63, 1009);
  check("slow-get-data-completion", {}, {S_OK, TRUE}, 1100, true, 1, 1100, 0);
  check("one-ms-sleep-timeout", {}, {S_FALSE, FALSE}, 0, false, 1101, 1000,
        1000, -1, {S_OK, TRUE}, 1);
  check("one-ms-sleep-deadline-completion", {}, {S_FALSE, FALSE}, 0, true,
        1101, 1000, 1000, 1000, {S_OK, TRUE}, 1);
  check("frozen-clock-attempt-limit", {}, {S_FALSE, FALSE}, 0, false, 1101, 0,
        1000, -1, {S_OK, TRUE}, 0);
  check("frozen-clock-final-poll-completion", {}, {S_FALSE, FALSE}, 0, true,
        1101, 0, 1000, -1, {S_OK, TRUE}, 0, 1101);
  check("frozen-clock-final-poll-error", {}, {S_FALSE, FALSE}, 0, false, 1101,
        0, 1000, -1, {DXGI_ERROR_DEVICE_REMOVED, FALSE}, 0, 1101);
  check("frozen-clock-completion-after-limit", {}, {S_FALSE, FALSE}, 0, false,
        1101, 0, 1000, -1, {S_OK, TRUE}, 0, 1102);
  std::printf("Failures: %d\n", failures);
  return failures ? 1 : 0;
}
