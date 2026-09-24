extern "C" {
#include <libavcodec/avcodec.h>
}
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>

namespace {
std::vector<AVFrame *> retained;
int replacements, stale_copies;
bool fail_allocation, packet_ready;
int get_buffer(AVFrame *frame, int align) {
  ++replacements;
  if (fail_allocation) {
    fail_allocation = false;
    return AVERROR(ENOMEM);
  }
  return av_frame_get_buffer(frame, align);
}
int make_writable(AVFrame *frame) {
  if (!av_frame_is_writable(frame)) {
    ++replacements;
    if (fail_allocation) {
      fail_allocation = false;
      return AVERROR(ENOMEM);
    }
    ++stale_copies;
  }
  return av_frame_make_writable(frame);
}
int open_codec(AVCodecContext *, const AVCodec *, AVDictionary **) { return 0; }
int send_frame(AVCodecContext *, const AVFrame *frame) {
  retained.push_back(av_frame_clone(frame));
  assert(retained.back());
  packet_ready = true;
  return 0;
}
int receive_packet(AVCodecContext *, AVPacket *packet) {
  av_packet_unref(packet);
  if (!packet_ready)
    return AVERROR(EAGAIN);
  packet_ready = false;
  return av_new_packet(packet, 1);
}
} // namespace
#define avcodec_open2 open_codec
#define avcodec_send_frame send_frame
#define avcodec_receive_packet receive_packet
#define av_frame_get_buffer get_buffer
#define av_frame_make_writable make_writable
#include "production.inc"
#undef avcodec_open2
#undef avcodec_send_frame
#undef avcodec_receive_packet
#undef av_frame_get_buffer
#undef av_frame_make_writable
extern "C" void hwcodec_log(int, const char *) {}
extern "C" void hwcodec_av_log_callback(int, const char *) {}

namespace {
void output(const uint8_t *, int, int64_t, int, const void *) {}
void release_retained() {
  for (auto &frame : retained)
    av_frame_free(&frame);
  retained.clear();
}
int failures;
void result(const char *name, bool pass, int before, int after) {
  std::printf("%s %s observed=%d expected=%d\n", pass ? "PASS" : "FAIL", name,
              before, after);
  if (!pass)
    ++failures;
}
struct Fixture {
  FFmpegRamEncoder encoder;
  int stride[8] = {}, offset[8] = {}, length = 0;
  std::vector<uint8_t> input;
  Fixture(AVPixelFormat format = AV_PIX_FMT_YUV420P, int align = 1)
      : encoder("h264_qsv", nullptr, 66, 34, format, align, 30, 60, RC_CBR,
                Quality_Default, 1000, -1, 1, -1, output) {
    assert(encoder.init(stride, offset, &length));
    input.resize(length + 256, 0x55);
    replacements = stale_copies = 0;
  }
  int encode(int declared = -1) {
    return encoder.encode(input.data(), declared < 0 ? length : declared,
                          nullptr, 0);
  }
  ~Fixture() {
    encoder.free_encoder();
    release_retained();
  }
};
} // namespace
int main(int argc, char **) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  if (argc > 1) {
    Fixture f;
    int ret = f.encoder.encode(nullptr, f.length, nullptr, 0);
    result("null-input", ret < 0 && retained.empty() && replacements == 0, ret,
           -1);
    return failures ? 1 : 0;
  }
  for (AVPixelFormat format : {AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P}) {
    std::printf("format=%d\n", format);
    {
      Fixture f(format);
      assert(f.encode() == 0);
      std::fill(f.input.begin(), f.input.end(), 0);
      int pixel = retained[0]->data[0][0];
      result("caller-overwrite", pixel == 0x55, pixel, 0x55);
    }
    {
      Fixture f(format, 256);
      av_frame_unref(f.encoder.frame_);
      f.encoder.frame_->format = format;
      f.encoder.frame_->width = 66;
      f.encoder.frame_->height = 34;
      assert(av_frame_get_buffer(f.encoder.frame_, 0) == 0);
      assert(f.encoder.frame_->linesize[0] != f.stride[0]);
      for (int y = 0; y < 34; ++y)
        std::fill_n(f.input.data() + y * f.stride[0], 66,
                    static_cast<uint8_t>(y + 1));
      assert(f.encode() == 0);
      int pixel = retained[0]->data[0][retained[0]->linesize[0]];
      result("independent-strides", pixel == 2, pixel, 2);
    }
    {
      Fixture f(format);
      int ret = f.encode(f.length - 1);
      result("one-byte-short", ret < 0 && retained.empty(), ret, -1);
    }
    {
      Fixture f(format, 256);
      assert(f.encode() == 0);
      const int count = replacements;
      int ret = f.encode(0);
      result("reject-before-allocation", ret < 0 && replacements == count,
             replacements - count, 0);
    }
    {
      Fixture f(format, 256);
      assert(f.encode() == 0);
      assert(f.encode() == 0);
      result("no-stale-frame-copy", stale_copies == 0, stale_copies, 0);
      result("replacement-alignment",
             f.encoder.frame_->linesize[0] == f.stride[0],
             f.encoder.frame_->linesize[0], f.stride[0]);
    }
    {
      Fixture f(format, 256);
      assert(f.encode() == 0);
      fail_allocation = true;
      int ret = f.encode();
      bool intact = retained.size() == 1 && retained[0]->data[0][0] == 0x55;
      int retry = f.encode();
      result("allocation-failure-retry",
             ret == AVERROR(ENOMEM) && intact && retry == 0, ret,
             AVERROR(ENOMEM));
    }
    {
      Fixture f(format);
      auto *buffer = f.encoder.frame_->buf[0]->data;
      assert(f.encode() == 0);
      release_retained();
      const int count = replacements;
      assert(f.encode() == 0);
      bool reused =
          f.encoder.frame_->buf[0]->data == buffer && replacements == count;
      result("writable-buffer-reuse", reused, replacements - count, 0);
    }
  }
  // Supported even dimensions must have the same layout with floor/ceil chroma
  // height.
  int layouts = 0;
  for (auto format : {AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P})
    for (int width : {2, 66, 1920, 3840})
      for (int height : {2, 34, 1080, 2160})
        for (int align : {0, 1, 256}) {
          int stride[8] = {}, offset[8] = {}, length = 0;
          assert(ffmpeg_ram_get_linesize_offset_length(format, width, height,
                                                       align, stride, offset,
                                                       &length) == 0);
          int expected =
              stride[0] * height +
              (stride[1] + (format == AV_PIX_FMT_YUV420P ? stride[2] : 0)) *
                  (height / 2);
          assert(length == expected);
          assert(offset[0] == stride[0] * height);
          if (format == AV_PIX_FMT_YUV420P)
            assert(offset[1] == offset[0] + stride[1] * (height / 2));
          ++layouts;
        }
  std::printf("PASS even-dimension layouts: %d\nFailures: %d\n", layouts,
              failures);
  return failures ? 1 : 0;
}
