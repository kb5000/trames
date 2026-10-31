// SPDX-License-Identifier: GPL-3.0-or-later
// Completed-frame FFmpeg benchmark. No rendering, hashing or frame-rate pacing.
// Linux: cc -O2 ... $(pkg-config --cflags --libs libavformat libavcodec libavutil libva)
// macOS: build against the same FFmpeg release with H.264 VideoToolbox enabled.
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#ifndef __APPLE__
#include <libavutil/hwcontext_vaapi.h>
#include <va/va.h>
#include <va/va_drmcommon.h>
#endif

static void check(int result, const char *operation) {
    if (result >= 0) return;
    char error[128];
    av_strerror(result, error, sizeof(error));
    fprintf(stderr, "%s: %s\n", operation, error);
    exit(1);
}
static double now(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) abort();
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
#ifdef __APPLE__
#define HW_TYPE AV_HWDEVICE_TYPE_VIDEOTOOLBOX
#define HW_FORMAT AV_PIX_FMT_VIDEOTOOLBOX
#else
#define HW_TYPE AV_HWDEVICE_TYPE_VAAPI
#define HW_FORMAT AV_PIX_FMT_VAAPI
static void va_check(VAStatus status) {
    if (status == VA_STATUS_SUCCESS) return;
    fprintf(stderr, "VA: %s\n", vaErrorStr(status));
    exit(1);
}
#endif
static enum AVPixelFormat select_hardware(AVCodecContext *ctx, const enum AVPixelFormat *formats) {
    (void)ctx;
    for (; *formats != AV_PIX_FMT_NONE; formats++) if (*formats == HW_FORMAT) return *formats;
    fprintf(stderr, "required hardware format unavailable\n");
    exit(1);
}
struct Stats { uint64_t frames; double consume; int mode; AVFrame *download; };
static void receive(AVCodecContext *codec, AVFrame *frame, struct Stats *stats) {
    for (;;) {
        int status = avcodec_receive_frame(codec, frame);
        if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) return;
        check(status, "receive");
        if (frame->format != HW_FORMAT) abort();
        double began = now();
        if (stats->mode == 2) {
            check(av_hwframe_transfer_data(stats->download, frame, 0), "download");
            av_frame_unref(stats->download);
        } else {
#ifndef __APPLE__
            AVHWFramesContext *frames = (void *)frame->hw_frames_ctx->data;
            AVVAAPIDeviceContext *va = frames->device_ctx->hwctx;
            VASurfaceID surface = (uintptr_t)frame->data[3];
            if (stats->mode == 1) {
                VADRMPRIMESurfaceDescriptor desc = {0};
                // Include export-before-sync and FD close, as a real consumer does.
                va_check(vaExportSurfaceHandle(va->display, surface,
                    VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                    VA_EXPORT_SURFACE_READ_ONLY | VA_EXPORT_SURFACE_SEPARATE_LAYERS, &desc));
                va_check(vaSyncSurface(va->display, surface));
                for (unsigned i = 0; i < desc.num_objects; i++) if (close(desc.objects[i].fd)) abort();
            } else {
                va_check(vaSyncSurface(va->display, surface));
            }
#endif
            // FFmpeg's native VideoToolbox backend waits before returning AVFrame.
        }
        stats->consume += now() - began;
        stats->frames++;
        av_frame_unref(frame);
    }
}
int main(int argc, char **argv) {
    if (argc != 4 || (strcmp(argv[2], "sync") && strcmp(argv[2], "export") && strcmp(argv[2], "download"))) {
        fprintf(stderr, "usage: %s INPUT sync|export|download EXPECTED_FRAMES\n", argv[0]);
        return 2;
    }
    int mode = !strcmp(argv[2], "download") ? 2 : !strcmp(argv[2], "export");
#ifdef __APPLE__
    if (mode == 1) { fprintf(stderr, "VA FD export applies to the Linux guest only\n"); return 2; }
#endif
    uint64_t expected = strtoull(argv[3], NULL, 10);
    if (!expected) return 2;
    av_log_set_level(AV_LOG_WARNING);
    AVBufferRef *hardware = NULL;
    check(av_hwdevice_ctx_create(&hardware, HW_TYPE, NULL, NULL, 0), "hardware device");
    AVFormatContext *input = NULL;
    check(avformat_open_input(&input, argv[1], NULL, NULL), "input");
    check(avformat_find_stream_info(input, NULL), "stream info");
    int stream = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    check(stream, "video stream");
    const AVCodec *decoder = avcodec_find_decoder(input->streams[stream]->codecpar->codec_id);
    AVCodecContext *codec = avcodec_alloc_context3(decoder);
    if (!codec) abort();
    check(avcodec_parameters_to_context(codec, input->streams[stream]->codecpar), "parameters");
    codec->thread_count = 1;
    codec->get_format = select_hardware;
    codec->hw_device_ctx = av_buffer_ref(hardware);
    check(avcodec_open2(codec, decoder, NULL), "decoder");
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    struct Stats stats = {.mode = mode, .download = av_frame_alloc()};
    if (!packet || !frame || !stats.download) abort();
    double began = now();
    for (;;) {
        int status = av_read_frame(input, packet);
        if (status == AVERROR_EOF) break;
        check(status, "read packet");
        if (packet->stream_index == stream) {
            check(avcodec_send_packet(codec, packet), "send");
            receive(codec, frame, &stats);
        }
        av_packet_unref(packet);
    }
    check(avcodec_send_packet(codec, NULL), "flush");
    receive(codec, frame, &stats);
    double seconds = now() - began;
    av_frame_free(&stats.download);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec);
    avformat_close_input(&input);
    av_buffer_unref(&hardware);
    if (stats.frames != expected) {
        fprintf(stderr, "frame count: %" PRIu64 " != %" PRIu64 "\n", stats.frames, expected);
        return 1;
    }
    printf("{\"frames\":%" PRIu64 ",\"seconds\":%.9f,\"fps\":%.3f,\"consume_seconds\":%.9f}\n",
        stats.frames, seconds, stats.frames / seconds, stats.consume);
    return 0;
}
