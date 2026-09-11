extern "C" {
#include <libavcodec/avcodec.h>
}

#include <cassert>
#include <cstdio>
#include <deque>
#include <initializer_list>

#ifdef NDEBUG
#error These tests require assertions.
#endif

namespace {
const AVFrame *expected_frame;
int64_t expected_ms;
int send_error;
int send_calls;
int callbacks;
std::deque<int> receive_results;

int injected_send(AVCodecContext *, const AVFrame *frame) {
  ++send_calls;
  assert(frame == expected_frame);
  assert(frame->pts == expected_ms);
  return send_error;
}

int injected_receive(AVCodecContext *, AVPacket *packet) {
  assert(!receive_results.empty());
  int result = receive_results.front();
  receive_results.pop_front();
  if (result < 0)
    return result;
  if (result == 0) {
    assert(av_new_packet(packet, 1) == 0);
    packet->data[0] = 42;
    packet->pts = expected_ms;
  } else {
    // A successful receive with no packet data exercises the empty-packet
    // guard.
    av_packet_unref(packet);
  }
  return 0;
}
} // namespace

// Compile the production methods with only the FFmpeg I/O boundary replaced.
#define avcodec_send_frame injected_send
#define avcodec_receive_packet injected_receive
#include "../../cpp/ffmpeg_vram/ffmpeg_vram_encode.cpp"
#undef avcodec_send_frame
#undef avcodec_receive_packet

extern "C" void hwcodec_log(int, const char *) {}
extern "C" void hwcodec_av_log_callback(int, const char *) {}

namespace {
void output(const uint8_t *data, int size, int, const void *, int64_t pts) {
  assert(size == 1 && data[0] == 42);
  assert(pts == expected_ms);
  ++callbacks;
}

void prepare(int send_result, std::initializer_list<int> receive) {
  send_error = send_result;
  send_calls = 0;
  callbacks = 0;
  receive_results = receive;
  expected_ms += 100;
}
} // namespace

int main() {
  using EncoderPtr = std::unique_ptr<FFmpegVRamEncoder,
                                     decltype(&ffmpeg_vram_destroy_encoder)>;
  EncoderPtr encoder(
      new FFmpegVRamEncoder(nullptr, 0, H264, 640, 360, 5000, 30, 60),
      ffmpeg_vram_destroy_encoder);
  encoder->c_ = avcodec_alloc_context3(nullptr);
  encoder->frame_ = av_frame_alloc();
  encoder->pkt_ = av_packet_alloc();
  assert(encoder->c_ && encoder->frame_ && encoder->pkt_);
  expected_frame = encoder->frame_;

  prepare(0, {});
  assert(ffmpeg_vram_encode_repeat(encoder.get(), output, nullptr,
                                   expected_ms) < 0);
  assert(send_calls == 0);

  // WARP supplies a valid device for the repeat health check without a GPU.
  assert(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                                    nullptr, 0, D3D11_SDK_VERSION,
                                    &encoder->d3d11Device_, nullptr, nullptr)));
  // Model an input made ready by normal encoding.
  encoder->repeat_ready_ = true;
  const int errors[] = {AVERROR(EAGAIN), AVERROR(EIO), AVERROR_EOF};
  int scenarios = 1;
  for (int error : errors) {
    for (bool fail_send : {true, false}) {
      prepare(fail_send ? error : 0, fail_send
                                         ? std::initializer_list<int>{}
                                         : std::initializer_list<int>{error});
      int result = ffmpeg_vram_encode_repeat(encoder.get(), output, nullptr,
                                             expected_ms);
      assert(result == (fail_send ? error : -1));
      assert(send_calls == 1 && callbacks == 0 && receive_results.empty());
      assert(encoder->repeat_ready_);

      prepare(0, {0, AVERROR(EAGAIN)});
      assert(ffmpeg_vram_encode_repeat(encoder.get(), output, nullptr,
                                       expected_ms) == 0);
      assert(send_calls == 1 && callbacks == 1 && receive_results.empty());
      assert(encoder->repeat_ready_);
      assert(!encoder->pkt_->data && !encoder->pkt_->buf);
      ++scenarios;
    }
  }

  prepare(0, {1});
  assert(ffmpeg_vram_encode_repeat(encoder.get(), output, nullptr,
                                   expected_ms) < 0);
  assert(callbacks == 0 && receive_results.empty() && encoder->repeat_ready_);
  prepare(0, {0, AVERROR(EAGAIN)});
  assert(ffmpeg_vram_encode_repeat(encoder.get(), output, nullptr,
                                   expected_ms) == 0);
  assert(callbacks == 1 && receive_results.empty());
  ++scenarios;

  for (int i = 0; i < 100; ++i) {
    prepare(0, {0, AVERROR(EAGAIN)});
    assert(ffmpeg_vram_encode_repeat(encoder.get(), nullptr, nullptr,
                                     expected_ms) == 0);
    assert(send_calls == 1 && callbacks == 0 && receive_results.empty());
    assert(!encoder->pkt_->data && !encoder->pkt_->buf);
  }
  ++scenarios;

  prepare(0, {});
  assert(ffmpeg_vram_encode(encoder.get(), nullptr, output, nullptr,
                            expected_ms) < 0);
  assert(!encoder->repeat_ready_);
  assert(ffmpeg_vram_encode_repeat(encoder.get(), output, nullptr,
                                   expected_ms) < 0);
  assert(send_calls == 0 && callbacks == 0);
  ++scenarios;

  std::printf("PASS %d repeat failure/retry scenarios\n", scenarios);
}
