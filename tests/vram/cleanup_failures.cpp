extern "C" {
#include <libavcodec/avcodec.h>
}

#include "system.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <stdexcept>

#ifdef NDEBUG
#error These tests require assertions.
#endif

namespace {
void *codec_allocation = nullptr;
size_t codec_size = 0;
bool fail_allocation = false;
bool track_allocation = false;
int deleted_codecs = 0;
int deleted_natives = 0;
int freed_frames = 0;
int cleanup_error = 0;
int init_error = 0;
enum CleanupSite { Frame, Packet, Context, Buffer, MappedFrame };
CleanupSite cleanup_site = Frame;
AVPacket *pending_packet = nullptr;
AVCodecContext *pending_context = nullptr;
AVBufferRef *pending_buffer = nullptr;
AVFrame *pending_mapped_frame = nullptr;

void fail(int mode) {
  switch (mode) {
  case 1:
    throw std::runtime_error("injected codec failure");
  case 2:
    throw 17;
  case 3:
    RaiseException(0xe0424242, 0, 0, nullptr);
    break;
  case 4:
    *static_cast<volatile int *>(nullptr) = 1;
  }
}

void fail_init();

class FailingNativeDevice : public NativeDevice {
public:
  bool Init(int64_t, ID3D11Device *, int = 1) {
    fail_init();
    return false;
  }
  ~FailingNativeDevice() { ++deleted_natives; }
};

void injected_frame_free(AVFrame **frame) {
  bool mapped = pending_mapped_frame && *frame == pending_mapped_frame;
  if (mapped)
    pending_mapped_frame = nullptr;
  av_frame_free(frame);
  ++freed_frames;
  if (cleanup_site == (mapped ? MappedFrame : Frame))
    fail(cleanup_error);
}

void injected_packet_free(AVPacket **packet) {
  if (*packet == pending_packet)
    pending_packet = nullptr;
  av_packet_free(packet);
  if (cleanup_site == Packet)
    fail(cleanup_error);
}

void injected_context_free(AVCodecContext **context) {
  if (*context == pending_context)
    pending_context = nullptr;
  avcodec_free_context(context);
  if (cleanup_site == Context)
    fail(cleanup_error);
}

void injected_buffer_unref(AVBufferRef **buffer) {
  if (*buffer == pending_buffer)
    pending_buffer = nullptr;
  av_buffer_unref(buffer);
  if (cleanup_site == Buffer)
    fail(cleanup_error);
}
} // namespace

void *operator new(size_t size) {
  const bool is_codec = track_allocation && size == codec_size;
  if (is_codec) {
    track_allocation = false;
    if (fail_allocation)
      throw std::bad_alloc();
  }
  void *p = std::malloc(size ? size : 1);
  if (!p)
    throw std::bad_alloc();
  if (is_codec)
    codec_allocation = p;
  return p;
}

void operator delete(void *p) noexcept {
  if (p && p == codec_allocation) {
    codec_allocation = nullptr;
    ++deleted_codecs;
  }
  std::free(p);
}

void operator delete(void *p, size_t) noexcept { ::operator delete(p); }

// Compile the production ownership paths with failing initialization and
// cleanup.
#define NativeDevice FailingNativeDevice
#define av_frame_free injected_frame_free
#define av_packet_free injected_packet_free
#define avcodec_free_context injected_context_free
#define av_buffer_unref injected_buffer_unref
#ifdef TEST_ENCODER
#include "../../cpp/ffmpeg_vram/ffmpeg_vram_encode.cpp"
using Codec = FFmpegVRamEncoder;
#else
#include "../../cpp/ffmpeg_vram/ffmpeg_vram_decode.cpp"
using Codec = FFmpegVRamDecoder;
#endif
#undef av_frame_free
#undef av_packet_free
#undef avcodec_free_context
#undef av_buffer_unref
#undef NativeDevice

extern "C" void hwcodec_log(int, const char *) { throw 23; }
extern "C" void hwcodec_av_log_callback(int, const char *) {}

namespace {
void fail_init() {
  auto *codec = static_cast<Codec *>(codec_allocation);
  assert(codec);
  codec->frame_ = av_frame_alloc();
  assert(codec->frame_);
  fail(init_error);
}

Codec *allocate_codec() {
#ifdef TEST_ENCODER
  return new Codec(nullptr, 0, H264, 640, 360, 5000, 30, 60);
#else
  return new Codec(nullptr, 0, H264);
#endif
}

Codec *create_codec() {
#ifdef TEST_ENCODER
  return ffmpeg_vram_new_encoder(nullptr, 0, H264, 640, 360, 5000, 30, 60);
#else
  return ffmpeg_vram_new_decoder(nullptr, 0, H264);
#endif
}

int destroy_codec(Codec *codec) {
#ifdef TEST_ENCODER
  return ffmpeg_vram_destroy_encoder(codec);
#else
  return ffmpeg_vram_destroy_decoder(codec);
#endif
}

void prepare() {
  assert(!codec_allocation);
  track_allocation = true;
  deleted_codecs = 0;
  deleted_natives = 0;
  freed_frames = 0;
}

void check_released() {
  assert(!codec_allocation);
  assert(deleted_codecs == 1 && deleted_natives == 1 && freed_frames == 1);
}
} // namespace

int main(int argc, char **argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  codec_size = sizeof(Codec);
  if (argc > 1) {
    assert(argc == 3);
    init_error = std::atoi(argv[1]);
    cleanup_error = std::atoi(argv[2]);
    prepare();
    if (init_error) {
      create_codec();
    } else {
      Codec *codec = allocate_codec();
      codec->native_ = std::make_unique<FailingNativeDevice>();
      codec->frame_ = av_frame_alloc();
      assert(codec->frame_);
      destroy_codec(codec);
    }
    std::puts("FAIL: SEH returned through a codec boundary");
    return 1;
  }
  assert(destroy_codec(nullptr) == 0);
  int scenarios = 1;
  for (cleanup_error = 0; cleanup_error < 3; ++cleanup_error) {
    prepare();
    Codec *codec = allocate_codec();
    codec->native_ = std::make_unique<FailingNativeDevice>();
    codec->frame_ = av_frame_alloc();
    assert(codec->frame_);
    assert(destroy_codec(codec) == (cleanup_error ? -1 : 0));
    check_released();
    ++scenarios;

    for (init_error = 0; init_error < 3; ++init_error) {
      prepare();
      assert(!create_codec());
      check_released();
      ++scenarios;
    }
  }
  int leaked_scenarios = 0;
  const CleanupSite sites[] = {
      Frame,       Packet, Context, Buffer,
#ifdef TEST_ENCODER
      MappedFrame,
#endif
  };
  for (CleanupSite site : sites) {
    cleanup_site = site;
    for (cleanup_error = 1; cleanup_error < 3; ++cleanup_error) {
      prepare();
      Codec *codec = allocate_codec();
      codec->native_ = std::make_unique<FailingNativeDevice>();
      codec->frame_ = av_frame_alloc();
      codec->pkt_ = pending_packet = av_packet_alloc();
      codec->c_ = pending_context = avcodec_alloc_context3(nullptr);
      codec->hw_device_ctx_ = pending_buffer = av_buffer_alloc(64);
#ifdef TEST_ENCODER
      codec->mapped_frame_ = pending_mapped_frame = av_frame_alloc();
      assert(pending_mapped_frame);
#endif
      assert(codec->frame_ && pending_packet && pending_context &&
             pending_buffer);
      assert(destroy_codec(codec) == -1);
      assert(!codec_allocation && deleted_codecs == 1 && deleted_natives == 1);
      if (pending_packet || pending_context || pending_buffer ||
          pending_mapped_frame) {
        std::printf("LEAK after cleanup exception site=%d mode=%d: packet=%d "
                    "context=%d buffer=%d mapped_frame=%d\n",
                    site, cleanup_error, !!pending_packet, !!pending_context,
                    !!pending_buffer, !!pending_mapped_frame);
        ++leaked_scenarios;
        av_packet_free(&pending_packet);
        avcodec_free_context(&pending_context);
        av_buffer_unref(&pending_buffer);
        av_frame_free(&pending_mapped_frame);
      }
      ++scenarios;
    }
  }
  prepare();
  fail_allocation = true;
  assert(!create_codec());
  assert(!codec_allocation && deleted_codecs == 0);
  ++scenarios;
  std::printf("%s %d codec initialization/cleanup scenarios\n",
              leaked_scenarios ? "FAIL" : "PASS", scenarios);
  return leaked_scenarios ? 1 : 0;
}
