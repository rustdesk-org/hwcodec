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
bool fail_next_buffer;
int buffer_allocations;

int injected_get_buffer(AVFrame *frame, int align) {
  if (fail_next_buffer) {
    fail_next_buffer = false;
    return AVERROR(ENOMEM);
  }
  ++buffer_allocations;
  return av_frame_get_buffer(frame, align);
}

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

// Keep real buffers, references and copies; inject codec I/O and allocation failure.
#define av_frame_get_buffer injected_get_buffer
#define avcodec_open2 injected_open
#define avcodec_send_frame injected_send
#define avcodec_receive_packet injected_receive
#include "../../cpp/ffmpeg_ram/ffmpeg_ram_encode.cpp"
#undef avcodec_open2
#undef avcodec_send_frame
#undef avcodec_receive_packet
#undef av_frame_get_buffer

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
    const int width = p == 0 ? frame->width :
                      ((frame->width + 1) / 2) * (nv12 ? 2 : 1);
    const int height = p == 0 ? frame->height : (frame->height + 1) / 2;
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x)
        assert(frame->data[p][y * frame->linesize[p] + x] == pixel(p, x, y, seed));
  }
}

void check_lifetime(AVPixelFormat format, int align, int height,
                    bool different_destination = false) {
  const int width = 66;
  FFmpegRamEncoder encoder("h264_qsv", nullptr, width, height, format, align,
                           30, 60, RC_CBR, Quality_Default, 1000, -1, 1, -1,
                           output);
  int stride[AV_NUM_DATA_POINTERS] = {};
  int offset[AV_NUM_DATA_POINTERS] = {};
  int length = 0;
  assert(encoder.init(stride, offset, &length));
  const bool nv12 = format == AV_PIX_FMT_NV12;
  const int chroma_height = height / 2;
  assert(offset[0] == stride[0] * height);
  if (!nv12)
    assert(offset[1] == offset[0] + stride[1] * chroma_height);
  assert(length == stride[0] * height +
                   (stride[1] + (nv12 ? 0 : stride[2])) * chroma_height);
  if (format == AV_PIX_FMT_YUV420P && align == 1)
    assert(stride[1] == 33 && stride[2] == 33);
  if (different_destination) {
    // Exercise copying between different layouts independently of how the
    // encoder chooses replacement buffers for retained frames.
    av_frame_unref(encoder.frame_);
    encoder.frame_->format = format;
    encoder.frame_->width = width;
    encoder.frame_->height = height;
    assert(av_frame_get_buffer(encoder.frame_, 0) == 0);
    assert(encoder.frame_->linesize[0] != stride[0]);
  }
  callbacks = 0;
  for (int i = 0; i < 3; ++i) {
    const int seed = 23 + i * 59;
    std::vector<uint8_t> input(length, 0xee);
    for (int p = 0; p < (nv12 ? 2 : 3); ++p) {
      const int row_bytes = p == 0 || nv12 ? width : width / 2;
      const int rows = p == 0 ? height : chroma_height;
      const int start = p == 0 ? 0 : offset[p - 1];
      for (int y = 0; y < rows; ++y)
        for (int x = 0; x < row_bytes; ++x)
          input[start + y * stride[p] + x] = pixel(p, x, y, seed);
    }
    if (i == 1) {
      fail_next_buffer = true;
      assert(encoder.encode(input.data(), length, nullptr, i * 33) == AVERROR(ENOMEM));
      assert(!fail_next_buffer);
      assert(retained.size() == 1 && callbacks == 1);
      check_image(retained[0], 23);
    }
    assert(encoder.encode(input.data(), length, nullptr, i * 33) == 0);
    assert(callbacks == i + 1);
    assert(retained.size() == static_cast<size_t>(i + 1));
    check_image(retained.back(), seed);
    std::fill(input.begin(), input.end(), 0);
    check_image(retained.back(), seed);
    std::vector<uint8_t>().swap(input);
    for (int j = 0; j <= i; ++j)
      check_image(retained[j], 23 + j * 59);

    const int allocations_before = buffer_allocations;
    for (int short_length : {0, length - 1}) {
      std::vector<uint8_t> short_input(short_length);
      assert(encoder.encode(short_input.data(), short_length, nullptr, i * 33 + 1) < 0);
      assert(callbacks == i + 1);
      assert(retained.size() == static_cast<size_t>(i + 1));
    }
    assert(encoder.encode(nullptr, length, nullptr, i * 33 + 1) < 0);
    assert(buffer_allocations == allocations_before);
    for (int j = 0; j <= i; ++j)
      check_image(retained[j], 23 + j * 59);
  }
  encoder.free_encoder();
  for (size_t i = 0; i < retained.size(); ++i) {
    check_image(retained[i], 23 + static_cast<int>(i) * 59);
    av_frame_free(&retained[i]);
  }
  retained.clear();
  std::printf("PASS RAM input lifetime: format=%d align=%d height=%d different_destination=%d\n",
              format, align, height, different_destination);
}

void check_buffer_reuse(AVPixelFormat format) {
  FFmpegRamEncoder encoder("h264_qsv", nullptr, 66, 34, format, 256,
                           30, 60, RC_CBR, Quality_Default, 1000, -1, 1, -1,
                           output);
  int stride[AV_NUM_DATA_POINTERS] = {};
  int offset[AV_NUM_DATA_POINTERS] = {};
  int length = 0;
  assert(encoder.init(stride, offset, &length));
  std::vector<uint8_t> input(length, 0x55);
  const uint8_t *buffer = encoder.frame_->data[0];
  const int allocations_before = buffer_allocations;
  for (int i = 0; i < 3; ++i) {
    assert(encoder.encode(input.data(), length, nullptr, i * 33) == 0);
    assert(encoder.frame_->data[0] == buffer);
    av_frame_free(&retained.back());
    retained.clear();
  }
  assert(buffer_allocations == allocations_before);
  encoder.free_encoder();
  std::printf("PASS RAM writable buffer reuse: format=%d\n", format);
}
} // namespace

int main() {
  for (AVPixelFormat format : {AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P}) {
    for (int align : {0, 1, 256})
      check_lifetime(format, align, 34);
    check_lifetime(format, 256, 34, true);
    check_buffer_reuse(format);
  }
}
