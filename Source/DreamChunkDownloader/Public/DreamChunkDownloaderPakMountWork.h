#pragma once

#include "CoreMinimal.h"
#include "DreamChunkDownloaderTypes.h"

struct FDreamPakFile;

class FDreamPakMountWork : public FNonAbandonableTask
{
public:
	friend class FAsyncTask<FDreamPakMountWork>;

	void DoWork();

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FPakMountWork, STATGROUP_ThreadPoolAsyncTasks);
	}

public: // inputs

	int32 ChunkId;

	// folders to save pak files into on disk
	FString CacheFolder;
	FString EmbeddedFolder;

	// mount these IN ORDER
	TArray<TSharedRef<FDreamPakFile>> PakFiles;

	// callbacks
	TArray<FDreamChunkDownloaderTypes::FDreamCallback> PostMountCallbacks;

public: // results

	// files which were successfully mounted
	TArray<TSharedRef<FDreamPakFile>> MountedPakFiles;
};
