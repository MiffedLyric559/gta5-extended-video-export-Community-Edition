#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 26812)

#include "EncoderSession.h"
#include "logger.h"
#include "util.h"

#include <filesystem>

#pragma warning(pop)

namespace Encoder {
    EncoderSession::EncoderSession() 
        : videoFrameQueue_(128), 
        exrImageQueue_(16) {
        PRE();
        LOG(LL_NFO, "Opening encoding session: ", reinterpret_cast<uint64_t>(this));
        
        LOG(LL_DBG, "EncoderSession: Initializing FFmpeg encoder");
        ffmpegEncoder_ = std::make_unique<FFmpegEncoder>();
        
        videoFrame_.buffer = nullptr;
        videoFrame_.rowsize = nullptr;
        videoFrame_.planes = 0;
        videoFrame_.width = 0;
        videoFrame_.height = 0;
        videoFrame_.pass = 0;
        memset(videoFrame_.format, 0, sizeof(videoFrame_.format));
        memset(videoFrame_.colorRange, 0, sizeof(videoFrame_.colorRange));
        memset(videoFrame_.colorSpace, 0, sizeof(videoFrame_.colorSpace));
        memset(videoFrame_.colorPrimaries, 0, sizeof(videoFrame_.colorPrimaries));
        memset(videoFrame_.colorTrc, 0, sizeof(videoFrame_.colorTrc));

        audioChunk_.buffer = nullptr;
        audioChunk_.samples = 0;
        audioChunk_.blockSize = 0;
        audioChunk_.planes = 0;
        audioChunk_.sampleRate = 0;
        audioChunk_.layout = FFmpeg::ChannelLayout::Stereo;
        memset(audioChunk_.format, 0, sizeof(audioChunk_.format));
        
        LOG(LL_DBG, "EncoderSession: Initialization complete");
        POST();
    }

    EncoderSession::~EncoderSession() {
        PRE();
        LOG(LL_NFO, "Closing encoding session: ", reinterpret_cast<uint64_t>(this));
        
        isCapturing = false;
        LOG_CALL(LL_DBG, finishVideo());
        LOG_CALL(LL_DBG, finishAudio());
        LOG_CALL(LL_DBG, endSession());
        isBeingDeleted_ = true;
        
        POST();
    }

    HRESULT EncoderSession::createContext(const FFmpeg::FFENCODERCONFIG& config, 
                                        const std::wstring& filename, 
                                        uint32_t width,
                                        uint32_t height, 
                                        const std::string& inputPixelFormat, 
                                        uint32_t fpsNumerator, 
                                        uint32_t fpsDenominator,
                                        uint32_t inputChannels, 
                                        uint32_t inputSampleRate, 
                                        const std::string& inputSampleFormat,
                                        uint32_t inputAlign, 
                                        bool exportOpenExr, 
                                        uint32_t openExrWidth,
                                        uint32_t openExrHeight) {
        PRE();

        LOG(LL_DBG, "EncoderSession::createContext - Starting encoder context creation");
        
        width_ = static_cast<int32_t>(width);
        height_ = static_cast<int32_t>(height);

        ASSERT_RUNTIME(filename.length() < 255, 
                    "Filename is too long for FFmpeg encoder");
        ASSERT_RUNTIME(inputChannels == 1 || inputChannels == 2 || inputChannels == 6,
                    "Invalid number of audio channels. Only 1 (mono), 2 (stereo), and 6 (5.1) are supported");

        LOG(LL_DBG, "EncoderSession::createContext - Setting FFmpeg configuration");
        REQUIRE(ffmpegEncoder_->SetConfig(config), "Failed to set FFmpeg configuration");

        FFmpeg::ChannelLayout channelLayout = FFmpeg::ChannelLayout::Stereo;
        switch (inputChannels) {
            case 1:
                channelLayout = FFmpeg::ChannelLayout::Mono;
                break;
            case 2:
                channelLayout = FFmpeg::ChannelLayout::Stereo;
                break;
            case 6:
                channelLayout = FFmpeg::ChannelLayout::FivePointOne;
                break;
        }

        LOG(LL_DBG, "EncoderSession::createContext - Preparing encoder info structure");
        
        FFmpeg::FFENCODERINFO encoderInfo{
            .application = L"Extended Video Export Revived",
            .video{
                .enabled = true,
                .width = static_cast<int>(width),
                .height = static_cast<int>(height),
                .timebase = {static_cast<int>(fpsDenominator), static_cast<int>(fpsNumerator)},
                .aspectratio = {1, 1},
                .fieldorder = FFmpeg::FieldOrder::Progressive,
            },
            .audio{
                .enabled = true,
                .samplerate = static_cast<int>(inputSampleRate),
                .channellayout = channelLayout,
                .numberChannels = static_cast<int>(inputChannels),
            },
        };
        
        filename.copy(encoderInfo.filename, std::size(encoderInfo.filename));

        LOG(LL_DBG, "EncoderSession::createContext - Encoder info prepared");
        LOG(LL_DBG, "EncoderSession::createContext - Video: ", encoderInfo.video.width, "x", encoderInfo.video.height, " @ ", fpsNumerator, "/", fpsDenominator);
        LOG(LL_DBG, "EncoderSession::createContext - Audio: ", encoderInfo.audio.samplerate, "Hz, ", encoderInfo.audio.numberChannels, " channels");

        exportExr_ = exportOpenExr;
        if (exportExr_) {
            LOG(LL_DBG, "EncoderSession::createContext - OpenEXR export enabled");
            std::string exrOutputPath = utf8_encode(filename) + ".OpenEXR";
            exrExporter_.initialize(exrOutputPath, openExrWidth, openExrHeight);
        }

        LOG(LL_NFO, "EncoderSession::createContext - Opening FFmpeg encoder");
        REQUIRE(ffmpegEncoder_->Open(encoderInfo), "Failed to open FFmpeg encoder");
        LOG(LL_NFO, "FFmpeg encoder opened successfully");

        LOG(LL_DBG, "EncoderSession::createContext - Initializing video frame structure");
        videoFrame_ = {
            .buffer = new byte*[1],
            .rowsize = new int[1],
            .planes = 1,
            .width = static_cast<int>(width),
            .height = static_cast<int>(height),
            .pass = 1,
        };
        inputPixelFormat.copy(videoFrame_.format, std::size(videoFrame_.format));

        LOG(LL_DBG, "EncoderSession::createContext - Initializing audio chunk structure");
        audioChunk_ = {
            .buffer = new byte*[1],
            .samples = 0,
            .blockSize = static_cast<int>(inputAlign),
            .planes = 1,
            .sampleRate = static_cast<int>(inputSampleRate),
            .layout = channelLayout,
        };
        inputSampleFormat.copy(audioChunk_.format, std::size(audioChunk_.format));

        audioBlockAlign_ = static_cast<int32_t>(inputAlign);
        inputAudioChannels_ = static_cast<int32_t>(inputChannels);
        inputAudioSampleRate_ = static_cast<int32_t>(inputSampleRate);

        isCapturing = true;

        LOG(LL_NFO, "EncoderSession::createContext - Encoder context creation complete");
        POST();
        return S_OK;
    }

    HRESULT EncoderSession::enqueueExrImage(const Microsoft::WRL::ComPtr<ID3D11DeviceContext>& deviceContext,
                                        const Microsoft::WRL::ComPtr<ID3D11Texture2D>& colorTexture,
                                        const Microsoft::WRL::ComPtr<ID3D11Texture2D>& depthTexture) {
        PRE();

        if (isBeingDeleted_) {
            POST();
            return E_FAIL;
        }

        if (!exportExr_) {
            POST();
            return S_OK;
        }

        REQUIRE(exrExporter_.exportFrame(deviceContext, colorTexture, depthTexture, exrFrameNumber_++),
                "Failed to export OpenEXR frame");

        POST();
        return S_OK;
    }

    HRESULT EncoderSession::enqueueVideoFrame(const D3D11_MAPPED_SUBRESOURCE& subresource) {
        PRE();

        if (isBeingDeleted_) {
            POST();
            return E_FAIL;
        }

        LOG(LL_NFO, "Encoding video frame: ", videoPts_);
        
        REQUIRE(writeVideoFrame(static_cast<BYTE*>(subresource.pData), 
                            static_cast<int32_t>(subresource.DepthPitch),
                            subresource.RowPitch, 
                            videoPts_++),
                "Failed to write video frame");

        POST();
        return S_OK;
    }

    HRESULT EncoderSession::writeVideoFrame(BYTE* data, int32_t length, int rowPitch, LONGLONG presentationTime) {
        PRE();
        
        if (isBeingDeleted_) {
            POST();
            return E_FAIL;
        }

        LOG(LL_TRC, "EncoderSession::writeVideoFrame - Writing video frame, length: ", length, ", rowPitch: ", rowPitch, ", PTS: ", presentationTime);
        
        videoFrame_.buffer[0] = data;
        videoFrame_.rowsize[0] = rowPitch;

        LOG(LL_TRC, "EncoderSession::writeVideoFrame - Sending frame to FFmpeg encoder");
        REQUIRE(ffmpegEncoder_->SendVideoFrame(videoFrame_), "Failed to send video frame to FFmpeg");

        POST();
        return S_OK;
    }

    HRESULT EncoderSession::writeAudioFrame(BYTE* data, int32_t lengthBytes, LONGLONG presentationTime) {
        PRE();

        if (isBeingDeleted_) {
            POST();
            return E_FAIL;
        }

        LOG(LL_TRC, "EncoderSession::writeAudioFrame - Writing audio frame, bytes: ", lengthBytes, ", PTS: ", presentationTime);

        if (audioBlockAlign_ <= 0) {
            LOG(LL_ERR, "EncoderSession::writeAudioFrame - Invalid audio block alignment");
            POST();
            return E_FAIL;
        }

        const int32_t samples = lengthBytes / audioBlockAlign_;
        if (samples <= 0) {
            LOG(LL_ERR, "EncoderSession::writeAudioFrame - Computed non-positive sample count, bytes: ", lengthBytes, " blockAlign: ", audioBlockAlign_);
            POST();
            return E_FAIL;
        }

        audioChunk_.buffer[0] = data;
        audioChunk_.samples = samples;

        LOG(LL_TRC, "EncoderSession::writeAudioFrame - Sending audio chunk to FFmpeg encoder");
        REQUIRE(ffmpegEncoder_->SendAudioSampleChunk(audioChunk_), "Failed to send audio chunk to FFmpeg");

        POST();
        return S_OK;
    }

    HRESULT EncoderSession::finishVideo() {
        PRE();
        std::lock_guard<std::mutex> guard(finishMutex_);

        if (!isVideoFinished_) {
            if (exrEncodingThread_.joinable()) {
                exrImageQueue_.enqueue(ExrQueueItem());
                
                std::unique_lock<std::mutex> lock(exrEncodingThreadMutex_);
                while (!isExrEncodingThreadFinished_) {
                    exrEncodingThreadFinishedCondition_.wait(lock);
                }

                exrEncodingThread_.join();
            }

            if (videoFrame_.buffer != nullptr) {
                delete[] videoFrame_.buffer;
                videoFrame_.buffer = nullptr;
            }
            
            if (videoFrame_.rowsize != nullptr) {
                delete[] videoFrame_.rowsize;
                videoFrame_.rowsize = nullptr;
            }

            isVideoFinished_ = true;
        }

        POST();
        return S_OK;
    }

    HRESULT EncoderSession::finishAudio() {
        PRE();
        std::lock_guard<std::mutex> guard(finishMutex_);

        if (!isAudioFinished_) {
            if (audioChunk_.buffer != nullptr) {
                delete[] audioChunk_.buffer;
                audioChunk_.buffer = nullptr;
            }
            
            isAudioFinished_ = true;
        }

        POST();
        return S_OK;
    }

    HRESULT EncoderSession::endSession() {
        PRE();
        std::lock_guard<std::mutex> lock(endSessionMutex_);

        if (isSessionFinished_ || isBeingDeleted_) {
            POST();
            return S_OK;
        }

        while (true) {
            std::lock_guard<std::mutex> guard(finishMutex_);
            if (isVideoFinished_ && isAudioFinished_) {
                break;
            }
        }

        isCapturing = false;
        LOG(LL_NFO, "Ending encoding session...");

        if (ffmpegEncoder_) {
            LOG(LL_DBG, "EncoderSession::endSession - Closing FFmpeg encoder");
            LOG_IF_FAILED(ffmpegEncoder_->Close(true), "Failed to close FFmpeg encoder");
        } else {
            LOG(LL_DBG, "FFmpeg encoder instance was never created (audio-only mode)");
        }

        isSessionFinished_ = true;
        endSessionCondition_.notify_all();
        
        LOG(LL_NFO, "Encoding session ended successfully");

        POST();
        return S_OK;
    }
}
