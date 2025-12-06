#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

namespace Encoder {
    struct FrameQueueItem {
        FrameQueueItem() : subresource(nullptr), rowPitch(0) {}
        
        explicit FrameQueueItem(std::shared_ptr<D3D11_MAPPED_SUBRESOURCE> mappedSubresource, int pitch)
            : subresource(std::move(mappedSubresource)), rowPitch(pitch) {}

        std::shared_ptr<D3D11_MAPPED_SUBRESOURCE> subresource;
        int rowPitch;
    };

    struct ExrQueueItem {
        ExrQueueItem();

        ExrQueueItem(Microsoft::WRL::ComPtr<ID3D11Texture2D> rgbTexture, void* rgbData,
                    Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTexture, void* depthData,
                    Microsoft::WRL::ComPtr<ID3D11Texture2D> stencilTexture, 
                    const D3D11_MAPPED_SUBRESOURCE& stencilMappedData);

        bool isEndOfStream;
        
        Microsoft::WRL::ComPtr<ID3D11Texture2D> colorTexture;
        void* colorData;
        
        Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTexture;
        void* depthData;
        
        Microsoft::WRL::ComPtr<ID3D11Texture2D> stencilTexture;
        D3D11_MAPPED_SUBRESOURCE stencilMappedData;
    };
}
