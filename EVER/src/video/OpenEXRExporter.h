#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>

namespace Encoder {
    class OpenEXRExporter {
    public:
        OpenEXRExporter() = default;
        ~OpenEXRExporter() = default;

        void initialize(const std::string& outputPath, int32_t width, int32_t height);

        HRESULT exportFrame(const Microsoft::WRL::ComPtr<ID3D11DeviceContext>& deviceContext,
                        const Microsoft::WRL::ComPtr<ID3D11Texture2D>& colorTexture,
                        const Microsoft::WRL::ComPtr<ID3D11Texture2D>& depthTexture,
                        uint64_t frameNumber);

        const std::string& getOutputPath() const { return outputPath_; }

        int32_t getWidth() const { return width_; }

        int32_t getHeight() const { return height_; }

    private:
        std::string outputPath_;
        int32_t width_ = 0;
        int32_t height_ = 0;
    };

}
