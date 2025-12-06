#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 26812)

#define NOMINMAX
#include <Windows.h>
#undef min
#undef max

#include "OpenEXRExporter.h"
#include "logger.h"

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfRgba.h>
#include <OpenEXR/ImfRgbaFile.h>
#include <filesystem>
#include <iomanip>
#include <sstream>

#pragma warning(pop)

namespace Encoder {
    void OpenEXRExporter::initialize(const std::string& outputPath, int32_t width, int32_t height) {
        PRE();
        
        outputPath_ = outputPath;
        width_ = width;
        height_ = height;
        
        std::filesystem::create_directories(outputPath_);
        
        LOG(LL_NFO, "OpenEXR exporter initialized: ", outputPath_, " (", width_, "x", height_, ")");
        
        POST();
    }

    HRESULT OpenEXRExporter::exportFrame(const Microsoft::WRL::ComPtr<ID3D11DeviceContext>& deviceContext,
                                        const Microsoft::WRL::ComPtr<ID3D11Texture2D>& colorTexture,
                                        const Microsoft::WRL::ComPtr<ID3D11Texture2D>& depthTexture,
                                        uint64_t frameNumber) {
        PRE();

        D3D11_MAPPED_SUBRESOURCE mappedColor = {nullptr};
        D3D11_MAPPED_SUBRESOURCE mappedDepth = {nullptr};

        if (colorTexture) {
            REQUIRE(deviceContext->Map(colorTexture.Get(), 0, D3D11_MAP::D3D11_MAP_READ, 0, &mappedColor), 
                    "Failed to map color texture for EXR export");
        }
        if (depthTexture) {
            REQUIRE(deviceContext->Map(depthTexture.Get(), 0, D3D11_MAP::D3D11_MAP_READ, 0, &mappedDepth),
                    "Failed to map depth texture for EXR export");
        }

        struct RGBA {
            half r;
            half g;
            half b;
            half a;
        };

        struct Depth {
            float depth;
        };

        Imf::Header header(width_, height_);
        Imf::FrameBuffer framebuffer;

        if (mappedColor.pData != nullptr) {
            LOG_CALL(LL_DBG, header.channels().insert("R", Imf::Channel(Imf::HALF)));
            LOG_CALL(LL_DBG, header.channels().insert("G", Imf::Channel(Imf::HALF)));
            LOG_CALL(LL_DBG, header.channels().insert("B", Imf::Channel(Imf::HALF)));
            LOG_CALL(LL_DBG, header.channels().insert("SubsurfaceScatter", Imf::Channel(Imf::HALF)));
            
            const auto colorArray = static_cast<RGBA*>(mappedColor.pData);

            LOG_CALL(LL_DBG, framebuffer.insert("R", Imf::Slice(Imf::HALF, 
                reinterpret_cast<char*>(&colorArray[0].r),
                sizeof(RGBA), sizeof(RGBA) * width_)));

            LOG_CALL(LL_DBG, framebuffer.insert("G", Imf::Slice(Imf::HALF, 
                reinterpret_cast<char*>(&colorArray[0].g),
                sizeof(RGBA), sizeof(RGBA) * width_)));

            LOG_CALL(LL_DBG, framebuffer.insert("B", Imf::Slice(Imf::HALF, 
                reinterpret_cast<char*>(&colorArray[0].b),
                sizeof(RGBA), sizeof(RGBA) * width_)));

            LOG_CALL(LL_DBG, framebuffer.insert("SubsurfaceScatter", Imf::Slice(Imf::HALF, 
                reinterpret_cast<char*>(&colorArray[0].a),
                sizeof(RGBA), sizeof(RGBA) * width_)));
        }

        if (mappedDepth.pData != nullptr) {
            LOG_CALL(LL_DBG, header.channels().insert("depth.Z", Imf::Channel(Imf::FLOAT)));
            
            const auto depthArray = static_cast<Depth*>(mappedDepth.pData);

            LOG_CALL(LL_DBG, framebuffer.insert("depth.Z", Imf::Slice(Imf::FLOAT, 
                reinterpret_cast<char*>(&depthArray[0].depth),
                sizeof(Depth), sizeof(Depth) * width_)));
        }

        std::stringstream filenameStream;
        filenameStream << outputPath_ << "\\frame." 
                    << std::setw(5) << std::setfill('0') << frameNumber 
                    << ".exr";

        Imf::OutputFile file(filenameStream.str().c_str(), header);
        LOG_CALL(LL_DBG, file.setFrameBuffer(framebuffer));
        LOG_CALL(LL_DBG, file.writePixels(height_));

        if (colorTexture) {
            LOG_CALL(LL_DBG, deviceContext->Unmap(colorTexture.Get(), 0));
        }

        if (depthTexture) {
            LOG_CALL(LL_DBG, deviceContext->Unmap(depthTexture.Get(), 0));
        }

        LOG(LL_NFO, "Exported EXR frame: ", frameNumber);

        POST();
        return S_OK;
    }
}
