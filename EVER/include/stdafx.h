#pragma once

#define NOMINMAX  // Prevent Windows.h min/max macros from interfering with std::numeric_limits

//#pragma comment(lib, "../ScriptHookV/lib/ScriptHookV.lib")
#pragma comment(lib, "mfuuid.lib")

#include <sstream>

#include "../ScriptHookV/inc/main.h"
#include "../ScriptHookV/inc/nativeCaller.h"
#include "../ScriptHookV/inc/natives.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <string>
#include <regex>
#include <codecapi.h>
#include <wmcodecdsp.h>
#include <wrl.h>

#include <INIReader.h>

#include "../EVER/src/config/Manager.h"

#include <ImfImage.h>