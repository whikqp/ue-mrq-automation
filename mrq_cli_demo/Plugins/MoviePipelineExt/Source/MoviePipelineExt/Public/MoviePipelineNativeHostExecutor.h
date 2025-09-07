
#pragma once

#include "CoreMinimal.h"
#include "MoviePipelineExecutor.h"
#include "MoviePipelineNativeHostExecutor.generated.h"

class URenderGateWorldSubsystem;
class UMoviePipelineBase;
class UMoviePipelineOutputSetting;
class UMoviePipelineCommandLineEncoder;
class UMoviePipelineGameOverrideSetting;

/**
 * 
 */
UCLASS()
class MOVIEPIPELINEEXT_API UMoviePipelineNativeHostExecutor : public UMoviePipelineExecutorBase
{
	GENERATED_BODY()

public:
	UMoviePipelineNativeHostExecutor();

	virtual void Execute_Implementation(UMoviePipelineQueue* InPipelineQueue) override;

	virtual bool IsRendering_Implementation() const override;

	virtual void OnExecutorFinishedImpl() override;

	UWorld* FindGameWorld() const;

private:
	void InitFromCommandLineParams();

	void RequestForJobInfo(const FString& JobId);

	UFUNCTION()
	void OnReceiveJobInfo(int32 RequestIndex, int32 ResponseCode, const FString& Message);

	void CallbackOnEnginePreExit();
	
	bool PollReady(float DeltaTime);

	void StartRenderNow();

	bool UpdateProgress(float DeltaTime);

	void SendProgressUpdate(const FString& JobId, float Progress);

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
	FString LevelPath;
	
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
};
