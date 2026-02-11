// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "VCCharacterAttributeSet.generated.h"

// Macro pair from GAS documentation for boilerplate attribute accessors.
#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/**
 * Core character attributes.
 *
 * Owned by AVCPlayerState (survives respawn). IncomingDamage is a meta
 * attribute — never replicated, consumed immediately in
 * PostGameplayEffectExecute to modify Health.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCCharacterAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UVCCharacterAttributeSet();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

	// ==================== Vitals ====================

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Health, Category = "VoxelCharacter|Attributes")
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS(UVCCharacterAttributeSet, Health)

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxHealth, Category = "VoxelCharacter|Attributes")
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS(UVCCharacterAttributeSet, MaxHealth)

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Stamina, Category = "VoxelCharacter|Attributes")
	FGameplayAttributeData Stamina;
	ATTRIBUTE_ACCESSORS(UVCCharacterAttributeSet, Stamina)

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxStamina, Category = "VoxelCharacter|Attributes")
	FGameplayAttributeData MaxStamina;
	ATTRIBUTE_ACCESSORS(UVCCharacterAttributeSet, MaxStamina)

	// ==================== Movement ====================

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MoveSpeedMultiplier, Category = "VoxelCharacter|Attributes")
	FGameplayAttributeData MoveSpeedMultiplier;
	ATTRIBUTE_ACCESSORS(UVCCharacterAttributeSet, MoveSpeedMultiplier)

	// ==================== Voxel Interaction ====================

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MiningSpeed, Category = "VoxelCharacter|Attributes")
	FGameplayAttributeData MiningSpeed;
	ATTRIBUTE_ACCESSORS(UVCCharacterAttributeSet, MiningSpeed)

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_InteractionRange, Category = "VoxelCharacter|Attributes")
	FGameplayAttributeData InteractionRange;
	ATTRIBUTE_ACCESSORS(UVCCharacterAttributeSet, InteractionRange)

	// ==================== Meta (not replicated) ====================

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Attributes")
	FGameplayAttributeData IncomingDamage;
	ATTRIBUTE_ACCESSORS(UVCCharacterAttributeSet, IncomingDamage)

protected:
	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_Stamina(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MaxStamina(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MoveSpeedMultiplier(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MiningSpeed(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_InteractionRange(const FGameplayAttributeData& OldValue);
};
