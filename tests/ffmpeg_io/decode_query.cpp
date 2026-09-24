extern "C" {
#include <libavcodec/avcodec.h>
}
#include "platform/win/win.h"
#include "util.h"
#include <cassert>
#include <cstdio>
#include <deque>
#include <vector>

#ifdef NDEBUG
#error These tests require assertions.
#endif

namespace {
int sends, receives, marker, query_fail_at, query_calls;
std::deque<int> receive_results;
std::vector<int> output_markers;
ComPtr<ID3D11Texture2D> texture;

int send_packet(AVCodecContext *, const AVPacket *packet) {
  assert(packet->size == 4 && packet->data[0] == 42);
  ++sends;
  return 0;
}
int receive_frame(AVCodecContext *, AVFrame *frame) {
  av_frame_unref(frame);
  ++receives;
  assert(!receive_results.empty());
  int ret = receive_results.front();
  receive_results.pop_front();
  if (ret < 0)
    return ret;
  marker = ret;
  frame->width = frame->height = 16;
  frame->format = AV_PIX_FMT_D3D11;
  frame->data[0] = reinterpret_cast<uint8_t *>(texture.Get());
  return 0;
}
// Exercise the production decoder while injecting conversion/completion results.
class TestNativeDevice : public NativeDevice {
public:
  bool EnsureTexture(int, int) { return true; }
  int next() { return 0; }
  void BeginQuery() {}
  void EndQuery() {}
  bool Query() { return ++query_calls != query_fail_at; }
  bool Nv12ToBgra(int, int, ID3D11Texture2D *, ID3D11Texture2D *, int) {
    return true;
  }
  ID3D11Texture2D *GetCurrentTexture() { return texture.Get(); }
};
} // namespace
namespace util {
inline int64_t test_elapsed_ms(std::chrono::steady_clock::time_point) { return 0; }
} // namespace util
#define elapsed_ms test_elapsed_ms
#define avcodec_send_packet send_packet
#define avcodec_receive_frame receive_frame
#define NativeDevice TestNativeDevice
// Supplied by run.ps1, from either the working tree or an exact git revision.
#include "production.inc"
#undef NativeDevice
#undef elapsed_ms
#undef avcodec_send_packet
#undef avcodec_receive_frame

extern "C" void hwcodec_log(int, const char *) {}
extern "C" void hwcodec_av_log_callback(int, const char *) {}

namespace {
void output(void *value, const void *) {
  assert(value == texture.Get());
  output_markers.push_back(marker);
}
int run_codec() {
  uint8_t input[] = {42, 42, 42, 42};
  FFmpegVRamDecoder codec(nullptr, 0, H264);
  codec.c_ = avcodec_alloc_context3(nullptr);
  codec.frame_ = av_frame_alloc();
  codec.pkt_ = av_packet_alloc();
  codec.native_ = std::make_unique<TestNativeDevice>();
  int ret = ffmpeg_vram_decode(&codec, input, sizeof(input), output, nullptr);
  codec.destroy();
  return ret;
}
} // namespace

int main() {
  ComPtr<ID3D11Device> device;
  assert(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                                   nullptr, 0, D3D11_SDK_VERSION, &device,
                                   nullptr, nullptr)));
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = desc.Height = 16;
  desc.MipLevels = desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_NV12;
  desc.SampleDesc.Count = 1;
  assert(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)));

  int failures = 0;
  auto check = [&](const char *name, int fail_at, int expected_ret,
                   int expected_receives, std::initializer_list<int> expected) {
    query_fail_at = fail_at;
    sends = receives = query_calls = 0;
    receive_results = fail_at == 1 ? std::deque<int>{31, AVERROR(EAGAIN)}
                                  : std::deque<int>{30, 31, AVERROR(EAGAIN)};
    output_markers.clear();
    int ret = run_codec();
    bool pass = ret == expected_ret && sends == 1 &&
                receives == expected_receives &&
                query_calls == (fail_at ? fail_at : 2) &&
                output_markers == std::vector<int>(expected);
    std::printf("%s %s ret=%d sends=%d receives=%d queries=%d outputs=%zu\n",
                pass ? "PASS" : "FAIL", name, ret, sends, receives, query_calls,
                output_markers.size());
    if (!pass)
      ++failures;
  };
  check("query-success", 0, 0, 3, {30, 31});
  check("query-failure", 1, -1, 1, {});
  check("query-failure-after-output", 2, -1, 2, {30});
  std::printf("Failures: %d\n", failures);
  return failures ? 1 : 0;
}
