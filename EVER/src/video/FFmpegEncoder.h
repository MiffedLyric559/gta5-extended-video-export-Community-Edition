#pragma once

#include "FFmpegTypes.h"
#include "logger.h"

#include <string>
#include <memory>
#include <mutex>
#include <Windows.h>

struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct AVFrame;
struct AVPacket;
struct SwsContext;
struct SwrContext;
struct AVAudioFifo;

namespace Encoder {
    class FFmpegEncoder {
    public:
        FFmpegEncoder();
        ~FFmpegEncoder();

        FFmpegEncoder(const FFmpegEncoder&) = delete;
        FFmpegEncoder& operator=(const FFmpegEncoder&) = delete;

        HRESULT SetConfig(const FFmpeg::FFENCODERCONFIG& config);

        HRESULT Open(const FFmpeg::FFENCODERINFO& info);

        HRESULT SendVideoFrame(const FFmpeg::FFVIDEOFRAME& frame);

        HRESULT SendAudioSampleChunk(const FFmpeg::FFAUDIOCHUNK& chunk);

        HRESULT Close(BOOL finalize);

        HRESULT GetConfig(FFmpeg::FFENCODERCONFIG& config);

        BOOL IsVideoActive() const { return videoCodecContext_ != nullptr; }

        BOOL IsAudioActive() const { return audioCodecContext_ != nullptr; }

    private:
        FFmpeg::FFENCODERCONFIG config_;
        FFmpeg::FFENCODERINFO info_;
        bool configSet_ = false;
        bool isOpen_ = false;

        AVFormatContext* formatContext_ = nullptr;
        AVCodecContext* videoCodecContext_ = nullptr;
        AVCodecContext* audioCodecContext_ = nullptr;
        AVStream* videoStream_ = nullptr;
        AVStream* audioStream_ = nullptr;
        SwsContext* swsContext_ = nullptr;
        SwrContext* swrContext_ = nullptr;
        AVAudioFifo* audioFifo_ = nullptr;

        AVFrame* videoFrame_ = nullptr;
        AVFrame* audioFrame_ = nullptr;
        AVPacket* packet_ = nullptr;

        int64_t videoPts_ = 0;
        int64_t audioPts_ = 0;

        std::mutex encoderMutex_;

        std::wstring outputFilename_;

        HRESULT InitializeVideoEncoder();
        HRESULT InitializeAudioEncoder();
        HRESULT ParseEncoderOptions(const char* optionsString, AVCodecContext* codecContext);
        HRESULT EncodeVideoFrame(AVFrame* frame);
        HRESULT EncodeAudioFrame(AVFrame* frame);
        HRESULT WritePacket(AVPacket* pkt, AVStream* stream);
        void Cleanup();

        std::string GetOptionValue(const std::string& options, const std::string& key, const std::string& defaultValue = "");
        int GetOptionValueInt(const std::string& options, const std::string& key, int defaultValue = 0);
        double GetOptionValueDouble(const std::string& options, const std::string& key, double defaultValue = 0.0);
    };
}
