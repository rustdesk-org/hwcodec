// https://github.com/FFmpeg/FFmpeg/blob/master/doc/examples/muxing.c

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/timestamp.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void my_fprintf(FILE *const _Stream, const char *const _Format, ...);
#define fprintf my_fprintf

typedef struct OutputStream {
  AVStream *st;
  AVPacket *tmp_pkt;
} OutputStream;

typedef struct Muxer {
  OutputStream video_st;
  AVFormatContext *oc;
  int framerate;
  int64_t start_ms;
  int64_t last_pts;
} Muxer;

Muxer *new_muxer(const char *filename, int width, int height, int is265,
                 int framerate) {
  Muxer *muxer = NULL;
  OutputStream *ost = NULL;
  AVFormatContext *oc = NULL;
  int ret;

  if (!(muxer = calloc(1, sizeof(Muxer)))) {
    fprintf(stderr, "Failed to alloc Muxer\n");
    return NULL;
  }
  ost = &muxer->video_st;

  if ((ret = avformat_alloc_output_context2(&oc, NULL, NULL, filename)) < 0) {
    fprintf(stderr, "Cannot open output formatContext %s\n", av_err2str(ret));
    goto _exit;
  }

  ost->st = avformat_new_stream(oc, NULL);
  if (!ost->st) {
    fprintf(stderr, "Could not allocate stream\n");
    goto _exit;
  }
  ost->st->id = oc->nb_streams - 1;
  ost->st->codecpar->codec_id = is265 ? AV_CODEC_ID_H265 : AV_CODEC_ID_H264;
  ost->st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
  ost->st->codecpar->width = width;
  ost->st->codecpar->height = height;

  if (!(oc->oformat->flags & AVFMT_NOFILE)) {
    ret = avio_open(&oc->pb, filename, AVIO_FLAG_WRITE);
    if (ret < 0) {
      fprintf(stderr, "Could not open '%s': %s\n", filename, av_err2str(ret));
      goto _exit;
    }
  }

  ost->tmp_pkt = av_packet_alloc();
  if (!ost->tmp_pkt) {
    fprintf(stderr, "Could not allocate AVPacket\n");
    goto _exit;
  }

  ret = avformat_write_header(oc, NULL);
  if (ret < 0) {
    fprintf(stderr, "Error occurred when opening output file: %s\n",
            av_err2str(ret));
    goto _exit;
  }

  muxer->oc = oc;
  muxer->framerate = framerate;
  muxer->start_ms = 0;
  muxer->last_pts = 0;
  return muxer;

_exit:
  if (ost && ost->tmp_pkt) av_packet_free(&ost->tmp_pkt);
  if (oc && oc->pb && !(oc->oformat->flags & AVFMT_NOFILE))
    avio_closep(&oc->pb);
  if (oc) avformat_free_context(oc);
  free(muxer);

  return NULL;
}

int write_video_frame(Muxer *muxer, const uint8_t *data, int len,
                      int64_t elapsed_ms) {
  OutputStream *ost = &muxer->video_st;
  AVPacket *pkt = ost->tmp_pkt;
  AVFormatContext *fmt_ctx = muxer->oc;
  int ret;

  if (muxer->start_ms == 0) muxer->start_ms = elapsed_ms;
  int64_t pts = (elapsed_ms - muxer->start_ms);  // use write timestamp

  pkt->data = (uint8_t *)data;
  pkt->size = len;
  pkt->pts = pts;
  pkt->dts = pkt->pts;  // no B-frame
  int64_t duration = pkt->pts - muxer->last_pts;
  muxer->last_pts = pkt->pts;
  pkt->duration = duration > 0 ? duration : muxer->framerate;  // predict
  av_packet_rescale_ts(pkt, (AVRational){1, 1000},
                       ost->st->time_base);  // ms -> stream timebase
  pkt->stream_index = ost->st->index;

  ret = av_write_frame(fmt_ctx, pkt);
  if (ret < 0) {
    fprintf(stderr, "Error while writing output packet: %s\n", av_err2str(ret));
    return -1;
  }
  return 0;
}

int write_tail(Muxer *muxer) { return av_write_trailer(muxer->oc); }

void free_muxer(Muxer *muxer) {
  if (!muxer) return;
  av_packet_free(&muxer->video_st.tmp_pkt);
  AVFormatContext *oc = muxer->oc;
  if (oc && oc->pb && !(oc->oformat->flags & AVFMT_NOFILE))
    avio_closep(&oc->pb);
  avformat_free_context(oc);
}