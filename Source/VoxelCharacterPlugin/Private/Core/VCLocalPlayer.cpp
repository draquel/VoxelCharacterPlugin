// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCLocalPlayer.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"

const TCHAR* UVCLocalPlayer::LoginOption = TEXT("VCPIEInstance");

FString UVCLocalPlayer::GetGameLoginOptions() const
{
	FString Options = Super::GetGameLoginOptions();
#if WITH_EDITOR
	const UGameInstance* GameInstance = GetGameInstance();
	const FWorldContext* Context = GameInstance ? GameInstance->GetWorldContext() : nullptr;
	if (Context && Context->WorldType == EWorldType::PIE && Context->PIEInstance > 0)
	{
		Options += FString::Printf(TEXT("%s%s=%d"), Options.IsEmpty() ? TEXT("") : TEXT("?"), LoginOption, Context->PIEInstance);
	}
#endif
	return Options;
}
