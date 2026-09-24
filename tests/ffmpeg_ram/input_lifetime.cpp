extern "C" {
#include <libavcodec/avcodec.h>
}

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>

#ifdef NDEBUG
#error These tests require assertions.
#endif

namespace {
std::vector<AVFrame *> retained;
bool packet_ready;
int callbacks;

int injected_open(AVCodecContext *, const AVCodec *, AVDictionary **) {
  return 0;
}

int injected_send(AVCodecContext *, const AVFrame *frame) {
  AVFrame *copy = av_frame_clone(frame);
  assert(copy);
  retained.push_back(copy);
  packet_ready = true;
  return 0;
}

int injected_receive(AVCodecContext *, AVPacket *packet) {
  if (!packet_ready)
    return AVERROR(EAGAIN);
  packet_ready = false;
  assert(av_new_packet(packet, 1) == 0);
  packet->data[0] = 42;
  packet->pts = retained.back()->pts;
  return 0;
}
} // namespace

// Keep real frame allocation, reference counting and copies; replace codec I/O.
#define avcodec_open2 injected_open
#define avcodec_send_frame injected_send
#define avcodec_receive_packet injected_receive
#include "../../cpp/ffmpeg_ram/ffmpeg_ram_encode.cpp"
#undef avcodec_open2
#undef avcodec_send_frame
#undef avcodec_receive_packet

extern "C" void hwcodec_log(int, const char *) {}
extern "C" void hwcodec_av_log_callback(int, const char *) {}

namespace {
void output(const uint8_t *data, int size, int64_t, int, const void *) {
  assert(size == 1 && data[0] == 42);
  ++callbacks;
}

uint8_t pixel(int plane, int x, int y, int seed) {
  return static_cast<uint8_t>(plane * 47 + x * 3 + y * 7 + seed);
}

void check_image(const AVFrame *frame, int seed) {
  const bool nv12 = frame->format == AV_PIX_FMT_NV12;
  for (int p = 0; p < (nv12 ? 2 : 3); ++p) {
    const int width = p == 0 || nv12 ? frame->width : frame->width / 2;
    const int height = p == 0 ? frame->height : frame->height / 2;
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x)
        assert(frame->data[p][y * frame->linesize[p] + x] == pixel(p, x, y, seed));
  }
}

void check_lifetime(AVPixelFormat format, int align) {
  const int width = 66, height = 34;
  FFmpegRamEncoder encoder("h264_qsv", nullptr, width, height, format, align,
                           30, 60, RC_CBR, Quality_Default, 1000, -1, 1, -1,
                           output);
  int stride[AV_NUM_DATA_POINTERS] = {};
  int offset[AV_NUM_DATA_POINTERS] = {};
  int length = 0;
  assert(encoder.init(stride, offset, &length));
  callbacks = 0;
  for (int i = 0; i < 3; ++i) {
    const int seed = 23 + i * 59;
    std::vector<uint8_t> input(length, 0xee);
    const bool nv12 = format == AV_PIX_FMT_NV12;
    for (int p = 0; p < (nv12 ? 2 : 3); ++p) {
      const int row_bytes = p == 0 || nv12 ? width : width / 2;
      const int rows = p == 0 ? height : height / 2;
      const int start = p == 0 ? 0 : offset[p - 1];
      for (int y = 0; y < rows; ++y)
        for (int x = 0; x < row_bytes; ++x)
          input[start + y * stride[p] + x] = pixel(p, x, y, seed);
    }
    assert(encoder.encode(input.data(), length, nullptr, i * 33) == 0);
    assert(callbacks == i + 1);
    check_image(retained.back(), seed);
    std::fill(input.begin(), input.end(), 0);
    check_image(retained.back(), seed);
    std::vector<uint8_t>().swap(input);
    for (int j = 0; j <= i; ++j)
      check_image(retained[j], 23 + j * 59);

    std::vector<uint8_t> short_input(length - 1);
    assert(encoder.encode(short_input.data(), length - 1, nullptr, i * 33 + 1) < 0);
    assert(callbacks == i + 1);
    if (align == 256)
      assert(encoder.frame_->linesize[0] != stride[0]);
  }
  encoder.free_encoder();
  for (size_t i = 0; i < retained.size(); ++i) {
    check_image(retained[i], 23 + static_cast<int>(i) * 59);
    av_frame_free(&retained[i]);
  }
  retained.clear();
  std::printf("PASS RAM input lifetime: format=%d align=%d\n", format, align);
}
} // namespace

int main() {
  for (AVPixelFormat format : {AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P})
    for (int align : {0, 256})
      check_lifetime(format, align);
}
