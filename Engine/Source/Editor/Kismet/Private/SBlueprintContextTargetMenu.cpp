// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "BlueprintEditorPrivatePCH.h"
#include "SBlueprintContextTargetMenu.h"
#include "BlueprintActionMenuUtils.h"	// for EContextTargetFlags
#include "BlueprintActionFilter.h"		// for FBlueprintActionContext
#include "BlueprintEditorUtils.h"		// for DoesSupportComponents
#include "BlueprintEditorSettings.h"

#define LOCTEXT_NAMESPACE "SBlueprintContextTargetMenu"

/*******************************************************************************
 * FContextMenuTargetProfile
 ******************************************************************************/
 
namespace ContextMenuTargetProfileImpl
{
	const FString ConfigSection("BlueprintEditor.ContextTargets");
	const FString SharedProfileName("SharedMenuProfile");

	/**
	 * Query methods to check and see if the user is dragging off a specific pin
	 * type (for the context menu).
	 * 
	 * @param  MenuContext    Defines the context from which the menu was requested.
	 * @return True if the context contains a pin matching the type in question, otherwise false.
	 */
	static bool HasExecPinContext(const FBlueprintActionContext& MenuContext);
	static bool HasObjectPinContext(const FBlueprintActionContext& MenuContext);

	static bool IsObjectPin(UEdGraphPin* Pin);
	static UClass* GetPinClass(UEdGraphPin const* Pin);
	static bool HasAnyExposedComponents(UClass* TargetClass);

	/**
	 * Returns a user facing description that provides a short succinct title,
	 * discerning this profile from others.
	 */
	static FText GetProfileDescription(const FBlueprintActionContext& MenuContext);

	/**
	 * Determines which target options are unused, given the current context.
	 */
	static uint32 GetIncompatibleTargetFlags(const FBlueprintActionContext& MenuContext, uint32& HasComponentsMaskOut);

	/**
	 * Controls how user set targets are saved. Determines how to split context 
	 * scenarios by providing a separate save name for each.
	 */
	static FString GetProfileSaveName(const FBlueprintActionContext& MenuContext);
}

//------------------------------------------------------------------------------
static bool ContextMenuTargetProfileImpl::HasObjectPinContext(const FBlueprintActionContext& MenuContext)
{
	for (UEdGraphPin* ContextPin : MenuContext.Pins)
	{
		if (IsObjectPin(ContextPin))
		{
			return true;
		}
	}
	return false;
}

//------------------------------------------------------------------------------
static bool ContextMenuTargetProfileImpl::HasExecPinContext(const FBlueprintActionContext& MenuContext)
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	for (UEdGraphPin* ContextPin : MenuContext.Pins)
	{
		if (K2Schema->IsExecPin(*ContextPin))
		{
			return true;
		}
	}
	return false;
}

//------------------------------------------------------------------------------
static bool ContextMenuTargetProfileImpl::IsObjectPin(UEdGraphPin* Pin)
{
	return GetPinClass(Pin) != nullptr;
}

//------------------------------------------------------------------------------
static UClass* ContextMenuTargetProfileImpl::GetPinClass(UEdGraphPin const* Pin)
{
	UClass* PinObjClass = nullptr;

	FEdGraphPinType const& PinType = Pin->PinType;
	if ((PinType.PinCategory == UEdGraphSchema_K2::PC_Object) ||
		(PinType.PinCategory == UEdGraphSchema_K2::PC_Interface))
	{
		bool const bIsSelfPin = !PinType.PinSubCategoryObject.IsValid();
		if (bIsSelfPin)
		{
			PinObjClass = CastChecked<UK2Node>(Pin->GetOwningNode())->GetBlueprint()->SkeletonGeneratedClass;
		}
		else
		{
			PinObjClass = Cast<UClass>(PinType.PinSubCategoryObject.Get());
			if (PinObjClass != nullptr)
			{
				if (UBlueprint* ClassBlueprint = Cast<UBlueprint>(PinObjClass->ClassGeneratedBy))
				{
					if (ClassBlueprint->SkeletonGeneratedClass != nullptr)
					{
						PinObjClass = ClassBlueprint->SkeletonGeneratedClass;
					}
				}
			}
		}
	}

	return PinObjClass;
}

//------------------------------------------------------------------------------
static bool ContextMenuTargetProfileImpl::HasAnyExposedComponents(UClass* TargetClass)
{
	for (TFieldIterator<UObjectProperty> PropertyIt(TargetClass, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
	{
		UObjectProperty* ObjectProperty = *PropertyIt;
		if (!ObjectProperty->HasAnyPropertyFlags(CPF_BlueprintVisible) || !ObjectProperty->PropertyClass->IsChildOf<UActorComponent>())
		{
			continue;
		}

		return true;
	}
	return false;
}

//------------------------------------------------------------------------------
FText ContextMenuTargetProfileImpl::GetProfileDescription(const FBlueprintActionContext& MenuContext)
{
	if (MenuContext.Pins.Num() == 0)
	{
		return LOCTEXT("BlueprintContextTarget", "Blueprint Context Target(s)");
	}
	else if (ContextMenuTargetProfileImpl::HasObjectPinContext(MenuContext))
	{
		return LOCTEXT("ObjectContextTarget", "Object Context Target(s)");
	}
	else if (ContextMenuTargetProfileImpl::HasExecPinContext(MenuContext))
	{
		return LOCTEXT("ExecContextTarget", "Exec Context Target(s)");
	}
	else
	{
// 		const FEdGraphPinType& PinType = MenuContext.Pins[0]->PinType;
// 		FText PinTypename = UEdGraphSchema_K2::TypeToText(PinType);
// 		return FText::Format(LOCTEXT("TypedPinContextTarget", "{0} Context Target(s)"), PinTypename);

		return LOCTEXT("PODPinContextTarget", "Pin Context Target(s)");
	}
}

//------------------------------------------------------------------------------
uint32 ContextMenuTargetProfileImpl::GetIncompatibleTargetFlags(const FBlueprintActionContext& MenuContext, uint32& HasComponentsMaskOut)
{
	uint32 IncompatibleFlags = (EContextTargetFlags::TARGET_NodeTarget | EContextTargetFlags::TARGET_PinObject | 
		EContextTargetFlags::TARGET_SiblingPinObjects | EContextTargetFlags::TARGET_SubComponents);

	TArray<UClass*> BlueprintClasses;

	HasComponentsMaskOut = 0;
	for (UBlueprint* Blueprint : MenuContext.Blueprints)
	{
		UClass* BpClass = Blueprint->SkeletonGeneratedClass;
		BlueprintClasses.Add(BpClass);

		if (HasAnyExposedComponents(BpClass))
		{
			HasComponentsMaskOut |= EContextTargetFlags::TARGET_Blueprint;
		}
	}

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	for (UEdGraphPin* ContextPin : MenuContext.Pins)
	{
		UEdGraphNode* OwnerNode = ContextPin->GetOwningNodeUnchecked();
		if (OwnerNode != nullptr)
		{
			UEdGraphPin* SelfPin = K2Schema->FindSelfPin(*OwnerNode, EGPD_Input);
			UClass* SelfClass = (SelfPin != nullptr) ? GetPinClass(SelfPin) : nullptr;
			// if this node has a hidden self pin, then that probably means its 
			// target is a static lib, or the same as the current blueprint
			bool const bHasUniqueTarget = (SelfPin != nullptr) && !SelfPin->bHidden;// && !BlueprintClasses.Contains(SelfClass);
			// @TODO: check that "self" is different from this blueprint
			if (bHasUniqueTarget)
			{
				IncompatibleFlags &= ~EContextTargetFlags::TARGET_NodeTarget;
				if (HasAnyExposedComponents(SelfClass))
				{
					HasComponentsMaskOut |= EContextTargetFlags::TARGET_NodeTarget;
				}
			}

			for (UEdGraphPin* SiblingPin : OwnerNode->Pins)
			{
				UClass* PinClass = GetPinClass(SiblingPin);
				if ((SiblingPin != ContextPin) && (SiblingPin->Direction == EGPD_Output) && (PinClass != nullptr))
				{
					IncompatibleFlags &= ~EContextTargetFlags::TARGET_SiblingPinObjects;
					if (HasAnyExposedComponents(PinClass))
					{
						HasComponentsMaskOut |= EContextTargetFlags::TARGET_SiblingPinObjects;
					}
				}
			}
		}

		if (ContextPin->Direction == EGPD_Input)
		{
			continue;
		}

		if (UClass* PinClass = GetPinClass(ContextPin))
		{
			IncompatibleFlags &= ~EContextTargetFlags::TARGET_PinObject;
			if (HasAnyExposedComponents(PinClass))
			{
				HasComponentsMaskOut |= EContextTargetFlags::TARGET_PinObject;
			}
		}
	}

	if (HasComponentsMaskOut != 0)
	{
		IncompatibleFlags &= ~EContextTargetFlags::TARGET_SubComponents;
	}

	return IncompatibleFlags;
}


//------------------------------------------------------------------------------
static FString ContextMenuTargetProfileImpl::GetProfileSaveName(const FBlueprintActionContext& MenuContext)
{
	const UBlueprintEditorSettings* BpSettings = GetDefault<UBlueprintEditorSettings>();
	if (!BpSettings->bSplitContextTargetSettings)
	{
		return ContextMenuTargetProfileImpl::SharedProfileName;
	}

	if (MenuContext.Pins.Num() == 0)
	{
		return TEXT("NoPinProfile");
	}
	else if (HasExecPinContext(MenuContext))
	{
		return TEXT("ExecPinProfile");
	}

	uint32 HasComponentMask = 0;
	uint32 IncompatibleTargetFlags = GetIncompatibleTargetFlags(MenuContext, HasComponentMask);
	return FString::Printf(TEXT("MenuProfile_%d"), IncompatibleTargetFlags);

// 	if (MenuContext.Pins.Num() == 0)
// 	{
// 		return TEXT("NoPin");
// 	}
// 	else if (ContextMenuTargetProfileImpl::HasObjectPinContext(MenuContext))
// 	{
// 		return TEXT("ObjectPin");
// 	}
// 	else
// 	{
// 		return TEXT("PODPin");
// 	}	
}

//------------------------------------------------------------------------------
FContextMenuTargetProfile::FContextMenuTargetProfile()
	: ProfileSaveName(TEXT("Default"))
	, HasComponentsMask(0)
	, IncompatibleTargetFlags(0)
	, SavedTargetFlags(~IncompatibleTargetFlags)
{
}

//------------------------------------------------------------------------------
FContextMenuTargetProfile::FContextMenuTargetProfile(const FBlueprintActionContext& MenuContext)
	: ProfileSaveName(ContextMenuTargetProfileImpl::GetProfileSaveName(MenuContext))
	, HasComponentsMask(0)
	, IncompatibleTargetFlags(ContextMenuTargetProfileImpl::GetIncompatibleTargetFlags(MenuContext, HasComponentsMask))
	, SavedTargetFlags(~IncompatibleTargetFlags)
{
	const UBlueprintEditorSettings* BpSettings = GetDefault<UBlueprintEditorSettings>();
	FString const BpConfigKey = BpSettings->GetClass()->GetPathName();

	bool bOldUseTargetContextForNodeMenu = true;
	if (GConfig->GetBool(*BpConfigKey, TEXT("bUseTargetContextForNodeMenu"), bOldUseTargetContextForNodeMenu, GEditorUserSettingsIni) && !bOldUseTargetContextForNodeMenu)
	{
		SavedTargetFlags = 0;
	}

	if (!LoadProfile())
	{
		// maybe they were originally using the shared context profile? so let's default to that
		FString CachedProfileSaveName = ProfileSaveName;
		ProfileSaveName = ContextMenuTargetProfileImpl::SharedProfileName;
		LoadProfile();
		ProfileSaveName = CachedProfileSaveName;
	}
}

//------------------------------------------------------------------------------
uint32 FContextMenuTargetProfile::GetContextTargetMask() const
{
	return SavedTargetFlags & ~IncompatibleTargetFlags;
}

//------------------------------------------------------------------------------
void FContextMenuTargetProfile::SetContextTarget(EContextTargetFlags::Type Flag, bool bClear)
{
	if (bClear)
	{
		SavedTargetFlags &= ~Flag;
	}
	else
	{
		SavedTargetFlags |= Flag;
	}
	SaveProfile();
}

//------------------------------------------------------------------------------
uint32 FContextMenuTargetProfile::GetIncompatibleTargetsMask() const
{
	return IncompatibleTargetFlags;
}

//------------------------------------------------------------------------------
bool FContextMenuTargetProfile::IsTargetEnabled(EContextTargetFlags::Type Flag) const 
{
	if (IncompatibleTargetFlags & Flag)
	{
		return false;
	}
	else if (Flag == EContextTargetFlags::TARGET_SubComponents)
	{
		return ((SavedTargetFlags|EContextTargetFlags::TARGET_Blueprint) & HasComponentsMask) != 0;
	}
	return true;
}

//------------------------------------------------------------------------------
void FContextMenuTargetProfile::SaveProfile() const
{
	GConfig->SetInt(*ContextMenuTargetProfileImpl::ConfigSection, *ProfileSaveName, SavedTargetFlags, GEditorIni);
}

//------------------------------------------------------------------------------
bool FContextMenuTargetProfile::LoadProfile() 
{
	int32 SavedFlags = SavedTargetFlags;
	if (GConfig->GetInt(*ContextMenuTargetProfileImpl::ConfigSection, *ProfileSaveName, SavedFlags, GEditorIni))
	{
		SavedTargetFlags = SavedFlags;
		return true;
	}
	return false;
}

/*******************************************************************************
 * SBlueprintContextTargetMenu
 ******************************************************************************/

/**  */
namespace BlueprintContextTargetMenuImpl
{
	static FText GetContextTargetDisplayName(UEnum* Enum, EContextTargetFlags::Type ContextTarget);
}

//------------------------------------------------------------------------------
static FText BlueprintContextTargetMenuImpl::GetContextTargetDisplayName(UEnum* Enum, EContextTargetFlags::Type ContextTarget)
{
	if (Enum != nullptr)
	{
		return Enum->GetDisplayNameText(ContextTarget);
	}
	return LOCTEXT("UnrecognizedTarget", "Error: <UNRECOGNIZED>");
}

//------------------------------------------------------------------------------
void SBlueprintContextTargetMenu::Construct(const FArguments& InArgs, const FBlueprintActionContext& MenuContext)
{
	TargetProfile = FContextMenuTargetProfile(MenuContext);
	OnTargetMaskChanged = InArgs._OnTargetMaskChanged;

	FSlateFontInfo HeaderFontStyle = FEditorStyle::GetFontStyle("BlueprintEditor.ActionMenu.ContextDescriptionFont");
	HeaderFontStyle.Size -= 2.f;
	FText const HeaderText = ContextMenuTargetProfileImpl::GetProfileDescription(MenuContext);

	FText const MenuToolTip = LOCTEXT("MenuToolTip", "Select whose functions/variables you want to see.\nNOTE: Unchecking everything is akin to 'SHOW EVERYTHING' (you're choosing to have NO target context and to not limit the scope)");

	TSharedPtr<SHorizontalBox> MenuBody;
	SBorder::Construct(SBorder::FArguments()
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5.f)
		.ToolTipText(MenuToolTip)
		[
			SNew(SBox)
				.MinDesiredWidth(200)
				.ToolTipText(MenuToolTip)
				.Padding(FMargin(0.f, 0.f, 0.f, 18.f))
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
						.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
						.ForegroundColor(FEditorStyle::GetSlateColor("DefaultForeground"))
					[
						SNew(STextBlock)
							.Text(HeaderText)
							.Font(HeaderFontStyle)
					]
				]

				+SVerticalBox::Slot()
					.VAlign(VAlign_Fill)
					.HAlign(HAlign_Fill)
				[
					SAssignNew(MenuBody, SHorizontalBox)
						.ToolTipText(MenuToolTip)
				]
			]
		]
	);

	const uint32 ColumnCount = 2;
	TSharedPtr<SVerticalBox> Columns[ColumnCount];

	for (int32 Col = 0; Col < ColumnCount; ++Col)
	{
		MenuBody->AddSlot()
			.AutoWidth()
		[
			SAssignNew(Columns[Col], SVerticalBox)
				.ToolTipText(MenuToolTip)
		];
	}

	UEnum* const TargetEnum = FindObject<UEnum>(ANY_PACKAGE, TEXT("EContextTargetFlags"));
	uint32 const GreatestFlag = (EContextTargetFlags::ContextTargetFlagsEnd & ~1);

	int32  ColIndex = 0;
	uint32 TargetFlag = (1<<0);
	while (TargetFlag <= GreatestFlag)
	{
		EContextTargetFlags::Type ContextTarget = (EContextTargetFlags::Type)TargetFlag;
		TargetFlag <<= 1;

		if (TargetEnum && TargetEnum->HasMetaData(TEXT("Hidden"), ContextTarget))
		{
			continue;
		}
		
		FText const MenuName = BlueprintContextTargetMenuImpl::GetContextTargetDisplayName(TargetEnum, ContextTarget);
		const FContextMenuTargetProfile& ProfileRef = TargetProfile;

		Columns[ColIndex]->AddSlot()
			.AutoHeight()
			.VAlign(VAlign_Top)
			.Padding(3.f, 2.5f)
		[
			SNew(SCheckBox)
				.IsEnabled_Raw(&TargetProfile, &FContextMenuTargetProfile::IsTargetEnabled, ContextTarget)
				.IsChecked(this, &SBlueprintContextTargetMenu::GetTargetCheckedState, ContextTarget)
				.OnCheckStateChanged(this, &SBlueprintContextTargetMenu::OnTargetCheckStateChanged, ContextTarget)
				.ToolTipText_Lambda([TargetEnum, ContextTarget, &ProfileRef]()->FText
				{
					if (!ProfileRef.IsTargetEnabled(ContextTarget))
					{
						return LOCTEXT("DisabledTargetTooltip", "This target is invalid or redundent for this context.");
					}
					else if (TargetEnum != nullptr)
					{
						return TargetEnum->GetToolTipText(ContextTarget);
					}
					return LOCTEXT("GenericTargetTooltip", "Include variables/functions that belong to this target.");
				})
			[
				SNew(STextBlock).Text(MenuName)
			]
		];
		
		
		ColIndex = (ColIndex + 1) % ColumnCount;
	}
}

//------------------------------------------------------------------------------
uint32 SBlueprintContextTargetMenu::GetContextTargetMask() const
{
	return TargetProfile.GetContextTargetMask();
}

//------------------------------------------------------------------------------
void SBlueprintContextTargetMenu::OnTargetCheckStateChanged(const ECheckBoxState NewCheckedState, EContextTargetFlags::Type ContextTarget)
{
	TargetProfile.SetContextTarget(ContextTarget, (NewCheckedState != ECheckBoxState::Checked));
	OnTargetMaskChanged.ExecuteIfBound(TargetProfile.GetContextTargetMask());
}

//------------------------------------------------------------------------------
ECheckBoxState SBlueprintContextTargetMenu::GetTargetCheckedState(EContextTargetFlags::Type ContextTarget) const
{
	return ((TargetProfile.GetContextTargetMask() & ContextTarget) != 0) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

#undef LOCTEXT_NAMESPACE