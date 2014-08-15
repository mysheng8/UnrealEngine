// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "MergePrivatePCH.h"

#include "BlueprintEditor.h"
#include "BlueprintEditorModes.h"
#include "IDiffControl.h"
#include "ISourceControlModule.h"
#include "SBlueprintEditorToolbar.h"
#include "SBlueprintMerge.h"
#include "SMergeDetailsView.h"
#include "SMergeGraphView.h"

#define LOCTEXT_NAMESPACE "SBlueprintMerge"

SBlueprintMerge::SBlueprintMerge()
	: Data()
	, CurrentDiffControl( NULL )
{
}

void SBlueprintMerge::Construct(const FArguments InArgs, const FBlueprintMergeData& InData)
{
	check( InData.OwningEditor.Pin().IsValid() );

	Data = InData;

	TSharedPtr<SMergeGraphView> GraphView = SNew( SMergeGraphView, InData );
	GraphControl.Widget = GraphView;
	GraphControl.DiffControl = GraphView.Get();

	TreeControl.Widget = SNew(SBorder);

	auto DetailsView = SNew( SMergeDetailsView, InData );
	DetailsControl.Widget = DetailsView;
	DetailsControl.DiffControl = &DetailsView.Get();

	FToolBarBuilder ToolbarBuilder(TSharedPtr< FUICommandList >(), FMultiBoxCustomization::None);
	ToolbarBuilder.AddToolBarButton( 
		FUIAction( FExecuteAction::CreateSP(this, &SBlueprintMerge::PrevDiff), FCanExecuteAction::CreateSP(this, &SBlueprintMerge::CanCycleDiffs) )
		, NAME_None
		, LOCTEXT("PrevMergeLabel", "Prev")
		, LOCTEXT("PrevMergeTooltip", "Go to previous difference")
		, FSlateIcon(FEditorStyle::GetStyleSetName(), "BlueprintMerge.PrevDiff")
	);
	ToolbarBuilder.AddToolBarButton(
		FUIAction(FExecuteAction::CreateSP(this, &SBlueprintMerge::NextDiff), FCanExecuteAction::CreateSP(this, &SBlueprintMerge::CanCycleDiffs))
		, NAME_None
		, LOCTEXT("NextMergeLabel", "Next")
		, LOCTEXT("NextMergeTooltip", "Go to next difference")
		, FSlateIcon(FEditorStyle::GetStyleSetName(), "BlueprintMerge.NextDiff") 
	);
	ToolbarBuilder.AddSeparator();
	ToolbarBuilder.AddToolBarButton(
		FUIAction(FExecuteAction::CreateSP(this, &SBlueprintMerge::OnAcceptResultClicked) )
		, NAME_None
		, LOCTEXT("FinishMergeLabel", "Finish Merge")
		, LOCTEXT("FinishMergeTooltip", "Complete the merge operation - saves the blueprint and resolves the conflict with the SCC provider")
		, FSlateIcon(FEditorStyle::GetStyleSetName(), "BlueprintMerge.Finish")
	);
	ToolbarBuilder.AddToolBarButton(
		FUIAction(FExecuteAction::CreateSP(this, &SBlueprintMerge::OnCancelClicked))
		, NAME_None
		, LOCTEXT("CancelMergeLabel", "Cancel")
		, LOCTEXT("CancelMergeTooltip", "Abort the merge operation")
		, FSlateIcon(FEditorStyle::GetStyleSetName(), "BlueprintMerge.Cancel")
	);

	ChildSlot [
		SNew( SVerticalBox )
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew( SHorizontalBox )
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				ToolbarBuilder.MakeWidget()
			]
		]
		+ SVerticalBox::Slot()
		[
			SAssignNew(MainView, SBorder)
			.BorderImage(FEditorStyle::GetBrush("NoBorder"))
		]
	];

	InData.OwningEditor.Pin()->OnModeSet().AddSP( this, &SBlueprintMerge::OnModeChanged );
	OnModeChanged( InData.OwningEditor.Pin()->GetCurrentMode() );
}

template< typename T >
static void CopyToShared( const TArray<T>& Source, TArray< TSharedPtr<T> >& Dest )
{
	check( Dest.Num() == 0 );
	for( auto i : Source )
	{
		Dest.Add( MakeShareable(new T( i )) );
	}
}

static void WriteBackup( UPackage& Package, const FString& Directory, const FString& Filename )
{
	if( GEditor )
	{
		const FString DestinationFilename = Directory + FString("/") + Filename;
		FString OriginalFilename;
		if( FPackageName::DoesPackageExist(Package.GetName(), NULL, &OriginalFilename) )
		{
			IFileManager::Get().Copy(*DestinationFilename, *OriginalFilename);
		}
	}
}

UBlueprint* SBlueprintMerge::GetTargetBlueprint()
{
	return Data.OwningEditor.Pin()->GetBlueprintObj();
}

void SBlueprintMerge::NextDiff()
{
	CurrentDiffControl->NextDiff();
}

void SBlueprintMerge::PrevDiff()
{
	CurrentDiffControl->PrevDiff();
}

bool SBlueprintMerge::CanCycleDiffs() const
{
	return CurrentDiffControl && CurrentDiffControl->HasDifferences();
}

void SBlueprintMerge::OnAcceptResultClicked()
{
	// Because merge operations are so destructive and can be confusing, lets write backups of the files involved:
	const FString BackupSubDir = FPaths::GameSavedDir() / TEXT("Backup") / TEXT("Resolve_Backup[") + FDateTime::Now().ToString(TEXT("%Y-%m-%d-%H-%M-%S")) + TEXT("]");
	WriteBackup(*Data.BlueprintRemote->GetOutermost(), BackupSubDir, TEXT("RemoteAsset") + FPackageName::GetAssetPackageExtension() );
	WriteBackup(*Data.BlueprintBase->GetOutermost(), BackupSubDir, TEXT("CommonBaseAsset") + FPackageName::GetAssetPackageExtension() );
	WriteBackup(*Data.BlueprintLocal->GetOutermost(), BackupSubDir, TEXT("LocalAsset") + FPackageName::GetAssetPackageExtension());

	UPackage* Package = GetTargetBlueprint()->GetOutermost();
	TArray<UPackage*> PackagesToSave;
	PackagesToSave.Add(Package);

	// Perform the resolve with the SCC plugin, we do this first so that the editor doesn't warn about writing to a file that is unresolved:
	ISourceControlModule::Get().GetProvider().Execute(ISourceControlOperation::Create<FResolve>(), SourceControlHelpers::PackageFilenames(PackagesToSave), EConcurrency::Synchronous);

	const auto Result = FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, /*bCheckDirty=*/ false, /*bPromptToSave=*/ false);
	if (Result != FEditorFileUtils::PR_Success)
	{
		static const float MessageTime = 5.0f;
		const FText ErrorMessage = FText::Format( LOCTEXT("MergeWriteFailedError", "Failed to write merged files, please look for backups in {0}"), FText::FromString(BackupSubDir) );

		FNotificationInfo ErrorNotification(ErrorMessage);
		FSlateNotificationManager::Get().AddNotification( ErrorNotification );
	}

	Data.OwningEditor.Pin()->CloseMergeTool();
}

void SBlueprintMerge::OnCancelClicked()
{
	Data.OwningEditor.Pin()->CloseMergeTool();
}

void SBlueprintMerge::OnModeChanged(FName NewMode)
{
	if (NewMode == FBlueprintEditorApplicationModes::StandardBlueprintEditorMode ||
		NewMode == FBlueprintEditorApplicationModes::BlueprintMacroMode)
	{
		MainView->SetContent( GraphControl.Widget.ToSharedRef() );
		CurrentDiffControl = GraphControl.DiffControl;
	}
	else if (NewMode == FBlueprintEditorApplicationModes::BlueprintComponentsMode)
	{
		MainView->SetContent(TreeControl.Widget.ToSharedRef());
		CurrentDiffControl = TreeControl.DiffControl;
	}
	else if (NewMode == FBlueprintEditorApplicationModes::BlueprintDefaultsMode ||
			NewMode == FBlueprintEditorApplicationModes::BlueprintInterfaceMode)
	{
		MainView->SetContent(DetailsControl.Widget.ToSharedRef());
		CurrentDiffControl = DetailsControl.DiffControl;
	}
	else
	{
		ensureMsgf(false, TEXT("Diff panel does not support mode %s"), *NewMode.ToString());
	}
}

#undef LOCTEXT_NAMESPACE
