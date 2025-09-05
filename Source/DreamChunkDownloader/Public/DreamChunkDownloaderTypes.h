#pragma once

#include "CoreMinimal.h"
#include "IPlatformFilePak.h"
#include "DreamChunkDownloaderTypes.generated.h"

class FDreamChunkDownload;
class FDreamPakMountWork;

struct FDreamPakFile;
struct FDreamChunkDownloaderStats;
struct FDreamChunk;
struct FDreamPakFileEntry;

namespace FDreamChunkDownloaderTypes
{
	typedef FAsyncTask<FDreamPakMountWork> FDreamMountTask;
	typedef TFunction<void(bool bSuccess)> FDreamCallback;
	typedef TFunction<void(
		const FString& FileName,
		const FString& Url,
		uint64 SizeBytes,
		const FTimespan& DownloadTime,
		int32 HttpStatus)> FDreamDownloadAnalytics;
}

namespace FDreamChunkDownloaderStatics
{
	static const FString BUILD_ID_KEY = TEXT("build-id");
	static const FString ENTRIES_COUNT_FIELD = TEXT("entries-count");
	static const FString ENTRIES_FIELD = TEXT("entries");
	static const FString FILE_NAME_FIELD = TEXT("file-name");
	static const FString FILE_SIZE_FIELD = TEXT("file-size");
	static const FString FILE_VERSION_FIELD = TEXT("file-version");
	static const FString FILE_CHUNK_ID_FIELD = TEXT("chunk-id");
	static const FString FILE_RELATIVE_URL_FIELD = TEXT("relative-url");
	static const FString DOWNLOAD_CHUNK_ID_LIST_FIELD = TEXT("download-chunk-id-list");
	static const FString CLIENT_BUILD_ID = "client-build-id";
}

UENUM(BlueprintType)
enum class EDreamChunkStatus : uint8
{
	Mounted UMETA(DisplayName = "Mounted"),
	Cached UMETA(DisplayName = "Cached"),
	Downloading UMETA(DisplayName = "Downloading"),
	Partial UMETA(DisplayName = "Partial"),
	Remote UMETA(DisplayName = "Remote"),
	Unknown UMETA(DisplayName = "Unknown")
};

UENUM(BlueprintType)
enum class EDreamChunkDownloaderCacheLocation : uint8
{
	// 存储在用户目录下
	User UMETA(DisplayName = "User"),
	// 存储在游戏目录下
	Game UMETA(DisplayName = "Game"),
};

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

USTRUCT(BlueprintType)
struct FDreamChunkDownloaderStats
{
public:
	GENERATED_BODY()

	// number of pak files downloaded
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int FilesDownloaded = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int TotalFilesToDownload = 0;

	// number of bytes downloaded
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int64 BytesDownloaded = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int64 TotalBytesToDownload = 0;

	// number of chunks mounted (chunk is an ordered array of paks)
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int ChunksMounted = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int TotalChunksToMount = 0;

	// UTC time that loading began (for rate estimates)
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FDateTime LoadingStartTime = FDateTime::MinValue();
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText LastError;
};

USTRUCT(BlueprintType)
struct FDreamPakFileEntry
{
	GENERATED_BODY()

	// unique name of the pak file (not path, i.e. no folder)
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString FileName;

	// final size of the file in bytes
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int64 FileSize = 0;

	// unique ID representing a particular version of this pak file
	// when it is used for validation (not done on golden path, but can be requested) this is assumed 
	// to be a SHA1 hash if it begins with "SHA1:" otherwise it's considered just a unique ID.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString FileVersion;

	// chunk ID this pak file is assigned to
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 ChunkId = -1;

	// URL for this pak file (relative to CDN root, includes build-specific folder)
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString RelativeUrl;
};

USTRUCT(BlueprintType)
struct FDreamPakFile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FDreamPakFileEntry Entry;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsCached = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsMounted = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsEmbedded = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int64 SizeOnDisk = 0; // grows as the file is downloaded. See Entry.FileSize for the target size

	// async download
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Priority = 0;

	TSharedPtr<FDreamChunkDownload> Download;
	TArray<FDreamChunkDownloaderTypes::FDreamCallback> PostDownloadCallbacks;
};

USTRUCT(BlueprintType)
struct FDreamChunk
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 ChunkId = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsMounted = false;

	TArray<TSharedRef<FDreamPakFile>> PakFiles;

	inline bool IsCached() const
	{
		for (const auto& PakFile : PakFiles)
		{
			if (!PakFile->bIsCached)
			{
				return false;
			}
		}
		return true;
	}

	// async mount
	FDreamChunkDownloaderTypes::FDreamMountTask* MountTask = nullptr;
};

USTRUCT(BlueprintType)
struct FDreamManifestData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString BuildId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Platform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FDreamPakFileEntry> PakFiles;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<FString, FString> Properties;
};

class FDreamMultiCallback
{
public:
	FDreamMultiCallback(const FDreamChunkDownloaderTypes::FDreamCallback& OnCallback);

	inline const FDreamChunkDownloaderTypes::FDreamCallback& AddPending()
	{
		++NumPending;
		return IndividualCb;
	}

	inline int GetNumPending() const
	{
		return NumPending;
	}

	void Abort()
	{
		check(NumPending == 0);
		delete this;
	}

private:
	~FDreamMultiCallback()
	{
	}

	int NumPending = 0;
	int NumSucceeded = 0;
	int NumFailed = 0;
	FDreamChunkDownloaderTypes::FDreamCallback IndividualCb;
	FDreamChunkDownloaderTypes::FDreamCallback OuterCallback;
};
