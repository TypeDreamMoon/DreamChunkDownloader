// Fill out your copyright notice in the Description page of Project Settings.


#include "DreamChunkDownloaderSubsystem.h"

#include "Http.h"

#include "DreamChunkDownloaderLog.h"
#include "DreamChunkDownloaderSettings.h"
#include "DreamChunkDownloaderUtils.h"
#include "DreamChunkDownload.h"
#include "DreamChunkDownloaderPakMountWork.h"

#define LOCTEXT_NAMESPACE "DreamChunkDownloaderSubsystem"

using namespace FDreamChunkDownloaderTypes;
using namespace FDreamChunkDownloaderStatics;

UDreamChunkDownloaderSubsystem::~UDreamChunkDownloaderSubsystem()
{
	// this will be true unless we forgot to have Finalize called.
	check(PakFiles.Num() <= 0);
}

void UDreamChunkDownloaderSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	PlatformName = FDreamChunkDownloaderUtils::GetTargetPlatformName();

	FString PackageBaseDir;

	switch (UDreamChunkDownloaderSettings::Get()->CacheFolderPath)
	{
	case EDreamChunkDownloaderCacheLocation::User:
		{
			PackageBaseDir = FPaths::ProjectSavedDir();
			break;
		}
	case EDreamChunkDownloaderCacheLocation::Game:
		{
			PackageBaseDir = FPaths::ProjectDir();
			break;
		}
	}

	PackageBaseDir /= TEXT("DreamChunkDownloader");

	check(!PackageBaseDir.IsEmpty())
	check(PakFiles.Num() == 0);
	check(PlatformName != TEXT("Unknown"));
	DCD_LOG(Log, TEXT("Initializing with platform = '%s' With cache Path = '%s'"),
	        *PlatformName,
	        *PackageBaseDir)

	FString PackageCacheDir = FPaths::Combine(PackageBaseDir, TEXT("PakCache"));
	FString PackageEmbeddedDir = FPaths::Combine(PackageBaseDir, TEXT("Embedded"));

	DCD_LOG(Log, TEXT("Initialize dirs : cache %s embedded %s"), *PackageCacheDir, *PackageEmbeddedDir);

	FPlatformMisc::AddAdditionalRootDirectory(PackageCacheDir);

	TargetDownloadsInFlight = FMath::Max(1, UDreamChunkDownloaderSettings::Get()->MaxConcurrentDownloads);

	CacheFolder = PackageCacheDir;
	EmbeddedFolder = PackageEmbeddedDir;

	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.MakeDirectory(*PackageCacheDir, true))
	{
		DCD_LOG(Error, TEXT("Failed to create cache folder '%s'"), *PackageCacheDir);
	}

	EmbeddedPaks.Empty();
	for (const FDreamPakFileEntry& Entry : FDreamChunkDownloaderUtils::ParseManifest(EmbeddedFolder / UDreamChunkDownloaderSettings::Get()->EmbeddedManifestFileName))
	{
		EmbeddedPaks.Add(Entry.FileName, Entry);
	}

	TArray<FString> StrayFiles;
	FileManager.FindFiles(StrayFiles, *CacheFolder, TEXT("*.pak"));

	TSharedPtr<FJsonObject> JsonObject;
	TArray<FDreamPakFileEntry> LocalManifest = FDreamChunkDownloaderUtils::ParseManifest(CacheFolder / UDreamChunkDownloaderSettings::Get()->LocalManifestFileName, JsonObject);

	// Decode remote download list
	if (UDreamChunkDownloaderSettings::Get()->bUseRemoteChunkDownloadList)
	{
		if (JsonObject.IsValid())
		{
			if (JsonObject->HasField(DOWNLOAD_CHUNK_ID_LIST_FIELD))
			{
				TArray<TSharedPtr<FJsonValue>> values = JsonObject->GetArrayField(DOWNLOAD_CHUNK_ID_LIST_FIELD);
				for (auto Value : values)
				{
					int AddedChunkID = Value->AsNumber();
					ChunkDownloadList.Add(AddedChunkID);
					DCD_LOG(Log, TEXT("Adding chunk %d to download list"), AddedChunkID);
				}
			}
		}
		else
		{
			DCD_LOG(Error, TEXT("Failed to parse remote download list. Maybe the Manifest file was deleted at runtime"));
		}
	}
	else
	{
		ChunkDownloadList = UDreamChunkDownloaderSettings::Get()->DownloadChunkIds;
	}

	if (UDreamChunkDownloaderSettings::Get()->bUseRemoteBuildID)
	{
		if (JsonObject.IsValid())
		{
			if (JsonObject->HasField(CLIENT_BUILD_ID))
			{
				SetContentBuildId(FDreamChunkDownloaderUtils::GetTargetPlatformName(), JsonObject->GetStringField(CLIENT_BUILD_ID));
				DCD_LOG(Log, TEXT("Using remote build id '%s'"), *ContentBuildId);
			}
		}
		else
		{
			DCD_LOG(Error, TEXT("Failed to parse remote build id. Maybe the Manifest file was deleted at runtime"));
		}
	}
	else
	{
		SetContentBuildId(FDreamChunkDownloaderUtils::GetTargetPlatformName(), UDreamChunkDownloaderSettings::Get()->BuildID);
		DCD_LOG(Log, TEXT("Using local build id '%s'"), *ContentBuildId);
	}

	if (LocalManifest.Num() > 0)
	{
		for (const FDreamPakFileEntry& Entry : LocalManifest)
		{
			TSharedRef<FDreamPakFile> FileInfo = MakeShared<FDreamPakFile>();

			FileInfo->Entry = Entry;

			FString LocalPath = CacheFolder / Entry.FileName;
			int64 SizeOnDisk = FileManager.FileSize(*LocalPath);
			if (SizeOnDisk > 0)
			{
				FileInfo->SizeOnDisk = SizeOnDisk;
				if (FileInfo->SizeOnDisk > Entry.FileSize)
				{
					DCD_LOG(Warning, TEXT("File '%s' is need update, size on disk = %lld, size in manifest = %lld"),
					        *LocalPath,
					        FileInfo->SizeOnDisk,
					        Entry.FileSize);
					bNeedsManifestSave = true;
					continue;
				}

				if (FileInfo->SizeOnDisk = Entry.FileSize)
				{
					FileInfo->bIsCached = true;
				}

				PakFiles.Add(Entry.FileName, FileInfo);;
			}
			else
			{
				DCD_LOG(Log, TEXT("'%s' appears in LocalManifest but is not on disk (not necessarily a problem)"), *LocalPath);
				bNeedsManifestSave = true;
			}

			StrayFiles.RemoveSingle(Entry.FileName);
		}
	}

	for (FString Orphan : StrayFiles)
	{
		bNeedsManifestSave = true;
		FString FullPathOnDisk = CacheFolder / Orphan;
		DCD_LOG(Log, TEXT("Deleting orphaned file '%s'"), *FullPathOnDisk)
		if (!ensure(FileManager.Delete(*FullPathOnDisk)))
		{
			// log an error (best we can do)
			DCD_LOG(Error, TEXT("Unable to delete '%s'"), *FullPathOnDisk);
		}
	}

	SaveLocalManifest(false);

	LoadCachedBuild(LastDeploymentName);
	UpdateBuild(LastDeploymentName, ContentBuildId, [this](bool bSuccess)
	{
		DCD_LOG(Log, TEXT("UpdateBuild completed: %s"), bSuccess ? TEXT("success") : TEXT("failed"));
		bIsDownloadManifestUpToDate = bSuccess;
	});
}

void UDreamChunkDownloaderSubsystem::Deinitialize()
{
	Super::Deinitialize();

	Finalize();
}

void UDreamChunkDownloaderSubsystem::Finalize()
{
	DCD_LOG(Display, TEXT("Finalizing."));

	// wait for all mounts to finish
	WaitForMounts();

	// update the mount tasks (queues up callbacks)
	ensure(UpdateMountTasks(0.0f) == false);

	// cancel all downloads
	for (const auto& It : PakFiles)
	{
		const TSharedRef<FDreamPakFile>& File = It.Value;
		if (File->Download.IsValid())
		{
			CancelDownload(File, false);
		}
	}

	// unmount all mounted chunks (best effort)
	for (const auto& It : Chunks)
	{
		const TSharedRef<FDreamChunk>& Chunk = It.Value;
		if (Chunk->bIsMounted)
		{
			// unmount the paks (in reverse order)
			for (int32 i = Chunk->PakFiles.Num() - 1; i >= 0; --i)
			{
				const TSharedRef<FDreamPakFile>& PakFile = Chunk->PakFiles[i];
				UnmountPakFile(PakFile);
			}

			// clear the flag
			Chunk->bIsMounted = false;
		}
	}

	// clear pak files and chunks
	PakFiles.Empty();
	Chunks.Empty();

	// cancel any pending manifest request
	if (ManifestRequest.IsValid())
	{
		ManifestRequest->CancelRequest();
		ManifestRequest.Reset();
	}

	// any loading mode is de-facto complete
	if (PostLoadCallbacks.Num() > 0)
	{
		TArray<FDreamChunkDownloaderTypes::FDreamCallback> Callbacks = MoveTemp(PostLoadCallbacks);
		PostLoadCallbacks.Empty();
		for (const auto& Callback : Callbacks)
		{
			ExecuteNextTick(Callback, false);
		}
	}

	// update is also de-facto complete
	if (UpdateBuildCallback)
	{
		FDreamChunkDownloaderTypes::FDreamCallback Callback = MoveTemp(UpdateBuildCallback);
		ExecuteNextTick(Callback, false);
	}

	// clear out the content build id
	ContentBuildId.Empty();
}

bool UDreamChunkDownloaderSubsystem::LoadCachedBuild(const FString& DeploymentName)
{
	// try to re-populate ContentBuildId and the cached manifest
	TMap<FString, FString> CachedManifestProps;
	TArray<FDreamPakFileEntry> CachedManifest = FDreamChunkDownloaderUtils::ParseManifest(CacheFolder / UDreamChunkDownloaderSettings::Get()->CachedBuildManifestFileName, &CachedManifestProps);
	const FString* BuildId = CachedManifestProps.Find(BUILD_ID_KEY);
	if (BuildId == nullptr || BuildId->IsEmpty())
	{
		return false;
	}

	SetContentBuildId(DeploymentName, *BuildId);
	LoadManifest(CachedManifest);
	return true;
}

void UDreamChunkDownloaderSubsystem::UpdateBuild(const FString& InDeploymentName, const FString& InContentBuildId, const FDreamChunkDownloaderTypes::FDreamCallback OnCallback)
{
	check(!InContentBuildId.IsEmpty());

	// if the build ID hasn't changed, there's no work to do
	if (InContentBuildId == ContentBuildId && LastDeploymentName == InDeploymentName)
	{
		ExecuteNextTick(OnCallback, true);
		return;
	}
	SetContentBuildId(InDeploymentName, InContentBuildId);

	// no overlapped UpdateBuild calls allowed, and Callback is required
	check(!UpdateBuildCallback);
	check(OnCallback);
	UpdateBuildCallback = OnCallback;

	// start the load/download process
	TryLoadBuildManifest(0);
}

void UDreamChunkDownloaderSubsystem::MountChunks(const TArray<int32>& ChunkIds, const FDreamChunkDownloaderTypes::FDreamCallback& OnCallback)
{
	// convert to chunk references
	TArray<TSharedRef<FDreamChunk>> ChunksToMount;
	for (int32 ChunkId : ChunkIds)
	{
		TSharedRef<FDreamChunk>* ChunkPtr = Chunks.Find(ChunkId);
		if (ChunkPtr != nullptr)
		{
			TSharedRef<FDreamChunk>& ChunkRef = *ChunkPtr;
			if (ChunkRef->PakFiles.Num() > 0)
			{
				if (!ChunkRef->bIsMounted)
				{
					ChunksToMount.Add(ChunkRef);
				}
				continue;
			}
		}
		DCD_LOG(Warning, TEXT("Ignoring mount request for chunk %d (no mapped pak files)."), ChunkId);
	}

	// make sure there are some chunks to mount (saves a frame)
	if (ChunksToMount.Num() <= 0)
	{
		// trivial success
		ExecuteNextTick(OnCallback, true);
		return;
	}

	// if there's no callback for some reason, avoid a bunch of boilerplate
#ifndef PVS_STUDIO // Build machine refuses to disable this warning
	if (OnCallback)
	{
		// loop over chunks and issue individual callback
		FDreamMultiCallback* MultiCallback = new FDreamMultiCallback(OnCallback);
		for (const TSharedRef<FDreamChunk>& Chunk : ChunksToMount)
		{
			MountChunkInternal(*Chunk, MultiCallback->AddPending());
		}
		check(MultiCallback->GetNumPending() > 0);
	} //-V773
	else
	{
		// no need to manage callbacks
		for (const TSharedRef<FDreamChunk>& Chunk : ChunksToMount)
		{
			MountChunkInternal(*Chunk, FDreamChunkDownloaderTypes::FDreamCallback());
		}
	}
#endif

	// resave manifest if needed
	SaveLocalManifest(false);
	ComputeLoadingStats();
}

void UDreamChunkDownloaderSubsystem::MountChunk(int32 ChunkId, const FDreamChunkDownloaderTypes::FDreamCallback& OnCallback)
{
	// look up the chunk
	TSharedRef<FDreamChunk>* ChunkPtr = Chunks.Find(ChunkId);
	if (ChunkPtr == nullptr || (*ChunkPtr)->PakFiles.Num() <= 0)
	{
		// a chunk that doesn't exist or one with no pak files are both considered "complete" for the purposes of this call
		// use GetChunkStatus to differentiate from chunks that mounted successfully
		DCD_LOG(Warning, TEXT("Ignoring mount request for chunk %d (no mapped pak files)."), ChunkId);
		ExecuteNextTick(OnCallback, true);
		return;
	}
	FDreamChunk& Chunk = **ChunkPtr;

	// see if we're mounted already
	if (Chunk.bIsMounted)
	{
		// trivial success
		ExecuteNextTick(OnCallback, true);
		return;
	}

	// mount the chunk
	MountChunkInternal(Chunk, OnCallback);

	// resave manifest if needed
	SaveLocalManifest(false);
	ComputeLoadingStats();
}

void UDreamChunkDownloaderSubsystem::DownloadChunks(const TArray<int32>& ChunkIds, const FDreamChunkDownloaderTypes::FDreamCallback& OnCallback, int32 Priority)
{
	// convert to chunk references
	TArray<TSharedRef<FDreamChunk>> ChunksToDownload;
	for (int32 ChunkId : ChunkIds)
	{
		TSharedRef<FDreamChunk>* ChunkPtr = Chunks.Find(ChunkId);
		if (ChunkPtr != nullptr)
		{
			TSharedRef<FDreamChunk>& ChunkRef = *ChunkPtr;
			if (ChunkRef->PakFiles.Num() > 0)
			{
				if (!ChunkRef->IsCached())
				{
					ChunksToDownload.Add(ChunkRef);
				}
				continue;
			}
		}
		DCD_LOG(Warning, TEXT("Ignoring download request for chunk %d (no mapped pak files)."), ChunkId);
	}

	// make sure there are some chunks to mount (saves a frame)
	if (ChunksToDownload.Num() <= 0)
	{
		// trivial success
		ExecuteNextTick(OnCallback, true);
		return;
	}

	// if there's no callback for some reason, avoid a bunch of boilerplate
#ifndef PVS_STUDIO // Build machine refuses to disable this warning
	if (OnCallback)
	{
		// loop over chunks and issue individual callback
		FDreamMultiCallback* MultiCallback = new FDreamMultiCallback(OnCallback);
		for (const TSharedRef<FDreamChunk>& Chunk : ChunksToDownload)
		{
			DownloadChunkInternal(*Chunk, MultiCallback->AddPending(), Priority);
		}
		check(MultiCallback->GetNumPending() > 0);
	} //-V773
	else
	{
		// no need to manage callbacks
		for (const TSharedRef<FDreamChunk>& Chunk : ChunksToDownload)
		{
			DownloadChunkInternal(*Chunk, FDreamChunkDownloaderTypes::FDreamCallback(), Priority);
		}
	}
#endif

	// resave manifest if needed
	SaveLocalManifest(false);
	ComputeLoadingStats();
}

void UDreamChunkDownloaderSubsystem::DownloadChunk(int32 ChunkId, const FDreamChunkDownloaderTypes::FDreamCallback& OnCallback, int32 Priority)
{
	// look up the chunk
	TSharedRef<FDreamChunk>* ChunkPtr = Chunks.Find(ChunkId);
	if (ChunkPtr == nullptr || (*ChunkPtr)->PakFiles.Num() <= 0)
	{
		// a chunk that doesn't exist or one with no pak files are both considered "complete" for the purposes of this call
		// use GetChunkStatus to differentiate from chunks that mounted successfully
		DCD_LOG(Warning, TEXT("Ignoring download request for chunk %d (no mapped pak files)."), ChunkId);
		ExecuteNextTick(OnCallback, true);
		return;
	}
	const FDreamChunk& Chunk = **ChunkPtr;

	// if all the paks are cached, just succeed
	if (Chunk.IsCached())
	{
		ExecuteNextTick(OnCallback, true);
		return;
	}

	// queue the download
	DownloadChunkInternal(Chunk, OnCallback, Priority);

	// resave manifest if needed
	SaveLocalManifest(false);
	ComputeLoadingStats();
}

int32 UDreamChunkDownloaderSubsystem::FlushCache()
{
	IFileManager& FileManager = IFileManager::Get();

	// wait for all mounts to finish
	WaitForMounts();

	DCD_LOG(Display, TEXT("Flushing chunk caches at %s"), *CacheFolder);
	int FilesDeleted = 0, FilesSkipped = 0;
	for (const auto& It : Chunks)
	{
		const TSharedRef<FDreamChunk>& Chunk = It.Value;
		check(Chunk->MountTask == nullptr); // we waited for mounts

		// cancel background downloads
		bool bDownloadPending = false;
		for (const TSharedRef<FDreamPakFile>& PakFile : Chunk->PakFiles)
		{
			if (PakFile->Download.IsValid() && !PakFile->Download->HasCompleted())
			{
				// skip paks that are being downloaded
				bDownloadPending = true;
				break;
			}
		}

		// skip chunks that have a foreground download pending
		if (bDownloadPending)
		{
			for (const TSharedRef<FDreamPakFile>& PakFile : Chunk->PakFiles)
			{
				if (PakFile->SizeOnDisk > 0)
				{
					// log that we skipped this one
					DCD_LOG(Warning, TEXT("Could not flush %s (chunk %d) due to download in progress."), *PakFile->Entry.FileName, Chunk->ChunkId);
					++FilesSkipped;
				}
			}
		}
		else
		{
			// delete paks
			for (const TSharedRef<FDreamPakFile>& PakFile : Chunk->PakFiles)
			{
				if (PakFile->SizeOnDisk > 0 && !PakFile->bIsEmbedded)
				{
					// log that we deleted this one
					FString FullPathOnDisk = CacheFolder / PakFile->Entry.FileName;
					if (ensure(FileManager.Delete(*FullPathOnDisk)))
					{
						DCD_LOG(Log, TEXT("Deleted %s (chunk %d)."), *FullPathOnDisk, Chunk->ChunkId);
						++FilesDeleted;

						// flag uncached (may have been partial)
						PakFile->bIsCached = false;
						PakFile->SizeOnDisk = 0;
						bNeedsManifestSave = true;
					}
					else
					{
						// log an error (best we can do)
						DCD_LOG(Error, TEXT("Unable to delete %s"), *FullPathOnDisk);
						++FilesSkipped;
					}
				}
			}
		}
	}

	// resave the manifest
	SaveLocalManifest(false);

	DCD_LOG(Display, TEXT("Chunk cache flush complete. %d files deleted. %d files skipped."), FilesDeleted, FilesSkipped);
	return FilesSkipped;
}

int UDreamChunkDownloaderSubsystem::ValidateCache()
{
	IFileManager& FileManager = IFileManager::Get();

	// wait for all mounts to finish
	WaitForMounts();

	DCD_LOG(Display, TEXT("Starting inline chunk validation."));
	int ValidFiles = 0, InvalidFiles = 0, SkippedFiles = 0;
	for (const auto& It : PakFiles)
	{
		const TSharedRef<FDreamPakFile>& PakFile = It.Value;
		if (PakFile->bIsCached && !PakFile->bIsEmbedded)
		{
			// we know how to validate certain hash versions
			bool bFileIsValid = false;
			if (PakFile->Entry.FileVersion.StartsWith(TEXT("SHA1:")))
			{
				// check the sha1 hash
				bFileIsValid = FDreamChunkDownloaderUtils::CheckFileSha1Hash(CacheFolder / PakFile->Entry.FileName, PakFile->Entry.FileVersion);
			}
			else
			{
				// we don't know how to validate this version format
				DCD_LOG(Warning, TEXT("Unable to validate %s with version '%s'."), *PakFile->Entry.FileName, *PakFile->Entry.FileVersion);
				++SkippedFiles;
				continue;
			}

			// see if it's valid or not
			if (bFileIsValid)
			{
				// log valid
				DCD_LOG(Log, TEXT("%s matches hash '%s'."), *PakFile->Entry.FileName, *PakFile->Entry.FileVersion);
				++ValidFiles;
			}
			else
			{
				// log invalid
				DCD_LOG(Warning, TEXT("%s does NOT match hash '%s'."), *PakFile->Entry.FileName, *PakFile->Entry.FileVersion);
				++InvalidFiles;

				// delete invalid files
				FString FullPathOnDisk = CacheFolder / PakFile->Entry.FileName;
				if (ensure(FileManager.Delete(*FullPathOnDisk)))
				{
					DCD_LOG(Log, TEXT("Deleted invalid pak %s (chunk %d)."), *FullPathOnDisk, PakFile->Entry.ChunkId);
					PakFile->bIsCached = false;
					PakFile->SizeOnDisk = 0;
					bNeedsManifestSave = true;
				}
			}
		}
	}

	// resave the manifest
	SaveLocalManifest(false);

	DCD_LOG(Display, TEXT("Chunk validation complete. %d valid, %d invalid, %d skipped"), ValidFiles, InvalidFiles, SkippedFiles);
	return InvalidFiles;
}

void UDreamChunkDownloaderSubsystem::BeginLoadingMode(const FDreamChunkDownloaderTypes::FDreamCallback& OnCallback)
{
	check(OnCallback); // you can't start loading mode without a valid callback

	// see if we're already in loading mode
	if (PostLoadCallbacks.Num() > 0)
	{
		DCD_LOG(Log, TEXT("JoinLoadingMode"));

		// just wait on the existing loading mode to finish
		PostLoadCallbacks.Add(OnCallback);
		return;
	}

	// start loading mode
	DCD_LOG(Log, TEXT("BeginLoadingMode"));
#if PLATFORM_ANDROID || PLATFORM_IOS
	FPlatformApplicationMisc::ControlScreensaver(FPlatformApplicationMisc::Disable);
#endif

	// reset stats
	LoadingModeStats.LastError = FText();
	LoadingModeStats.BytesDownloaded = 0;
	LoadingModeStats.FilesDownloaded = 0;
	LoadingModeStats.ChunksMounted = 0;
	LoadingModeStats.LoadingStartTime = FDateTime::UtcNow();
	ComputeLoadingStats(); // recompute before binding callback in case there's nothing queued yet

	// set the callback
	PostLoadCallbacks.Add(OnCallback);
	LoadingCompleteLatch = 0;

	// compute again next frame (if nothing's queued by then, we'll fire the callback
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float dts)
	{
		if (!IsValid(this) || this->PostLoadCallbacks.Num() <= 0)
		{
			return false; // stop ticking
		}
		return this->UpdateLoadingMode();
	}));
}

bool UDreamChunkDownloaderSubsystem::StartPatchGame(int InManifestFileDownloadHostIndex)
{
	if (bIsDownloadManifestUpToDate)
	{
		for (int id : ChunkDownloadList)
		{
			EDreamChunkStatus Status = GetChunkStatus(id);
			DCD_LOG(Log, TEXT("Chunk %d status %s"), id, *UEnum::GetValueAsString(Status));
		}

		TryDownloadBuildManifest(InManifestFileDownloadHostIndex);

		DownloadChunks(ChunkDownloadList, [this](bool bSuccess)
		{
			HandleDownloadCompleted(bSuccess);
		}, 0);

		BeginLoadingMode([this](bool bSuccess)
		{
			HandleLoadingModeCompleted(bSuccess);
		});

		return true;
	}

	DCD_LOG(Warning, TEXT("Chunk manifest is out of date. Please update the build."));
	return false;
}

void UDreamChunkDownloaderSubsystem::HandleDownloadCompleted(bool bSuccess)
{
	if (bSuccess)
	{
		DCD_LOG(Log, TEXT("Download completed successfully."));

		FJsonSerializableArrayInt DownloadedChunks;

		for (int ChunkID : ChunkDownloadList)
		{
			DownloadedChunks.Add(ChunkID);
		}

		MountChunks(DownloadedChunks, [this](bool bSuccess)
		{
			HandleMountCompleted(bSuccess);
		});

		OnPatchCompleted.Broadcast(true);
	}
	else
	{
		DCD_LOG(Error, TEXT("Download failed."));
		OnPatchCompleted.Broadcast(false);
	}
}

void UDreamChunkDownloaderSubsystem::HandleLoadingModeCompleted(bool bSuccess)
{
	OnPatchCompleted.Broadcast(bSuccess);
}

void UDreamChunkDownloaderSubsystem::HandleMountCompleted(bool bSuccess)
{
	OnMountCompleted.Broadcast(bSuccess);
}

EDreamChunkStatus UDreamChunkDownloaderSubsystem::GetChunkStatus(int32 ChunkId)
{
	// do we know about this chunk at all?
	const TSharedRef<FDreamChunk>* ChunkPtr = Chunks.Find(ChunkId);
	if (ChunkPtr == nullptr)
	{
		return EDreamChunkStatus::Unknown;
	}
	const FDreamChunk& Chunk = **ChunkPtr;

	// if it has no pak files, treat it the same as not found (shouldn't happen)
	if (!ensure(Chunk.PakFiles.Num() > 0))
	{
		return EDreamChunkStatus::Unknown;
	}

	// see if it's fully mounted
	if (Chunk.bIsMounted)
	{
		return EDreamChunkStatus::Mounted;
	}

	// count the number of paks in flight vs local
	int32 NumPaks = Chunk.PakFiles.Num(), NumCached = 0, NumDownloading = 0;
	for (const TSharedRef<FDreamPakFile>& PakFile : Chunk.PakFiles)
	{
		if (PakFile->bIsCached)
		{
			++NumCached;
		}
		else if (PakFile->Download.IsValid())
		{
			++NumDownloading;
		}
	}

	if (NumCached >= NumPaks)
	{
		// all cached
		return EDreamChunkStatus::Cached;
	}
	else if (NumCached + NumDownloading >= NumPaks)
	{
		// some downloads still in progress
		return EDreamChunkStatus::Downloading;
	}
	else if (NumCached + NumDownloading > 0)
	{
		// any progress at all? (might be paused or partially preserved from manifest update)
		return EDreamChunkStatus::Partial;
	}

	// nothing
	return EDreamChunkStatus::Remote;
}

void UDreamChunkDownloaderSubsystem::GetAllChunkIds(TArray<int32>& ChunkIds) const
{
	Chunks.GetKeys(ChunkIds);
}

void UDreamChunkDownloaderSubsystem::SetContentBuildId(const FString& DeploymentName, const FString& NewContentBuildId)
{
	// save the content build id
	ContentBuildId = NewContentBuildId;
	LastDeploymentName = DeploymentName;
	DCD_LOG(Display, TEXT("Deployment = %s, ContentBuildId = %s"), *DeploymentName, *ContentBuildId);

	// read CDN urls from deployment configs
	TArray<FString> CdnBaseUrls;
	TArray<FDreamChunkDownloaderDeploymentSet> Sets;
	Sets = UDreamChunkDownloaderSettings::Get()->DeploymentSets;
	for (const FDreamChunkDownloaderDeploymentSet& Set : Sets)
	{
		if (DeploymentName == Set.DeploymentName)
		{
			CdnBaseUrls = Set.Hosts;
			break;
		}
	}
	if (CdnBaseUrls.Num() <= 0)
	{
		DCD_LOG(Warning, TEXT("Please see the ProjectSettings DreamPlugin/Dream the Chunk Downloader Setting - > DeploymentSets and set! Count: %d"), UDreamChunkDownloaderSettings::Get()->DeploymentSets.Num());
	}

	// combine CdnBaseUrls with ContentBuildId
	BuildBaseUrls.Empty();
	for (int32 i = 0, n = CdnBaseUrls.Num(); i < n; ++i)
	{
		const FString& BaseUrl = CdnBaseUrls[i];
		check(!BaseUrl.IsEmpty());
		FString BuildUrl = BaseUrl / ContentBuildId;
		DCD_LOG(Display, TEXT("ContentBaseUrl[%d] = %s"), i, *BuildUrl);
		BuildBaseUrls.Add(BuildUrl);
	}
}

void UDreamChunkDownloaderSubsystem::LoadManifest(const TArray<FDreamPakFileEntry>& ManifestPakFiles)
{
	DCD_LOG(Display, TEXT("Beginning manifest load."));

	// wait for all mounts to finish
	WaitForMounts();

	// trigger garbage collection (give any unmounts which are about to happen a good chance of success)
	CollectGarbage(RF_NoFlags);

	// group the manifest paks by chunk ID (maintain ordering)
	TMap<int32, TArray<FDreamPakFileEntry>> Manifest;
	for (const FDreamPakFileEntry& FileEntry : ManifestPakFiles)
	{
		check(FileEntry.ChunkId >= 0);
		Manifest.FindOrAdd(FileEntry.ChunkId).Add(FileEntry);
	}

	// copy old chunk map (we will reuse any that still exist)
	TMap<int32, TSharedRef<FDreamChunk>> OldChunks = MoveTemp(Chunks);
	TMap<FString, TSharedRef<FDreamPakFile>> OldPakFiles = MoveTemp(PakFiles);

	// loop over the new chunks
	int32 NumChunks = 0, NumPaks = 0;
	for (const auto& It : Manifest)
	{
		int32 ChunkId = It.Key;

		// keep track of new chunk and old pak files
		TSharedPtr<FDreamChunk> Chunk;
		TArray<TSharedRef<FDreamPakFile>> PrevPakList;

		// create or reuse the chunk
		TSharedRef<FDreamChunk>* OldChunk = OldChunks.Find(ChunkId);
		if (OldChunk != nullptr)
		{
			// move over the old chunk
			Chunk = *OldChunk;
			check(Chunk->ChunkId == ChunkId);

			// don't clean it up later
			OldChunks.Remove(ChunkId);

			// move out OldPakFiles
			PrevPakList = MoveTemp(Chunk->PakFiles);
		}
		else
		{
			// make a brand new chunk
			Chunk = MakeShared<FDreamChunk>();
			Chunk->ChunkId = ChunkId;
		}

		// add the chunk to the new map
		Chunks.Add(Chunk->ChunkId, Chunk.ToSharedRef());

		// find or create new pak files
		check(Chunk->PakFiles.Num() == 0);
		for (const FDreamPakFileEntry& FileEntry : It.Value)
		{
			// see if there's an existing file for this one
			const TSharedRef<FDreamPakFile>* ExistingFilePtr = OldPakFiles.Find(FileEntry.FileName);
			if (ExistingFilePtr != nullptr)
			{
				const TSharedRef<FDreamPakFile>& ExistingFile = *ExistingFilePtr;
				if (ExistingFile->Entry.FileVersion == FileEntry.FileVersion)
				{
					// if version matched, size should too
					check(ExistingFile->Entry.FileSize == FileEntry.FileSize);

					// update and add to list (may populate ChunkId and RelativeUrl if we loaded from cache)
					ExistingFile->Entry = FileEntry;
					Chunk->PakFiles.Add(ExistingFile);
					PakFiles.Add(ExistingFile->Entry.FileName, ExistingFile);

					// remove from old pak files list
					OldPakFiles.Remove(FileEntry.FileName);
					continue;
				}
			}

			// create a new entry
			TSharedRef<FDreamPakFile> NewFile = MakeShared<FDreamPakFile>();
			NewFile->Entry = FileEntry;
			Chunk->PakFiles.Add(NewFile);
			PakFiles.Add(NewFile->Entry.FileName, NewFile);

			// see if it matches an embedded pak file
			const FDreamPakFileEntry* CachedEntry = EmbeddedPaks.Find(FileEntry.FileName);
			if (CachedEntry != nullptr && CachedEntry->FileVersion == FileEntry.FileVersion)
			{
				NewFile->bIsEmbedded = true;
				NewFile->bIsCached = true;
				NewFile->SizeOnDisk = CachedEntry->FileSize;
			}
		}

		// log the chunk and pak file count
		DCD_LOG(Verbose, TEXT("Found chunk %d (%d pak files)."), ChunkId, Chunk->PakFiles.Num());
		++NumChunks;
		NumPaks += Chunk->PakFiles.Num();

		// if the chunk is already mounted, we want to unmount any invalid data
		check(Chunk->MountTask == nullptr); // we already waited for mounts to finish
		if (Chunk->bIsMounted)
		{
			// see if all the existing pak files match to the new manifest (means it can stay mounted)
			// this is a common case so we're trying to be more efficient here
			int LongestCommonPrefix = 0;
			for (int i = 0; i < PrevPakList.Num() && i < Chunk->PakFiles.Num(); ++i, ++LongestCommonPrefix)
			{
				if (Chunk->PakFiles[i]->Entry.FileVersion != PrevPakList[i]->Entry.FileVersion)
				{
					break;
				}
			}

			// if they don't all match we need to remount
			if (LongestCommonPrefix != PrevPakList.Num() || LongestCommonPrefix != Chunk->PakFiles.Num())
			{
				// this chunk is no longer fully mounted
				Chunk->bIsMounted = false;

				// unmount any old paks that didn't match (reverse order)
				for (int i = PrevPakList.Num() - 1; i >= 0; --i)
				{
					UnmountPakFile(PrevPakList[i]);
				}

				// unmount any new paks that didn't match (may have changed position) (reverse order)
				// any new pak files unmounted will be re-mounted (in the right order) if this chunk is requested again
				for (int i = Chunk->PakFiles.Num() - 1; i >= 0; --i)
				{
					UnmountPakFile(Chunk->PakFiles[i]);
				}
			}
		}
	}

	// any files still left in OldPakFiles should be cancelled, unmounted, and deleted
	IFileManager& FileManager = IFileManager::Get();
	for (const auto& It : OldPakFiles)
	{
		const TSharedRef<FDreamPakFile>& File = It.Value;
		DCD_LOG(Log, TEXT("Removing orphaned pak file %s (was chunk %d)."), *File->Entry.FileName, File->Entry.ChunkId);

		// cancel downloads of pak files that are no longer valid
		if (File->Download.IsValid())
		{
			// treat these cancellations as successful since the pak is no longer needed (we've successfully downloaded nothing)
			CancelDownload(File, true);
		}

		// if a chunk completely disappeared we may need to clean up its mounts this way (otherwise would have been taken care of above)
		if (File->bIsMounted)
		{
			UnmountPakFile(File);
		}

		// delete any locally cached file
		if (File->SizeOnDisk > 0 && !File->bIsEmbedded)
		{
			bNeedsManifestSave = true;
			FString FullPathOnDisk = CacheFolder / File->Entry.FileName;
			if (!ensure(FileManager.Delete(*FullPathOnDisk)))
			{
				DCD_LOG(Error, TEXT("Failed to delete orphaned pak %s."), *FullPathOnDisk);
			}
		}
	}

	// resave the manifest
	SaveLocalManifest(false);

	// log end
	check(ManifestPakFiles.Num() == NumPaks);
	DCD_LOG(Display, TEXT("Manifest load complete. %d chunks with %d pak files."), NumChunks, NumPaks);
}

void UDreamChunkDownloaderSubsystem::TryLoadBuildManifest(int TryNumber)
{
	// load the local build manifest
	TMap<FString, FString> CachedManifestProps;
	TArray<FDreamPakFileEntry> CachedManifest = FDreamChunkDownloaderUtils::ParseManifest(CacheFolder / UDreamChunkDownloaderSettings::Get()->CachedBuildManifestFileName, &CachedManifestProps);

	// see if the BUILD_ID property matches
	if (CachedManifestProps.FindOrAdd(BUILD_ID_KEY) != ContentBuildId)
	{
		// if we have no CDN configured, we're done
		if (BuildBaseUrls.Num() <= 0)
		{
			DCD_LOG(Error, TEXT("Unable to download build manifest. No CDN urls configured."));
			LoadingModeStats.LastError = LOCTEXT("UnableToDownloadManifest", "Unable to download build manifest. (NoCDN)");

			// execute and clear the callback
			FDreamChunkDownloaderTypes::FDreamCallback Callback = MoveTemp(UpdateBuildCallback);
			ExecuteNextTick(Callback, false);
			return;
		}

		// fast path the first try
		if (TryNumber <= 0)
		{
			// download it
			TryDownloadBuildManifest(TryNumber);
			return;
		}

		// compute delay before re-starting download
		float SecondsToDelay = TryNumber * 5.0f;
		if (SecondsToDelay > 60)
		{
			SecondsToDelay = 60;
		}

		// set a ticker to delay
		DCD_LOG(Log, TEXT("Will re-attempt manifest download in %f seconds"), SecondsToDelay);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this, TryNumber](float Unused)
		{
			if (IsValid(this))
			{
				this->TryDownloadBuildManifest(TryNumber);
			}
			return false;
		}), SecondsToDelay);
		return;
	}

	// cached build manifest is up to date, load this one
	LoadManifest(CachedManifest);

	// execute and clear the callback
	FDreamChunkDownloaderTypes::FDreamCallback Callback = MoveTemp(UpdateBuildCallback);
	ExecuteNextTick(Callback, true);
}

void UDreamChunkDownloaderSubsystem::TryDownloadBuildManifest(int TryNumber)
{
	check(BuildBaseUrls.Num() > 0);

	// download the manifest from CDN, then load it
	FString ManifestFileName = FString::Printf(TEXT("BuildManifest-%s.json"), *PlatformName);
	FString Url = BuildBaseUrls[TryNumber % BuildBaseUrls.Num()] / ManifestFileName;
	DCD_LOG(Log, TEXT("Downloading build manifest (attempt #%d) from %s"), TryNumber+1, *Url);

	// download the manifest from the root CDN
	FHttpModule& HttpModule = FModuleManager::LoadModuleChecked<FHttpModule>("HTTP");
	check(!ManifestRequest.IsValid());
	ManifestRequest = HttpModule.Get().CreateRequest();
	ManifestRequest->SetURL(Url);
	ManifestRequest->SetVerb(TEXT("GET"));
	FString CachedManifestFullPath = CacheFolder / UDreamChunkDownloaderSettings::Get()->CachedBuildManifestFileName;
	ManifestRequest->OnProcessRequestComplete().BindLambda([this, TryNumber, CachedManifestFullPath](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSuccess)
	{
		// if successful, save
		FText LastError;
		if (bSuccess && HttpResponse.IsValid())
		{
			const int32 HttpStatus = HttpResponse->GetResponseCode();
			if (EHttpResponseCodes::IsOk(HttpStatus))
			{
				// Save the manifest to a file
				if (!FDreamChunkDownloaderUtils::WriteStringAsUtf8TextFile(HttpResponse->GetContentAsString(), CachedManifestFullPath))
				{
					DCD_LOG(Error, TEXT("Failed to write manifest to '%s'"), *CachedManifestFullPath);
					LastError = FText::Format(LOCTEXT("FailedToWriteManifest", "[Try {0}] Failed to write manifest."), FText::AsNumber(TryNumber));
				}
			}
			else
			{
				DCD_LOG(Error, TEXT("HTTP %d while downloading manifest from '%s'"), HttpStatus, *HttpRequest->GetURL());
				LastError = FText::Format(LOCTEXT("ManifestHttpError_FailureCode", "[Try {0}] Manifest download failed (HTTP {1})"), FText::AsNumber(TryNumber), FText::AsNumber(HttpStatus));
			}
		}
		else
		{
			DCD_LOG(Error, TEXT("HTTP connection issue while downloading manifest '%s'"), *HttpRequest->GetURL());
			LastError = FText::Format(LOCTEXT("ManifestHttpError_Generic", "[Try {0}] Connection issues downloading manifest. Check your network connection..."), FText::AsNumber(TryNumber));
		}

		// try to load it
		if (!IsValid(this))
		{
			DCD_LOG(Warning, TEXT("FChunkDownloader was destroyed while downloading manifest '%s'"), *HttpRequest->GetURL());
			return;
		}
		this->ManifestRequest.Reset();
		this->LoadingModeStats.LastError = LastError; // ok with this clearing the error on success
		this->TryLoadBuildManifest(TryNumber + 1);
	});
	ManifestRequest->ProcessRequest();
}

void UDreamChunkDownloaderSubsystem::WaitForMounts()
{
	bool bWaiting = false;

	for (const auto& It : Chunks)
	{
		const TSharedRef<FDreamChunk>& Chunk = It.Value;
		if (Chunk->MountTask != nullptr)
		{
			if (!bWaiting)
			{
				DCD_LOG(Display, TEXT("Waiting for chunk mounts to complete..."));
				bWaiting = true;
			}

			// wait for the async task to end
			Chunk->MountTask->EnsureCompletion(true);

			// complete the task on the main thread
			CompleteMountTask(*Chunk);
			check(Chunk->MountTask == nullptr);
		}
	}

	if (bWaiting)
	{
		DCD_LOG(Display, TEXT("...chunk mounts finished."));
	}
}

void UDreamChunkDownloaderSubsystem::SaveLocalManifest(bool bForce)
{
	if (bForce || bNeedsManifestSave)
	{
		// build the whole file into an FString (wish we could stream it out)
		int32 NumEntries = 0;
		for (const auto& It : PakFiles)
		{
			if (!It.Value->bIsEmbedded)
			{
				if (It.Value->SizeOnDisk > 0 || It.Value->Download.IsValid())
				{
					++NumEntries;
				}
			}
		}

		FString JsonData;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonData);
		Writer->WriteObjectStart();

		Writer->WriteValue(ENTRIES_COUNT_FIELD, NumEntries);

		Writer->WriteArrayStart(ENTRIES_FIELD);
		for (const auto& It : PakFiles)
		{
			if (!It.Value->bIsEmbedded)
			{
				if (It.Value->SizeOnDisk > 0 || It.Value->Download.IsValid())
				{
					// local manifest
					const FDreamPakFileEntry& PakFile = It.Value->Entry;
					Writer->WriteObjectStart();
					Writer->WriteValue(FILE_NAME_FIELD, PakFile.FileName);
					Writer->WriteValue(FILE_SIZE_FIELD, PakFile.FileSize);
					Writer->WriteValue(FILE_VERSION_FIELD, PakFile.FileVersion);
					Writer->WriteValue(FILE_CHUNK_ID_FIELD, -1);
					Writer->WriteValue(FILE_RELATIVE_URL_FIELD, TEXT("/"));
					Writer->WriteObjectEnd();
				}
			}
		}
		Writer->WriteArrayEnd();
		Writer->WriteObjectEnd();
		Writer->Close();

		DCD_LOG(Log, TEXT("Saved Data : %s"), *JsonData);

		FString ManifestPath = CacheFolder / UDreamChunkDownloaderSettings::Get()->LocalManifestFileName;
		if (FDreamChunkDownloaderUtils::WriteStringAsUtf8TextFile(JsonData, ManifestPath))
		{
			// mark that we have saved
			bNeedsManifestSave = false;
		}
	}
}

bool UDreamChunkDownloaderSubsystem::UpdateLoadingMode()
{
	// recompute loading stats
	ComputeLoadingStats();

	// check for the end of loading mode
	if (LoadingModeStats.FilesDownloaded >= LoadingModeStats.TotalFilesToDownload &&
		LoadingModeStats.ChunksMounted >= LoadingModeStats.TotalChunksToMount)
	{
		// make sure loading's been done for at least 2 frames before firing the callback
		// this adds a negligible amount of time to the loading screen but gives dependent loads a chance to queue
		static const int32 NUM_CONSECUTIVE_IDLE_FRAMES_FOR_LOADING_COMPLETION = 5;
		if (++LoadingCompleteLatch >= NUM_CONSECUTIVE_IDLE_FRAMES_FOR_LOADING_COMPLETION)
		{
			// end loading mode
			DCD_LOG(Log, TEXT("EndLoadingMode (%d files downloaded, %d chunks mounted)"), LoadingModeStats.FilesDownloaded, LoadingModeStats.ChunksMounted);
#if PLATFORM_ANDROID || PLATFORM_IOS
			FPlatformApplicationMisc::ControlScreensaver(FPlatformApplicationMisc::Enable);
#endif

			// fire any loading mode completion callbacks
			TArray<FDreamChunkDownloaderTypes::FDreamCallback> Callbacks = MoveTemp(PostLoadCallbacks);
			if (Callbacks.Num() > 0)
			{
				PostLoadCallbacks.Empty(); // shouldn't be necessary due to MoveTemp but just in case
				for (const auto& Callback : Callbacks)
				{
					Callback(LoadingModeStats.LastError.IsEmpty());
				}
			}
			return false; // stop ticking
		}
	}
	else
	{
		// reset the latch
		LoadingCompleteLatch = 0;
	}

	return true; // keep ticking
}

void UDreamChunkDownloaderSubsystem::ComputeLoadingStats()
{
	LoadingModeStats.TotalBytesToDownload = LoadingModeStats.BytesDownloaded;
	LoadingModeStats.TotalFilesToDownload = LoadingModeStats.FilesDownloaded;
	LoadingModeStats.TotalChunksToMount = LoadingModeStats.ChunksMounted;

	// loop over all chunks
	for (const auto& It : Chunks)
	{
		const TSharedRef<FDreamChunk>& Chunk = It.Value;

		// if it's mounting, add files to mount
		if (Chunk->MountTask != nullptr)
		{
			++LoadingModeStats.TotalChunksToMount;
		}
	}

	// check downloads
	for (const TSharedRef<FDreamPakFile>& PakFile : DownloadRequests)
	{
		++LoadingModeStats.TotalFilesToDownload;
		if (PakFile->Download.IsValid())
		{
			LoadingModeStats.TotalBytesToDownload += PakFile->Entry.FileSize - PakFile->Download->GetProgress();
		}
		else
		{
			LoadingModeStats.TotalBytesToDownload += PakFile->Entry.FileSize;
		}
	}
}

void UDreamChunkDownloaderSubsystem::UnmountPakFile(const TSharedRef<FDreamPakFile>& PakFile)
{
	// if it's already unmounted, don't do anything
	if (PakFile->bIsMounted)
	{
		// unmount
		if (ensure(FCoreDelegates::OnUnmountPak.IsBound()))
		{
			FString FullPathOnDisk = (PakFile->bIsEmbedded ? EmbeddedFolder : CacheFolder) / PakFile->Entry.FileName;
			if (ensure(FCoreDelegates::OnUnmountPak.Execute(FullPathOnDisk)))
			{
				// clear the mounted flag
				PakFile->bIsMounted = false;
			}
			else
			{
				DCD_LOG(Error, TEXT("Unable to unmount %s"), *FullPathOnDisk);
			}
		}
		else
		{
			DCD_LOG(Error, TEXT("Unable to unmount %s because no OnUnmountPak is bound"), *PakFile->Entry.FileName);
		}
	}
}

void UDreamChunkDownloaderSubsystem::CancelDownload(const TSharedRef<FDreamPakFile>& PakFile, bool bResult)
{
	if (PakFile->Download.IsValid())
	{
		// cancel the download itself
		PakFile->Download->Cancel(bResult);
		check(!PakFile->Download.IsValid());
	}
}

void UDreamChunkDownloaderSubsystem::DownloadPakFileInternal(const TSharedRef<FDreamPakFile>& PakFile, const FDreamChunkDownloaderTypes::FDreamCallback& Callback, int32 Priority)
{
	check(BuildBaseUrls.Num() > 0);

	// increase priority if it's updated
	if (Priority > PakFile->Priority)
	{
		// if the download has already started this won't really change anything
		PakFile->Priority = Priority;
	}

	// just piggyback on the existing post-download callback
	if (Callback)
	{
		PakFile->PostDownloadCallbacks.Add(Callback);
	}

	// see if the download is already started
	if (PakFile->Download.IsValid())
	{
		// nothing to do then (we already added our callback)
		return;
	}

	// add it to the downloading set
	DownloadRequests.AddUnique(PakFile);
	DownloadRequests.StableSort([](const TSharedRef<FDreamPakFile>& A, const TSharedRef<FDreamPakFile>& B)
	{
		return A->Priority < B->Priority;
	});

	// start the first N pak files in flight
	IssueDownloads();
}

void UDreamChunkDownloaderSubsystem::MountChunkInternal(FDreamChunk& Chunk, const FDreamChunkDownloaderTypes::FDreamCallback& Callback)
{
	check(!Chunk.bIsMounted);

	// see if there's already a mount pending
	if (Chunk.MountTask != nullptr)
	{
		// join with the existing callbacks
		if (Callback)
		{
			Chunk.MountTask->GetTask().PostMountCallbacks.Add(Callback);
		}
		return;
	}

	// see if we need to trigger any downloads
	bool bAllPaksCached = true;
	for (const auto& PakFile : Chunk.PakFiles)
	{
		if (!PakFile->bIsCached)
		{
			bAllPaksCached = false;
			break;
		}
	}

	if (bAllPaksCached)
	{
		// if all pak files are cached, mount now
		DCD_LOG(Log, TEXT("Chunk %d mount requested (%d pak sequence)."), Chunk.ChunkId, Chunk.PakFiles.Num());

		// spin up a background task to mount the pak file
		check(Chunk.MountTask == nullptr);
		Chunk.MountTask = new FDreamChunkDownloaderTypes::FDreamMountTask();

		// configure the task
		FDreamPakMountWork& MountWork = Chunk.MountTask->GetTask();
		MountWork.ChunkId = Chunk.ChunkId;
		MountWork.CacheFolder = CacheFolder;
		MountWork.EmbeddedFolder = EmbeddedFolder;
		for (const TSharedRef<FDreamPakFile>& PakFile : Chunk.PakFiles)
		{
			if (!PakFile->bIsMounted)
			{
				MountWork.PakFiles.Add(PakFile);
			}
		}
		if (Callback)
		{
			MountWork.PostMountCallbacks.Add(Callback);
		}

		// start as a background task
		Chunk.MountTask->StartBackgroundTask();

		// start a per-frame ticker until mounts are finished
		if (!MountTicker.IsValid())
		{
			MountTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDreamChunkDownloaderSubsystem::UpdateMountTasks));
		}
	}
	else
	{
		// queue up pak file downloads
		int32 ChunkId = Chunk.ChunkId;
		DownloadChunkInternal(Chunk, [this, ChunkId, Callback](bool bDownloadSuccess)
		{
			// if the download failed, we can't mount
			if (bDownloadSuccess)
			{
				if (IsValid(this))
				{
					// if all chunks are downloaded, do the mount again (this will pick up any changes and continue downloading if needed)
					this->MountChunk(ChunkId, Callback);
					return;
				}
			}

			// if anything went wrong, fire the callback now
			if (Callback)
			{
				Callback(false);
			}
		}, MAX_int32);
	}
}

void UDreamChunkDownloaderSubsystem::DownloadChunkInternal(const FDreamChunk& Chunk, const FDreamChunkDownloaderTypes::FDreamCallback& Callback, int32 Priority)
{
	DCD_LOG(Log, TEXT("Chunk %d download requested."), Chunk.ChunkId);

	// see if we need to download anything at all
	bool bNeedsDownload = false;
	for (const auto& PakFile : Chunk.PakFiles)
	{
		if (!PakFile->bIsCached)
		{
			bNeedsDownload = true;
			break;
		}
	}
	if (!bNeedsDownload)
	{
		ExecuteNextTick(Callback, true);
		return;
	}

	// make sure we have CDN configured
	if (BuildBaseUrls.Num() <= 0)
	{
		DCD_LOG(Error, TEXT("Unable to download Chunk %d (no CDN urls)."), Chunk.ChunkId);
		ExecuteNextTick(Callback, false);
		return;
	}

	// download all pak files that aren't already cached
	FDreamMultiCallback* MultiCallback = new FDreamMultiCallback(Callback);
	for (const auto& PakFile : Chunk.PakFiles)
	{
		if (!PakFile->bIsCached)
		{
			DownloadPakFileInternal(PakFile, MultiCallback->AddPending(), Priority);
		}
	}
	check(MultiCallback->GetNumPending() > 0);
}

void UDreamChunkDownloaderSubsystem::CompleteMountTask(FDreamChunk& Chunk)
{
	check(Chunk.MountTask != nullptr);
	check(Chunk.MountTask->IsDone());

	// increment chunks mounted
	++LoadingModeStats.ChunksMounted;

	// remove the mount
	FDreamChunkDownloaderTypes::FDreamMountTask* Mount = Chunk.MountTask;
	Chunk.MountTask = nullptr;

	// get the work
	const FDreamPakMountWork& MountWork = Mount->GetTask();

	// update bIsMounted on paks that actually succeeded
	for (const TSharedRef<FDreamPakFile>& PakFile : MountWork.MountedPakFiles)
	{
		PakFile->bIsMounted = true;
	}

	// update bIsMounted on the chunk
	bool bAllPaksMounted = true;
	for (const TSharedRef<FDreamPakFile>& PakFile : Chunk.PakFiles)
	{
		if (!PakFile->bIsMounted)
		{
			LoadingModeStats.LastError = FText::Format(LOCTEXT("FailedToMount", "Failed to mount {0}."), FText::FromString(PakFile->Entry.FileName));
			bAllPaksMounted = false;
			break;
		}
	}
	Chunk.bIsMounted = bAllPaksMounted;
	if (Chunk.bIsMounted)
	{
		DCD_LOG(Log, TEXT("Chunk %d mount succeeded."), Chunk.ChunkId);
	}
	else
	{
		DCD_LOG(Error, TEXT("Chunk %d mount failed."), Chunk.ChunkId);
	}

	// trigger the post-mount callbacks
	for (const FDreamChunkDownloaderTypes::FDreamCallback& Callback : MountWork.PostMountCallbacks)
	{
		ExecuteNextTick(Callback, bAllPaksMounted);
	}

	// also trigger the multicast event
	OnChunkMounted.Broadcast(Chunk.ChunkId, bAllPaksMounted);

	// finally delete the task
	delete Mount;

	// recompute loading stats
	ComputeLoadingStats();
}

bool UDreamChunkDownloaderSubsystem::UpdateMountTasks(float dts)
{
	bool bMountsPending = false;

	for (const auto& It : Chunks)
	{
		const TSharedRef<FDreamChunk>& Chunk = It.Value;
		if (Chunk->MountTask != nullptr)
		{
			if (Chunk->MountTask->IsDone())
			{
				// complete it
				CompleteMountTask(*Chunk);
			}
			else
			{
				// mount still pending
				bMountsPending = true;
			}
		}
	}

	if (!bMountsPending)
	{
		MountTicker.Reset();
	}
	return bMountsPending; // keep ticking
}

void UDreamChunkDownloaderSubsystem::ExecuteNextTick(const FDreamChunkDownloaderTypes::FDreamCallback& Callback, bool bSuccess)
{
	if (Callback)
	{
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Callback, bSuccess](float dts)
		{
			Callback(bSuccess);
			return false;
		}));
	}
}

void UDreamChunkDownloaderSubsystem::IssueDownloads()
{
	for (int32 i = 0; i < DownloadRequests.Num() && i < TargetDownloadsInFlight; ++i)
	{
		TSharedRef<FDreamPakFile> DownloadPakFile = DownloadRequests[i];
		if (DownloadPakFile->Download.IsValid())
		{
			// already downloading
			continue;
		}

		// log that we're starting a download
		DCD_LOG(Log, TEXT("Pak file %s download requested (%s)."),
		        *DownloadPakFile->Entry.FileName,
		        *DownloadPakFile->Entry.RelativeUrl
		);
		bNeedsManifestSave = true;

		// make a new download (platform specific)
		DownloadPakFile->Download = MakeShared<FDreamChunkDownload>(MakeShared<ThisClass*>(this), DownloadPakFile);
		DownloadPakFile->Download->Start();
	}
}

#undef LOCTEXT_NAMESPACE
