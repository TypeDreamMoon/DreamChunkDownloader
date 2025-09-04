#pragma once

#include "Logging/LogCategory.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDreamChunkDownloader, All, All);

#define DCD_LOG(V, F, ...) UE_LOG(LogDreamChunkDownloader, V, F, ##__VA_ARGS__)