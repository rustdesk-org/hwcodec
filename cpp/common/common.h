#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

#define MAX_DATA_NUM 8
#define MAX_GOP 0xFFFF

enum AdapterVendor {
  ADAPTER_VENDOR_AMD = 0x1002,
  ADAPTER_VENDOR_INTEL = 0x8086,
  ADAPTER_VENDOR_NVIDIA = 0x10DE,
};

enum DataFormat {
  H264,
  H265,
  VP8,
  VP9,
  AV1,
};

enum API {
  API_DX11,
  API_OPENCL,
  API_OPENGL,
  API_VULKAN,
};

enum SurfaceFormat {
  SURFACE_FORMAT_BGRA,
  SURFACE_FORMAT_RGBA,
  SURFACE_FORMAT_NV12,
};

enum Usage {
  ULTRA_LOW_LATENCY,
  LOW_LATENCY,
  LOW_LATENCY_HIGH_QUALITY,
};

enum Preset { BALANCED, SPEED, QUALITY };

/**
 * Chromaticity coordinates of the source primaries.
 * These values match the ones defined by ISO/IEC 23091-2_2019 subclause 8.1 and
 * ITU-T H.273.
 */
enum AVColorPrimaries {
  AVCOL_PRI_RESERVED0 = 0,
  AVCOL_PRI_BT709 =
      1, ///< also ITU-R BT1361 / IEC 61966-2-4 / SMPTE RP 177 Annex B
  AVCOL_PRI_UNSPECIFIED = 2,
  AVCOL_PRI_RESERVED = 3,
  AVCOL_PRI_BT470M =
      4, ///< also FCC Title 47 Code of Federal Regulations 73.682 (a)(20)

  AVCOL_PRI_BT470BG = 5, ///< also ITU-R BT601-6 625 / ITU-R BT1358 625 / ITU-R
                         ///< BT1700 625 PAL & SECAM
  AVCOL_PRI_SMPTE170M =
      6, ///< also ITU-R BT601-6 525 / ITU-R BT1358 525 / ITU-R BT1700 NTSC
  AVCOL_PRI_SMPTE240M =
      7, ///< identical to above, also called "SMPTE C" even though it uses D65
  AVCOL_PRI_FILM = 8,      ///< colour filters using Illuminant C
  AVCOL_PRI_BT2020 = 9,    ///< ITU-R BT2020
  AVCOL_PRI_SMPTE428 = 10, ///< SMPTE ST 428-1 (CIE 1931 XYZ)
  AVCOL_PRI_SMPTEST428_1 = AVCOL_PRI_SMPTE428,
  AVCOL_PRI_SMPTE431 = 11, ///< SMPTE ST 431-2 (2011) / DCI P3
  AVCOL_PRI_SMPTE432 = 12, ///< SMPTE ST 432-1 (2010) / P3 D65 / Display P3
  AVCOL_PRI_EBU3213 = 22,  ///< EBU Tech. 3213-E (nothing there) / one of JEDEC
                           ///< P22 group phosphors
  AVCOL_PRI_JEDEC_P22 = AVCOL_PRI_EBU3213,
  AVCOL_PRI_NB ///< Not part of ABI
};

/**
 * Color Transfer Characteristic.
 * These values match the ones defined by ISO/IEC 23091-2_2019 subclause 8.2.
 */
enum AVColorTransferCharacteristic {
  AVCOL_TRC_RESERVED0 = 0,
  AVCOL_TRC_BT709 = 1, ///< also ITU-R BT1361
  AVCOL_TRC_UNSPECIFIED = 2,
  AVCOL_TRC_RESERVED = 3,
  AVCOL_TRC_GAMMA22 = 4,   ///< also ITU-R BT470M / ITU-R BT1700 625 PAL & SECAM
  AVCOL_TRC_GAMMA28 = 5,   ///< also ITU-R BT470BG
  AVCOL_TRC_SMPTE170M = 6, ///< also ITU-R BT601-6 525 or 625 / ITU-R BT1358 525
                           ///< or 625 / ITU-R BT1700 NTSC
  AVCOL_TRC_SMPTE240M = 7,
  AVCOL_TRC_LINEAR = 8, ///< "Linear transfer characteristics"
  AVCOL_TRC_LOG = 9,    ///< "Logarithmic transfer characteristic (100:1 range)"
  AVCOL_TRC_LOG_SQRT =
      10, ///< "Logarithmic transfer characteristic (100 * Sqrt(10) : 1 range)"
  AVCOL_TRC_IEC61966_2_4 = 11, ///< IEC 61966-2-4
  AVCOL_TRC_BT1361_ECG = 12,   ///< ITU-R BT1361 Extended Colour Gamut
  AVCOL_TRC_IEC61966_2_1 = 13, ///< IEC 61966-2-1 (sRGB or sYCC)
  AVCOL_TRC_BT2020_10 = 14,    ///< ITU-R BT2020 for 10-bit system
  AVCOL_TRC_BT2020_12 = 15,    ///< ITU-R BT2020 for 12-bit system
  AVCOL_TRC_SMPTE2084 =
      16, ///< SMPTE ST 2084 for 10-, 12-, 14- and 16-bit systems
  AVCOL_TRC_SMPTEST2084 = AVCOL_TRC_SMPTE2084,
  AVCOL_TRC_SMPTE428 = 17, ///< SMPTE ST 428-1
  AVCOL_TRC_SMPTEST428_1 = AVCOL_TRC_SMPTE428,
  AVCOL_TRC_ARIB_STD_B67 = 18, ///< ARIB STD-B67, known as "Hybrid log-gamma"
  AVCOL_TRC_NB                 ///< Not part of ABI
};

/**
 * YUV colorspace type.
 * These values match the ones defined by ISO/IEC 23091-2_2019 subclause 8.3.
 */
enum AVColorSpace {
  AVCOL_SPC_RGB = 0,   ///< order of coefficients is actually GBR, also IEC
                       ///< 61966-2-1 (sRGB), YZX and ST 428-1
  AVCOL_SPC_BT709 = 1, ///< also ITU-R BT1361 / IEC 61966-2-4 xvYCC709 / derived
                       ///< in SMPTE RP 177 Annex B
  AVCOL_SPC_UNSPECIFIED = 2,
  AVCOL_SPC_RESERVED =
      3, ///< reserved for future use by ITU-T and ISO/IEC just like 15-255 are
  AVCOL_SPC_FCC =
      4, ///< FCC Title 47 Code of Federal Regulations 73.682 (a)(20)
  AVCOL_SPC_BT470BG = 5, ///< also ITU-R BT601-6 625 / ITU-R BT1358 625 / ITU-R
                         ///< BT1700 625 PAL & SECAM / IEC 61966-2-4 xvYCC601
  AVCOL_SPC_SMPTE170M =
      6, ///< also ITU-R BT601-6 525 / ITU-R BT1358 525 / ITU-R BT1700 NTSC /
         ///< functionally identical to above
  AVCOL_SPC_SMPTE240M = 7, ///< derived from 170M primaries and D65 white point,
                           ///< 170M is derived from BT470 System M's primaries
  AVCOL_SPC_YCGCO = 8, ///< used by Dirac / VC-2 and H.264 FRext, see ITU-T SG16
  AVCOL_SPC_YCOCG = AVCOL_SPC_YCGCO,
  AVCOL_SPC_BT2020_NCL = 9, ///< ITU-R BT2020 non-constant luminance system
  AVCOL_SPC_BT2020_CL = 10, ///< ITU-R BT2020 constant luminance system
  AVCOL_SPC_SMPTE2085 = 11, ///< SMPTE 2085, Y'D'zD'x
  AVCOL_SPC_CHROMA_DERIVED_NCL =
      12, ///< Chromaticity-derived non-constant luminance system
  AVCOL_SPC_CHROMA_DERIVED_CL =
      13,               ///< Chromaticity-derived constant luminance system
  AVCOL_SPC_ICTCP = 14, ///< ITU-R BT.2100-0, ICtCp
  AVCOL_SPC_NB          ///< Not part of ABI
};

struct AdapterDesc {
  int64_t luid;
};

void hwcodec_get_bin_file(int32_t is265, uint8_t **p, int32_t *len);

#endif // COMMON_H