// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DreamChunkDownloaderSettings.generated.h"

enum class EDreamChunkDownloaderCacheLocation : uint8;

USTRUCT(BlueprintType)
struct FDreamChunkDownloaderDeploymentSet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config)
	FString DeploymentName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config)
	TArray<FString> Hosts;
};
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
	// Cache Folder
	// This setting only windows platform
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite)
	EDreamChunkDownloaderCacheLocation CacheFolderPath;

	// Max Concurrent Downloads
	// Maximum number of simultaneous downloads
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite)
	int MaxConcurrentDownloads = 5;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite)
	TArray<FDreamChunkDownloaderDeploymentSet> DeploymentSets;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite)
	FString DownloadServerPath = "sample-package/";

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite)
	FString EmbeddedManifestFileName = "EmbeddedManifest.json";

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite)
	FString LocalManifestFileName = "LocalManifest.json";

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite)
	FString CachedBuildManifestFileName = "CachedBuildManifest.json";

public:
	static UDreamChunkDownloaderSettings* Get();
};
