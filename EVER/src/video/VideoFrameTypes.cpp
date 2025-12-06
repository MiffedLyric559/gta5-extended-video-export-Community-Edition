#include "VideoFrameTypes.h"

namespace Encoder {
  ExrQueueItem::ExrQueueItem() 
      : isEndOfStream(true),
        colorData(nullptr),
        depthData(nullptr) {
      stencilMappedData.pData = nullptr;
      stencilMappedData.RowPitch = 0;
      stencilMappedData.DepthPitch = 0;
  }

  ExrQueueItem::ExrQueueItem(Microsoft::WRL::ComPtr<ID3D11Texture2D> rgbTexture, void* rgbData,
                            Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTexture, void* depthData,
                            Microsoft::WRL::ComPtr<ID3D11Texture2D> stencilTexture, 
                            const D3D11_MAPPED_SUBRESOURCE& stencilMappedData)
      : colorTexture(std::move(rgbTexture)), 
        colorData(rgbData), 
        depthTexture(std::move(depthTexture)), 
        depthData(depthData),
        stencilTexture(std::move(stencilTexture)), 
        stencilMappedData(stencilMappedData),
        isEndOfStream(false) {
  }
}
