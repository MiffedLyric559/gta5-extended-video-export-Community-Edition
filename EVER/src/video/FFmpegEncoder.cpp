#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 26812)

#include "FFmpegEncoder.h"
#include "util.h"

#include <map>
#include <sstream>
#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

#pragma warning(pop)

namespace Encoder {

    FFmpegEncoder::FFmpegEncoder() {
        PRE();
        LOG(LL_NFO, "FFmpegEncoder: Constructor called");
        
        swsFlags_ = SWS_BILINEAR;
        
        static bool ffmpegInitialized = false;
        if (!ffmpegInitialized) {
            LOG(LL_DBG, "FFmpegEncoder: Initializing FFmpeg library");
#if LIBAVCODEC_VERSION_MAJOR < 58
            av_register_all();
#endif
            avformat_network_init();
            ffmpegInitialized = true;
            LOG(LL_DBG, "FFmpegEncoder: FFmpeg library initialized successfully");
        }
        
        POST();
    }

    FFmpegEncoder::~FFmpegEncoder() {
        PRE();
        LOG(LL_NFO, "FFmpegEncoder: Destructor called");
        Cleanup();
        POST();
    }

    HRESULT FFmpegEncoder::SetConfig(const FFmpeg::FFENCODERCONFIG& config) {
        PRE();
        LOG(LL_DBG, "FFmpegEncoder::SetConfig called");
        
        std::lock_guard<std::mutex> lock(encoderMutex_);
        
        config_ = config;
        configSet_ = true;
        
        LOG(LL_DBG, "FFmpegEncoder::SetConfig - Video encoder: ", config_.video.encoder);
        LOG(LL_DBG, "FFmpegEncoder::SetConfig - Audio encoder: ", config_.audio.encoder);
        LOG(LL_DBG, "FFmpegEncoder::SetConfig - Container: ", config_.format.container);
        LOG(LL_DBG, "FFmpegEncoder::SetConfig - Faststart: ", config_.format.faststart ? "true" : "false");
        LOG(LL_DBG, "FFmpegEncoder::SetConfig - Video options: ", config_.video.options);
        LOG(LL_DBG, "FFmpegEncoder::SetConfig - Audio options: ", config_.audio.options);
        
        POST();
        return S_OK;
    }

    HRESULT FFmpegEncoder::Open(const FFmpeg::FFENCODERINFO& info) {
        PRE();
        LOG(LL_NFO, "FFmpegEncoder::Open called");
        
        std::lock_guard<std::mutex> lock(encoderMutex_);
        
        if (!configSet_) {
            LOG(LL_ERR, "FFmpegEncoder::Open - Configuration not set");
            POST();
            return E_FAIL;
        }
        
        if (isOpen_) {
            LOG(LL_WRN, "FFmpegEncoder::Open - Encoder already open");
            POST();
            return S_OK;
        }
        
        info_ = info;
        outputFilename_ = std::wstring(info_.filename);
        
        LOG(LL_NFO, "FFmpegEncoder::Open - Output file: ", utf8_encode(outputFilename_));
        LOG(LL_DBG, "FFmpegEncoder::Open - Video enabled: ", info_.video.enabled);
        if (info_.video.enabled) {
            LOG(LL_DBG, "FFmpegEncoder::Open - Video resolution: ", info_.video.width, "x", info_.video.height);
            LOG(LL_DBG, "FFmpegEncoder::Open - Video timebase: ", info_.video.timebase.num, "/", info_.video.timebase.den);
        }
        LOG(LL_DBG, "FFmpegEncoder::Open - Audio enabled: ", info_.audio.enabled);
        if (info_.audio.enabled) {
            LOG(LL_DBG, "FFmpegEncoder::Open - Audio sample rate: ", info_.audio.samplerate);
            LOG(LL_DBG, "FFmpegEncoder::Open - Audio channels: ", info_.audio.numberChannels);
        }
        
        std::string filename = utf8_encode(outputFilename_);
        std::string formatName = config_.format.container;
        
        LOG(LL_DBG, "FFmpegEncoder::Open - Allocating output context for format: ", formatName);
        
        int ret = avformat_alloc_output_context2(&formatContext_, nullptr, formatName.c_str(), filename.c_str());
        if (ret < 0 || !formatContext_) {
            LOG(LL_ERR, "FFmpegEncoder::Open - Failed to allocate output context, error code: ", ret);
            Cleanup();
            POST();
            return E_FAIL;
        }
        
        LOG(LL_DBG, "FFmpegEncoder::Open - Output context allocated successfully");
        
        if (info_.video.enabled) {
            LOG(LL_DBG, "FFmpegEncoder::Open - Initializing video encoder");
            HRESULT hr = InitializeVideoEncoder();
            if (FAILED(hr)) {
                LOG(LL_ERR, "FFmpegEncoder::Open - Video encoder initialization failed");
                Cleanup();
                POST();
                return hr;
            }
            LOG(LL_DBG, "FFmpegEncoder::Open - Video encoder initialized successfully");
        }
        
        if (info_.audio.enabled) {
            LOG(LL_DBG, "FFmpegEncoder::Open - Initializing audio encoder");
            HRESULT hr = InitializeAudioEncoder();
            if (FAILED(hr)) {
                LOG(LL_ERR, "FFmpegEncoder::Open - Audio encoder initialization failed");
                Cleanup();
                POST();
                return hr;
            }
            LOG(LL_DBG, "FFmpegEncoder::Open - Audio encoder initialized successfully");
        }
        
        if (!(formatContext_->oformat->flags & AVFMT_NOFILE)) {
            LOG(LL_DBG, "FFmpegEncoder::Open - Opening output file: ", filename);
            ret = avio_open(&formatContext_->pb, filename.c_str(), AVIO_FLAG_WRITE);
            if (ret < 0) {
                LOG(LL_ERR, "FFmpegEncoder::Open - Failed to open output file, error code: ", ret);
                Cleanup();
                POST();
                return E_FAIL;
            }
            LOG(LL_DBG, "FFmpegEncoder::Open - Output file opened successfully");
        }
        
        AVDictionary* muxerOpts = nullptr;
        if (config_.format.faststart && std::string(config_.format.container) == "mp4") {
            LOG(LL_DBG, "FFmpegEncoder::Open - Setting faststart option for MP4");
            av_dict_set(&muxerOpts, "movflags", "faststart", 0);
        }
        
        LOG(LL_DBG, "FFmpegEncoder::Open - Writing file header");
        ret = avformat_write_header(formatContext_, &muxerOpts);
        av_dict_free(&muxerOpts);
        if (ret < 0) {
            LOG(LL_ERR, "FFmpegEncoder::Open - Failed to write file header, error code: ", ret);
            Cleanup();
            POST();
            return E_FAIL;
        }
        
        LOG(LL_NFO, "FFmpegEncoder::Open - File header written successfully");
        LOG(LL_DBG, "FFmpegEncoder::Open - Encoder opened and ready for encoding");
        
        isOpen_ = true;
        videoPts_ = 0;
        audioPts_ = 0;
        
        POST();
        return S_OK;
    }

    HRESULT FFmpegEncoder::InitializeVideoEncoder() {
        PRE();
        LOG(LL_DBG, "FFmpegEncoder::InitializeVideoEncoder - Finding video codec: ", config_.video.encoder);
        
        const AVCodec* codec = avcodec_find_encoder_by_name(config_.video.encoder);
        if (!codec) {
            LOG(LL_ERR, "FFmpegEncoder::InitializeVideoEncoder - Codec not found: ", config_.video.encoder);
            POST();
            return E_FAIL;
        }
        
        LOG(LL_DBG, "FFmpegEncoder::InitializeVideoEncoder - Codec found: ", codec->name);
        
        LOG(LL_DBG, "FFmpegEncoder::InitializeVideoEncoder - Creating video stream");
        videoStream_ = avformat_new_stream(formatContext_, nullptr);
        if (!videoStream_) {
            LOG(LL_ERR, "FFmpegEncoder::InitializeVideoEncoder - Failed to create video stream");
            POST();
            return E_FAIL;
        }
        
        videoStream_->id = formatContext_->nb_streams - 1;
        LOG(LL_DBG, "FFmpegEncoder::InitializeVideoEncoder - Video stream created, ID: ", videoStream_->id);
        
        LOG(LL_DBG, "FFmpegEncoder::InitializeVideoEncoder - Allocating codec context");
        videoCodecContext_ = avcodec_alloc_context3(codec);
        if (!videoCodecContext_) {
            LOG(LL_ERR, "FFmpegEncoder::InitializeVideoEncoder - Failed to allocate codec context");
            POST();
            return E_FAIL;
        }
        
        videoCodecContext_->width = info_.video.width;
        videoCodecContext_->height = info_.video.height;
        videoCodecContext_->time_base = AVRational{info_.video.timebase.num, info_.video.timebase.den};
        videoCodecContext_->framerate = AVRational{info_.video.timebase.den, info_.video.timebase.num};
        
        LOG(LL_DBG, "FFmpegEncoder::InitializeVideoEncoder - Resolution: ", videoCodecContext_->width, "x", videoCodecContext_->height);
        LOG(LL_DBG, "FFmpegEncoder::InitializeVideoEncoder - Time base: ", videoCodecContext_->time_base.num, "/", videoCodecContext_->time_base.den);
        LOG(LL_DBG, "FFmpegEncoder::InitializeVideoEncoder - Frame rate: ", videoCodecContext_->framerate.num, "/", videoCodecContext_->framerate.den);
        
        LOG(LL_DBG, "FFmpegEncoder::InitializeVideoEncoder - Parsing encoder options");
        HRESULT hr = ParseEncoderOptions(config_.video.options, videoCodecContext_);
        if (FAILED(hr)) {
            LOG(LL_ERR, "FFmpegEncoder::InitializeVideoEncoder - Failed to parse encoder options");
            POST();
            return hr;
        }
        
        // If pixel format not set, use a default
        if (videoCodecContext_->pix_fmt == AV_PIX_FMT_NONE) {
            LOG(LL_WRN, "FFmpegEncoder::InitializeVideoEncoder - Pixel format not set, using default");
            if (codec->pix_fmts) {
                videoCodecContext_->pix_fmt = codec->pix_fmts[0];
                LOG(LL_NFO, "FFmpegEncoder::InitializeVideoEncoder - Set pixel format to: ", av_get_pix_fmt_name(videoCodecContext_->pix_fmt));
            }
        }
        
        LOG(LL_DBG, "FFmpegEncoder::InitializeVideoEncoder - Final pixel format: ", av_get_pix_fmt_name(videoCodecContext_->pix_fmt));
        LOG(LL_DBG, "FFmpegEncoder::InitializeVideoEncoder - Codec has constraints:");
        LOG(LL_DBG, "  Width: ", videoCodecContext_->width, ", Height: ", videoCodecContext_->height);
        LOG(LL_DBG, "  Time base: ", videoCodecContext_->time_base.num, "/", videoCodecContext_->time_base.den);
        LOG(LL_DBG, "  Frame rate: ", videoCodecContext_->framerate.num, "/", videoCodecContext_->framerate.den);
        
        // Check if pixel format is supported by codec
        if (codec->pix_fmts) {
            bool pix_fmt_supported = false;
            for (int i = 0; codec->pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
                if (codec->pix_fmts[i] == videoCodecContext_->pix_fmt) {
                    pix_fmt_supported = true;
                    break;
                }
            }
            if (!pix_fmt_supported) {
                LOG(LL_ERR, "FFmpegEncoder::InitializeVideoEncoder - Pixel format ", av_get_pix_fmt_name(videoCodecContext_->pix_fmt), " is NOT supported by codec!");
                LOG(LL_ERR, "FFmpegEncoder::InitializeVideoEncoder - Supported formats:");
                for (int i = 0; codec->pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
                    LOG(LL_ERR, "  - ", av_get_pix_fmt_name(codec->pix_fmts[i]));
                }
            } else {
                LOG(LL_DBG, "FFmpegEncoder::InitializeVideoEncoder - Pixel format is supported by codec");
            }
        }
        
        if (formatContext_->oformat->flags & AVFMT_GLOBALHEADER) {
            LOG(LL_DBG, "FFmpegEncoder::InitializeVideoEncoder - Setting global header flag");
            videoCodecContext_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        
        LOG(LL_DBG, "FFmpegEncoder::InitializeVideoEncoder - About to open codec with:");
        LOG(LL_DBG, "  Codec: ", codec->name);
        LOG(LL_DBG, "  Resolution: ", videoCodecContext_->width, "x", videoCodecContext_->height);
        LOG(LL_DBG, "  Pixel format: ", av_get_pix_fmt_name(videoCodecContext_->pix_fmt));
        LOG(LL_DBG, "  Time base: ", videoCodecContext_->time_base.num, "/", videoCodecContext_->time_base.den);
        
        LOG(LL_DBG, "FFmpegEncoder::InitializeVideoEncoder - Opening video codec");
        int ret = avcodec_open2(videoCodecContext_, codec, nullptr);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG(LL_ERR, "FFmpegEncoder::InitializeVideoEncoder - Failed to open codec, error code: ", ret, " (", errbuf, ")");
            LOG(LL_ERR, "FFmpegEncoder::InitializeVideoEncoder - Codec: ", config_.video.encoder);
            LOG(LL_ERR, "FFmpegEncoder::InitializeVideoEncoder - Resolution: ", videoCodecContext_->width, "x", videoCodecContext_->height);
            LOG(LL_ERR, "FFmpegEncoder::InitializeVideoEncoder - Pixel format: ", av_get_pix_fmt_name(videoCodecContext_->pix_fmt));
            LOG(LL_ERR, "FFmpegEncoder::InitializeVideoEncoder - Options: ", config_.video.options);
            
            // Try to get more details from priv_data if available
            if (videoCodecContext_->priv_data) {
                LOG(LL_ERR, "FFmpegEncoder::InitializeVideoEncoder - Checking codec private options:");
                const AVOption *opt = nullptr;
                void *obj = videoCodecContext_->priv_data;
                while ((opt = av_opt_next(obj, opt))) {
                    uint8_t *val_str = nullptr;
                    if (av_opt_get(obj, opt->name, 0, &val_str) >= 0) {
                        LOG(LL_ERR, "    ", opt->name, " = ", (const char*)val_str);
                        av_free(val_str);
                    }
                }
            }
            
            POST();
            return E_FAIL;
        }
        
        LOG(LL_DBG, "FFmpegEncoder::InitializeVideoEncoder - Video codec opened successfully");
        
        ret = avcodec_parameters_from_context(videoStream_->codecpar, videoCodecContext_);
        if (ret < 0) {
            LOG(LL_ERR, "FFmpegEncoder::InitializeVideoEncoder - Failed to copy codec parameters, error code: ", ret);
            POST();
            return E_FAIL;
        }
        
        videoStream_->time_base = videoCodecContext_->time_base;
        
        LOG(LL_DBG, "FFmpegEncoder::InitializeVideoEncoder - Allocating video frame");
        videoFrame_ = av_frame_alloc();
        if (!videoFrame_) {
            LOG(LL_ERR, "FFmpegEncoder::InitializeVideoEncoder - Failed to allocate video frame");
            POST();
            return E_FAIL;
        }
        
        videoFrame_->format = videoCodecContext_->pix_fmt;
        videoFrame_->width = videoCodecContext_->width;
        videoFrame_->height = videoCodecContext_->height;
        
        LOG(LL_DBG, "FFmpegEncoder::InitializeVideoEncoder - Video frame allocated");
        LOG(LL_NFO, "FFmpegEncoder::InitializeVideoEncoder - Video encoder initialization complete");
        
        POST();
        return S_OK;
    }

    HRESULT FFmpegEncoder::InitializeAudioEncoder() {
        PRE();
        LOG(LL_DBG, "FFmpegEncoder::InitializeAudioEncoder - Finding audio codec: ", config_.audio.encoder);
        
        const AVCodec* codec = avcodec_find_encoder_by_name(config_.audio.encoder);
        if (!codec) {
            LOG(LL_ERR, "FFmpegEncoder::InitializeAudioEncoder - Codec not found: ", config_.audio.encoder);
            POST();
            return E_FAIL;
        }
        
        LOG(LL_DBG, "FFmpegEncoder::InitializeAudioEncoder - Codec found: ", codec->name);
        
        LOG(LL_DBG, "FFmpegEncoder::InitializeAudioEncoder - Creating audio stream");
        audioStream_ = avformat_new_stream(formatContext_, nullptr);
        if (!audioStream_) {
            LOG(LL_ERR, "FFmpegEncoder::InitializeAudioEncoder - Failed to create audio stream");
            POST();
            return E_FAIL;
        }
        
        audioStream_->id = formatContext_->nb_streams - 1;
        LOG(LL_DBG, "FFmpegEncoder::InitializeAudioEncoder - Audio stream created, ID: ", audioStream_->id);
        
        LOG(LL_DBG, "FFmpegEncoder::InitializeAudioEncoder - Allocating codec context");
        audioCodecContext_ = avcodec_alloc_context3(codec);
        if (!audioCodecContext_) {
            LOG(LL_ERR, "FFmpegEncoder::InitializeAudioEncoder - Failed to allocate codec context");
            POST();
            return E_FAIL;
        }
        
        audioCodecContext_->sample_rate = info_.audio.samplerate;
        
#if LIBAVUTIL_VERSION_MAJOR >= 57
        // FFmpeg 5.0+
        switch (info_.audio.channellayout) {
            case FFmpeg::ChannelLayout::Mono:
                av_channel_layout_default(&audioCodecContext_->ch_layout, 1);
                break;
            case FFmpeg::ChannelLayout::Stereo:
                av_channel_layout_default(&audioCodecContext_->ch_layout, 2);
                break;
            case FFmpeg::ChannelLayout::FivePointOne:
                av_channel_layout_default(&audioCodecContext_->ch_layout, 6);
                break;
        }
        LOG(LL_DBG, "FFmpegEncoder::InitializeAudioEncoder - Channels: ", audioCodecContext_->ch_layout.nb_channels);
#else
        switch (info_.audio.channellayout) {
            case FFmpeg::ChannelLayout::Mono:
                audioCodecContext_->channel_layout = AV_CH_LAYOUT_MONO;
                audioCodecContext_->channels = 1;
                break;
            case FFmpeg::ChannelLayout::Stereo:
                audioCodecContext_->channel_layout = AV_CH_LAYOUT_STEREO;
                audioCodecContext_->channels = 2;
                break;
            case FFmpeg::ChannelLayout::FivePointOne:
                audioCodecContext_->channel_layout = AV_CH_LAYOUT_5POINT1;
                audioCodecContext_->channels = 6;
                break;
        }
        LOG(LL_DBG, "FFmpegEncoder::InitializeAudioEncoder - Channels: ", audioCodecContext_->channels);
#endif
        
        audioCodecContext_->time_base = AVRational{1, audioCodecContext_->sample_rate};
        
        LOG(LL_DBG, "FFmpegEncoder::InitializeAudioEncoder - Sample rate: ", audioCodecContext_->sample_rate);
        LOG(LL_DBG, "FFmpegEncoder::InitializeAudioEncoder - Time base: ", audioCodecContext_->time_base.num, "/", audioCodecContext_->time_base.den);
        
        LOG(LL_DBG, "FFmpegEncoder::InitializeAudioEncoder - Parsing encoder options");
        HRESULT hr = ParseEncoderOptions(config_.audio.options, audioCodecContext_);
        if (FAILED(hr)) {
            LOG(LL_ERR, "FFmpegEncoder::InitializeAudioEncoder - Failed to parse encoder options");
            POST();
            return hr;
        }
        
        if (formatContext_->oformat->flags & AVFMT_GLOBALHEADER) {
            LOG(LL_DBG, "FFmpegEncoder::InitializeAudioEncoder - Setting global header flag");
            audioCodecContext_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        
        LOG(LL_DBG, "FFmpegEncoder::InitializeAudioEncoder - Opening audio codec");
        int ret = avcodec_open2(audioCodecContext_, codec, nullptr);
        if (ret < 0) {
            LOG(LL_ERR, "FFmpegEncoder::InitializeAudioEncoder - Failed to open codec, error code: ", ret);
            POST();
            return E_FAIL;
        }
        
        LOG(LL_DBG, "FFmpegEncoder::InitializeAudioEncoder - Audio codec opened successfully");
        
        ret = avcodec_parameters_from_context(audioStream_->codecpar, audioCodecContext_);
        if (ret < 0) {
            LOG(LL_ERR, "FFmpegEncoder::InitializeAudioEncoder - Failed to copy codec parameters, error code: ", ret);
            POST();
            return E_FAIL;
        }
        
        audioStream_->time_base = audioCodecContext_->time_base;
        
        LOG(LL_DBG, "FFmpegEncoder::InitializeAudioEncoder - Allocating audio frame");
        audioFrame_ = av_frame_alloc();
        if (!audioFrame_) {
            LOG(LL_ERR, "FFmpegEncoder::InitializeAudioEncoder - Failed to allocate audio frame");
            POST();
            return E_FAIL;
        }
        
        audioFrame_->format = audioCodecContext_->sample_fmt;
        audioFrame_->sample_rate = audioCodecContext_->sample_rate;
#if LIBAVUTIL_VERSION_MAJOR >= 57
        av_channel_layout_copy(&audioFrame_->ch_layout, &audioCodecContext_->ch_layout);
#else
        audioFrame_->channel_layout = audioCodecContext_->channel_layout;
        audioFrame_->channels = audioCodecContext_->channels;
#endif

    int initialNbSamples = audioCodecContext_->frame_size > 0 ? audioCodecContext_->frame_size : 1024;
    audioFrame_->nb_samples = initialNbSamples;
    ret = av_frame_get_buffer(audioFrame_, 0);
    if (ret < 0) {
        LOG(LL_ERR, "FFmpegEncoder::InitializeAudioEncoder - Failed to allocate audio frame buffer, error code: ", ret);
        POST();
        return E_FAIL;
    }

    int fifoChannels = 0;
#if LIBAVUTIL_VERSION_MAJOR >= 57
    fifoChannels = audioCodecContext_->ch_layout.nb_channels;
#else
    fifoChannels = audioCodecContext_->channels;
#endif
    audioFifo_ = av_audio_fifo_alloc(audioCodecContext_->sample_fmt, fifoChannels, initialNbSamples);
    if (!audioFifo_) {
        LOG(LL_ERR, "FFmpegEncoder::InitializeAudioEncoder - Failed to allocate audio FIFO");
        POST();
        return E_FAIL;
    }

    LOG(LL_DBG, "FFmpegEncoder::InitializeAudioEncoder - Audio frame allocated");
        LOG(LL_NFO, "FFmpegEncoder::InitializeAudioEncoder - Audio encoder initialization complete");
        
        POST();
        return S_OK;
    }

    HRESULT FFmpegEncoder::InitializeVideoFilterGraph(int inputPixFmtInt, int inputWidth, int inputHeight) {
        PRE();
        LOG(LL_DBG, "FFmpegEncoder::InitializeVideoFilterGraph - Initializing video filter graph");

        AVPixelFormat inputPixFmt = static_cast<AVPixelFormat>(inputPixFmtInt);

        const AVFilter* buffersrc  = avfilter_get_by_name("buffer");
        const AVFilter* buffersink = avfilter_get_by_name("buffersink");

        if (!buffersrc || !buffersink) {
            LOG(LL_ERR, "FFmpegEncoder::InitializeVideoFilterGraph - Failed to find buffer/buffersink filters");
            POST();
            return E_FAIL;
        }

        videoFilterGraph_ = avfilter_graph_alloc();
        if (!videoFilterGraph_) {
            LOG(LL_ERR, "FFmpegEncoder::InitializeVideoFilterGraph - Failed to allocate filter graph");
            POST();
            return E_FAIL;
        }

        char args[512];
        snprintf(args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=1/1",
            inputWidth, inputHeight, static_cast<int>(inputPixFmt),
            videoCodecContext_->time_base.num,
            videoCodecContext_->time_base.den);

        int ret = avfilter_graph_create_filter(&videoBufferSrcCtx_, buffersrc, "in",
            args, nullptr, videoFilterGraph_);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG(LL_ERR, "FFmpegEncoder::InitializeVideoFilterGraph - Failed to create buffer source: ", errbuf);
            avfilter_graph_free(&videoFilterGraph_);
            videoFilterGraph_ = nullptr;
            POST();
            return E_FAIL;
        }

        ret = avfilter_graph_create_filter(&videoBufferSinkCtx_, buffersink, "out",
            nullptr, nullptr, videoFilterGraph_);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG(LL_ERR, "FFmpegEncoder::InitializeVideoFilterGraph - Failed to create buffer sink: ", errbuf);
            avfilter_graph_free(&videoFilterGraph_);
            videoFilterGraph_ = nullptr;
            videoBufferSrcCtx_ = nullptr;
            POST();
            return E_FAIL;
        }

        AVPixelFormat pix_fmts[] = { videoCodecContext_->pix_fmt, AV_PIX_FMT_NONE };
        ret = av_opt_set_int_list(videoBufferSinkCtx_, "pix_fmts", pix_fmts,
            AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG(LL_ERR, "FFmpegEncoder::InitializeVideoFilterGraph - Failed to set sink pixel formats: ", errbuf);
            avfilter_graph_free(&videoFilterGraph_);
            videoFilterGraph_ = nullptr;
            videoBufferSrcCtx_ = nullptr;
            videoBufferSinkCtx_ = nullptr;
            POST();
            return E_FAIL;
        }

        AVFilterInOut* outputs = avfilter_inout_alloc();
        AVFilterInOut* inputs  = avfilter_inout_alloc();
        if (!outputs || !inputs) {
            LOG(LL_ERR, "FFmpegEncoder::InitializeVideoFilterGraph - Failed to allocate filter inout");
            avfilter_inout_free(&outputs);
            avfilter_inout_free(&inputs);
            avfilter_graph_free(&videoFilterGraph_);
            videoFilterGraph_ = nullptr;
            videoBufferSrcCtx_ = nullptr;
            videoBufferSinkCtx_ = nullptr;
            POST();
            return E_FAIL;
        }

        outputs->name       = av_strdup("in");
        outputs->filter_ctx = videoBufferSrcCtx_;
        outputs->pad_idx    = 0;
        outputs->next       = nullptr;

        inputs->name        = av_strdup("out");
        inputs->filter_ctx  = videoBufferSinkCtx_;
        inputs->pad_idx     = 0;
        inputs->next        = nullptr;

        ret = avfilter_graph_parse_ptr(videoFilterGraph_, config_.video.filters,
            &inputs, &outputs, nullptr);
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);

        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG(LL_ERR, "FFmpegEncoder::InitializeVideoFilterGraph - Failed to parse filter graph: ", errbuf);
            avfilter_graph_free(&videoFilterGraph_);
            videoFilterGraph_ = nullptr;
            videoBufferSrcCtx_ = nullptr;
            videoBufferSinkCtx_ = nullptr;
            POST();
            return E_FAIL;
        }

        ret = avfilter_graph_config(videoFilterGraph_, nullptr);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG(LL_ERR, "FFmpegEncoder::InitializeVideoFilterGraph - Failed to configure filter graph: ", errbuf);
            avfilter_graph_free(&videoFilterGraph_);
            videoFilterGraph_ = nullptr;
            videoBufferSrcCtx_ = nullptr;
            videoBufferSinkCtx_ = nullptr;
            POST();
            return E_FAIL;
        }

        int sinkW = av_buffersink_get_w(videoBufferSinkCtx_);
        int sinkH = av_buffersink_get_h(videoBufferSinkCtx_);

        if (sinkW != videoCodecContext_->width || sinkH != videoCodecContext_->height) {
            LOG(LL_ERR, "FFmpegEncoder::InitializeVideoFilterGraph - Filter graph output dimensions ",
                sinkW, "x", sinkH, " do not match codec dimensions ",
                videoCodecContext_->width, "x", videoCodecContext_->height,
                " - falling back to SWS path");
            avfilter_graph_free(&videoFilterGraph_);
            videoFilterGraph_ = nullptr;
            videoBufferSrcCtx_ = nullptr;
            videoBufferSinkCtx_ = nullptr;
            POST();
            return E_FAIL;
        }

        LOG(LL_NFO, "FFmpegEncoder::InitializeVideoFilterGraph - Video filter graph initialized successfully");
        POST();
        return S_OK;
    }

    HRESULT FFmpegEncoder::InitializeAudioFilterGraph(int inputSampleFmtInt, int inputSampleRate, int inputNbChannels) {
        PRE();
        LOG(LL_DBG, "FFmpegEncoder::InitializeAudioFilterGraph - Initializing audio filter graph");

        AVSampleFormat inputSampleFmt = static_cast<AVSampleFormat>(inputSampleFmtInt);

        const AVFilter* abuffersrc  = avfilter_get_by_name("abuffer");
        const AVFilter* abuffersink = avfilter_get_by_name("abuffersink");

        if (!abuffersrc || !abuffersink) {
            LOG(LL_ERR, "FFmpegEncoder::InitializeAudioFilterGraph - Failed to find abuffer/abuffersink filters");
            POST();
            return E_FAIL;
        }

        audioFilterGraph_ = avfilter_graph_alloc();
        if (!audioFilterGraph_) {
            LOG(LL_ERR, "FFmpegEncoder::InitializeAudioFilterGraph - Failed to allocate audio filter graph");
            POST();
            return E_FAIL;
        }

        char args[512];
#if LIBAVUTIL_VERSION_MAJOR >= 57
        char ch_layout_str[64];
        AVChannelLayout inputLayout = {};
        av_channel_layout_default(&inputLayout, inputNbChannels);
        av_channel_layout_describe(&inputLayout, ch_layout_str, sizeof(ch_layout_str));
        av_channel_layout_uninit(&inputLayout);
        snprintf(args, sizeof(args),
            "time_base=1/%d:sample_rate=%d:sample_fmt=%s:channels=%d:channel_layout=%s",
            inputSampleRate, inputSampleRate,
            av_get_sample_fmt_name(inputSampleFmt),
            inputNbChannels,
            ch_layout_str);
#else
        uint64_t inChLayout = av_get_default_channel_layout(inputNbChannels);
        snprintf(args, sizeof(args),
            "time_base=1/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%" PRIx64,
            inputSampleRate, inputSampleRate,
            av_get_sample_fmt_name(inputSampleFmt),
            inChLayout);
#endif

        int ret = avfilter_graph_create_filter(&audioBufferSrcCtx_, abuffersrc, "in",
            args, nullptr, audioFilterGraph_);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG(LL_ERR, "FFmpegEncoder::InitializeAudioFilterGraph - Failed to create abuffer source: ", errbuf);
            avfilter_graph_free(&audioFilterGraph_);
            audioFilterGraph_ = nullptr;
            POST();
            return E_FAIL;
        }

        ret = avfilter_graph_create_filter(&audioBufferSinkCtx_, abuffersink, "out",
            nullptr, nullptr, audioFilterGraph_);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG(LL_ERR, "FFmpegEncoder::InitializeAudioFilterGraph - Failed to create abuffer sink: ", errbuf);
            avfilter_graph_free(&audioFilterGraph_);
            audioFilterGraph_ = nullptr;
            audioBufferSrcCtx_ = nullptr;
            POST();
            return E_FAIL;
        }

        AVSampleFormat out_sample_fmts[] = { audioCodecContext_->sample_fmt, AV_SAMPLE_FMT_NONE };
        int out_sample_rates[] = { audioCodecContext_->sample_rate, -1 };
#if LIBAVUTIL_VERSION_MAJOR >= 57
        int out_channel_counts[] = { audioCodecContext_->ch_layout.nb_channels, -1 };
#else
        int out_channel_counts[] = { audioCodecContext_->channels, -1 };
#endif

        ret = av_opt_set_int_list(audioBufferSinkCtx_, "sample_fmts", out_sample_fmts,
            AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG(LL_ERR, "FFmpegEncoder::InitializeAudioFilterGraph - Failed to set sink sample formats: ", errbuf);
            avfilter_graph_free(&audioFilterGraph_);
            audioFilterGraph_ = nullptr;
            audioBufferSrcCtx_ = nullptr;
            audioBufferSinkCtx_ = nullptr;
            POST();
            return E_FAIL;
        }

        ret = av_opt_set_int_list(audioBufferSinkCtx_, "sample_rates", out_sample_rates,
            -1, AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG(LL_ERR, "FFmpegEncoder::InitializeAudioFilterGraph - Failed to set sink sample rates: ", errbuf);
            avfilter_graph_free(&audioFilterGraph_);
            audioFilterGraph_ = nullptr;
            audioBufferSrcCtx_ = nullptr;
            audioBufferSinkCtx_ = nullptr;
            POST();
            return E_FAIL;
        }

        ret = av_opt_set_int_list(audioBufferSinkCtx_, "channel_counts", out_channel_counts,
            -1, AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG(LL_ERR, "FFmpegEncoder::InitializeAudioFilterGraph - Failed to set sink channel counts: ", errbuf);
            avfilter_graph_free(&audioFilterGraph_);
            audioFilterGraph_ = nullptr;
            audioBufferSrcCtx_ = nullptr;
            audioBufferSinkCtx_ = nullptr;
            POST();
            return E_FAIL;
        }

        AVFilterInOut* outputs = avfilter_inout_alloc();
        AVFilterInOut* inputs  = avfilter_inout_alloc();
        if (!outputs || !inputs) {
            LOG(LL_ERR, "FFmpegEncoder::InitializeAudioFilterGraph - Failed to allocate filter inout");
            avfilter_inout_free(&outputs);
            avfilter_inout_free(&inputs);
            avfilter_graph_free(&audioFilterGraph_);
            audioFilterGraph_ = nullptr;
            audioBufferSrcCtx_ = nullptr;
            audioBufferSinkCtx_ = nullptr;
            POST();
            return E_FAIL;
        }

        outputs->name       = av_strdup("in");
        outputs->filter_ctx = audioBufferSrcCtx_;
        outputs->pad_idx    = 0;
        outputs->next       = nullptr;

        inputs->name        = av_strdup("out");
        inputs->filter_ctx  = audioBufferSinkCtx_;
        inputs->pad_idx     = 0;
        inputs->next        = nullptr;

        ret = avfilter_graph_parse_ptr(audioFilterGraph_, config_.audio.filters,
            &inputs, &outputs, nullptr);
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);

        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG(LL_ERR, "FFmpegEncoder::InitializeAudioFilterGraph - Failed to parse audio filter graph: ", errbuf);
            avfilter_graph_free(&audioFilterGraph_);
            audioFilterGraph_ = nullptr;
            audioBufferSrcCtx_ = nullptr;
            audioBufferSinkCtx_ = nullptr;
            POST();
            return E_FAIL;
        }

        ret = avfilter_graph_config(audioFilterGraph_, nullptr);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOG(LL_ERR, "FFmpegEncoder::InitializeAudioFilterGraph - Failed to configure audio filter graph: ", errbuf);
            avfilter_graph_free(&audioFilterGraph_);
            audioFilterGraph_ = nullptr;
            audioBufferSrcCtx_ = nullptr;
            audioBufferSinkCtx_ = nullptr;
            POST();
            return E_FAIL;
        }

        LOG(LL_NFO, "FFmpegEncoder::InitializeAudioFilterGraph - Audio filter graph initialized successfully");
        POST();
        return S_OK;
    }

    HRESULT FFmpegEncoder::ParseEncoderOptions(const char* optionsString, AVCodecContext* codecContext) {
        PRE();
        LOG(LL_DBG, "FFmpegEncoder::ParseEncoderOptions - Parsing options: ", optionsString);
        
        if (!optionsString || strlen(optionsString) == 0) {
            LOG(LL_DBG, "FFmpegEncoder::ParseEncoderOptions - No options to parse");
            POST();
            return S_OK;
        }
        
        std::string options(optionsString);
        std::istringstream stream(options);
        std::string token;
        
        int optionCount = 0;
        
        while (std::getline(stream, token, '|')) {
            if (token.empty()) continue;
            
            size_t pos = token.find('=');
            if (pos == std::string::npos) {
                LOG(LL_WRN, "FFmpegEncoder::ParseEncoderOptions - Invalid option format (no '='): ", token);
                continue;
            }
            
            std::string key = token.substr(0, pos);
            std::string value = token.substr(pos + 1);
            
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            LOG(LL_DBG, "FFmpegEncoder::ParseEncoderOptions - Setting option: ", key, " = ", value);
            
            if (key == "_pixelFormat" || key == "_pixel_format") {
                AVPixelFormat pix_fmt = av_get_pix_fmt(value.c_str());
                if (pix_fmt != AV_PIX_FMT_NONE) {
                    codecContext->pix_fmt = pix_fmt;
                    LOG(LL_DBG, "FFmpegEncoder::ParseEncoderOptions - Set pixel format: ", value);
                    optionCount++;
                } else {
                    LOG(LL_WRN, "FFmpegEncoder::ParseEncoderOptions - Unknown pixel format: ", value);
                }
            } else if (key == "_sampleFormat" || key == "_sample_format") {
                AVSampleFormat sample_fmt = av_get_sample_fmt(value.c_str());
                if (sample_fmt != AV_SAMPLE_FMT_NONE) {
                    codecContext->sample_fmt = sample_fmt;
                    LOG(LL_DBG, "FFmpegEncoder::ParseEncoderOptions - Set sample format: ", value);
                    optionCount++;
                } else {
                    LOG(LL_WRN, "FFmpegEncoder::ParseEncoderOptions - Unknown sample format: ", value);
                }
            } else if (key == "_scaling") {
                static const std::map<std::string, int> swsAlgoMap = {
                    {"fast_bilinear", SWS_FAST_BILINEAR},
                    {"bilinear",      SWS_BILINEAR},
                    {"bicubic",       SWS_BICUBIC},
                    {"x",             SWS_X},
                    {"point",         SWS_POINT},
                    {"neighbor",      SWS_POINT},
                    {"area",          SWS_AREA},
                    {"bicublin",      SWS_BICUBLIN},
                    {"gauss",         SWS_GAUSS},
                    {"sinc",          SWS_SINC},
                    {"lanczos",       SWS_LANCZOS},
                    {"spline",        SWS_SPLINE},
                };
                auto algoIt = swsAlgoMap.find(value);
                if (algoIt != swsAlgoMap.end()) {
                    swsFlags_ = algoIt->second;
                    LOG(LL_DBG, "FFmpegEncoder::ParseEncoderOptions - Set SWS scaling flags: ", value);
                    optionCount++;
                } else {
                    LOG(LL_WRN, "FFmpegEncoder::ParseEncoderOptions - Unknown scaling algorithm: ", value, ", using bilinear");
                }
            } else {
                int ret = av_opt_set(codecContext->priv_data, key.c_str(), value.c_str(), 0);
                if (ret < 0) {
                    ret = av_opt_set(codecContext, key.c_str(), value.c_str(), 0);
                    if (ret < 0) {
                        char errbuf[256];
                        av_strerror(ret, errbuf, sizeof(errbuf));
                        LOG(LL_WRN, "FFmpegEncoder::ParseEncoderOptions - Failed to set option '", key, "' to '", value, "', error code: ", ret, " (", errbuf, ")");
                    } else {
                        LOG(LL_DBG, "FFmpegEncoder::ParseEncoderOptions - Set codec context option: ", key, " = ", value);
                        optionCount++;
                    }
                } else {
                    LOG(LL_DBG, "FFmpegEncoder::ParseEncoderOptions - Set private data option: ", key, " = ", value);
                    optionCount++;
                }
            }
        }
        
        LOG(LL_DBG, "FFmpegEncoder::ParseEncoderOptions - Parsed ", optionCount, " options successfully");
        
        POST();
        return S_OK;
    }

    HRESULT FFmpegEncoder::SendVideoFrame(const FFmpeg::FFVIDEOFRAME& frame) {
        PRE();
        LOG(LL_TRC, "FFmpegEncoder::SendVideoFrame called - PTS: ", videoPts_);
        
        std::lock_guard<std::mutex> lock(encoderMutex_);
        
        if (!isOpen_ || !videoCodecContext_) {
            LOG(LL_ERR, "FFmpegEncoder::SendVideoFrame - Encoder not open or video not enabled");
            POST();
            return E_FAIL;
        }
        
        LOG(LL_TRC, "FFmpegEncoder::SendVideoFrame - Frame size: ", frame.width, "x", frame.height, ", planes: ", frame.planes);
        
        if (!packet_) {
            packet_ = av_packet_alloc();
            if (!packet_) {
                LOG(LL_ERR, "FFmpegEncoder::SendVideoFrame - Failed to allocate packet");
                POST();
                return E_FAIL;
            }
        }
        
        AVFrame* inputFrame = av_frame_alloc();
        if (!inputFrame) {
            LOG(LL_ERR, "FFmpegEncoder::SendVideoFrame - Failed to allocate input frame");
            POST();
            return E_FAIL;
        }
        
        inputFrame->width = frame.width;
        inputFrame->height = frame.height;
        inputFrame->pts = videoPts_++;
        
        AVPixelFormat inputPixelFormat = AV_PIX_FMT_NONE;
        std::string formatStr(frame.format);
        if (formatStr == "rgba") {
            inputPixelFormat = AV_PIX_FMT_RGBA;
        } else if (formatStr == "rgb") {
            inputPixelFormat = AV_PIX_FMT_RGB24;
        } else if (formatStr == "bgra") {
            inputPixelFormat = AV_PIX_FMT_BGRA;
        } else {
            inputPixelFormat = av_get_pix_fmt(frame.format);
        }
        
        if (inputPixelFormat == AV_PIX_FMT_NONE) {
            LOG(LL_ERR, "FFmpegEncoder::SendVideoFrame - Unknown pixel format: ", frame.format);
            av_frame_free(&inputFrame);
            POST();
            return E_FAIL;
        }
        
        inputFrame->format = inputPixelFormat;
        
        LOG(LL_TRC, "FFmpegEncoder::SendVideoFrame - Input pixel format: ", av_get_pix_fmt_name(inputPixelFormat));
        LOG(LL_TRC, "FFmpegEncoder::SendVideoFrame - Output pixel format: ", av_get_pix_fmt_name(videoCodecContext_->pix_fmt));
        
        for (int i = 0; i < frame.planes; i++) {
            inputFrame->data[i] = frame.buffer[i];
            inputFrame->linesize[i] = frame.rowsize[i];
        }

        if (strlen(config_.video.filters) > 0 && !videoFilterGraph_) {
            LOG(LL_DBG, "FFmpegEncoder::SendVideoFrame - Initializing video filter graph");
            HRESULT fghr = InitializeVideoFilterGraph(static_cast<int>(inputPixelFormat), frame.width, frame.height);
            if (FAILED(fghr)) {
                LOG(LL_WRN, "FFmpegEncoder::SendVideoFrame - Video filter graph init failed, falling back to SWS path");
            }
        }

        if (videoFilterGraph_) {
            LOG(LL_TRC, "FFmpegEncoder::SendVideoFrame - Using filter graph path");

            int ret = av_buffersrc_add_frame_flags(videoBufferSrcCtx_, inputFrame, AV_BUFFERSRC_FLAG_KEEP_REF);
            if (ret < 0) {
                char errbuf[256];
                av_strerror(ret, errbuf, sizeof(errbuf));
                LOG(LL_ERR, "FFmpegEncoder::SendVideoFrame - Failed to push frame into filter graph: ", errbuf);
                av_frame_free(&inputFrame);
                POST();
                return E_FAIL;
            }

            while (true) {
                AVFrame* filteredFrame = av_frame_alloc();
                if (!filteredFrame) {
                    LOG(LL_ERR, "FFmpegEncoder::SendVideoFrame - Failed to allocate filtered frame");
                    av_frame_free(&inputFrame);
                    POST();
                    return E_FAIL;
                }

                ret = av_buffersink_get_frame(videoBufferSinkCtx_, filteredFrame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    av_frame_free(&filteredFrame);
                    break;
                }
                if (ret < 0) {
                    char errbuf[256];
                    av_strerror(ret, errbuf, sizeof(errbuf));
                    LOG(LL_ERR, "FFmpegEncoder::SendVideoFrame - Failed to get frame from filter graph: ", errbuf);
                    av_frame_free(&filteredFrame);
                    av_frame_free(&inputFrame);
                    POST();
                    return E_FAIL;
                }

                HRESULT hr = EncodeVideoFrame(filteredFrame);
                av_frame_free(&filteredFrame);

                if (FAILED(hr)) {
                    LOG(LL_ERR, "FFmpegEncoder::SendVideoFrame - Failed to encode filtered frame");
                    av_frame_free(&inputFrame);
                    POST();
                    return hr;
                }
            }

            av_frame_free(&inputFrame);
        } else {
            AVFrame* frameToEncode = inputFrame;
            AVFrame* convertedFrame = nullptr;

            if (inputPixelFormat != videoCodecContext_->pix_fmt) {
                LOG(LL_TRC, "FFmpegEncoder::SendVideoFrame - Pixel format conversion required");

                if (!swsContext_) {
                    LOG(LL_DBG, "FFmpegEncoder::SendVideoFrame - Creating SWS context for pixel format conversion");
                    swsContext_ = sws_getContext(
                        frame.width, frame.height, inputPixelFormat,
                        videoCodecContext_->width, videoCodecContext_->height, videoCodecContext_->pix_fmt,
                        swsFlags_, nullptr, nullptr, nullptr);

                    if (!swsContext_) {
                        LOG(LL_ERR, "FFmpegEncoder::SendVideoFrame - Failed to create SWS context");
                        av_frame_free(&inputFrame);
                        POST();
                        return E_FAIL;
                    }
                }

                convertedFrame = av_frame_alloc();
                if (!convertedFrame) {
                    LOG(LL_ERR, "FFmpegEncoder::SendVideoFrame - Failed to allocate converted frame");
                    av_frame_free(&inputFrame);
                    POST();
                    return E_FAIL;
                }

                convertedFrame->width = videoCodecContext_->width;
                convertedFrame->height = videoCodecContext_->height;
                convertedFrame->format = videoCodecContext_->pix_fmt;
                convertedFrame->pts = inputFrame->pts;

                int ret = av_frame_get_buffer(convertedFrame, 0);
                if (ret < 0) {
                    LOG(LL_ERR, "FFmpegEncoder::SendVideoFrame - Failed to allocate converted frame buffer, error code: ", ret);
                    av_frame_free(&inputFrame);
                    av_frame_free(&convertedFrame);
                    POST();
                    return E_FAIL;
                }

                ret = sws_scale(swsContext_, inputFrame->data, inputFrame->linesize, 0, frame.height,
                               convertedFrame->data, convertedFrame->linesize);

                if (ret < 0) {
                    LOG(LL_ERR, "FFmpegEncoder::SendVideoFrame - Pixel format conversion failed, error code: ", ret);
                    av_frame_free(&inputFrame);
                    av_frame_free(&convertedFrame);
                    POST();
                    return E_FAIL;
                }

                LOG(LL_TRC, "FFmpegEncoder::SendVideoFrame - Pixel format conversion completed");
                frameToEncode = convertedFrame;
            }

            HRESULT hr = EncodeVideoFrame(frameToEncode);

            av_frame_free(&inputFrame);
            if (convertedFrame) {
                av_frame_free(&convertedFrame);
            }

            if (FAILED(hr)) {
                LOG(LL_ERR, "FFmpegEncoder::SendVideoFrame - Failed to encode video frame");
                POST();
                return hr;
            }
        }

        LOG(LL_TRC, "FFmpegEncoder::SendVideoFrame - Frame encoded successfully");

        POST();
        return S_OK;
    }

    HRESULT FFmpegEncoder::SendAudioSampleChunk(const FFmpeg::FFAUDIOCHUNK& chunk) {
        PRE();
        LOG(LL_TRC, "FFmpegEncoder::SendAudioSampleChunk called - Samples: ", chunk.samples, ", PTS: ", audioPts_);
        
        std::lock_guard<std::mutex> lock(encoderMutex_);
        
        if (!isOpen_ || !audioCodecContext_) {
            LOG(LL_ERR, "FFmpegEncoder::SendAudioSampleChunk - Encoder not open or audio not enabled");
            POST();
            return E_FAIL;
        }
        
        LOG(LL_TRC, "FFmpegEncoder::SendAudioSampleChunk - Block size: ", chunk.blockSize, ", planes: ", chunk.planes, ", rate: ", chunk.sampleRate);
        
        if (!packet_) {
            packet_ = av_packet_alloc();
            if (!packet_) {
                LOG(LL_ERR, "FFmpegEncoder::SendAudioSampleChunk - Failed to allocate packet");
                POST();
                return E_FAIL;
            }
        }
        
        AVFrame* inputFrame = av_frame_alloc();
        if (!inputFrame) {
            LOG(LL_ERR, "FFmpegEncoder::SendAudioSampleChunk - Failed to allocate input frame");
            POST();
            return E_FAIL;
        }
        
        inputFrame->nb_samples = chunk.samples;
        inputFrame->sample_rate = chunk.sampleRate;
        inputFrame->pts = audioPts_;
        
        AVSampleFormat inputSampleFormat = av_get_sample_fmt(chunk.format);
        if (inputSampleFormat == AV_SAMPLE_FMT_NONE) {
            inputSampleFormat = AV_SAMPLE_FMT_S16;
            LOG(LL_WRN, "FFmpegEncoder::SendAudioSampleChunk - Unknown sample format: ", chunk.format, ", using s16");
        }
        
        inputFrame->format = inputSampleFormat;
        
#if LIBAVUTIL_VERSION_MAJOR >= 57
        switch (chunk.layout) {
            case FFmpeg::ChannelLayout::Mono:
                av_channel_layout_default(&inputFrame->ch_layout, 1);
                break;
            case FFmpeg::ChannelLayout::Stereo:
                av_channel_layout_default(&inputFrame->ch_layout, 2);
                break;
            case FFmpeg::ChannelLayout::FivePointOne:
                av_channel_layout_default(&inputFrame->ch_layout, 6);
                break;
        }
#else
        switch (chunk.layout) {
            case FFmpeg::ChannelLayout::Mono:
                inputFrame->channel_layout = AV_CH_LAYOUT_MONO;
                inputFrame->channels = 1;
                break;
            case FFmpeg::ChannelLayout::Stereo:
                inputFrame->channel_layout = AV_CH_LAYOUT_STEREO;
                inputFrame->channels = 2;
                break;
            case FFmpeg::ChannelLayout::FivePointOne:
                inputFrame->channel_layout = AV_CH_LAYOUT_5POINT1;
                inputFrame->channels = 6;
                break;
        }
#endif
        
        LOG(LL_TRC, "FFmpegEncoder::SendAudioSampleChunk - Input sample format: ", av_get_sample_fmt_name(inputSampleFormat));
        LOG(LL_TRC, "FFmpegEncoder::SendAudioSampleChunk - Output sample format: ", av_get_sample_fmt_name(audioCodecContext_->sample_fmt));
        
        int channels = 0;
    #if LIBAVUTIL_VERSION_MAJOR >= 57
        channels = inputFrame->ch_layout.nb_channels;
    #else
        channels = inputFrame->channels;
    #endif

        const int expectedBytes = chunk.samples * chunk.blockSize;
        int fillRet = avcodec_fill_audio_frame(
            inputFrame,
            channels,
            inputSampleFormat,
            chunk.buffer[0],
            expectedBytes,
            1);
        if (fillRet < 0) {
            LOG(LL_ERR, "FFmpegEncoder::SendAudioSampleChunk - avcodec_fill_audio_frame failed, error code: ", fillRet);
            av_frame_free(&inputFrame);
            POST();
            return E_FAIL;
        }
        
        AVFrame* frameToEncode = inputFrame;
        AVFrame* convertedFrame = nullptr;

        bool needsConversion = (inputSampleFormat != audioCodecContext_->sample_fmt);
        int producedSamples = 0;

        if (strlen(config_.audio.filters) > 0 && !audioFilterGraph_) {
            LOG(LL_DBG, "FFmpegEncoder::SendAudioSampleChunk - Initializing audio filter graph");
            HRESULT fghr = InitializeAudioFilterGraph(
                static_cast<int>(inputSampleFormat),
                chunk.sampleRate,
                channels);
            if (FAILED(fghr)) {
                LOG(LL_WRN, "FFmpegEncoder::SendAudioSampleChunk - Audio filter graph init failed, falling back to SWR path");
            }
        }

        if (audioFilterGraph_) {
            LOG(LL_TRC, "FFmpegEncoder::SendAudioSampleChunk - Using audio filter graph path");

            int ret = av_buffersrc_add_frame_flags(audioBufferSrcCtx_, inputFrame, AV_BUFFERSRC_FLAG_KEEP_REF);
            if (ret < 0) {
                char errbuf[256];
                av_strerror(ret, errbuf, sizeof(errbuf));
                LOG(LL_ERR, "FFmpegEncoder::SendAudioSampleChunk - Failed to push frame into audio filter graph: ", errbuf);
                av_frame_free(&inputFrame);
                POST();
                return E_FAIL;
            }

            AVFrame* filteredFrame = av_frame_alloc();
            while (filteredFrame) {
                ret = av_buffersink_get_frame(audioBufferSinkCtx_, filteredFrame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }
                if (ret < 0) {
                    char errbuf[256];
                    av_strerror(ret, errbuf, sizeof(errbuf));
                    LOG(LL_ERR, "FFmpegEncoder::SendAudioSampleChunk - Failed to get frame from audio filter graph: ", errbuf);
                    av_frame_free(&filteredFrame);
                    av_frame_free(&inputFrame);
                    POST();
                    return E_FAIL;
                }

                int fgSamples = filteredFrame->nb_samples;
                producedSamples += fgSamples;

                int fifoRealloc = av_audio_fifo_realloc(audioFifo_, av_audio_fifo_size(audioFifo_) + fgSamples);
                if (fifoRealloc < 0) {
                    LOG(LL_ERR, "FFmpegEncoder::SendAudioSampleChunk - Failed to realloc audio FIFO (filter path), error code: ", fifoRealloc);
                    av_frame_free(&filteredFrame);
                    av_frame_free(&inputFrame);
                    POST();
                    return E_FAIL;
                }

                int fifoWrite = av_audio_fifo_write(audioFifo_, (void**)filteredFrame->data, fgSamples);
                if (fifoWrite < fgSamples) {
                    LOG(LL_ERR, "FFmpegEncoder::SendAudioSampleChunk - Failed to write filtered samples to FIFO, wrote: ", fifoWrite);
                    av_frame_free(&filteredFrame);
                    av_frame_free(&inputFrame);
                    POST();
                    return E_FAIL;
                }

                av_frame_unref(filteredFrame);
            }
            av_frame_free(&filteredFrame);
        } else {
            if (needsConversion) {
                LOG(LL_TRC, "FFmpegEncoder::SendAudioSampleChunk - Sample format conversion required");

                if (!swrContext_) {
                    LOG(LL_DBG, "FFmpegEncoder::SendAudioSampleChunk - Creating SWR context for sample format conversion");

#if LIBAVUTIL_VERSION_MAJOR >= 57
                    int ret = swr_alloc_set_opts2(&swrContext_,
                        &audioCodecContext_->ch_layout, audioCodecContext_->sample_fmt, audioCodecContext_->sample_rate,
                        &inputFrame->ch_layout, inputSampleFormat, chunk.sampleRate,
                        0, nullptr);
#else
                    swrContext_ = swr_alloc_set_opts(nullptr,
                        audioCodecContext_->channel_layout, audioCodecContext_->sample_fmt, audioCodecContext_->sample_rate,
                        inputFrame->channel_layout, inputSampleFormat, chunk.sampleRate,
                        0, nullptr);
                    int ret = swrContext_ ? 0 : -1;
#endif

                    if (ret < 0 || !swrContext_) {
                        LOG(LL_ERR, "FFmpegEncoder::SendAudioSampleChunk - Failed to allocate SWR context");
                        av_frame_free(&inputFrame);
                        POST();
                        return E_FAIL;
                    }

                    ret = swr_init(swrContext_);
                    if (ret < 0) {
                        LOG(LL_ERR, "FFmpegEncoder::SendAudioSampleChunk - Failed to initialize SWR context, error code: ", ret);
                        av_frame_free(&inputFrame);
                        POST();
                        return E_FAIL;
                    }
                }

                convertedFrame = av_frame_alloc();
                if (!convertedFrame) {
                    LOG(LL_ERR, "FFmpegEncoder::SendAudioSampleChunk - Failed to allocate converted frame");
                    av_frame_free(&inputFrame);
                    POST();
                    return E_FAIL;
                }

                convertedFrame->nb_samples = chunk.samples;
                convertedFrame->sample_rate = audioCodecContext_->sample_rate;
                convertedFrame->format = audioCodecContext_->sample_fmt;
                convertedFrame->pts = inputFrame->pts;

#if LIBAVUTIL_VERSION_MAJOR >= 57
                av_channel_layout_copy(&convertedFrame->ch_layout, &audioCodecContext_->ch_layout);
#else
                convertedFrame->channel_layout = audioCodecContext_->channel_layout;
                convertedFrame->channels = audioCodecContext_->channels;
#endif

                int ret = av_frame_get_buffer(convertedFrame, 0);
                if (ret < 0) {
                    LOG(LL_ERR, "FFmpegEncoder::SendAudioSampleChunk - Failed to allocate converted frame buffer, error code: ", ret);
                    av_frame_free(&inputFrame);
                    av_frame_free(&convertedFrame);
                    POST();
                    return E_FAIL;
                }

                ret = swr_convert(swrContext_, convertedFrame->data, chunk.samples,
                                 (const uint8_t**)inputFrame->data, chunk.samples);

                if (ret < 0) {
                    LOG(LL_ERR, "FFmpegEncoder::SendAudioSampleChunk - Sample format conversion failed, error code: ", ret);
                    av_frame_free(&inputFrame);
                    av_frame_free(&convertedFrame);
                    POST();
                    return E_FAIL;
                }

                LOG(LL_TRC, "FFmpegEncoder::SendAudioSampleChunk - Sample format conversion completed, converted samples: ", ret);
                convertedFrame->nb_samples = ret;
                frameToEncode = convertedFrame;
            }

            producedSamples = frameToEncode->nb_samples;

            int fifoRealloc = av_audio_fifo_realloc(audioFifo_, av_audio_fifo_size(audioFifo_) + producedSamples);
            if (fifoRealloc < 0) {
                LOG(LL_ERR, "FFmpegEncoder::SendAudioSampleChunk - Failed to realloc audio FIFO, error code: ", fifoRealloc);
                av_frame_free(&inputFrame);
                if (convertedFrame) av_frame_free(&convertedFrame);
                POST();
                return E_FAIL;
            }

            int fifoWrite = av_audio_fifo_write(audioFifo_, (void**)frameToEncode->data, producedSamples);
            if (fifoWrite < producedSamples) {
                LOG(LL_ERR, "FFmpegEncoder::SendAudioSampleChunk - Failed to write samples into FIFO, wrote: ", fifoWrite);
                av_frame_free(&inputFrame);
                if (convertedFrame) av_frame_free(&convertedFrame);
                POST();
                return E_FAIL;
            }
        }

        int targetFrameSize = audioCodecContext_->frame_size > 0 ? audioCodecContext_->frame_size : producedSamples;
        while (audioFifo_ && av_audio_fifo_size(audioFifo_) >= targetFrameSize && targetFrameSize > 0) {
            if (targetFrameSize > audioFrame_->nb_samples) {
                av_frame_unref(audioFrame_);
                audioFrame_->format = audioCodecContext_->sample_fmt;
                audioFrame_->sample_rate = audioCodecContext_->sample_rate;
#if LIBAVUTIL_VERSION_MAJOR >= 57
                av_channel_layout_copy(&audioFrame_->ch_layout, &audioCodecContext_->ch_layout);
#else
                audioFrame_->channel_layout = audioCodecContext_->channel_layout;
                audioFrame_->channels = audioCodecContext_->channels;
#endif
                audioFrame_->nb_samples = targetFrameSize;
                int bufRet = av_frame_get_buffer(audioFrame_, 0);
                if (bufRet < 0) {
                    LOG(LL_ERR, "FFmpegEncoder::SendAudioSampleChunk - Failed to allocate audio frame buffer, error code: ", bufRet);
                    av_frame_free(&inputFrame);
                    if (convertedFrame) av_frame_free(&convertedFrame);
                    POST();
                    return E_FAIL;
                }
            }

            int writable = av_frame_make_writable(audioFrame_);
            if (writable < 0) {
                LOG(LL_ERR, "FFmpegEncoder::SendAudioSampleChunk - Frame not writable, error code: ", writable);
                av_frame_free(&inputFrame);
                if (convertedFrame) av_frame_free(&convertedFrame);
                POST();
                return E_FAIL;
            }

            int readRet = av_audio_fifo_read(audioFifo_, (void**)audioFrame_->data, targetFrameSize);
            if (readRet < targetFrameSize) {
                LOG(LL_ERR, "FFmpegEncoder::SendAudioSampleChunk - Failed to read from FIFO, read: ", readRet);
                av_frame_free(&inputFrame);
                if (convertedFrame) av_frame_free(&convertedFrame);
                POST();
                return E_FAIL;
            }

            audioFrame_->pts = audioPts_;
            audioPts_ += targetFrameSize;

            HRESULT hr = EncodeAudioFrame(audioFrame_);
            if (FAILED(hr)) {
                LOG(LL_ERR, "FFmpegEncoder::SendAudioSampleChunk - Failed to encode audio frame");
                av_frame_free(&inputFrame);
                if (convertedFrame) av_frame_free(&convertedFrame);
                POST();
                return hr;
            }
        }

        av_frame_free(&inputFrame);
        if (convertedFrame) {
            av_frame_free(&convertedFrame);
        }

        LOG(LL_TRC, "FFmpegEncoder::SendAudioSampleChunk - Audio chunk accepted into FIFO");

        POST();
        return S_OK;
    }

    HRESULT FFmpegEncoder::EncodeVideoFrame(AVFrame* frame) {
        PRE();
        LOG(LL_TRC, "FFmpegEncoder::EncodeVideoFrame - Sending frame to encoder");
        
        int ret = avcodec_send_frame(videoCodecContext_, frame);
        if (ret < 0) {
            LOG(LL_ERR, "FFmpegEncoder::EncodeVideoFrame - Failed to send frame to encoder, error code: ", ret);
            POST();
            return E_FAIL;
        }
        
        while (ret >= 0) {
            av_packet_unref(packet_);
            ret = avcodec_receive_packet(videoCodecContext_, packet_);
            
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                LOG(LL_TRC, "FFmpegEncoder::EncodeVideoFrame - No more packets available (EAGAIN/EOF)");
                break;
            } else if (ret < 0) {
                LOG(LL_ERR, "FFmpegEncoder::EncodeVideoFrame - Failed to receive packet from encoder, error code: ", ret);
                POST();
                return E_FAIL;
            }
            
            LOG(LL_TRC, "FFmpegEncoder::EncodeVideoFrame - Received packet, size: ", packet_->size, " bytes");
            
            HRESULT hr = WritePacket(packet_, videoStream_);
            if (FAILED(hr)) {
                LOG(LL_ERR, "FFmpegEncoder::EncodeVideoFrame - Failed to write packet");
                POST();
                return hr;
            }
        }
        
        POST();
        return S_OK;
    }

    HRESULT FFmpegEncoder::EncodeAudioFrame(AVFrame* frame) {
        PRE();
        LOG(LL_TRC, "FFmpegEncoder::EncodeAudioFrame - Sending frame to encoder");
        
        int ret = avcodec_send_frame(audioCodecContext_, frame);
        if (ret < 0) {
            LOG(LL_ERR, "FFmpegEncoder::EncodeAudioFrame - Failed to send frame to encoder, error code: ", ret);
            POST();
            return E_FAIL;
        }
        
        while (ret >= 0) {
            av_packet_unref(packet_);
            ret = avcodec_receive_packet(audioCodecContext_, packet_);
            
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                LOG(LL_TRC, "FFmpegEncoder::EncodeAudioFrame - No more packets available (EAGAIN/EOF)");
                break;
            } else if (ret < 0) {
                LOG(LL_ERR, "FFmpegEncoder::EncodeAudioFrame - Failed to receive packet from encoder, error code: ", ret);
                POST();
                return E_FAIL;
            }
            
            LOG(LL_TRC, "FFmpegEncoder::EncodeAudioFrame - Received packet, size: ", packet_->size, " bytes");
            
            HRESULT hr = WritePacket(packet_, audioStream_);
            if (FAILED(hr)) {
                LOG(LL_ERR, "FFmpegEncoder::EncodeAudioFrame - Failed to write packet");
                POST();
                return hr;
            }
        }
        
        POST();
        return S_OK;
    }

    HRESULT FFmpegEncoder::WritePacket(AVPacket* pkt, AVStream* stream) {
        PRE();
        LOG(LL_TRC, "FFmpegEncoder::WritePacket - Writing packet to stream ", stream->index);
        
        AVCodecContext* codecCtx = (stream->index == videoStream_->index) ? videoCodecContext_ : audioCodecContext_;
        av_packet_rescale_ts(pkt, codecCtx->time_base, stream->time_base);
        pkt->stream_index = stream->index;
        
        LOG(LL_TRC, "FFmpegEncoder::WritePacket - PTS: ", pkt->pts, ", DTS: ", pkt->dts, ", duration: ", pkt->duration);
        
        int ret = av_interleaved_write_frame(formatContext_, pkt);
        if (ret < 0) {
            LOG(LL_ERR, "FFmpegEncoder::WritePacket - Failed to write packet, error code: ", ret);
            POST();
            return E_FAIL;
        }
        
        LOG(LL_TRC, "FFmpegEncoder::WritePacket - Packet written successfully");
        
        POST();
        return S_OK;
    }

    HRESULT FFmpegEncoder::Close(BOOL finalize) {
        PRE();
        LOG(LL_NFO, "FFmpegEncoder::Close called, finalize: ", finalize ? "true" : "false");
        
        std::lock_guard<std::mutex> lock(encoderMutex_);
        
        if (!isOpen_) {
            LOG(LL_WRN, "FFmpegEncoder::Close - Encoder not open");
            POST();
            return S_OK;
        }
        
        if (finalize) {
            if (videoCodecContext_) {
                LOG(LL_DBG, "FFmpegEncoder::Close - Flushing video encoder");

                if (videoFilterGraph_ && videoBufferSrcCtx_) {
                    LOG(LL_DBG, "FFmpegEncoder::Close - Draining video filter graph");
                    av_buffersrc_add_frame_flags(videoBufferSrcCtx_, nullptr, 0);

                    AVFrame* filteredFrame = av_frame_alloc();
                    while (filteredFrame) {
                        int fgRet = av_buffersink_get_frame(videoBufferSinkCtx_, filteredFrame);
                        if (fgRet == AVERROR_EOF || fgRet == AVERROR(EAGAIN)) {
                            break;
                        }
                        if (fgRet < 0) {
                            break;
                        }
                        EncodeVideoFrame(filteredFrame);
                        av_frame_unref(filteredFrame);
                    }
                    av_frame_free(&filteredFrame);
                    LOG(LL_DBG, "FFmpegEncoder::Close - Video filter graph drained");
                }

                avcodec_send_frame(videoCodecContext_, nullptr);
                
                while (true) {
                    if (!packet_) {
                        packet_ = av_packet_alloc();
                    }
                    av_packet_unref(packet_);
                    
                    int ret = avcodec_receive_packet(videoCodecContext_, packet_);
                    if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
                        break;
                    } else if (ret < 0) {
                        LOG(LL_ERR, "FFmpegEncoder::Close - Error flushing video encoder, error code: ", ret);
                        break;
                    }
                    
                    WritePacket(packet_, videoStream_);
                }
                LOG(LL_DBG, "FFmpegEncoder::Close - Video encoder flushed");
            }
            
            if (audioCodecContext_) {
                if (audioFilterGraph_ && audioBufferSrcCtx_) {
                    LOG(LL_DBG, "FFmpegEncoder::Close - Draining audio filter graph");
                    av_buffersrc_add_frame_flags(audioBufferSrcCtx_, nullptr, 0);

                    AVFrame* filteredFrame = av_frame_alloc();
                    while (filteredFrame) {
                        int fgRet = av_buffersink_get_frame(audioBufferSinkCtx_, filteredFrame);
                        if (fgRet == AVERROR_EOF || fgRet == AVERROR(EAGAIN)) {
                            break;
                        }
                        if (fgRet < 0) {
                            break;
                        }
                        int fgSamples = filteredFrame->nb_samples;
                        if (audioFifo_) {
                            av_audio_fifo_realloc(audioFifo_, av_audio_fifo_size(audioFifo_) + fgSamples);
                            av_audio_fifo_write(audioFifo_, (void**)filteredFrame->data, fgSamples);
                        }
                        av_frame_unref(filteredFrame);
                    }
                    av_frame_free(&filteredFrame);
                    LOG(LL_DBG, "FFmpegEncoder::Close - Audio filter graph drained");
                }

                if (audioFifo_ && av_audio_fifo_size(audioFifo_) > 0) {
                    int frameSize = audioCodecContext_->frame_size > 0 ? audioCodecContext_->frame_size : av_audio_fifo_size(audioFifo_);
                    while (av_audio_fifo_size(audioFifo_) > 0) {
                        int sendSamples = frameSize > 0 ? (std::min)(frameSize, av_audio_fifo_size(audioFifo_)) : av_audio_fifo_size(audioFifo_);
                        if (sendSamples <= 0) break;

                        if (sendSamples > audioFrame_->nb_samples) {
                            av_frame_unref(audioFrame_);
                            audioFrame_->format = audioCodecContext_->sample_fmt;
                            audioFrame_->sample_rate = audioCodecContext_->sample_rate;
#if LIBAVUTIL_VERSION_MAJOR >= 57
                            av_channel_layout_copy(&audioFrame_->ch_layout, &audioCodecContext_->ch_layout);
#else
                            audioFrame_->channel_layout = audioCodecContext_->channel_layout;
                            audioFrame_->channels = audioCodecContext_->channels;
#endif
                            audioFrame_->nb_samples = sendSamples;
                            int bufRet = av_frame_get_buffer(audioFrame_, 0);
                            if (bufRet < 0) {
                                LOG(LL_ERR, "FFmpegEncoder::Close - Failed to allocate audio frame buffer during drain, error code: ", bufRet);
                                break;
                            }
                        }

                        if (av_frame_make_writable(audioFrame_) < 0) {
                            LOG(LL_ERR, "FFmpegEncoder::Close - Audio frame not writable during drain");
                            break;
                        }

                        int readRet = av_audio_fifo_read(audioFifo_, (void**)audioFrame_->data, sendSamples);
                        if (readRet < sendSamples) {
                            LOG(LL_ERR, "FFmpegEncoder::Close - Failed to read from FIFO during drain, read: ", readRet);
                            break;
                        }

                        audioFrame_->pts = audioPts_;
                        audioPts_ += sendSamples;
                        audioFrame_->nb_samples = sendSamples;

                        if (FAILED(EncodeAudioFrame(audioFrame_))) {
                            LOG(LL_ERR, "FFmpegEncoder::Close - Failed to encode drained audio frame");
                            break;
                        }
                    }
                }

                LOG(LL_DBG, "FFmpegEncoder::Close - Flushing audio encoder");
                avcodec_send_frame(audioCodecContext_, nullptr);
                
                while (true) {
                    if (!packet_) {
                        packet_ = av_packet_alloc();
                    }
                    av_packet_unref(packet_);
                    
                    int ret = avcodec_receive_packet(audioCodecContext_, packet_);
                    if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
                        break;
                    } else if (ret < 0) {
                        LOG(LL_ERR, "FFmpegEncoder::Close - Error flushing audio encoder, error code: ", ret);
                        break;
                    }
                    
                    WritePacket(packet_, audioStream_);
                }
                LOG(LL_DBG, "FFmpegEncoder::Close - Audio encoder flushed");
            }
            
            if (formatContext_) {
                LOG(LL_DBG, "FFmpegEncoder::Close - Writing file trailer");
                int ret = av_write_trailer(formatContext_);
                if (ret < 0) {
                    LOG(LL_ERR, "FFmpegEncoder::Close - Failed to write file trailer, error code: ", ret);
                } else {
                    LOG(LL_DBG, "FFmpegEncoder::Close - File trailer written successfully");
                }
            }
        } else {
            LOG(LL_NFO, "FFmpegEncoder::Close - Aborting encoding (finalize=false)");
        }
        
        Cleanup();
        isOpen_ = false;
        
        LOG(LL_NFO, "FFmpegEncoder::Close - Encoder closed successfully");
        
        POST();
        return S_OK;
    }

    HRESULT FFmpegEncoder::GetConfig(FFmpeg::FFENCODERCONFIG& config) {
        PRE();
        LOG(LL_DBG, "FFmpegEncoder::GetConfig called");
        
        std::lock_guard<std::mutex> lock(encoderMutex_);
        
        if (!configSet_) {
            LOG(LL_WRN, "FFmpegEncoder::GetConfig - Configuration not set");
            POST();
            return E_FAIL;
        }
        
        config = config_;
        
        POST();
        return S_OK;
    }

    void FFmpegEncoder::Cleanup() {
        PRE();
        LOG(LL_DBG, "FFmpegEncoder::Cleanup - Cleaning up resources");
        
        if (videoFrame_) {
            av_frame_free(&videoFrame_);
            LOG(LL_DBG, "FFmpegEncoder::Cleanup - Video frame freed");
        }
        
        if (audioFrame_) {
            av_frame_free(&audioFrame_);
            LOG(LL_DBG, "FFmpegEncoder::Cleanup - Audio frame freed");
        }

        if (audioFifo_) {
            av_audio_fifo_free(audioFifo_);
            audioFifo_ = nullptr;
            LOG(LL_DBG, "FFmpegEncoder::Cleanup - Audio FIFO freed");
        }
        
        if (packet_) {
            av_packet_free(&packet_);
            LOG(LL_DBG, "FFmpegEncoder::Cleanup - Packet freed");
        }
        
        if (videoCodecContext_) {
            avcodec_free_context(&videoCodecContext_);
            LOG(LL_DBG, "FFmpegEncoder::Cleanup - Video codec context freed");
        }
        
        if (audioCodecContext_) {
            avcodec_free_context(&audioCodecContext_);
            LOG(LL_DBG, "FFmpegEncoder::Cleanup - Audio codec context freed");
        }
        
        if (swsContext_) {
            sws_freeContext(swsContext_);
            swsContext_ = nullptr;
            LOG(LL_DBG, "FFmpegEncoder::Cleanup - SWS context freed");
        }
        
        if (swrContext_) {
            swr_free(&swrContext_);
            LOG(LL_DBG, "FFmpegEncoder::Cleanup - SWR context freed");
        }

        if (videoFilterGraph_) {
            avfilter_graph_free(&videoFilterGraph_);
            videoFilterGraph_ = nullptr;
            videoBufferSrcCtx_ = nullptr;
            videoBufferSinkCtx_ = nullptr;
            LOG(LL_DBG, "FFmpegEncoder::Cleanup - Video filter graph freed");
        }

        if (audioFilterGraph_) {
            avfilter_graph_free(&audioFilterGraph_);
            audioFilterGraph_ = nullptr;
            audioBufferSrcCtx_ = nullptr;
            audioBufferSinkCtx_ = nullptr;
            LOG(LL_DBG, "FFmpegEncoder::Cleanup - Audio filter graph freed");
        }
        
        if (formatContext_) {
            if (formatContext_->pb && !(formatContext_->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&formatContext_->pb);
                LOG(LL_DBG, "FFmpegEncoder::Cleanup - Output file closed");
            }
            avformat_free_context(formatContext_);
            formatContext_ = nullptr;
            LOG(LL_DBG, "FFmpegEncoder::Cleanup - Format context freed");
        }
        
        videoStream_ = nullptr;
        audioStream_ = nullptr;
        
        LOG(LL_DBG, "FFmpegEncoder::Cleanup - Cleanup complete");
        
        POST();
    }
}
