#include "common.h"
#include "util.h"

extern "C" {
#include <libavutil/opt.h>
}

#include <cassert>
#include <cstddef>
#include <iostream>

namespace gol {
void error(const std::string &message) { std::cerr << message << '\n'; }
}

// Exercise the real AVOption writes without requiring a VAAPI device.
struct Options {
  const AVClass *av_class;
  int rc_mode;
  int qp;
};

static const AVOption options[] = {
    {"rc_mode", nullptr, offsetof(Options, rc_mode), AV_OPT_TYPE_INT,
     {.i64 = 0}, 0, 6, 0, "rc_mode"},
    {"CQP", nullptr, 0, AV_OPT_TYPE_CONST, {.i64 = 1}, 0, 0, 0, "rc_mode"},
    {"qp", nullptr, offsetof(Options, qp), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 51},
    {nullptr},
};

int main() {
  AVClass av_class = {};
  av_class.class_name = "VAAPI options test";
  av_class.item_name = av_default_item_name;
  av_class.option = options;
  av_class.version = LIBAVUTIL_VERSION_INT;

  Options priv = {&av_class, 0, 0};
  AVCodecContext ctx = {};
  ctx.priv_data = &priv;
  ctx.bit_rate = 1000000;

  for (const auto &name : {"h264_vaapi", "hevc_vaapi"}) {
    for (int qp : {1, 16, 23, 26, 51}) {
      av_opt_set_defaults(&priv);
      assert(util_encode::set_rate_control(&ctx, name, RC_CQ, qp));
      assert(priv.rc_mode == 1);
      assert(priv.qp == qp);
      assert(ctx.bit_rate == 1000000);
    }
    for (int qp : {-100, -1, 0}) {
      av_opt_set_defaults(&priv);
      assert(util_encode::set_rate_control(&ctx, name, RC_CQ, qp));
      assert(priv.rc_mode == 1);
      assert(priv.qp == 23);
    }
    for (int qp : {52, 100}) {
      av_opt_set_defaults(&priv);
      assert(!util_encode::set_rate_control(&ctx, name, RC_CQ, qp));
      assert(priv.rc_mode == 1);
    }
  }

  // Explicit CQP configuration must not affect callers requesting other modes.
  for (int rc : {RC_CBR, RC_VBR}) {
    av_opt_set_defaults(&priv);
    assert(util_encode::set_rate_control(&ctx, "h264_vaapi", rc, 16));
    assert(priv.rc_mode == 0);
    assert(priv.qp == 0);
  }

  // Missing options must be reported as failures, not as successful CQP setup.
  AVOption mode_only[] = {options[0], options[1], {nullptr}};
  av_class.option = mode_only;
  assert(!util_encode::set_rate_control(&ctx, "h264_vaapi", RC_CQ, 23));
  AVOption no_options[] = {{nullptr}};
  av_class.option = no_options;
  assert(!util_encode::set_rate_control(&ctx, "h264_vaapi", RC_CQ, 23));

  std::cout << "VAAPI CQP option tests passed\n";
}
