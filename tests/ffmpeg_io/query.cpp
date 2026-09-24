#include "common.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <deque>
#include <dxgi.h>
#include <string>
#include <windows.h>

namespace {
int elapsed, calls, sleeps, sleep_ms, get_data_ms;
struct QueryClock {
  static std::chrono::milliseconds now() {
    return std::chrono::milliseconds(elapsed);
  }
};
struct Reply {
  HRESULT hr;
  BOOL complete;
};
std::deque<Reply> replies;
Reply pending;
struct TestContext {
  HRESULT GetData(void *, void *data, UINT size, UINT flags) {
    assert(size == sizeof(BOOL) && flags == 0);
    ++calls;
    elapsed += get_data_ms;
    Reply reply = pending;
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
                   int expected_calls, int expected_ms, int expected_sleeps) {
    elapsed = calls = sleeps = 0;
    sleep_ms = 16;
    get_data_ms = call_ms;
    replies = sequence;
    pending = fallback;
    TestContext context;
    NativeDevice device;
    device.context_ = &context;
    bool ret = device.Query();
    bool pass = ret == expected && calls == expected_calls &&
                elapsed == expected_ms && sleeps == expected_sleeps;
    std::printf("%s %s ret=%d polls=%d elapsed_ms=%d sleeps=%d\n",
                pass ? "PASS" : "FAIL", name, ret, calls, elapsed, sleeps);
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
  check("pending-timeout", {}, {S_FALSE, FALSE}, 0, false, 163, 1008, 63);
  check("false-event-timeout", {}, {S_OK, FALSE}, 0, false, 163, 1008, 63);
  check("pending-data-is-not-completion", {}, {S_FALSE, TRUE}, 0, false, 163,
        1008, 63);
  check("slow-get-data-timeout", {}, {S_FALSE, FALSE}, 200, false, 5, 1000, 0);
  std::printf("Failures: %d\n", failures);
  return failures ? 1 : 0;
}
