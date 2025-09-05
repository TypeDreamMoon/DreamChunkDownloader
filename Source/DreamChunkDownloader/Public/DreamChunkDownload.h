// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DreamChunkDownloaderSubsystem.h"
#include "DreamChunkDownloaderPlatformStreamDownload.h"

class FDreamChunkDownload : public TSharedFromThis<FDreamChunkDownload>
{
public:
	FDreamChunkDownload(const TWeakObjectPtr<UDreamChunkDownloaderSubsystem>& DownloaderIn, const TSharedRef<FDreamPakFile>& PakFileIn);
	virtual ~FDreamChunkDownload();

	inline bool HasCompleted() const { return bHasCompleted; }
	inline int32 GetProgress() const { return LastBytesReceived; }

	void Start();
	void Cancel(bool bResult);

public:
	const TWeakObjectPtr<UDreamChunkDownloaderSubsystem> Downloader;
	const TSharedRef<FDreamPakFile> PakFile;
	const FString TargetFile;

protected:
	void UpdateFileSize();
	bool ValidateFile() const;
	bool HasDeviceSpaceRequired() const;
	void StartDownload(int TryNumber);
	void OnDownloadProgress(int32 BytesReceived);
	void OnDownloadComplete(const FString& Url, int TryNumber, int32 HttpStatus);
	void OnCompleted(bool bSuccess, const FText& ErrorText);

private:
	bool bIsCancelled = false;
	FDreamDownloadCancel CancelCallback;
	bool bHasCompleted = false;
	FDateTime BeginTime;
	int32 LastBytesReceived = 0;
};
