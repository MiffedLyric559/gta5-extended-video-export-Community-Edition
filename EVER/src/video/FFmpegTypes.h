#pragma once

#include <cstring>
#include <Windows.h>

namespace FFmpeg {

    typedef struct {
        INT num;
        INT den;
    } Rational;

    typedef enum {
        Progressive = 0,
        Top = 1,
        Bottom = 2
    } FieldOrder;

    typedef enum {
        Mono = 0,
        Stereo = 1,
        FivePointOne = 2
    } ChannelLayout;

    typedef enum {
        Limited = 0,
        Full = 1
    } ColorRange;

    typedef enum {
        bt601_PAL = 0,
        bt601_NTSC = 1,
        bt709 = 2,
        bt2020_CL = 3,
        bt2020_NCL = 4
    } ColorSpace;

    typedef struct {
        CHAR encoder[16];
        CHAR options[8192];
        CHAR filters[8192];
        CHAR sidedata[8192];
    } FFTRACKCONFIG;

    // Main encoder configuration
    typedef struct {
        INT version;
        FFTRACKCONFIG video;
        FFTRACKCONFIG audio;
        struct {
            CHAR container[16];
            BOOL faststart;
        } format;
    } FFENCODERCONFIG;

    typedef struct {
        WCHAR filename[255];
        WCHAR application[32];
        struct {
            BOOL enabled;
            INT width;
            INT height;
            Rational timebase;
            Rational aspectratio;
            FieldOrder fieldorder;
        } video;
        struct {
            BOOL enabled;
            INT samplerate;
            ChannelLayout channellayout;
            INT numberChannels;
        } audio;
    } FFENCODERINFO;

    typedef struct {
        BYTE** buffer;
        INT samples;
        INT blockSize;
        INT planes;
        INT sampleRate;
        ChannelLayout layout;
        CHAR format[16];
    } FFAUDIOCHUNK;

    typedef struct {
        BYTE** buffer;
        INT* rowsize;
        INT planes;
        INT width;
        INT height;
        INT pass;
        CHAR format[16];
        CHAR colorRange[16];
        CHAR colorSpace[16];
        CHAR colorPrimaries[16];
        CHAR colorTrc[16];
    } FFVIDEOFRAME;

    inline void ConvertVKConfigToFFConfig(const void* vkConfig, FFENCODERCONFIG& ffConfig) {
        memcpy(&ffConfig, vkConfig, sizeof(FFENCODERCONFIG));
    }
}
