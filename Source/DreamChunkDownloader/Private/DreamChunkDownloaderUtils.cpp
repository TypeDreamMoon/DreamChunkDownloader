#include "DreamChunkDownloaderUtils.h"

#include "DreamChunkDownloaderLog.h"
#include "DreamChunkDownloaderSubsystem.h"
#include "Kismet/GameplayStatics.h"

using namespace FDreamChunkDownloaderStatics;

bool FDreamChunkDownloaderUtils::CheckFileSha1Hash(const FString& FullPathOnDisk, const FString& Sha1HashString)
{
	IFileHandle* FilePtr = IPlatformFile::GetPlatformPhysical().OpenRead(*FullPathOnDisk);
	if (FilePtr == nullptr)
	{
		DCD_LOG(Error, TEXT("Unable to open %s for hash verify."), *FullPathOnDisk);
		return false;
	}

	// create a SHA1 reader
	FSHA1 HashContext;

	// read in 64K chunks to prevent raising the memory high water mark too much
	{
		static const int64 FILE_BUFFER_SIZE = 64 * 1024;
		uint8 Buffer[FILE_BUFFER_SIZE];
		int64 FileSize = FilePtr->Size();
		for (int64 Pointer = 0; Pointer < FileSize;)
		{
			// how many bytes to read in this iteration
			int64 SizeToRead = FileSize - Pointer;
			if (SizeToRead > FILE_BUFFER_SIZE)
			{
				SizeToRead = FILE_BUFFER_SIZE;
			}

			// read dem bytes
			if (!FilePtr->Read(Buffer, SizeToRead))
			{
				DCD_LOG(Error, TEXT("Read error while validating '%s' at offset %lld."), *FullPathOnDisk, Pointer);

				// don't forget to close
				delete FilePtr;
				return false;
			}
			Pointer += SizeToRead;

			// update the hash
			HashContext.Update(Buffer, SizeToRead);
		}

		// done with the file
		delete FilePtr;
	}

	// close up shop
	HashContext.Final();
	uint8 FinalHash[FSHA1::DigestSize];
	HashContext.GetHash(FinalHash);

	// build the hash string we just computed
	FString LocalHashStr = TEXT("SHA1:");
	for (int Idx = 0; Idx < 20; Idx++)
	{
		LocalHashStr += FString::Printf(TEXT("%02X"), FinalHash[Idx]);
	}
	return Sha1HashString == LocalHashStr;
}

void FDreamChunkDownloaderUtils::DumpLoadedChunks()
{
	TSharedRef<UDreamChunkDownloaderSubsystem> ChunkDownloader = MakeShareable(GWorld->GetGameInstance()->GetSubsystem<UDreamChunkDownloaderSubsystem>());

	TArray<int32> ChunkIdList;
	ChunkDownloader->GetAllChunkIds(ChunkIdList);

	DCD_LOG(Display, TEXT("Dumping loaded chunk status\n--------------------------"));
	for (int32 ChunkId : ChunkIdList)
	{
		auto ChunkStatus = ChunkDownloader->GetChunkStatus(ChunkId);
		DCD_LOG(Display, TEXT("Chunk #%d => %s"), ChunkId, ChunkStatusToString(ChunkStatus));
	}
}

const TCHAR* FDreamChunkDownloaderUtils::ChunkStatusToString(EDreamChunkStatus Status)
{
	switch (Status)
	{
	case EDreamChunkStatus::Mounted: return TEXT("Mounted");
	case EDreamChunkStatus::Cached: return TEXT("Cached");
	case EDreamChunkStatus::Downloading: return TEXT("Downloading");
	case EDreamChunkStatus::Partial: return TEXT("Partial");
	case EDreamChunkStatus::Remote: return TEXT("Remote");
	case EDreamChunkStatus::Unknown: return TEXT("Unknown");
	default: return TEXT("Invalid");
	}
}

FString FDreamChunkDownloaderUtils::GetTargetPlatformName()
{
	FString Str = TEXT("Unknown");

#if PLATFORM_ANDROID
	Str = TEXT("Android");
#elif PLATFORM_IOS
	Str = TEXT("IOS");
#elif PLATFORM_WINDOWS
	Str = TEXT("Windows");
#elif PLATFORM_LINUX
	Str = TEXT("Linux");
#elif PLATFORM_MAC
	Str = TEXT("Mac");
#endif

	return Str;
}

TArray<FDreamPakFileEntry> FDreamChunkDownloaderUtils::ParseManifest(const FString& ManifestPath, TMap<FString, FString>* Properties)
{
	int32 ExpectedEntries = -1;
	TArray<FDreamPakFileEntry> Entries;
	FString FileStrings;
	FFileHelper::LoadFileToString(FileStrings, *ManifestPath);
	if (!FileStrings.IsEmpty())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileStrings);
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		if (FJsonSerializer::Deserialize(Reader, Object))
		{
			DCD_LOG(Log, TEXT("Deserialize Data : %s"), *FileStrings);

			for (const TPair<FString, TSharedPtr<FJsonValue>>& Value : Object->Values)
			{
				if (Value.Key == ENTRIES_COUNT_FIELD)
				{
					ExpectedEntries = Value.Value->AsNumber();
				}
				else if (Value.Key == ENTRIES_FIELD)
				{
					TArray<TSharedPtr<FJsonValue>> EntryValues = Value.Value->AsArray();
					for (const TSharedPtr<FJsonValue>& Entry : EntryValues)
					{
						int ChunkID = -1;
						FDreamPakFileEntry EntryStruct;
						EntryStruct.FileName = Entry->AsObject()->GetStringField(FILE_NAME_FIELD);
						EntryStruct.FileSize = Entry->AsObject()->GetNumberField(FILE_SIZE_FIELD);
						EntryStruct.FileVersion = Entry->AsObject()->GetStringField(FILE_VERSION_FIELD);
						EntryStruct.ChunkId = Entry->AsObject()->GetIntegerField(FILE_CHUNK_ID_FIELD);
						EntryStruct.RelativeUrl = Entry->AsObject()->GetStringField(FILE_RELATIVE_URL_FIELD);
						Entries.Add(EntryStruct);
					}
				}
				else
				{
					Properties->Add(Value.Key, Value.Value->AsString());
				}
			}
		}
	}
	else
	{
		DCD_LOG(Log, TEXT("Unable to load manifest file %s"), *ManifestPath);
	}

	if (ExpectedEntries >= 0 && ExpectedEntries != Entries.Num())
	{
		DCD_LOG(Error, TEXT("Corrupt manifest at %s (expected %d entries, got %d)"), *ManifestPath, ExpectedEntries, Entries.Num());
		Entries.Empty();
		if (Properties != nullptr)
		{
			Properties->Empty();
		}
	}

	return Entries;
}

TArray<FDreamPakFileEntry> FDreamChunkDownloaderUtils::ParseManifest(const FString& ManifestPath, TSharedPtr<FJsonObject>& JsonObject)
{
	int32 ExpectedEntries = -1;
	TArray<FDreamPakFileEntry> Entries;
	FString FileStrings;
	FFileHelper::LoadFileToString(FileStrings, *ManifestPath);
	if (!FileStrings.IsEmpty())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileStrings);
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		if (FJsonSerializer::Deserialize(Reader, Object))
		{
			DCD_LOG(Log, TEXT("Deserialize Data : %s"), *FileStrings);

			JsonObject = Object;

			for (const TPair<FString, TSharedPtr<FJsonValue>>& Value : Object->Values)
			{
				if (Value.Key == ENTRIES_COUNT_FIELD)
				{
					ExpectedEntries = Value.Value->AsNumber();
				}
				else if (Value.Key == ENTRIES_FIELD)
				{
					TArray<TSharedPtr<FJsonValue>> EntryValues = Value.Value->AsArray();
					for (const TSharedPtr<FJsonValue>& Entry : EntryValues)
					{
						int ChunkID = -1;
						FDreamPakFileEntry EntryStruct;
						EntryStruct.FileName = Entry->AsObject()->GetStringField(FILE_NAME_FIELD);
						EntryStruct.FileSize = Entry->AsObject()->GetNumberField(FILE_SIZE_FIELD);
						EntryStruct.FileVersion = Entry->AsObject()->GetStringField(FILE_VERSION_FIELD);
						EntryStruct.ChunkId = Entry->AsObject()->GetIntegerField(FILE_CHUNK_ID_FIELD);
						EntryStruct.RelativeUrl = Entry->AsObject()->GetStringField(FILE_RELATIVE_URL_FIELD);
						Entries.Add(EntryStruct);
					}
				}
			}
		}
	}
	else
	{
		DCD_LOG(Log, TEXT("Unable to load manifest file %s"), *ManifestPath);
	}

	if (ExpectedEntries >= 0 && ExpectedEntries != Entries.Num())
	{
		DCD_LOG(Error, TEXT("Corrupt manifest at %s (expected %d entries, got %d)"), *ManifestPath, ExpectedEntries, Entries.Num());
		Entries.Empty();
	}

	return Entries;
}

bool FDreamChunkDownloaderUtils::WriteStringAsUtf8TextFile(const FString& FileText, const FString& FilePath)
{
	if (FFileHelper::SaveStringToFile(FileText, *FilePath))
	{
		DCD_LOG(Log, TEXT("Wrote file %s content %s"), *FilePath, *FileText);
		return true;
	}
	else
	{
		DCD_LOG(Error, TEXT("Failed to write file %s"), *FilePath);
		return false;
	}
}
