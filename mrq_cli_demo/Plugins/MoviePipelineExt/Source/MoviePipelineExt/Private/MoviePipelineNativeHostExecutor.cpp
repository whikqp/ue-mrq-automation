#include "MoviePipelineNativeHostExecutor.h"

#include "JsonObjectWrapper.h"
#include "MoviePipeline.h"
#include "RenderGateWorldSubsystem.h"
#include "MoviePipelineQueue.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineCommandLineEncoder.h"
#include "LevelSequence.h"
#include "MoviePipelineDeferredPasses.h"
#include "MoviePipelineImageSequenceOutput.h"
#include "MoviePipelineGameOverrideSetting.h"
#include "ShaderCompiler.h"

#define LOCTEXT_NAMESPACE "MoviePipelineExecutorExt"

UMoviePipelineNativeHostExecutor::UMoviePipelineNativeHostExecutor(){}

void UMoviePipelineNativeHostExecutor::InitFromCommandLineParams()
{
    FParse::Value(FCommandLine::Get(), TEXT("-JobId="), CurrentJobId);
    UE_LOG(LogTemp, Log, TEXT("%s Init CurrentJobId: %s"), UTF8_TO_TCHAR(__FUNCTION__), *CurrentJobId);

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

void UMoviePipelineNativeHostExecutor::CheckGameModeOverrides()
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

void UMoviePipelineNativeHostExecutor::Execute_Implementation(UMoviePipelineQueue* InPipelineQueue)
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
    DeferredMoviePipeline->OnMoviePipelineWorkFinished().AddUObject(this, &UMoviePipelineNativeHostExecutor::CallbackOnMoviePipelineWorkFinished);
    FCoreDelegates::OnEnginePreExit.AddUObject(this, &UMoviePipelineNativeHostExecutor::CallbackOnEnginePreExit);

    FApp::SetUseFixedTimeStep(true);
    FApp::SetFixedDeltaTime(RenderFrameRate.AsInterval());


    WaitShaderCompilingComplete();

    PollTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UMoviePipelineNativeHostExecutor::PollReady),
        PollIntervalSec
    );

}

bool UMoviePipelineNativeHostExecutor::IsRendering_Implementation() const
{
    return bRendering || bWaiting;
}

void UMoviePipelineNativeHostExecutor::RequestForJobInfo(const FString& JobId)
{
}

void UMoviePipelineNativeHostExecutor::OnReceiveJobInfo(int32 RequestIndex, int32 ResponseCode, const FString& Message)
{
}

void UMoviePipelineNativeHostExecutor::CallbackOnEnginePreExit()
{
	if (DeferredMoviePipeline && DeferredMoviePipeline->GetPipelineState() != EMovieRenderPipelineState::Finished)
	{
		UE_LOG(LogTemp, Log, TEXT("%s Application quit while Movie Pipeline was still active. Stalling to do full shutdown."), UTF8_TO_TCHAR(__FUNCTION__));
		DeferredMoviePipeline->RequestShutdown();
		DeferredMoviePipeline->Shutdown();

		UE_LOG(LogTemp, Log, TEXT("%s Stalling finished, pipeline has shut down."), UTF8_TO_TCHAR(__FUNCTION__));
	}
}

bool UMoviePipelineNativeHostExecutor::PollReady(float DeltaTime)
{
    if (!bWaiting || !PendingQueue)
        return false; // Stop Ticker

    const double Elapsed = FPlatformTime::Seconds() - StartSeconds;
    UE_LOG(LogTemp, Warning, TEXT("[MRQ] %s Poll for rendering, Elapsed: %.0f s; TimeoutSec: %.0f s"), UTF8_TO_TCHAR(__FUNCTION__), Elapsed, TimeoutSec);
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
						.AddUObject(this, &UMoviePipelineNativeHostExecutor::StartRenderNow);
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

void UMoviePipelineNativeHostExecutor::StartRenderNow()
{
    if (!bWaiting || bRendering || !PendingQueue)
        return;

    bWaiting = false;
    bRendering = true;

    DeferredMoviePipeline->Initialize(PendingJob);

    ProgressTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UMoviePipelineNativeHostExecutor::UpdateProgress),
        0.1f
    );
}

bool UMoviePipelineNativeHostExecutor::UpdateProgress(float DeltaTime)
{
    /**
	 * return false Stop Ticker
	 * return true  Continue Ticker
     */

    if (!PendingQueue)
        return false;

	int32 CurrentFrameIndex = 0;
	int32 TotalFrames = 1;

	UMoviePipelineBlueprintLibrary::GetOverallOutputFrames(Cast<UMoviePipeline>(DeferredMoviePipeline), CurrentFrameIndex, TotalFrames);
	
    float CompletionPercentage = FMath::Clamp(CurrentFrameIndex / (float)TotalFrames, 0.f, 1.f);
	UE_LOG(LogTemp, Log, TEXT("[MRQ] Rendering Progress: %.1f%%"), CompletionPercentage * 100.f);

    SendProgressUpdate(CurrentJobId, CompletionPercentage);

    if (FMath::IsNearlyEqual(CompletionPercentage, 1.0), 0.01)
    {
        UE_LOG(LogTemp, Log, TEXT("[MRQ] Completion Percentage is 100%%."));
        if (ProgressTickerHandle.IsValid())
        {
			FTSTicker::GetCoreTicker().RemoveTicker(ProgressTickerHandle);
			ProgressTickerHandle.Reset();
        }

        return false;
    }

    return true;
}

void UMoviePipelineNativeHostExecutor::SendProgressUpdate(const FString& JobId, float Progress)
{
    FHttpModule* Http = &FHttpModule::Get();
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http->CreateRequest();

    Request->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
    {
	    // Handle the response here
    });

    Request->SetURL(FString::Printf(TEXT("http://127.0.0.1:8080/ue-notifications/job/%s/progress"), *JobId));
	Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

    TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

    JsonObject->SetNumberField("progress_percent", Progress);
    JsonObject->SetStringField("status", "rendering");

    FTimespan OutEstimate;
    if (UMoviePipelineBlueprintLibrary::GetEstimatedTimeRemaining(DeferredMoviePipeline, OutEstimate))
    {
        JsonObject->SetNumberField("progress_eta_seconds", OutEstimate.GetSeconds());
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

void UMoviePipelineNativeHostExecutor::CallbackOnExecutorFinished(UMoviePipelineExecutorBase* PipelineExecutor,
    bool bSuccess)
{
}

void UMoviePipelineNativeHostExecutor::CallbackOnMoviePipelineWorkFinished(FMoviePipelineOutputData MoviePipelineOutputData)
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

void UMoviePipelineNativeHostExecutor::OnExecutorFinishedImpl()
{
    UE_LOG(LogTemp, Log, TEXT("%s OnExecutorFinishedImpl"), UTF8_TO_TCHAR(__FUNCTION__));
    bRendering = false;
    Super::OnExecutorFinishedImpl();
}

void UMoviePipelineNativeHostExecutor::SendHttpOnMoviePipelineWorkFinished(
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

	OnExecutorFinishedImpl();
}

void UMoviePipelineNativeHostExecutor::WaitShaderCompilingComplete()
{
    if (GShaderCompilingManager)
    {
	    while (GShaderCompilingManager->IsCompiling())
	    {
            GShaderCompilingManager->ProcessAsyncResults(false, false);
            FPlatformProcess::Sleep(0.5f);
            GLog->Flush();

            UE_LOG(LogTemp, Warning, TEXT("%s: Wait shader compile complete..."), UTF8_TO_TCHAR(__FUNCTION__));
	    }

        GShaderCompilingManager->ProcessAsyncResults(false, true);
        GShaderCompilingManager->FinishAllCompilation();
        UE_LOG(LogTemp, Warning, TEXT("%s: GShaderCompilingManager->FinishAllCompilation"), UTF8_TO_TCHAR(__FUNCTION__));
    }
}

UWorld* UMoviePipelineNativeHostExecutor::FindGameWorld() const
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