/*
 * FFmpeg decoder base class source for ffmpegfs
 *
 * Copyright (C) 2017-2018 Norbert Schlia (nschlia@oblivion-software.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "ffmpeg_base.h"
#include "transcode.h"

// Disable annoying warnings outside our code
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#ifdef __GNUC__
#  include <features.h>
#  if __GNUC_PREREQ(5,0) || defined(__clang__)
// GCC >= 5.0
#     pragma GCC diagnostic ignored "-Wfloat-conversion"
#  elif __GNUC_PREREQ(4,8)
// GCC >= 4.8
#  else
#     error("GCC < 4.8 not supported");
#  endif
#endif
#ifdef __cplusplus
extern "C" {
#endif
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#ifdef __cplusplus
}
#endif
#pragma GCC diagnostic pop

FFMPEG_Base::FFMPEG_Base()
{
}

FFMPEG_Base::~FFMPEG_Base()
{
}

// Open codec context for desired media type
int FFMPEG_Base::open_bestmatch_codec_context(AVCodecContext **avctx, int *stream_idx, AVFormatContext *fmt_ctx, AVMediaType type, const char *filename) const
{
    int ret;

    ret = av_find_best_stream(fmt_ctx, type, INVALID_STREAM, INVALID_STREAM, NULL, 0);
    if (ret < 0)
    {
        if (ret != AVERROR_STREAM_NOT_FOUND)    // Not an error
        {
            ffmpegfs_error(filename, "Could not find %s stream in input file (error '%s').", get_media_type_string(type), ffmpeg_geterror(ret).c_str());
        }
        return ret;
    }

    *stream_idx = ret;

    return open_codec_context(avctx, *stream_idx, fmt_ctx, type, filename);
}

int FFMPEG_Base::open_codec_context(AVCodecContext **avctx, int stream_idx, AVFormatContext *fmt_ctx, AVMediaType type, const char *filename) const
{
    AVCodecContext *dec_ctx = NULL;
    AVCodec *dec = NULL;
    AVDictionary *opts = NULL;
    AVStream *input_stream;
    AVCodecID codec_id = AV_CODEC_ID_NONE;
    int ret;

    input_stream = fmt_ctx->streams[stream_idx];

    // Init the decoders, with or without reference counting
    // av_dict_set_with_check(&opts, "refcounted_frames", refcount ? "1" : "0", 0);

#if LAVF_DEP_AVSTREAM_CODEC
    // allocate a new decoding context
    dec_ctx = avcodec_alloc_context3(dec);
    if (!dec_ctx)
    {
        ffmpegfs_error(filename, "Could not allocate a decoding context.");
        return AVERROR(ENOMEM);
    }

    // initialise the stream parameters with demuxer information
    ret = avcodec_parameters_to_context(dec_ctx, input_stream->codecpar);
    if (ret < 0)
    {
        return ret;
    }

    codec_id = input_stream->codecpar->codec_id;
#else
    dec_ctx = input_stream->codec;

    codec_id = dec_ctx->codec_id;
#endif

    // Find a decoder for the stream.
    dec = avcodec_find_decoder(codec_id);
    if (!dec)
    {
        ffmpegfs_error(filename, "Failed to find %s input codec.", get_media_type_string(type));
        return AVERROR(EINVAL);
    }

    dec_ctx->codec_id = dec->id;

    ret = avcodec_open2(dec_ctx, dec, &opts);

    av_dict_free(&opts);

    if (ret < 0)
    {
        ffmpegfs_error(filename, "Failed to open %s input codec for stream #%u (error '%s').", get_media_type_string(type), input_stream->index, ffmpeg_geterror(ret).c_str());
        return ret;
    }

    ffmpegfs_debug(filename, "Opened %s input codec for stream #%u.", get_codec_name(codec_id, true), input_stream->index);

    *avctx = dec_ctx;

    return 0;
}

// Initialise one data packet for reading or writing.
void FFMPEG_Base::init_packet(AVPacket *packet) const
{
    av_init_packet(packet);
    // Set the packet data and size so that it is recognised as being empty.
    packet->data = NULL;
    packet->size = 0;
}

// Initialise one frame for reading from the input file
int FFMPEG_Base::init_frame(AVFrame **frame, const char *filename) const
{
    if (!(*frame = av_frame_alloc()))
    {
        ffmpegfs_error(filename, "Could not allocate frame.");
        return AVERROR(ENOMEM);
    }
    return 0;
}

void FFMPEG_Base::video_stream_setup(AVCodecContext *output_codec_ctx, AVStream* output_stream, AVRational frame_rate) const
{
    AVRational time_base;

    if (!frame_rate.num || !frame_rate.den)
    {
        frame_rate                              = { .num = 25, .den = 1 };
        ffmpegfs_warning(NULL, "No information about the input framerate is available. Falling back to a default value of 25fps for output stream.");
    }

    // timebase: This is the fundamental unit of time (in seconds) in terms
    // of which frame timestamps are represented. For fixed-fps content,
    // timebase should be 1/framerate and timestamp increments should be
    // identical to 1.
    //time_base                                 = m_in.m_pVideo_stream->time_base;

    // tbn: must be set differently for the target format. Otherwise produces strange results.
    switch (output_codec_ctx->codec_id)
    {
    case AV_CODEC_ID_THEORA:        // ogg
    case AV_CODEC_ID_MPEG1VIDEO:
    case AV_CODEC_ID_MPEG2VIDEO:
    {
        time_base                               = av_inv_q(frame_rate);
        break;
    }
    case AV_CODEC_ID_VP9:           // webm
    {
        time_base                               = { .num = 1, .den = 1000 };
        break;
    }
    default:                        // mp4 and all others
    {
        time_base                               = { .num = 1, .den = 90000 };
        break;
    }
    }

    // tbc
    output_stream->time_base                    = time_base;
    output_codec_ctx->time_base                 = time_base;

    // tbc
#if LAVF_DEP_AVSTREAM_CODEC
    //output_stream->codecpar->time_base        = time_base;
#else
    output_stream->codec->time_base             = time_base;
#endif

#ifndef USING_LIBAV
    // tbr
    // output_stream->r_frame_rate              = m_in.m_pVideo_stream->r_frame_rate;
    // output_stream->r_frame_rate              = { .num = 25, .den = 1 };

    // fps
    output_stream->avg_frame_rate               = frame_rate;
    output_codec_ctx->framerate                 = frame_rate;
#endif

    // At this moment the output format must be AV_PIX_FMT_YUV420P;
    output_codec_ctx->pix_fmt                   = AV_PIX_FMT_YUV420P;

    output_codec_ctx->gop_size                  = 12;   // emit one intra frame every twelve frames at most
}

int FFMPEG_Base::av_dict_set_with_check(AVDictionary **pm, const char *key, const char *value, int flags, const char * filename) const
{
    int ret = av_dict_set(pm, key, value, flags);

    if (ret < 0)
    {
        ffmpegfs_error(filename, "Error setting dictionary option key(%s)='%s' (error '%s').", key, value, ffmpeg_geterror(ret).c_str());
    }

    return ret;
}

int FFMPEG_Base::av_opt_set_with_check(void *obj, const char *key, const char *value, int flags, const char * filename) const
{
    int ret = av_opt_set(obj, key, value, flags);

    if (ret < 0)
    {
        ffmpegfs_error(filename, "Error setting dictionary option key(%s)='%s' (error '%s').", key, value, ffmpeg_geterror(ret).c_str());
    }

    return ret;
}

void FFMPEG_Base::video_info(bool out_file, const AVFormatContext *format_ctx, const AVCodecContext *codec, const AVStream *stream)
{
    time_t duration = AV_NOPTS_VALUE;

    if (stream->duration != AV_NOPTS_VALUE)
    {
        duration = av_rescale_q_rnd(stream->duration, stream->time_base, av_get_time_base_q(), (AVRounding)(AV_ROUND_UP | AV_ROUND_PASS_MINMAX)) / AV_TIME_BASE;
    }

    ffmpegfs_info(out_file ? destname() : filename(), "Video %s: %s Bit Rate: %s Duration: %s",
                  out_file ? "out" : "in",
                  get_codec_name(codec->codec_id, 0),
                  format_bitrate((CODECPAR(stream)->bit_rate != 0) ? CODECPAR(stream)->bit_rate : format_ctx->bit_rate).c_str(),
                  format_duration(duration).c_str());
}

void FFMPEG_Base::audio_info(bool out_file, const AVFormatContext *format_ctx, const AVCodecContext *codec, const AVStream *stream)
{
    time_t duration = AV_NOPTS_VALUE;

    if (stream->duration != AV_NOPTS_VALUE)
    {
        duration = av_rescale_q_rnd(stream->duration, stream->time_base, av_get_time_base_q(), (AVRounding)(AV_ROUND_UP | AV_ROUND_PASS_MINMAX)) / AV_TIME_BASE;
    }

    ffmpegfs_info(out_file ? destname() : filename(), "Audio %s: %s Bit Rate: %s Channels: %i Sample Rate: %s Duration: %s",
                  out_file ? "out" : "in",
                  get_codec_name(codec->codec_id, 0),
                  format_bitrate((CODECPAR(stream)->bit_rate != 0) ? CODECPAR(stream)->bit_rate : format_ctx->bit_rate).c_str(),
                  codec->channels,
                  format_samplerate(codec->sample_rate).c_str(),
                  format_duration(duration).c_str());
}

