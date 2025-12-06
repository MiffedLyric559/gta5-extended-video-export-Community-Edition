#pragma once

#include "SafeQueue.h"
#include "VideoFrameTypes.h"
#include "OpenEXRExporter.h"

#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <mfidl.h>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <valarray>
#include <vector>
#include <wrl.h>

#pragma warning(push, 0)
#include <VoukoderTypeLib_h.h>
#pragma warning(pop)

namespace Encoder {
    class EncoderSession {
    public:
        EncoderSession();
        ~EncoderSession();

        EncoderSession(const EncoderSession&) = delete;
        EncoderSession& operator=(const EncoderSession&) = delete;

        HRESULT createContext(const VKENCODERCONFIG& config, 
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
                            uint32_t openExrHeight);

        HRESULT enqueueVideoFrame(const D3D11_MAPPED_SUBRESOURCE& subresource);

        HRESULT enqueueExrImage(const Microsoft::WRL::ComPtr<ID3D11DeviceContext>& deviceContext,
                            const Microsoft::WRL::ComPtr<ID3D11Texture2D>& colorTexture,
                            const Microsoft::WRL::ComPtr<ID3D11Texture2D>& depthTexture);

        HRESULT writeVideoFrame(BYTE* data, int32_t length, int rowPitch, LONGLONG presentationTime);

        HRESULT writeAudioFrame(BYTE* data, int32_t length, LONGLONG presentationTime);

        HRESULT finishVideo();

        HRESULT finishAudio();

        HRESULT endSession();

        bool isCapturing = false;

    private:
        Microsoft::WRL::ComPtr<IVoukoder> voukoder_;
        VKVIDEOFRAME videoFrame_;
        VKAUDIOCHUNK audioChunk_;

        int64_t videoPts_ = 0;
        int64_t audioPts_ = 0;

        bool isVideoFinished_ = false;
        bool isAudioFinished_ = false;
        bool isSessionFinished_ = false;
        bool isBeingDeleted_ = false;

        std::mutex finishMutex_;
        std::mutex endSessionMutex_;
        std::condition_variable endSessionCondition_;

        SafeQueue<FrameQueueItem> videoFrameQueue_;
        SafeQueue<ExrQueueItem> exrImageQueue_;

        std::valarray<uint16_t> motionBlurAccBuffer_;
        std::valarray<uint16_t> motionBlurTempBuffer_;
        std::valarray<uint8_t> motionBlurDestBuffer_;

        bool exportExr_ = false;
        uint64_t exrFrameNumber_ = 0;
        OpenEXRExporter exrExporter_;
        bool isExrEncodingThreadFinished_ = false;
        std::condition_variable exrEncodingThreadFinishedCondition_;
        std::mutex exrEncodingThreadMutex_;
        std::thread exrEncodingThread_;

        int32_t width_ = 0;
        int32_t height_ = 0;
        int32_t framerate_ = 0;
        uint8_t motionBlurSamples_ = 0;
        int32_t audioBlockAlign_ = 0;
        int32_t inputAudioSampleRate_ = 0;
        int32_t inputAudioChannels_ = 0;
        int32_t outputAudioSampleRate_ = 0;
        int32_t outputAudioChannels_ = 0;
        std::string filename_;
        float shutterPosition_ = 0.0f;
    };
}
