#pragma once

#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <Windows.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace ever {
    namespace crash {
        void Initialize();
        void Cleanup();
    }
}
