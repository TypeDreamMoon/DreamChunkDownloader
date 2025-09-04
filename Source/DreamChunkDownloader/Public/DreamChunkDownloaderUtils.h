#pragma once

#include "CoreMinimal.h"

struct FDreamPakFileEntry;
enum class EDreamChunkStatus : uint8;

struct DREAMCHUNKDOWNLOADER_API FDreamChunkDownloaderUtils
{
public:
	static bool CheckFileSha1Hash(const FString& FullPathOnDisk, const FString& Sha1HashString);
	static void DumpLoadedChunks();
	static const TCHAR* ChunkStatusToString(EDreamChunkStatus Status);
	static FString GetTargetPlatformName();
	static TArray<FDreamPakFileEntry> ParseManifest(const FString& ManifestPath, TMap<FString, FString>* Properties = nullptr);
	static bool WriteStringAsUtf8TextFile(const FString& FileText, const FString& FilePath);
};
