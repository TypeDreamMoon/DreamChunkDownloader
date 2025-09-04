// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "DreamChunkDownloaderTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DreamChunkDownloaderSubsystem.generated.h"

class FDreamChunkDownloaderPlatformWrapper;
class FDreamChunkDownload;
class IHttpRequest;

DECLARE_MULTICAST_DELEGATE_TwoParams(FDreamPlatformChunkInstallMultiDelegate, uint32, bool);

/**
 * 
 */
UCLASS()
class DREAMCHUNKDOWNLOADER_API UDreamChunkDownloaderSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

private:
	friend FDreamChunkDownloaderPlatformWrapper;
	friend FDreamChunkDownload;

public:
	~UDreamChunkDownloaderSubsystem() override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

public:
	void Finalize();

	bool LoadCachedBuild(const FString& DeploymentName);

	void UpdateBuild(const FString& InDeploymentName, const FString& InContentBuildId, const FDreamChunkDownloaderTypes::FDreamCallback OnCallback);

	void MountChunks(const TArray<int32>& ChunkIds, const FDreamChunkDownloaderTypes::FDreamCallback& OnCallback);

	void MountChunk(int32 ChunkId, const FDreamChunkDownloaderTypes::FDreamCallback& OnCallback);

	void DownloadChunks(const TArray<int32>& ChunkIds, const FDreamChunkDownloaderTypes::FDreamCallback& OnCallback, int32 Priority);

	void DownloadChunk(int32 ChunkId, const FDreamChunkDownloaderTypes::FDreamCallback& OnCallback, int32 Priority);

	int32 FlushCache();

	int ValidateCache();

	void BeginLoadingMode(const FDreamChunkDownloaderTypes::FDreamCallback& OnCallback);

	FDreamPlatformChunkInstallMultiDelegate OnChunkMounted;

public:
	UFUNCTION(BlueprintPure)
	EDreamChunkStatus GetChunkStatus(int32 ChunkId);

	UFUNCTION(BlueprintPure)
	void GetAllChunkIds(TArray<int32>& ChunkIds) const;

	UFUNCTION(BlueprintPure)
	int32 GetNumDownloadRequests() const
	{
		return DownloadRequests.Num();
	}

	UFUNCTION(BlueprintPure)
	FORCEINLINE FString GetCacheFolder() const
	{
		return CacheFolder;
	}

	UFUNCTION(BlueprintPure)
	FORCEINLINE TArray<FString>& GetBuildBaseUrls()
	{
		return BuildBaseUrls;
	}

	UFUNCTION(BlueprintPure)
	FORCEINLINE FDreamChunkDownloaderStats& GetStats()
	{
		return LoadingModeStats;
	}

	UFUNCTION(BlueprintPure)
	FORCEINLINE FString GetContentBuildId() const
	{
		return ContentBuildId;
	}

	UFUNCTION(BlueprintPure)
	FORCEINLINE FString GetDeploymentName() const
	{
		return LastDeploymentName;
	}

	FDreamChunkDownloaderTypes::FDreamDownloadAnalytics OnDownloadAnalytics;

	TMap<FString, TSharedRef<FDreamPakFile>>& GetPakFiles()
	{
		return PakFiles;
	}

	TArray<TSharedRef<FDreamPakFile>>& GetDownloadRequests()
	{
		return DownloadRequests;
	}

protected:
	FDreamChunkDownloaderStats LoadingModeStats;
	TArray<FDreamChunkDownloaderTypes::FDreamCallback> PostLoadCallbacks;
	int32 LoadingCompleteLatch = 0;

	FDreamChunkDownloaderTypes::FDreamCallback UpdateBuildCallback;

	// platform name (determines the manifest)
	FString PlatformName;

	// folders to save pak files into on disk
	FString CacheFolder;

	// content folder where we can find some chunks shipped with the build
	FString EmbeddedFolder;

	// build specific ID and URL paths
	FString LastDeploymentName;
	FString ContentBuildId;
	TArray<FString> BuildBaseUrls;

	// chunk id to chunk record
	TMap<int32, TSharedRef<FDreamChunk>> Chunks;

	// pak file name to pak file record
	TMap<FString, TSharedRef<FDreamPakFile>> PakFiles;

	// pak files embedded in the build (immutable, compressed)
	TMap<FString, FDreamPakFileEntry> EmbeddedPaks;

	// do we need to save the manifest (done whenever new downloads have started)
	bool bNeedsManifestSave = false;

	// handle for the per-frame mount ticker in the main thread
	FTSTicker::FDelegateHandle MountTicker;

	// manifest download request
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> ManifestRequest;

	// maximum number of downloads to allow concurrently
	int32 TargetDownloadsInFlight = 1;

	// list of pak files that have been requested
	TArray<TSharedRef<FDreamPakFile>> DownloadRequests;

private:
	void SetContentBuildId(const FString& DeploymentName, const FString& NewContentBuildId);
	void LoadManifest(const TArray<FDreamPakFileEntry>& ManifestPakFiles);
	void TryLoadBuildManifest(int TryNumber);
	void TryDownloadBuildManifest(int TryNumber);
	void WaitForMounts();
	void SaveLocalManifest(bool bForce);
	bool UpdateLoadingMode();
	void ComputeLoadingStats();
	void UnmountPakFile(const TSharedRef<FDreamPakFile>& PakFile);
	void CancelDownload(const TSharedRef<FDreamPakFile>& PakFile, bool bResult);
	void DownloadPakFileInternal(const TSharedRef<FDreamPakFile>& PakFile, const FDreamChunkDownloaderTypes::FDreamCallback& Callback, int32 Priority);
	void MountChunkInternal(FDreamChunk& Chunk, const FDreamChunkDownloaderTypes::FDreamCallback& Callback);
	void DownloadChunkInternal(const FDreamChunk& Chunk, const FDreamChunkDownloaderTypes::FDreamCallback& Callback, int32 Priority);
	void CompleteMountTask(FDreamChunk& Chunk);
	bool UpdateMountTasks(float dts);
	void ExecuteNextTick(const FDreamChunkDownloaderTypes::FDreamCallback& Callback, bool bSuccess);
	void IssueDownloads();
};
