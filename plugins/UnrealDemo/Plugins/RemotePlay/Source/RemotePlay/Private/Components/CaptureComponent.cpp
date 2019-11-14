// Copyright 2018 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "Components/CaptureComponent.h"
#include "RemotePlayModule.h"
#include "Pipelines/EncodePipelineMonoscopic.h"
#include "Pipelines/NetworkPipeline.h"

#include "Engine.h"
#include "Engine/GameViewportClient.h"

#include "GameFramework/Actor.h"
#include "RemotePlaySettings.h"
#include "RemotePlayMonitor.h"
#include "RemotePlayReflectionCaptureComponent.h"
#include "ContentStreaming.h"

URemotePlayCaptureComponent::URemotePlayCaptureComponent()
	: bRenderOwner(false)
	, RemotePlayContext(nullptr)
	, RemotePlayReflectionCaptureComponent(nullptr)
	, bIsStreaming(false)
	, bSendKeyframe(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	bCaptureEveryFrame = true;
	bCaptureOnMovement = false; 
}

URemotePlayCaptureComponent::~URemotePlayCaptureComponent()
{
}

void URemotePlayCaptureComponent::BeginPlay()
{	
	ShowFlags.EnableAdvancedFeatures();
	ShowFlags.SetTemporalAA(false);
	ShowFlags.SetAntiAliasing(true);

	if (TextureTarget && !TextureTarget->bCanCreateUAV)
	{
		TextureTarget->bCanCreateUAV = true;
	}

	ARemotePlayMonitor* Monitor = ARemotePlayMonitor::Instantiate(GetWorld());
	if (Monitor->bOverrideTextureTarget && Monitor->SceneCaptureTextureTarget)
	{
		TextureTarget = Monitor->SceneCaptureTextureTarget;
	}

	// Make sure that there is enough time in the render queue.
	UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), FString("g.TimeoutForBlockOnRenderFence 300000"));

	Super::BeginPlay();	

	AActor* OwnerActor = GetTypedOuter<AActor>();
	if (bRenderOwner)
	{
		RemotePlayReflectionCaptureComponent = Cast<URemotePlayReflectionCaptureComponent>(OwnerActor->GetComponentByClass(URemotePlayReflectionCaptureComponent::StaticClass()));
	}
	else
	{
		check(OwnerActor);

		TArray<AActor*> OwnerAttachedActors;
		OwnerActor->GetAttachedActors(OwnerAttachedActors);
		HiddenActors.Add(OwnerActor);
		HiddenActors.Append(OwnerAttachedActors);

	}
}

void URemotePlayCaptureComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Super::EndPlay(Reason);
}

void URemotePlayCaptureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Aidan: Below allows the capture to avail of Unreal's texture streaming
	// Add the view information every tick because its only used for one tick and then
	// removed by the streaming manager.
	int32 W = TextureTarget->GetSurfaceWidth();
	float FOV = 90.0f;
	IStreamingManager::Get().AddViewInformation(GetComponentLocation(), W, W / FMath::Tan(FOV));

	ARemotePlayMonitor *Monitor = ARemotePlayMonitor::Instantiate(GetWorld());
	if (bCaptureEveryFrame && Monitor && Monitor->bDisableMainCamera)
	{
		CaptureScene();
	}
}

const FRemotePlayEncodeParameters &URemotePlayCaptureComponent::GetEncodeParams()
{
	if (EncodeParams.bDecomposeCube)
	{
		int32 W = TextureTarget->GetSurfaceWidth();
		// 3 across...
		EncodeParams.FrameWidth = 3 * W;
		// and 2 down... for the colour, depth, and light cubes.
		EncodeParams.FrameHeight = 3 * W; // (W + W / 2);
	}
	else
	{
		EncodeParams.FrameWidth = 2048;
		EncodeParams.FrameHeight = 1024 + 512;
	}
	return EncodeParams;
}

void URemotePlayCaptureComponent::UpdateSceneCaptureContents(FSceneInterface* Scene)
{

	ARemotePlayMonitor *Monitor = ARemotePlayMonitor::Instantiate(GetWorld());
	if (Monitor&&Monitor->VideoEncodeFrequency > 1)
	{
		static int u = 1;
		u--;
		u = std::min(Monitor->VideoEncodeFrequency, u);
		if (!u)
		{
			u = Monitor->VideoEncodeFrequency;
		}
		else
		{
			return;
		}
	}
	// Aidan: The parent function belongs to SceneCaptureComponentCube and is located in SceneCaptureComponent.cpp. 
	// The parent function calls UpdateSceneCaptureContents function in SceneCaptureRendering.cpp.
	// UpdateSceneCaptureContents enqueues the rendering commands to render to the scene capture cube's render target.
	// The parent function is called from the static function UpdateDeferredCaptures located in
	// SceneCaptureComponent.cpp. UpdateDeferredCaptures is called by the BeginRenderingViewFamily function in SceneRendering.cpp.
	// Therefore the rendering commands queued after this function call below directly follow the scene capture cube's commands in the queue.

	Super::UpdateSceneCaptureContents(Scene);

	if (TextureTarget&&RemotePlayContext)
	{
		if (!RemotePlayContext->EncodePipeline.IsValid())
		{
			RemotePlayContext->EncodePipeline.Reset(new FEncodePipelineMonoscopic);
			RemotePlayContext->EncodePipeline->Initialize(EncodeParams, RemotePlayContext, Monitor, RemotePlayContext->ColorQueue.Get(), RemotePlayContext->DepthQueue.Get());

			if (RemotePlayReflectionCaptureComponent)
			{
				RemotePlayReflectionCaptureComponent->Initialize();
				RemotePlayReflectionCaptureComponent->bAttached = true;
			}
		}
		FTransform Transform = GetComponentTransform();
		 
		RemotePlayContext->EncodePipeline->PrepareFrame(Scene, TextureTarget, Transform);
		if (RemotePlayReflectionCaptureComponent && EncodeParams.bDecomposeCube)
		{
			RemotePlayReflectionCaptureComponent->UpdateContents(
				Scene->GetRenderScene(),
				TextureTarget,
				Scene->GetFeatureLevel());
			int32 W = TextureTarget->GetSurfaceWidth();
			FIntPoint Offset0((W*3)/2,W*2);
			RemotePlayReflectionCaptureComponent->PrepareFrame(
				Scene->GetRenderScene(),
				RemotePlayContext->EncodePipeline->GetSurfaceTexture(),
				Scene->GetFeatureLevel(),Offset0);
		}
		RemotePlayContext->EncodePipeline->EncodeFrame(Scene, TextureTarget, Transform, bSendKeyframe);
		// The client must request it again if it needs it
		bSendKeyframe = false;
	}
}

void URemotePlayCaptureComponent::StartStreaming(FRemotePlayContext *Context)
{
	RemotePlayContext = Context;

	ARemotePlayMonitor *Monitor = ARemotePlayMonitor::Instantiate(GetWorld());
	if (!ViewportDrawnDelegateHandle.IsValid())
	{
		if (UGameViewportClient* GameViewport = GEngine->GameViewport)
		{
			ViewportDrawnDelegateHandle = GameViewport->OnDrawn().AddUObject(this, &URemotePlayCaptureComponent::OnViewportDrawn);
			GameViewport->bDisableWorldRendering = Monitor->bDisableMainCamera;
		}
	}

	bIsStreaming = true;
	bCaptureEveryFrame = true;
	bSendKeyframe = false;
}

void URemotePlayCaptureComponent::StopStreaming()
{
	if (!RemotePlayContext)
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Capture: null RemotePlayContext"));
		return;
	}

	if (ViewportDrawnDelegateHandle.IsValid())
	{
		if (UGameViewportClient* GameViewport = GEngine->GameViewport)
		{
			GameViewport->OnDrawn().Remove(ViewportDrawnDelegateHandle);
		}
		ViewportDrawnDelegateHandle.Reset();
	}

	RemotePlayContext = nullptr;

	bCaptureEveryFrame = false;
	UE_LOG(LogRemotePlay, Log, TEXT("Capture: Stopped streaming"));
}

void URemotePlayCaptureComponent::RequestKeyframe()
{
	bSendKeyframe = true;
}

void URemotePlayCaptureComponent::OnViewportDrawn()
{
}

