#include "common.h"
#include <iostream>
#include <public/common/TraceAdapter.h>
#include <stdio.h>

#ifndef AMF_FACILITY
#define AMF_FACILITY L"AMFCommon"
#endif

static bool convert_api(API lhs, amf::AMF_MEMORY_TYPE &rhs) {
  switch (lhs) {
  case API_DX11:
    rhs = amf::AMF_MEMORY_DX11;
    break;
  default:
    std::cerr << "unsupported memory type: " << static_cast<int>(lhs) << "\n";
    return false;
  }
  return true;
}

static bool convert_surface_format(SurfaceFormat lhs,
                                   amf::AMF_SURFACE_FORMAT &rhs) {
  switch (lhs) {
  case SURFACE_FORMAT_NV12:
    rhs = amf::AMF_SURFACE_NV12;
    break;
  case SURFACE_FORMAT_RGBA:
    rhs = amf::AMF_SURFACE_RGBA;
    break;
  case SURFACE_FORMAT_BGRA:
    rhs = amf::AMF_SURFACE_BGRA;
    break;
  default:
    std::cerr << "unsupported surface format: " << static_cast<int>(lhs)
              << "\n";
    return false;
  }
  return true;
}
