#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <MacTypes.h>
#include <VideoToolbox/VideoToolbox.h>
#include <AVFoundation/AVFoundation.h>
#include <ratio>
#include <sys/_types/_int32_t.h>

static int32_t hasHardwareEncoder(bool h265) {
    CFMutableDictionaryRef spec = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                                            &kCFTypeDictionaryKeyCallBacks,
                                                            &kCFTypeDictionaryValueCallBacks);
    #if TARGET_OS_MAC
        // Specify that we require a hardware-accelerated video encoder
        CFDictionarySetValue(spec, kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder, kCFBooleanTrue);
    #endif

    CMVideoCodecType codecType = h265 ? kCMVideoCodecType_HEVC : kCMVideoCodecType_H264;
    CFDictionaryRef properties = NULL;
    CFStringRef outID = NULL;
    OSStatus result = VTCopySupportedPropertyDictionaryForEncoder(1920, 1080, codecType, spec, &outID, &properties);

    CFRelease(spec); // Clean up the specification dictionary

    if (result == kVTCouldNotFindVideoEncoderErr) {
        return 0; // No hardware encoder found
    }

    if (properties != NULL) {
        CFRelease(properties);
    }
    if (outID != NULL) {
        CFRelease(outID);
    }

    return result == noErr ? 1 : 0;
}

extern "C" void checkVideoToolboxSupport(int32_t *h264Encoder, int32_t *h265Encoder, int32_t *h264Decoder, int32_t *h265Decoder) {
    // https://stackoverflow.com/questions/50956097/determine-if-ios-device-can-support-hevc-encoding
    *h264Encoder = hasHardwareEncoder(false);
    *h265Encoder = hasHardwareEncoder(true);
    // *h265Encoder = [[AVAssetExportSession allExportPresets] containsObject:@"AVAssetExportPresetHEVCHighestQuality"];

    *h264Decoder = VTIsHardwareDecodeSupported(kCMVideoCodecType_H264);
    *h265Decoder = VTIsHardwareDecodeSupported(kCMVideoCodecType_HEVC);

    return;
}

extern "C" uint64_t GetHwcodecGpuSignature() {
    int32_t h264Encoder = 0;
    int32_t h265Encoder = 0;
    int32_t h264Decoder = 0;
    int32_t h265Decoder = 0;
    checkVideoToolboxSupport(&h264Encoder, &h265Encoder, &h264Decoder, &h265Decoder);
    return (uint64_t)h264Encoder << 24 | (uint64_t)h265Encoder << 16 | (uint64_t)h264Decoder << 8 | (uint64_t)h265Decoder;
}   
