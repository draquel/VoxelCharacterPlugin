// Copyright Daniel Raquel. All Rights Reserved.

#include "UI/VCCarriedWeaponsWidget.h"
#include "Core/VCCharacterBase.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/Texture2D.h"
#include "Tags/CGFGameplayTags.h"

#if WITH_EQUIPMENT_PLUGIN
#include "Components/EquipmentManagerComponent.h"
#endif

#if WITH_INVENTORY_PLUGIN
#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Data/ItemDefinition.h"
#endif

namespace
{
	const FLinearColor DrawnFrame(0.2f, 0.42f, 0.7f, 0.9f);
	const FLinearColor SheathedFrame(0.08f, 0.08f, 0.12f, 0.75f);
	constexpr float IconSize = 40.f;
}

void UVCCarriedWeaponsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
}

void UVCCarriedWeaponsWidget::NativeDestruct()
{
	Unbind();
	Super::NativeDestruct();
}

void UVCCarriedWeaponsWidget::BuildWidgetTree()
{
	// Idempotent, as the vitals widget: a widget made without a player context builds on its first bind.
	if (!WidgetTree || IconRow)
	{
		return;
	}
	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CarriedRoot"));
	WidgetTree->RootWidget = Root;
	IconRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CarriedIcons"));
	Root->AddChildToVerticalBox(IconRow);
	HintText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CarriedHint"));
	FSlateFontInfo Font = HintText->GetFont();
	Font.Size = 11;
	HintText->SetFont(Font);
	HintText->SetColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.85f, 0.8f)));
	HintText->SetText(NSLOCTEXT("VoxelCharacter", "CarriedHint", "R sheathe / draw   X next"));
	HintText->SetVisibility(ESlateVisibility::Collapsed);
	Root->AddChildToVerticalBox(HintText);
}

void UVCCarriedWeaponsWidget::InitWithCharacter(AVCCharacterBase* Character)
{
	BuildWidgetTree();
	Unbind();
	BoundCharacter = Character;
#if WITH_EQUIPMENT_PLUGIN
	if (UEquipmentManagerComponent* Equipment = Character ? Character->FindComponentByClass<UEquipmentManagerComponent>() : nullptr)
	{
		Equipment->OnEquipmentChanged.AddUniqueDynamic(this, &UVCCarriedWeaponsWidget::HandleEquipmentChanged);
	}
#endif
	Refresh();
}

void UVCCarriedWeaponsWidget::Unbind()
{
#if WITH_EQUIPMENT_PLUGIN
	if (AVCCharacterBase* Character = BoundCharacter.Get())
	{
		if (UEquipmentManagerComponent* Equipment = Character->FindComponentByClass<UEquipmentManagerComponent>())
		{
			Equipment->OnEquipmentChanged.RemoveDynamic(this, &UVCCarriedWeaponsWidget::HandleEquipmentChanged);
		}
	}
#endif
	BoundCharacter.Reset();
}

void UVCCarriedWeaponsWidget::HandleEquipmentChanged()
{
	Refresh();
}

void UVCCarriedWeaponsWidget::Refresh()
{
	AVCCharacterBase* Character = BoundCharacter.Get();
	ShownSheaths = Character ? Character->GetCarriedWeaponSlots() : TArray<FGameplayTag>();
	LitSheath = Character ? Character->GetDrawnSheath() : FGameplayTag();
	if (!IconRow)
	{
		return;
	}
	IconRow->ClearChildren();
#if WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
	const UEquipmentManagerComponent* Equipment = Character ? Character->FindComponentByClass<UEquipmentManagerComponent>() : nullptr;
	const UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UItemDatabaseSubsystem* ItemDB = GI ? GI->GetSubsystem<UItemDatabaseSubsystem>() : nullptr;
	for (const FGameplayTag& Sheath : ShownSheaths)
	{
		const UItemDefinition* Def = Equipment && ItemDB ? ItemDB->GetDefinition(Equipment->GetEquippedItem(Sheath).ItemDefinitionId) : nullptr;
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Size->SetWidthOverride(IconSize);
		Size->SetHeightOverride(IconSize);
		UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Frame->SetBrushColor(Sheath == LitSheath ? DrawnFrame : SheathedFrame);
		Frame->SetPadding(FMargin(3.f));
		Size->AddChild(Frame);
		// The icons are small textures the inventory already shows: loaded at once here.
		if (UTexture2D* Icon = Def ? Def->Icon.LoadSynchronous() : nullptr)
		{
			UImage* Image = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
			Image->SetBrushFromTexture(Icon);
			Image->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, Sheath == LitSheath ? 1.f : 0.6f));
			Frame->SetContent(Image);
		}
		if (UHorizontalBoxSlot* IconSlot = IconRow->AddChildToHorizontalBox(Size))
		{
			IconSlot->SetPadding(FMargin(2.f, 0.f));
		}
	}
#endif
	if (HintText)
	{
		HintText->SetVisibility(ShownSheaths.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}
