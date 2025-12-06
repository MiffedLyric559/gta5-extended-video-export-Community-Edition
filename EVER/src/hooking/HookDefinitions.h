#pragma once

#include "MemberFunctionHook.h"
#include "ImportExportHook.h"
#include "X64Detour.h"

#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

DEFINE_MEMBER_HOOK(ID3D11DeviceContext, Draw, 13, void,
                   UINT vertexCount,
                   UINT startVertexLocation);

DEFINE_MEMBER_HOOK(ID3D11DeviceContext, DrawIndexed, 12, void,
                   UINT indexCount,
                   UINT startIndexLocation,
                   INT baseVertexLocation);

DEFINE_MEMBER_HOOK(ID3D11DeviceContext, OMSetRenderTargets, 33, void,
                   UINT numViews,
                   ID3D11RenderTargetView* const* ppRenderTargetViews,
                   ID3D11DepthStencilView* pDepthStencilView);

DEFINE_MEMBER_HOOK(IDXGISwapChain, Present, 8, HRESULT,
                   UINT syncInterval,
                   UINT flags);

DEFINE_MEMBER_HOOK(IDXGIFactory, CreateSwapChain, 10, HRESULT,
                   IUnknown* pDevice,
                   DXGI_SWAP_CHAIN_DESC* pDesc,
                   IDXGISwapChain** ppSwapChain);

DEFINE_MEMBER_HOOK(IDXGIFactory2, CreateSwapChainForHwnd, 15, HRESULT,
                   IUnknown* pDevice,
                   HWND hWnd,
                   const DXGI_SWAP_CHAIN_DESC1* pDesc,
                   const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                   IDXGIOutput* pRestrictToOutput,
                   IDXGISwapChain1** ppSwapChain);

DEFINE_MEMBER_HOOK(IDXGIFactory2, CreateSwapChainForCoreWindow, 16, HRESULT,
                   IUnknown* pDevice,
                   IUnknown* pWindow,
                   const DXGI_SWAP_CHAIN_DESC1* pDesc,
                   IDXGIOutput* pRestrictToOutput,
                   IDXGISwapChain1** ppSwapChain);

DEFINE_MEMBER_HOOK(IDXGIFactory2, CreateSwapChainForComposition, 24, HRESULT,
                   IUnknown* pDevice,
                   const DXGI_SWAP_CHAIN_DESC1* pDesc,
                   IDXGIOutput* pRestrictToOutput,
                   IDXGISwapChain1** ppSwapChain);

DEFINE_MEMBER_HOOK(IMFSinkWriter, AddStream, 3, HRESULT,
                   IMFMediaType* pTargetMediaType,
                   DWORD* pdwStreamIndex);

DEFINE_MEMBER_HOOK(IMFSinkWriter, SetInputMediaType, 4, HRESULT,
                   DWORD dwStreamIndex,
                   IMFMediaType* pInputMediaType,
                   IMFAttributes* pEncodingParameters);

DEFINE_MEMBER_HOOK(IMFSinkWriter, WriteSample, 6, HRESULT,
                   DWORD dwStreamIndex,
                   IMFSample* pSample);

DEFINE_MEMBER_HOOK(IMFSinkWriter, Finalize, 11, HRESULT);

DEFINE_NAMED_IMPORT_HOOK("mfreadwrite.dll", MFCreateSinkWriterFromURL, HRESULT,
                         LPCWSTR pwszOutputURL,
                         IMFByteStream* pByteStream,
                         IMFAttributes* pAttributes,
                         IMFSinkWriter** ppSinkWriter);

DEFINE_NAMED_IMPORT_HOOK("kernel32.dll", LoadLibraryW, HMODULE,
                         LPCWSTR lpLibFileName);

DEFINE_NAMED_IMPORT_HOOK("kernel32.dll", LoadLibraryA, HMODULE,
                         LPCSTR lpLibFileName);

DEFINE_NAMED_EXPORT_HOOK("d3d11.dll", D3D11CreateDeviceAndSwapChain, HRESULT,
                         IDXGIAdapter* pAdapter,
                         D3D_DRIVER_TYPE driverType,
                         HMODULE software,
                         UINT flags,
                         const D3D_FEATURE_LEVEL* pFeatureLevels,
                         UINT featureLevels,
                         UINT sdkVersion,
                         const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
                         IDXGISwapChain** ppSwapChain,
                         ID3D11Device** ppDevice,
                         D3D_FEATURE_LEVEL* pFeatureLevel,
                         ID3D11DeviceContext** ppImmediateContext);

DEFINE_X64_HOOK(GetRenderTimeBase, float,
                int64_t choice);

DEFINE_X64_HOOK(CreateTexture, void*,
                void* rcx,
                char* name,
                uint32_t r8d,
                uint32_t width,
                uint32_t height,
                uint32_t format,
                void* rsp30);

DEFINE_X64_HOOK(CreateThread, HANDLE,
                void* pFunc,
                void* pParams,
                int32_t r8d,
                int32_t r9d,
                void* rsp20,
                int32_t rsp28,
                char* name);

DEFINE_X64_HOOK(CreateExportContext, uint8_t,
                void* pContext,
                uint32_t width,
                uint32_t height,
                void* r9d);

using IsPendingPendingBakeStartFunc = bool(*)();

DEFINE_X64_HOOK(StartBakeProject, bool,
                void* videoEditorInterface,  // this pointer (RCX)
                void* montage);              // CReplayMontage* (RDX)
