// Copyright Daniel Raquel. All Rights Reserved.

#include "VCEffectsAuthoringLibrary.h"

#include "Materials/MaterialInterface.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogVCEffectsAuthoring, Log, All);

namespace VCEffectsAuthoringPrivate
{
	/** Every script an emitter runs (the emitter and particle spawn and update): where its baked module constants live. */
	TArray<UNiagaraScript*> EmitterScripts(FVersionedNiagaraEmitterData& Data)
	{
		TArray<UNiagaraScript*> Out;
		for (UNiagaraScript* Script : { Data.EmitterSpawnScriptProps.Script.Get(), Data.EmitterUpdateScriptProps.Script.Get(),
			Data.SpawnScriptProps.Script.Get(), Data.UpdateScriptProps.Script.Get() })
		{
			if (Script)
			{
				Out.AddUnique(Script);
			}
		}
		return Out;
	}

	/** A constant's value as text, for the report. */
	FString ValueText(const FNiagaraVariable& Parameter, const uint8* Value)
	{
		if (!Value)
		{
			return TEXT("?");
		}
		const FNiagaraTypeDefinition Type = Parameter.GetType();
		if (Type == FNiagaraTypeDefinition::GetFloatDef())
		{
			return FString::Printf(TEXT("%g"), *reinterpret_cast<const float*>(Value));
		}
		if (Type == FNiagaraTypeDefinition::GetIntDef())
		{
			return FString::Printf(TEXT("%d"), *reinterpret_cast<const int32*>(Value));
		}
		if (Type == FNiagaraTypeDefinition::GetBoolDef())
		{
			return reinterpret_cast<const FNiagaraBool*>(Value)->GetValue() ? TEXT("true") : TEXT("false");
		}
		if (Type == FNiagaraTypeDefinition::GetVec2Def())
		{
			const FVector2f V = *reinterpret_cast<const FVector2f*>(Value);
			return FString::Printf(TEXT("(%g, %g)"), V.X, V.Y);
		}
		if (Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef())
		{
			const FVector3f V = *reinterpret_cast<const FVector3f*>(Value);
			return FString::Printf(TEXT("(%g, %g, %g)"), V.X, V.Y, V.Z);
		}
		if (Type == FNiagaraTypeDefinition::GetColorDef())
		{
			const FLinearColor C = *reinterpret_cast<const FLinearColor*>(Value);
			return FString::Printf(TEXT("(%g, %g, %g, %g)"), C.R, C.G, C.B, C.A);
		}
		return TEXT("-");
	}

	/**
	 * Sets every constant of the emitter whose name contains Needle and whose type Write accepts.
	 * @param Write Given the constant, fills Bytes with its new value and returns true when the type is one it sets
	 * @return How many constants were set
	 */
	int32 SetConstants(FVersionedNiagaraEmitterData& Data, const FString& Needle, TFunctionRef<bool(const FNiagaraVariable&, TArray<uint8>&)> Write, FString& OutReport)
	{
		int32 Set = 0;
		for (UNiagaraScript* Script : EmitterScripts(Data))
		{
			TArray<FNiagaraVariable> Parameters;
			Script->RapidIterationParameters.GetParameters(Parameters);
			for (const FNiagaraVariable& Parameter : Parameters)
			{
				if (!Parameter.GetName().ToString().Contains(Needle))
				{
					continue;
				}
				TArray<uint8> Bytes;
				if (!Write(Parameter, Bytes) || Bytes.Num() != Parameter.GetSizeInBytes())
				{
					continue;
				}
				Script->Modify();
				Script->RapidIterationParameters.SetParameterData(Bytes.GetData(), Parameter);
				++Set;
				OutReport += FString::Printf(TEXT("  set %s = %s\n"), *Parameter.GetName().ToString(), *ValueText(Parameter, Bytes.GetData()));
			}
		}
		return Set;
	}

	template <typename T>
	void PutBytes(TArray<uint8>& Bytes, const T& Value)
	{
		Bytes.SetNumUninitialized(sizeof(T));
		FMemory::Memcpy(Bytes.GetData(), &Value, sizeof(T));
	}
}

bool UVCEffectsAuthoringLibrary::ResetSystem(UNiagaraSystem* System, FString& OutReport)
{
	OutReport.Reset();
	if (!System)
	{
		OutReport = TEXT("no system");
		return false;
	}
	System->Modify();
	TSet<FGuid> Ids;
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		Ids.Add(Handle.GetId());
	}
	if (Ids.Num() > 0)
	{
		System->RemoveEmitterHandlesById(Ids);
	}
	OutReport = FString::Printf(TEXT("%s: %d emitter(s) removed\n"), *System->GetName(), Ids.Num());
	return System->GetEmitterHandles().Num() == 0;
}

bool UVCEffectsAuthoringLibrary::AddTemplateEmitter(UNiagaraSystem* System, UNiagaraEmitter* TemplateEmitter, FName EmitterName, UMaterialInterface* Material,
	bool bLocalSpace, const TMap<FString, float>& Floats, const TMap<FString, FVector>& Vectors, const TMap<FString, FLinearColor>& Colors,
	FString& OutReport)
{
	using namespace VCEffectsAuthoringPrivate;
	OutReport.Reset();
	if (!System || !TemplateEmitter)
	{
		OutReport = TEXT("no system or no template emitter");
		return false;
	}
	System->Modify();
	const FGuid HandleId = FNiagaraEditorUtilities::AddEmitterToSystem(*System, *TemplateEmitter, TemplateEmitter->GetExposedVersion().VersionGuid, /*bCreateCopy=*/true);
	FNiagaraEmitterHandle* Handle = nullptr;
	for (FNiagaraEmitterHandle& Candidate : System->GetEmitterHandles())
	{
		if (Candidate.GetId() == HandleId)
		{
			Handle = &Candidate;
			break;
		}
	}
	UNiagaraEmitter* Emitter = Handle ? Handle->GetInstance().Emitter : nullptr;
	FVersionedNiagaraEmitterData* Data = Handle ? Handle->GetEmitterData() : nullptr;
	if (!Handle || !Emitter || !Data)
	{
		OutReport = FString::Printf(TEXT("could not add a copy of %s\n"), *TemplateEmitter->GetPathName());
		return false;
	}
	if (!EmitterName.IsNone())
	{
		Handle->SetName(EmitterName, *System);
	}
	Emitter->Modify();
	Data->bLocalSpace = bLocalSpace;
	OutReport += FString::Printf(TEXT("%s: a copy of %s, %s space\n"), *Handle->GetName().ToString(), *TemplateEmitter->GetName(), bLocalSpace ? TEXT("local") : TEXT("world"));

	if (Material)
	{
		int32 Sprites = 0;
		for (UNiagaraRendererProperties* Renderer : Data->GetRenderers())
		{
			if (UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
			{
				Sprite->Modify();
				Sprite->Material = Material;
				++Sprites;
			}
		}
		OutReport += FString::Printf(TEXT("  %d sprite renderer(s) given %s\n"), Sprites, *Material->GetName());
	}

	bool bAllMatched = true;
	auto Missed = [&OutReport, &bAllMatched](const FString& Needle)
	{
		bAllMatched = false;
		OutReport += FString::Printf(TEXT("  NO CONSTANT matches '%s'\n"), *Needle);
	};
	for (const TPair<FString, float>& Pair : Floats)
	{
		const float Value = Pair.Value;
		const int32 Set = SetConstants(*Data, Pair.Key, [Value](const FNiagaraVariable& Parameter, TArray<uint8>& Bytes)
		{
			const FNiagaraTypeDefinition Type = Parameter.GetType();
			if (Type == FNiagaraTypeDefinition::GetFloatDef())
			{
				PutBytes(Bytes, Value);
				return true;
			}
			if (Type == FNiagaraTypeDefinition::GetIntDef())
			{
				PutBytes(Bytes, FMath::RoundToInt32(Value));
				return true;
			}
			if (Type == FNiagaraTypeDefinition::GetBoolDef())
			{
				PutBytes(Bytes, FNiagaraBool(Value != 0.f));
				return true;
			}
			return false;
		}, OutReport);
		if (Set == 0)
		{
			Missed(Pair.Key);
		}
	}
	for (const TPair<FString, FVector>& Pair : Vectors)
	{
		const FVector Value = Pair.Value;
		const int32 Set = SetConstants(*Data, Pair.Key, [Value](const FNiagaraVariable& Parameter, TArray<uint8>& Bytes)
		{
			const FNiagaraTypeDefinition Type = Parameter.GetType();
			if (Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef())
			{
				PutBytes(Bytes, FVector3f(Value));
				return true;
			}
			if (Type == FNiagaraTypeDefinition::GetVec2Def())
			{
				PutBytes(Bytes, FVector2f(static_cast<float>(Value.X), static_cast<float>(Value.Y)));
				return true;
			}
			return false;
		}, OutReport);
		if (Set == 0)
		{
			Missed(Pair.Key);
		}
	}
	for (const TPair<FString, FLinearColor>& Pair : Colors)
	{
		const FLinearColor Value = Pair.Value;
		const int32 Set = SetConstants(*Data, Pair.Key, [Value](const FNiagaraVariable& Parameter, TArray<uint8>& Bytes)
		{
			if (Parameter.GetType() == FNiagaraTypeDefinition::GetColorDef())
			{
				PutBytes(Bytes, Value);
				return true;
			}
			return false;
		}, OutReport);
		if (Set == 0)
		{
			Missed(Pair.Key);
		}
	}

	// Every constant, for the follow-up when a name matched nothing or a template's names change with the engine.
	for (UNiagaraScript* Script : EmitterScripts(*Data))
	{
		TArray<FNiagaraVariable> Parameters;
		Script->RapidIterationParameters.GetParameters(Parameters);
		for (const FNiagaraVariable& Parameter : Parameters)
		{
			OutReport += FString::Printf(TEXT("    constant %s : %s = %s\n"), *Parameter.GetName().ToString(), *Parameter.GetType().GetName(),
				*ValueText(Parameter, Script->RapidIterationParameters.GetParameterData(Parameter)));
		}
	}
	UE_LOG(LogVCEffectsAuthoring, Log, TEXT("AddTemplateEmitter %s:\n%s"), *System->GetName(), *OutReport);
	return bAllMatched;
}

bool UVCEffectsAuthoringLibrary::FinishSystem(UNiagaraSystem* System, FString& OutReport)
{
	OutReport.Reset();
	if (!System)
	{
		OutReport = TEXT("no system");
		return false;
	}
	System->Modify();
	System->RequestCompile(/*bForce=*/true);
	System->WaitForCompilationComplete();
	System->MarkPackageDirty();
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		OutReport += FString::Printf(TEXT("%s%s"), *Handle.GetName().ToString(), Handle.GetIsEnabled() ? TEXT("") : TEXT(" (disabled)"));
		if (Data)
		{
			OutReport += FString::Printf(TEXT(": %s, %s space, %d renderer(s)"), Data->SimTarget == ENiagaraSimTarget::GPUComputeSim ? TEXT("GPU") : TEXT("CPU"),
				Data->bLocalSpace ? TEXT("local") : TEXT("world"), Data->GetRenderers().Num());
		}
		OutReport += TEXT("\n");
	}
	return System->GetEmitterHandles().Num() > 0;
}
