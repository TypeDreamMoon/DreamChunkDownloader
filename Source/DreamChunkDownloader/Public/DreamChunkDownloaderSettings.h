// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DreamChunkDownloaderSettings.generated.h"

struct FDreamChunkDownloaderDeploymentSet;
enum class EDreamChunkDownloaderCacheLocation : uint8;

/**
 * 
 */
UCLASS(DefaultConfig, Config=DreamChunkDownloader)
class DREAMCHUNKDOWNLOADER_API UDreamChunkDownloaderSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetContainerName() const override { return FName(TEXT("Project")); }
	virtual FName GetCategoryName() const override { return FName(TEXT("DreamPlugin")); }
	virtual FName GetSectionName() const override { return FName(TEXT("ChunkDownloaderSetting")); }

public:
	// Use Remote Chunk Download List And Build ID
	// if use, please go to manifest to add "download-chunk-id-list" and "client-build-id" in manifest.json
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bUseStaticRemoteHost = false;

	// Static Remote Host
	// If remote build ID or remote chunk download list is enabled, you need to configure the static remote host to get BuildID and DownloadChunkIds
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Settings", Meta = (EditCondition = "bUseStaticRemoteHost"))
	FString StaticRemoteHost = "sample.com/data/";
	
	// Download Chunk Ids
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Settings", Meta = (EditCondition = "!bUseStaticRemoteHost"))
	TArray<int> DownloadChunkIds;

	// Build ID
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Settings", Meta = (EditCondition = "!bUseStaticRemoteHost"))
	FString BuildID = TEXT("0.0.0");
	
	// Cache Folder
	// This setting only windows platform
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Settings")
	EDreamChunkDownloaderCacheLocation CacheFolderPath;

	// Max Concurrent Downloads
	// Maximum number of simultaneous downloads
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Settings")
	int MaxConcurrentDownloads = 5;

	// Deployment Sets
	// Sample:
	// Deployment-> Windows / Hosts -> [ "https://example.com/windows/", "https://example.com/windows2/" ]
	// Support Deployment: Windows, Android, IOS, Mac, Linux
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Settings")
	TArray<FDreamChunkDownloaderDeploymentSet> DeploymentSets;

	// Embedded Manifest File Name
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "File")
	FString EmbeddedManifestFileName = "EmbeddedManifest.json";

	// Local Manifest File Name
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "File")
	FString LocalManifestFileName = "LocalManifest.json";

	// Cached Build Manifest File Name
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "File")
	FString CachedBuildManifestFileName = "CachedBuildManifest.json";

public:
	static UDreamChunkDownloaderSettings* Get();
};
