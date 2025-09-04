// Copyright Epic Games, Inc. All Rights Reserved.
// Refactoring By Dream Moon

#pragma once

#include "HAL/Platform.h"

class FString;
template <typename FuncType> class TFunction;

typedef TFunction<void(int32 HttpStatus)> FDreamDownloadComplete;
typedef TFunction<void(uint64 BytesReceived)> FDreamDownloadProgress;
typedef TFunction<void(void)> FDreamDownloadCancel;

extern FDreamDownloadCancel PlatformStreamDownload(const FString& Url, const FString& TargetFile, const FDreamDownloadProgress& Progress, const FDreamDownloadComplete& Callback);
