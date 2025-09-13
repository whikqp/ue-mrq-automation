#include "MoviePipelineNativeDeferredExecutor.h"

#include "JsonObjectWrapper.h"
#include "MoviePipeline.h"
#include "MoviePipelineBlueprintLibrary.h"
#include "RenderGateWorldSubsystem.h"
#include "MoviePipelineQueue.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineCommandLineEncoder.h"
#include "LevelSequence.h"
#include "MoviePipelineDeferredPasses.h"
#include "MoviePipelineImageSequenceOutput.h"
#include "MoviePipelineGameOverrideSetting.h"
#include "ShaderCompiler.h"
#include "HAL/IConsoleManager.h"

#define LOCTEXT_NAMESPACE "MoviePipelineExecutorExt"

UMoviePipelineNativeDeferredExecutor::UMoviePipelineNativeDeferredExecutor()
{
    LastPipelineState = EMovieRenderPipelineState::Uninitialized;
    LastReportedStatus = ERenderJobStatus::queued;
    bHasSentStartingNotification = false;
    bHasSentFinalizeNotification = false;
    bHasSentExportNotification = false;
}

void UMoviePipelineNativeDeferredExecutor::InitFromCommandLineParams()
{
    FParse::Value(FCommandLine::Get(), TEXT("-JobId="), CurrentJobId);
    UE_LOG(LogTemp, Log, TEXT("%s Init CurrentJobId: %s"), ANSI_TO_TCHAR(__FUNCTION__), *CurrentJobId);

    FParse::Value(FCommandLine::Get(), TEXT("-LevelSequence="), LevelSequencePath);
	FParse::Value(FCommandLine::Get(), TEXT("-MovieQuality="), MovieQuality);
	FParse::Value(FCommandLine::Get(), TEXT("-MovieFormat="), MovieFormat);

    switch (MovieQuality)
    {

    case 0:
        RenderFrameRate = FFrameRate(24, 1);
        break;

    case 1:
        RenderFrameRate = FFrameRate(30, 1);
        break;

    case 2:
        RenderFrameRate = FFrameRate(60, 1);
        break;

    case 3:
        RenderFrameRate = FFrameRate(120, 1);
        break;

    default:
        break;;

    }

}

void UMoviePipelineNativeDeferredExecutor::CheckGameModeOverrides()
{
    UWorld* World = FindGameWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("%s: Cannot find game world!"), ANSI_TO_TCHAR(__FUNCTION__));
    }

    AGameModeBase* AuthGameMode = World->GetAuthGameMode();
    if (AuthGameMode->GetClass() == AGameModeBase::StaticClass())
    {
        UE_LOG(LogTemp, Log, TEXT("%s: The current map does not specify a custom GameMode, we manually call the SetCinematicMode function."), ANSI_TO_TCHAR(__FUNCTION__));

        APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
        if (PlayerController)
        {
            const bool bCinematicMode = true;
            const bool bHidePlayer = true;
            const bool bHideHUD = true;
            const bool bPreventMovement = true;
            const bool bPreventTurning = true;
            PlayerController->SetCinematicMode(bCinematicMode, bHidePlayer, bHideHUD, bPreventMovement, bPreventTurning);
        }
    }
    else if (AuthGameMode->GetClass()->IsChildOf(AGameModeBase::StaticClass()))
    {
        UE_LOG(LogTemp, Log, TEXT("%s: The current map specify a custom GameMode: %s."), ANSI_TO_TCHAR(__FUNCTION__), *AuthGameMode->GetClass()->GetName());
    }
}

void UMoviePipelineNativeDeferredExecutor::Execute_Implementation(UMoviePipelineQueue* InPipelineQueue)
{

	InitFromCommandLineParams();

    CheckGameModeOverrides();

	UWorld* World = FindGameWorld();

	bWaiting = true;
    bRendering = false;
    StartSeconds = FPlatformTime::Seconds();

    
    PendingQueue = NewObject<UMoviePipelineQueue>(FindGameWorld(), TEXT("PendingQueue"));
	PendingJob = PendingQueue->AllocateNewJob(UMoviePipelineExecutorJob::StaticClass());
    PendingJob->Sequence = FSoftObjectPath(LevelSequencePath);
    PendingJob->Map = FSoftObjectPath(World);

	MRQ_OutputSetting = Cast<UMoviePipelineOutputSetting>(PendingJob->GetConfiguration()->FindOrAddSettingByClass(UMoviePipelineOutputSetting::StaticClass()));
    MRQ_CommandLineEncoder = Cast<UMoviePipelineCommandLineEncoder>(PendingJob->GetConfiguration()->FindOrAddSettingByClass(UMoviePipelineCommandLineEncoder::StaticClass()));
    MRQ_GameOverrideSetting = Cast<UMoviePipelineGameOverrideSetting>(PendingJob->GetConfiguration()->FindOrAddSettingByClass(UMoviePipelineGameOverrideSetting::StaticClass()));

    ULevelSequence* LevelSequence = Cast<ULevelSequence>(PendingJob->Sequence.TryLoad());
    if (!LevelSequence)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load Sequence specified by job: %s"), *PendingJob->Sequence.ToString());
        FText FailurReason = LOCTEXT("InvalidSequenceFailureDialog", "One or more jobs in the queue has an invalid/null sequence. See log for details.");
        OnExecutorErroredImpl(nullptr, true, FailurReason);
        return;
    }

    FString SequenceName = LevelSequence->GetName();
    if (!SequenceName.IsEmpty())
    {
        UE_LOG(LogTemp, Log, TEXT("Input sequence name: %s"), *SequenceName);
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("Input sequence name is empty."));
    }

    FString RenderOutputPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MovieRenders"), SequenceName, CurrentJobId);
    if (!FPaths::DirectoryExists(RenderOutputPath))
    {
        FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*RenderOutputPath);
    }

    if (FPaths::IsRelative(RenderOutputPath))
    {
        RenderOutputPath = FPaths::ConvertRelativePathToFull(RenderOutputPath);
    }
    FPaths::NormalizeFilename(RenderOutputPath);
    FPaths::CollapseRelativeDirectories(RenderOutputPath);

    MRQ_OutputSetting->OutputDirectory.Path = RenderOutputPath;
    MRQ_OutputSetting->bUseCustomFrameRate = true;
    MRQ_OutputSetting->OutputFrameRate = RenderFrameRate;
    MRQ_OutputSetting->FileNameFormat = TEXT("{sequence_name}.{frame_number}");

    MRQ_CommandLineEncoder->Quality = static_cast<EMoviePipelineEncodeQuality>(MovieQuality);
    MRQ_CommandLineEncoder->bDeleteSourceFiles = true;

    PendingJob->GetConfiguration()->FindOrAddSettingByClass(UMoviePipelineDeferredPassBase::StaticClass());
    PendingJob->GetConfiguration()->FindOrAddSettingByClass(UMoviePipelineImageSequenceOutput_PNG::StaticClass());
    PendingJob->GetConfiguration()->InitializeTransientSettings();

    DeferredMoviePipeline = NewObject<UMoviePipeline>(World, UMoviePipeline::StaticClass());
    DeferredMoviePipeline->OnMoviePipelineWorkFinished().AddUObject(this, &UMoviePipelineNativeDeferredExecutor::CallbackOnMoviePipelineWorkFinished);
    FCoreDelegates::OnEnginePreExit.AddUObject(this, &UMoviePipelineNativeDeferredExecutor::CallbackOnEnginePreExit);

    FApp::SetUseFixedTimeStep(true);
    FApp::SetFixedDeltaTime(RenderFrameRate.AsInterval());


    WaitShaderCompilingComplete();

    PollTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UMoviePipelineNativeDeferredExecutor::PollReady),
        PollIntervalSec
    );

}

bool UMoviePipelineNativeDeferredExecutor::IsRendering_Implementation() const
{
    return bRendering || bWaiting;
}

template<typename T>
static FString EnumToString(const T EnumValue)
{
	FString Name = StaticEnum<T>()->GetNameStringByValue(static_cast<__underlying_type(T)>(EnumValue));
	check(Name.Len() != 0);
	return Name;
}

void UMoviePipelineNativeDeferredExecutor::OnBeginFrame_Implementation()
{
	EMovieRenderPipelineState PipelineState = UMoviePipelineBlueprintLibrary::GetPipelineState(DeferredMoviePipeline);
	FString PipelineStateName = EnumToString<EMovieRenderPipelineState>(PipelineState);
	UE_LOG(LogTemp, Warning, TEXT("%s MoviePipelineState: %s"), ANSI_TO_TCHAR(__FUNCTION__), *PipelineStateName);

	switch (PipelineState)
	{
		case EMovieRenderPipelineState::Uninitialized:
		{
			if (!bHasSentStartingNotification)
			{
				SendStatusNotification(ERenderJobStatus::starting, 0.0f);
				bHasSentStartingNotification = true;
			}
			break;
		}
		case EMovieRenderPipelineState::ProducingFrames:
		{
			// Throttled progress update to limit HTTP calls.
			if (DeferredMoviePipeline)
			{
				int32 CurrentFrameIndex = 0;
				int32 TotalFrames = 1;
				UMoviePipelineBlueprintLibrary::GetOverallOutputFrames(Cast<UMoviePipeline>(DeferredMoviePipeline), CurrentFrameIndex, TotalFrames);
				const float CompletionPercentage = FMath::Clamp(TotalFrames > 0 ? (CurrentFrameIndex / (float)TotalFrames) : 0.f, 0.f, 1.f);
				MaybeSendProgressUpdate(CompletionPercentage);
			}
			break;	
		}
		case EMovieRenderPipelineState::Finalize:
		{
			if (!bHasSentFinalizeNotification)
			{
				SendStatusNotification(ERenderJobStatus::encoding, 1.0f);
				bHasSentFinalizeNotification = true;
			}
			break;	
		}
		case EMovieRenderPipelineState::Export:
		{
			if (!bHasSentExportNotification)
			{
				SendStatusNotification(ERenderJobStatus::encoding, 1.0f);
				bHasSentExportNotification = true;
			}
			break;	
		}
		case EMovieRenderPipelineState::Finished:
		{
			break;	
		}
		default:
			break;
	}
}

void UMoviePipelineNativeDeferredExecutor::RequestForJobInfo(const FString& JobId)
{
}

void UMoviePipelineNativeDeferredExecutor::OnReceiveJobInfo(int32 RequestIndex, int32 ResponseCode, const FString& Message)
{
}

void UMoviePipelineNativeDeferredExecutor::CallbackOnEnginePreExit()
{
	if (DeferredMoviePipeline && DeferredMoviePipeline->GetPipelineState() != EMovieRenderPipelineState::Finished)
	{
		UE_LOG(LogTemp, Log, TEXT("%s Application quit while Movie Pipeline was still active. Stalling to do full shutdown."), ANSI_TO_TCHAR(__FUNCTION__));
		DeferredMoviePipeline->RequestShutdown();
		DeferredMoviePipeline->Shutdown();

		UE_LOG(LogTemp, Log, TEXT("%s Stalling finished, pipeline has shut down."), ANSI_TO_TCHAR(__FUNCTION__));
	}
}

bool UMoviePipelineNativeDeferredExecutor::PollReady(float DeltaTime)
{
    if (!bWaiting || !PendingQueue)
        return false; // Stop Ticker

    const double Elapsed = FPlatformTime::Seconds() - StartSeconds;
    UE_LOG(LogTemp, Warning, TEXT("[MRQ] %s Poll for rendering, Elapsed: %.0f s; TimeoutSec: %.0f s"), ANSI_TO_TCHAR(__FUNCTION__), Elapsed, TimeoutSec);
    bool bCanStart = false;

    // 1) Find world（-game）
    UWorld* World = FindGameWorld();
    if (World)
    {
        UE_LOG(LogTemp, Log, TEXT("[MRQ] GameWorld: %s"), *World->GetName());

        // 2) Check the level: BeginPlay
        if (!World->HasBegunPlay())
        {
        	UE_LOG(LogTemp, Log, TEXT("[MRQ] Game world: %s has not begun play yet."), *World->GetName());
			return true; // Keep Poll
        }

    	// 3) Waiting for data sync, level generation.
    	if (URenderGateWorldSubsystem* Gate = World->GetSubsystem<URenderGateWorldSubsystem>())
    	{
    		if (Gate->IsReady())
    		{
    			bCanStart = true;
    		}
    		else
    		{
    			if (Gate != BoundGate.Get())
    			{
    				if (BoundGate.IsValid() && OnReadyHandle.IsValid())
    				{
    					BoundGate->OnReadyEvent().Remove(OnReadyHandle);
    					OnReadyHandle.Reset();
    				}
    				BoundGate = Gate;
    				bGateDelegateBound = false;
    			}

    			if (!bGateDelegateBound)
    			{
    				OnReadyHandle = Gate->OnReadyEvent()
						.AddUObject(this, &UMoviePipelineNativeDeferredExecutor::StartRenderNow);
    				bGateDelegateBound = true;
    			}
    		}
    	}
    }
	
    // —— Unified timeout processing exit ——
    if (bCanStart || Elapsed >= TimeoutSec)
    {
        if (!bCanStart)
        {
            UE_LOG(LogTemp, Warning, TEXT("[MRQ] Wait timed out: %.1fs/%.1fs, start anyway."), Elapsed, TimeoutSec);
        }

        StartRenderNow();
        return false; // Stop Ticker
    }

    UE_LOG(LogTemp, Log, TEXT("[MRQ] Check if the Level content is ready, waiting... %.0f s; Exceeding the threshold %.0f s will automatically enter the video rendering process."), Elapsed, TimeoutSec);
    return true; // Continue polling
}

void UMoviePipelineNativeDeferredExecutor::StartRenderNow()
{
    if (!bWaiting || bRendering || !PendingQueue)
        return;

    bWaiting = false;
    bRendering = true;

    DeferredMoviePipeline->Initialize(PendingJob);

    // Progress updates are now handled in OnBeginFrame with throttling.
}

void UMoviePipelineNativeDeferredExecutor::SendProgressUpdate(const FString& JobId, float Progress, ERenderJobStatus Status)
{
    FHttpModule* Http = &FHttpModule::Get();
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http->CreateRequest();

    bProgressRequestInFlight = true;
    Request->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr /*Request*/, FHttpResponsePtr /*Response*/, bool /*bSuccess*/)
    {
        bProgressRequestInFlight = false;
        if (bHasPendingProgress)
        {
            bHasPendingProgress = false;
            const float Latest = PendingProgressValue;
            PendingProgressValue = -1.0f;
            MaybeSendProgressUpdate(Latest);
        }
    });

    Request->SetURL(FString::Printf(TEXT("http://127.0.0.1:8080/ue-notifications/job/%s/progress"), *JobId));
	Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

    TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

    JsonObject->SetNumberField("progress_percent", Progress);
    JsonObject->SetStringField("status", GetStatusString(Status));

    // Only calculate ETA if we have a valid pipeline and are in rendering state
    if (DeferredMoviePipeline && Status == ERenderJobStatus::rendering)
    {
        FTimespan OutEstimate;
        if (UMoviePipelineBlueprintLibrary::GetEstimatedTimeRemaining(DeferredMoviePipeline, OutEstimate))
        {
            JsonObject->SetNumberField("progress_eta_seconds", OutEstimate.GetSeconds());
        }
        else
        {
            JsonObject->SetNumberField("progress_eta_seconds", -1);
        }
    }
    else
    {
        JsonObject->SetNumberField("progress_eta_seconds", -1);
    }

    FString JsonStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	Request->SetContentAsString(JsonStr);
	Request->ProcessRequest();
}

// Throttled progress reporting to avoid excessive HTTP calls.
void UMoviePipelineNativeDeferredExecutor::MaybeSendProgressUpdate(float Progress)
{
    // Clamp to [0,1]
    Progress = FMath::Clamp(Progress, 0.0f, 1.0f);

    // Allow console override of thresholds (optional)
    if (IConsoleVariable* CVarInterval = IConsoleManager::Get().FindConsoleVariable(TEXT("mrq.progress.MinIntervalSec")))
    {
        ProgressUpdateMinIntervalSec = FMath::Max(0.0f, CVarInterval->GetFloat());
    }
    if (IConsoleVariable* CVarDelta = IConsoleManager::Get().FindConsoleVariable(TEXT("mrq.progress.MinDelta")))
    {
        ProgressUpdateMinDelta = FMath::Clamp(CVarDelta->GetFloat(), 0.0f, 1.0f);
    }

    const double NowSec = FPlatformTime::Seconds();
    const bool bFirst = (LastProgressSentTimeSec < 0.0) || (LastProgressSentValue < 0.0f);
    const float Delta = (LastProgressSentValue < 0.0f) ? 1.0f : FMath::Abs(Progress - LastProgressSentValue);
    const double Elapsed = bFirst ? DBL_MAX : (NowSec - LastProgressSentTimeSec);

    const bool bAtEnd = FMath::IsNearlyEqual(Progress, 1.0f, 0.0001f);
    const bool bTimeOk = Elapsed >= ProgressUpdateMinIntervalSec;
    const bool bDeltaOk = Delta >= ProgressUpdateMinDelta;

    if (!bFirst && !bAtEnd && !bTimeOk && !bDeltaOk)
    {
        // Not enough change/time to justify an update.
        return;
    }

    if (bProgressRequestInFlight)
    {
        // Coalesce: remember the most recent progress and send when in-flight request completes.
        bHasPendingProgress = true;
        PendingProgressValue = Progress;
        return;
    }

    LastProgressSentTimeSec = NowSec;
    LastProgressSentValue = Progress;
    SendProgressUpdate(CurrentJobId, Progress, ERenderJobStatus::rendering);
}

void UMoviePipelineNativeDeferredExecutor::SendStatusNotification(ERenderJobStatus Status, float Progress)
{
    // Avoid sending duplicate status notifications
    if (Status == LastReportedStatus)
    {
        return;
    }
    
    SendProgressUpdate(CurrentJobId, Progress, Status);
    LastReportedStatus = Status;
    
    UE_LOG(LogTemp, Log, TEXT("%s: Sent status notification: %s (%.1f%%)"), ANSI_TO_TCHAR(__FUNCTION__), *GetStatusString(Status), Progress);
}

FString UMoviePipelineNativeDeferredExecutor::GetStatusString(ERenderJobStatus Status) const
{
    switch (Status)
    {
    case ERenderJobStatus::queued:
        return TEXT("queued");
    case ERenderJobStatus::starting:
        return TEXT("starting");
    case ERenderJobStatus::rendering:
        return TEXT("rendering");
    case ERenderJobStatus::encoding:
    	return TEXT("encoding");
    case ERenderJobStatus::completed:
        return TEXT("completed");
    case ERenderJobStatus::failed:
        return TEXT("failed");
    case ERenderJobStatus::canceled:
        return TEXT("canceled");

    default:
        return TEXT("failed");
    }
}

void UMoviePipelineNativeDeferredExecutor::CallbackOnExecutorFinished(UMoviePipelineExecutorBase* PipelineExecutor,
    bool bSuccess)
{
}

void UMoviePipelineNativeDeferredExecutor::CallbackOnMoviePipelineWorkFinished(FMoviePipelineOutputData MoviePipelineOutputData)
{
	SendHttpOnMoviePipelineWorkFinished(MoviePipelineOutputData);
	
	if (PollTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PollTickerHandle);
		PollTickerHandle.Reset();
	}

	if (ProgressTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ProgressTickerHandle);
		ProgressTickerHandle.Reset();
	}

    OnExecutorFinishedImpl();
}

void UMoviePipelineNativeDeferredExecutor::OnExecutorFinishedImpl()
{
    UE_LOG(LogTemp, Log, TEXT("%s"), ANSI_TO_TCHAR(__FUNCTION__));
    bRendering = false;
    Super::OnExecutorFinishedImpl();
}

void UMoviePipelineNativeDeferredExecutor::SendHttpOnMoviePipelineWorkFinished(
    const FMoviePipelineOutputData& MoviePipelineOutputData)
{
	bool bSuccess = MoviePipelineOutputData.bSuccess;

	const FString InURL = FString::Printf(TEXT("http://127.0.0.1:8080/ue-notifications/job/%s/render-complete"), *CurrentJobId);
	const FString InVerb = TEXT("POST");
	FString InMessage;
	FJsonObjectWrapper JsonObjectWrapper;
	JsonObjectWrapper.JsonObject.Get()->SetBoolField(TEXT("movie_pipeline_success"), bSuccess);

    FString VideoOutputDir = (FPaths::IsRelative(MRQ_OutputSetting->OutputDirectory.Path)) ? FPaths::ConvertRelativePathToFull(MRQ_OutputSetting->OutputDirectory.Path) : MRQ_OutputSetting->OutputDirectory.Path;
	JsonObjectWrapper.JsonObject.Get()->SetStringField(TEXT("video_directory"), VideoOutputDir);
	JsonObjectWrapper.JsonObjectToString(InMessage);

	TMap<FString, FString> InHeaders;
	InHeaders.Add(TEXT("Content-Type"), TEXT("application/json"));
	int32 RequestIndex = SendHTTPRequest(InURL, InVerb, InMessage, InHeaders);
}

void UMoviePipelineNativeDeferredExecutor::WaitShaderCompilingComplete()
{
    if (GShaderCompilingManager)
    {
	    while (GShaderCompilingManager->IsCompiling())
	    {
            GShaderCompilingManager->ProcessAsyncResults(false, false);
            FPlatformProcess::Sleep(0.5f);
            GLog->Flush();

            UE_LOG(LogTemp, Warning, TEXT("%s: Wait shader compile complete..."), ANSI_TO_TCHAR(__FUNCTION__));
	    }

        GShaderCompilingManager->ProcessAsyncResults(false, true);
        GShaderCompilingManager->FinishAllCompilation();
        UE_LOG(LogTemp, Warning, TEXT("%s: GShaderCompilingManager->FinishAllCompilation"), ANSI_TO_TCHAR(__FUNCTION__));
    }
}

UWorld* UMoviePipelineNativeDeferredExecutor::FindGameWorld() const
{
    if (!GEngine) return nullptr;

	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		if (Ctx.World() && Ctx.WorldType == EWorldType::Game)
		{
			return Ctx.World();
		}
	}

    return nullptr;
}


#undef LOCTEXT_NAMESPACE // "MoviePipelineExecutorExt"
