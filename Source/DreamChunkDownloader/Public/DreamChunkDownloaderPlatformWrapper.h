#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformChunkInstall.h"

class UDreamChunkDownloaderSubsystem;

class FDreamChunkDownloaderPlatformWrapper : public FGenericPlatformChunkInstall
{
public:
	virtual EChunkLocation::Type GetChunkLocation(uint32 ChunkID) override;
	virtual bool PrioritizeChunk(uint32 ChunkID, EChunkPriority::Type Priority) override;
	virtual FDelegateHandle AddChunkInstallDelegate(FPlatformChunkInstallDelegate Delegate) override;
	virtual void RemoveChunkInstallDelegate(FDelegateHandle Delegate) override;

public:
	virtual EChunkInstallSpeed::Type GetInstallSpeed() override;
	virtual bool SetInstallSpeed(EChunkInstallSpeed::Type InstallSpeed) override;
	virtual bool DebugStartNextChunk() override;
	virtual bool GetProgressReportingTypeSupported(EChunkProgressReportingType::Type ReportType) override;
	virtual float GetChunkProgress(uint32 ChunkID, EChunkProgressReportingType::Type ReportType) override;

public:
	FDreamChunkDownloaderPlatformWrapper(TSharedPtr<UDreamChunkDownloaderSubsystem>& InChunkDownloader) : ChunkDownloader(InChunkDownloader)
	{
	}

	virtual ~FDreamChunkDownloaderPlatformWrapper() override
	{
	}

private:
	TSharedPtr<UDreamChunkDownloaderSubsystem>& ChunkDownloader;
};
