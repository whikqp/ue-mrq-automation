#pragma once

#include "CoreMinimal.h"
#include "MoviePipelineExecutor.h"
#include "MoviePipelineNativeDeferredExecutor.generated.h"

class URenderGateWorldSubsystem;
class UMoviePipelineBase;
class UMoviePipelineOutputSetting;
class UMoviePipelineCommandLineEncoder;
class UMoviePipelineGameOverrideSetting;

// Render job status enumeration for server communication
UENUM(BlueprintType)
enum class ERenderJobStatus : uint8
{
	queued,
	starting,
	rendering,
	encoding,
	uploading,
	completed,
	failed,
	canceling,
	canceled
};

/**
 * 
 */
UCLASS()
class MOVIEPIPELINEEXT_API UMoviePipelineNativeDeferredExecutor : public UMoviePipelineExecutorBase
{
	GENERATED_BODY()

public:
	UMoviePipelineNativeDeferredExecutor();

	virtual void Execute_Implementation(UMoviePipelineQueue* InPipelineQueue) override;

	virtual bool IsRendering_Implementation() const override;

	virtual void OnBeginFrame_Implementation() override;

	virtual void OnExecutorFinishedImpl() override;

	UWorld* FindGameWorld() const;

private:
	void InitFromCommandLineParams();

	void CheckGameModeOverrides();

	void RequestForJobInfo(const FString& JobId);

	UFUNCTION()
	void OnReceiveJobInfo(int32 RequestIndex, int32 ResponseCode, const FString& Message);

	void CallbackOnEnginePreExit();

	bool PollReady(float DeltaTime);

	void StartRenderNow();

	void SendProgressUpdate(const FString& JobId, float Progress, ERenderJobStatus Status = ERenderJobStatus::rendering);

	// Rate-limited progress updates
	void MaybeSendProgressUpdate(float Progress);

	void SendStatusNotification(ERenderJobStatus Status, float Progress = 0.0f);

	FString GetStatusString(ERenderJobStatus Status) const;

	void CallbackOnExecutorFinished(UMoviePipelineExecutorBase* PipelineExecutor, bool bSuccess);

	void CallbackOnMoviePipelineWorkFinished(FMoviePipelineOutputData MoviePipelineOutputData);

	void SendHttpOnMoviePipelineWorkFinished(const FMoviePipelineOutputData& MoviePipelineOutputData);

	void WaitShaderCompilingComplete();

private:
	UPROPERTY()
	UMoviePipeline* DeferredMoviePipeline = nullptr;

	UPROPERTY()
	UMoviePipelineQueue* PendingQueue = nullptr;

	UPROPERTY()
	UMoviePipelineExecutorJob* PendingJob = nullptr;

	UPROPERTY()
	UMoviePipelineOutputSetting* MRQ_OutputSetting = nullptr;

	UPROPERTY()
	UMoviePipelineCommandLineEncoder* MRQ_CommandLineEncoder = nullptr;

	UPROPERTY()
	UMoviePipelineGameOverrideSetting* MRQ_GameOverrideSetting = nullptr;


	// {"LOW": 0, "MEDIUM": 1, "HIGH": 2, "EPIC": 3}
	int32 MovieQuality = 1;

	FFrameRate RenderFrameRate = FFrameRate(30, 1);

	FString MovieFormat;
	FString CurrentJobId;
	FString LevelSequencePath;

	bool bWaiting = false;
	bool bRendering = false;

	float TimeoutSec = 120.f; // Waiting for scene data synchronization
	float PollIntervalSec = 1.f;

	double StartSeconds = 0.0;
	FTSTicker::FDelegateHandle PollTickerHandle;

	TWeakObjectPtr<URenderGateWorldSubsystem> BoundGate;
	FDelegateHandle OnReadyHandle;
	bool bGateDelegateBound = false;

	FTSTicker::FDelegateHandle ProgressTickerHandle;

	// State tracking for optimized status notifications
	EMovieRenderPipelineState LastPipelineState = EMovieRenderPipelineState::Uninitialized;
	ERenderJobStatus LastReportedStatus = ERenderJobStatus::queued;
	bool bHasSentStartingNotification = false;
	bool bHasSentFinalizeNotification = false;
	bool bHasSentExportNotification = false;

	// Throttling configuration and state
	// Minimum seconds between HTTP progress updates
	float ProgressUpdateMinIntervalSec = 0.75f;
	// Minimum change in progress (0..1) to trigger an update
	float ProgressUpdateMinDelta = 0.01f; // 1%
	// Last time and value sent
	double LastProgressSentTimeSec = -1.0;
	float LastProgressSentValue = -1.0f;
	// Whether an HTTP request is currently in flight
	bool bProgressRequestInFlight = false;
	// If a request is in flight, remember the latest progress to send when possible
	bool bHasPendingProgress = false;
	float PendingProgressValue = -1.0f;
};
