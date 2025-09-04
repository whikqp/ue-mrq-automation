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

#define LOCTEXT_NAMESPACE "MoviePipelineExecutorExt"

UMoviePipelineNativeHostExecutor::UMoviePipelineNativeHostExecutor(){}

void UMoviePipelineNativeHostExecutor::InitFromCommandLineParams()
{
    FParse::Value(FCommandLine::Get(), TEXT("-JobId="), CurrentJobId);
    UE_LOG(LogTemp, Log, TEXT("[MRQ] %s Init CurrentJobId: %s"), UTF8_TO_TCHAR(__FUNCTION__), *CurrentJobId);

    FParse::Value(FCommandLine::Get(), TEXT("-LevelSequence="), LevelSequencePath);
	FParse::Value(FCommandLine::Get(), TEXT("-MovieQuality="), MovieQuality);
	FParse::Value(FCommandLine::Get(), TEXT("-MovieFormat="), MovieFormat);

}

void UMoviePipelineNativeHostExecutor::Execute_Implementation(UMoviePipelineQueue* InPipelineQueue)
{
	InitFromCommandLineParams();

	UWorld* World = FindGameWorld();

	bWaiting = true;
    bRendering = false;
    StartSeconds = FPlatformTime::Seconds();

    PollTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UMoviePipelineNativeHostExecutor::PollReady),
        PollIntervalSec
    );

    PendingQueue = NewObject<UMoviePipelineQueue>(FindGameWorld(), TEXT("PendingQueue"));
	PendingJob = PendingQueue->AllocateNewJob(UMoviePipelineExecutorJob::StaticClass());
    PendingJob->Sequence = FSoftObjectPath(LevelSequencePath);
    PendingJob->Map = FSoftObjectPath(World);

	MRQ_OutputSetting = Cast<UMoviePipelineOutputSetting>(PendingJob->GetConfiguration()->FindOrAddSettingByClass(UMoviePipelineOutputSetting::StaticClass()));
    MRQ_CommandLineEncoder = Cast<UMoviePipelineCommandLineEncoder>(PendingJob->GetConfiguration()->FindOrAddSettingByClass(UMoviePipelineCommandLineEncoder::StaticClass()));

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

    MRQ_OutputSetting->OutputDirectory.Path = RenderOutputPath;
    MRQ_OutputSetting->OutputFrameRate = FFrameRate(30, 1);
    MRQ_OutputSetting->FileNameFormat = TEXT("{sequence_name}.{frame_number}");

    MRQ_CommandLineEncoder->Quality = static_cast<EMoviePipelineEncodeQuality>(MovieQuality);

    PendingJob->GetConfiguration()->FindOrAddSettingByClass(UMoviePipelineDeferredPassBase::StaticClass());
    PendingJob->GetConfiguration()->FindOrAddSettingByClass(UMoviePipelineImageSequenceOutput_PNG::StaticClass());
    PendingJob->GetConfiguration()->InitializeTransientSettings();

    DeferredMoviePipeline = NewObject<UMoviePipeline>(World, UMoviePipeline::StaticClass());
    DeferredMoviePipeline->OnMoviePipelineWorkFinished().AddUObject(this, &UMoviePipelineNativeHostExecutor::CallbackOnMoviePipelineWorkFinished);
    FCoreDelegates::OnEnginePreExit.AddUObject(this, &UMoviePipelineNativeHostExecutor::CallbackOnEnginePreExit);
}

bool UMoviePipelineNativeHostExecutor::IsRendering_Implementation() const
{
    return bRendering || bWaiting;
}

void UMoviePipelineNativeHostExecutor::OnExecutorFinishedImpl()
{
	UE_LOG(LogTemp, Log, TEXT("%s OnExecutorFinishedImpl"), UTF8_TO_TCHAR(__FUNCTION__));
	bRendering = false;
    Super::OnExecutorFinishedImpl();
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
        return false; // 停止 Ticker

    const double Elapsed = FPlatformTime::Seconds() - StartSeconds;
    UE_LOG(LogTemp, Warning, TEXT("[MRQ] %s Poll for rendering, Elapsed: %.0f s; TimeoutSec: %.0f s"), UTF8_TO_TCHAR(__FUNCTION__), Elapsed, TimeoutSec);
    bool bCanStart = false;

    // 1) 找世界（-game 形态）
    UWorld* World = FindGameWorld();
    if (World)
    {
        UE_LOG(LogTemp, Log, TEXT("[MRQ] GameWorld: %s"), *World->GetName());

        // 2) 检查关卡是否 BeginPlay
        if (!World->HasBegunPlay())
        {
        	UE_LOG(LogTemp, Log, TEXT("[MRQ] Gameworld: %s has not begun play yet."), *World->GetName());
			return true; // 继续轮询
        }

    	// 3) 等待数据同步、关卡生成等
    	if (URenderGateWorldSubsystem* Gate = World->GetSubsystem<URenderGateWorldSubsystem>())
    	{
    		if (Gate->IsReady())
    		{
    			bCanStart = true; // 条件已满足
    		}
    		else
    		{
    			// 闸门对象变化 → 解绑旧回调再绑定新回调
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
	
    // —— 统一的超时处理出口 ——
    if (bCanStart || Elapsed >= TimeoutSec)
    {
        if (!bCanStart)
        {
            UE_LOG(LogTemp, Warning, TEXT("[MRQ] Wait timed out: %.1fs/%.1fs, start anyway."), Elapsed, TimeoutSec);
        }

        StartRenderNow();
        return false; // 停止 Ticker
    }

    UE_LOG(LogTemp, Log, TEXT("[MRQ] 检查 Level 内容是否就绪，等待中... %.0f s; 超过阈值 %.0f s 将自动进入视频渲染流程."), Elapsed, TimeoutSec);
    return true; // 继续轮询
}

void UMoviePipelineNativeHostExecutor::StartRenderNow()
{
    if (!bWaiting || bRendering || !PendingQueue)
        return;

    bWaiting = false;
    bRendering = true;

	ProgressTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UMoviePipelineNativeHostExecutor::UpdateProgress),
		0.1f
	);

    DeferredMoviePipeline->Initialize(PendingJob);
}

bool UMoviePipelineNativeHostExecutor::UpdateProgress(float DeltaTime)
{
    /**
	 * return false 停止 Ticker
	 * return true  继续 Ticker
     */

    if (!PendingQueue)
        return false;

	int32 CurrentFrameIndex = 0;
	int32 TotalFrames = 1;

	UMoviePipelineBlueprintLibrary::GetOverallOutputFrames(Cast<UMoviePipeline>(DeferredMoviePipeline), CurrentFrameIndex, TotalFrames);
	
    float CompletionPercentage = FMath::Clamp(CurrentFrameIndex / (float)TotalFrames, 0.f, 1.f);
	UE_LOG(LogTemp, Log, TEXT("[MRQ] 渲染进度: %.1f%%"), CompletionPercentage * 100.f);

    SendProgressUpdate(CurrentJobId, CurrentFrameIndex, TotalFrames);

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

void UMoviePipelineNativeHostExecutor::SendProgressUpdate(const FString& JobId, int32 CurrentFrame, int32 TotalFrames)
{
    FHttpModule* Http = &FHttpModule::Get();
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http->CreateRequest();

    Request->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
    {
	    // Handle the response here
    });

    Request->SetURL(FString::Printf(TEXT("http:://127.0.0.1:8080/ue-notifications/job/%s/progress"), *JobId));
	Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

    TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
    JsonObject->SetNumberField("current_frame", CurrentFrame);
    JsonObject->SetNumberField("total_frames", TotalFrames);

    float CompletionPercentage = FMath::Clamp(CurrentFrame / (float)TotalFrames, 0.f, 1.f);
    JsonObject->SetNumberField("progress_percent", CompletionPercentage);
    JsonObject->SetStringField("status", "rendering");
    JsonObject->SetStringField("stage", "rendering");

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
}

void UMoviePipelineNativeHostExecutor::SendHttpOnMoviePipelineWorkFinished(
    const FMoviePipelineOutputData& MoviePipelineOutputData)
{
	bool bSuccess = MoviePipelineOutputData.bSuccess;

	const FString InURL = FString::Printf(TEXT("http:://127.0.0.1:8080/ue-notifications/job/%s/render-complete"), *CurrentJobId);
	const FString InVerb = TEXT("POST");
	FString InMessage;
	FJsonObjectWrapper JsonObjectWrapper;
	JsonObjectWrapper.JsonObject.Get()->SetBoolField(TEXT("movie_pipeline_success"), bSuccess);

	JsonObjectWrapper.JsonObject.Get()->SetStringField(TEXT("video_path"), MRQ_OutputSetting->OutputDirectory.Path);
	JsonObjectWrapper.JsonObjectToString(InMessage);

	TMap<FString, FString> InHeaders;
	InHeaders.Add(TEXT("Content-Type"), TEXT("application/json"));
	int32 RequestIndex = SendHTTPRequest(InURL, InVerb, InMessage, InHeaders);

	OnExecutorFinishedImpl();
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