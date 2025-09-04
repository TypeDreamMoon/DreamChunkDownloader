// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

// TODO: 目前的下载部分有些问题 会循环下载Manifest文件 然后不下载pak,CahceManifest文件一直是和远程同步的 这是不应该的

class FDreamChunkDownloaderModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
