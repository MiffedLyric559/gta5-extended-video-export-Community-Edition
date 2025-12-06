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
        audioChunk_.layout = ChannelLayout::Stereo;
        memset(audioChunk_.format, 0, sizeof(audioChunk_.format));
        
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

    HRESULT EncoderSession::createContext(const VKENCODERCONFIG& config, 
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

        width_ = static_cast<int32_t>(width);
        height_ = static_cast<int32_t>(height);

        ASSERT_RUNTIME(filename.length() < sizeof(VKENCODERINFO::filename) / sizeof(wchar_t), 
                    "Filename is too long for Voukoder encoder");
        ASSERT_RUNTIME(inputChannels == 1 || inputChannels == 2 || inputChannels == 6,
                    "Invalid number of audio channels. Only 1 (mono), 2 (stereo), and 6 (5.1) are supported");

        Microsoft::WRL::ComPtr<IVoukoder> voukoderInstance = nullptr;
        REQUIRE(CoCreateInstance(CLSID_CoVoukoder, nullptr, CLSCTX_INPROC_SERVER, IID_IVoukoder,
                                reinterpret_cast<void**>(voukoderInstance.GetAddressOf())),
                "Failed to create Voukoder COM instance");

        REQUIRE(voukoderInstance->SetConfig(config), "Failed to set Voukoder configuration");

        ChannelLayout channelLayout = ChannelLayout::Stereo;
        switch (inputChannels) {
            case 1:
                channelLayout = ChannelLayout::Mono;
                break;
            case 2:
                channelLayout = ChannelLayout::Stereo;
                break;
            case 6:
                channelLayout = ChannelLayout::FivePointOne;
                break;
        }

        VKENCODERINFO encoderInfo{
            .application = L"Extended Video Export Revived",
            .video{
                .enabled = true,
                .width = static_cast<int>(width),
                .height = static_cast<int>(height),
                .timebase = {static_cast<int>(fpsDenominator), static_cast<int>(fpsNumerator)},
                .aspectratio = {1, 1},
                .fieldorder = FieldOrder::Progressive,
            },
            .audio{
                .enabled = true,
                .samplerate = static_cast<int>(inputSampleRate),
                .channellayout = channelLayout,
                .numberChannels = static_cast<int>(inputChannels),
            },
        };
        
        filename.copy(encoderInfo.filename, std::size(encoderInfo.filename));

        exportExr_ = exportOpenExr;
        if (exportExr_) {
            std::string exrOutputPath = utf8_encode(filename) + ".OpenEXR";
            exrExporter_.initialize(exrOutputPath, openExrWidth, openExrHeight);
        }

        REQUIRE(voukoderInstance->Open(encoderInfo), "Failed to open Voukoder encoder");
        LOG(LL_NFO, "Voukoder encoder opened successfully");

        videoFrame_ = {
            .buffer = new byte*[1],
            .rowsize = new int[1],
            .planes = 1,
            .width = static_cast<int>(width),
            .height = static_cast<int>(height),
            .pass = 1,
        };
        inputPixelFormat.copy(videoFrame_.format, std::size(videoFrame_.format));

        audioChunk_ = {
            .buffer = new byte*[1],
            .samples = 0,
            .blockSize = static_cast<int>(inputAlign),
            .planes = 1,
            .sampleRate = static_cast<int>(inputSampleRate),
            .layout = channelLayout,
        };
        inputSampleFormat.copy(audioChunk_.format, std::size(audioChunk_.format));

        voukoder_ = std::move(voukoderInstance);

        isCapturing = true;

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

        videoFrame_.buffer[0] = data;
        videoFrame_.rowsize[0] = rowPitch;

        REQUIRE(voukoder_->SendVideoFrame(videoFrame_), "Failed to send video frame to Voukoder");

        POST();
        return S_OK;
    }

    HRESULT EncoderSession::writeAudioFrame(BYTE* data, int32_t length, LONGLONG presentationTime) {
        PRE();

        if (isBeingDeleted_) {
            POST();
            return E_FAIL;
        }

        audioChunk_.buffer[0] = data;
        audioChunk_.samples = length;

        REQUIRE(voukoder_->SendAudioSampleChunk(audioChunk_), "Failed to send audio chunk to Voukoder");

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

        if (voukoder_) {
            LOG_IF_FAILED(voukoder_->Close(true), "Failed to close Voukoder encoder");
        } else {
            LOG(LL_DBG, "Voukoder instance was never created (audio-only mode)");
        }

        isSessionFinished_ = true;
        endSessionCondition_.notify_all();
        
        LOG(LL_NFO, "Encoding session ended successfully");

        POST();
        return S_OK;
    }
}
