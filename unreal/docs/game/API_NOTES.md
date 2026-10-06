# Stream `game` - Unreal API notes

Every Unreal API the `Fourfold` module uses, with the header it comes from. The code was written without Unreal on
the machine.

**How it was verified:** all 18 UE source files of the module, plus one unity TU that includes every source file of
the module, were syntax-checked with `clang++ -std=c++20 -fsyntax-only` against the public headers of a **UE 5.8.2**
source mirror (github.com/AFIshInWater/UE5.8, `Version.h` 5.8.2, sparse checkout), with the engine shared PCH
force-included and mock UHT headers. Result: 0 errors and 0 warnings in `Source/Fourfold` under
`-Wshadow-all -Wundef -Wunused-variable`, and no `-Wdeprecated-declarations` hit in our code (no API we call is marked
`UE_DEPRECATED` in 5.8.2). A negative control (an injected bad member call) is reported, so the check is live. The same
sources were checked earlier against 5.5.2 (github.com/Pekyyyyyy/Toon-UE) and, file by file, 5.7
(github.com/F-Fumino/UE5.7). The tooling is in `Source/Fourfold/Private/Logic/tools/ue_syntax_check/` (see README).

The project targets **5.8.3**. A hotfix rarely changes public signatures, but the check cannot see link errors, UHT
rules (UPROPERTY types, specifiers) or asset-side names. The entry marked "5.8 watch" (an asset-side name) is the place to look first.
These 5.8.2 declarations were also read by hand: `FAnimExtractContext(double, bool, FDeltaTimeRecord, bool)`,
`UAnimSequence::GetAnimationPose(FAnimationPoseData&, const FAnimExtractContext&)`, `FSlateDrawElement::MakeBox /
MakeLines(TArray<FVector2f>) / MakeText(FString)`, `APlayerController::IsInputKeyDown / WasInputKeyJustPressed /
WasInputKeyJustReleased / GetInputAnalogKeyState / ActivateTouchInterface`, `FSlateApplicationBase::GetSafeZoneSize`,
`FPlatformApplicationMisc::GetPhysicalScreenDensity`, `FPlatformMisc::Prepare / Trigger / ReleaseMobileHaptics`,
`UGameplayStatics::SetGlobalTimeDilation`, and `UEnhancedPlayerInput::InputKey` (plugin source) still calling
`Super::InputKey`, so the key state the game reads is filled. Paths are relative to `Engine/Source/Runtime/`.

## Module, logging, subsystems

| API | Header | Notes |
|---|---|---|
| `IMPLEMENT_PRIMARY_GAME_MODULE`, `DEFINE_LOG_CATEGORY`, `UE_LOG` | Core/Public/Modules/ModuleManager.h, Logging/LogMacros.h | 5.8 checks UE_LOG format arguments at compile time; every `%s` gets `*FString`. |
| `UTickableWorldSubsystem` (`Initialize` must call Super before ticking, `GetStatId`, `IsTickableWhenPaused`, `ShouldCreateSubsystem`) | Engine/Public/Subsystems/WorldSubsystem.h | `EWorldType::Game / PIE` filter. |
| `UGameInstanceSubsystem`, `UGameInstance::GetSubsystem<T>()`, `UWorld::GetSubsystem<T>()` | Engine/Public/Subsystems/GameInstanceSubsystem.h, Engine/Classes/Engine/GameInstance.h, Engine/Classes/Engine/World.h | |
| `UGameplayStatics::SetGlobalTimeDilation` | Engine/Classes/Kismet/GameplayStatics.h | Hit-stop 0.05, slow-motion 0.55. |
| `FApp::GetDeltaTime` | Core/Public/Misc/App.h | Undilated frame time. |
| `FPlatformTime::Seconds` | Core/Public/HAL/PlatformTime.h | |
| `FPaths::ProjectSavedDir / ProjectContentDir / GetPath`, `FFileHelper::LoadFileToString / SaveStringToFile (EEncodingOptions::ForceUTF8WithoutBOM)`, `IFileManager::Get().MakeDirectory` | Core/Public/Misc/Paths.h, Misc/FileHelper.h, HAL/FileManager.h | All persistence goes under `Saved/Fourfold/`. JSON goes through `ff::ParseJson` / `ff::ToJson` (no `FJsonObject`). |
| `IConsoleManager::Get().FindConsoleVariable("t.MaxFPS")->Set(v, ECVF_SetByGameSetting)` | Core/Public/HAL/IConsoleManager.h | Frame cap. |
| `Scalability::GetQualityLevels / SetQualityLevels / FQualityLevels::SetFromSingleQualityLevel` | Engine/Public/Scalability.h | Applied only when the tier changes. |
| `FPlatformMisc::NumberOfCores`, `FPlatformMemory::GetConstants().TotalPhysical` | Core/Public/HAL/PlatformMisc.h, PlatformMemory.h | Auto quality. |
| `FParse::Value(FCommandLine::Get(), ...)` | Core/Public/Misc/Parse.h, Misc/CommandLine.h | `-scenario=` / `-autoplay=`. |
| `FCoreDelegates::ApplicationWillDeactivateDelegate / ApplicationWillEnterBackgroundDelegate / ApplicationHasReactivatedDelegate` | Core/Public/Misc/CoreDelegates.h | `TMulticastDelegate<void()>`, bound with `AddUObject`. |
| `FPlatformMisc::PrepareMobileHaptics(EMobileHapticsType) / TriggerMobileHaptics / ReleaseMobileHaptics` | Core/Public/GenericPlatform/GenericPlatformMisc.h | No-ops on Mac. |

## Actors, game framework, input

| API | Header | Notes |
|---|---|---|
| `AGameModeBase` (`DefaultPawnClass`, `SpectatorClass`, `PlayerControllerClass`, `RestartPlayer`) | Engine/Classes/GameFramework/GameModeBase.h | `SpectatorClass = nullptr` is checked by `APlayerController::SpawnSpectatorPawn` (PlayerController.cpp). |
| `APlayerController::PlayerTick` (input processed by Super), `IsInputKeyDown`, `WasInputKeyJustPressed`, `GetInputAnalogKeyState`, `GetViewportSize`, `SetViewTarget`, `SetInputMode`, `ActivateTouchInterface(nullptr)`, `bShowMouseCursor`, `bAutoManageActiveCameraTarget`, `PlayerInput` | Engine/Classes/GameFramework/PlayerController.h | |
| `UPlayerInput::GetRawKeyValue(EKeys::MouseX / MouseY)` | Engine/Classes/GameFramework/PlayerInput.h | Raw pixels. `GetInputMouseDelta` returns axis-scaled values (`MassageAxisInput`). MouseY is up-positive. |
| `FInputModeGameAndUI` (`SetHideCursorDuringCapture`, `SetLockMouseToViewportBehavior`), `FInputModeGameOnly::SetConsumeCaptureMouseDown` | Engine/Classes/GameFramework/PlayerController.h | F1 toggles captured mouse-look. |
| `EKeys::*` (J K L U N H Q E One..Four Tab SpaceBar Escape BackSpace Enter F1 F2 arrows W A S D, LeftMouseButton RightMouseButton MiddleMouseButton, Gamepad_FaceButton_*, Gamepad_*Shoulder, Gamepad_*TriggerAxis, Gamepad_DPad_*, Gamepad_RightThumbstick, Gamepad_Special_Right, Gamepad_LeftX/LeftY/RightX/RightY), `FKey::IsGamepadKey / IsValid` | InputCore/Classes/InputCoreTypes.h | |
| `UWorld::SpawnActor<T>(UClass*, FTransform, FActorSpawnParameters)`, `ESpawnActorCollisionHandlingMethod::AlwaysSpawn`, `RF_Transient`, `TActorIterator<AActor>`, `AActor::ActorHasTag`, `AActor::Destroy`, `SetActorLocationAndRotation` | Engine/Classes/Engine/World.h, EngineUtils.h, GameFramework/Actor.h | |
| `UGameViewportClient::AddViewportWidgetContent / RemoveViewportWidgetContent / GetViewportSize`, `UWorld::GetGameViewport` | Engine/Classes/Engine/GameViewportClient.h | One root widget, Z-order 10. |
| `UCameraComponent` (`SetFieldOfView`, `FieldOfView`, `bConstrainAspectRatio`), `FTransform::InverseTransformPositionNoScale` | Engine/Classes/Camera/CameraComponent.h, Core/Public/Math/TransformNonVectorized.h | HUD projection uses the rig camera of the current frame (horizontal FOV, MaintainXFOV). |
| `UStaticMeshComponent` (`SetStaticMesh`, `SetRenderInMainPass`, `SetCollisionEnabled`), `ConstructorHelpers::FObjectFinder` (`/Engine/BasicShapes/Cube`, `BasicShapeMaterial`), `UMaterialInstanceDynamic::Create / SetVectorParameterValue / SetScalarParameterValue` | Engine/Classes/Components/StaticMeshComponent.h, UObject/ConstructorHelpers.h, Engine/Classes/Materials/MaterialInstanceDynamic.h | **5.8 watch:** the color parameter name of BasicShapeMaterial. Both "Color" and "BaseColor" are set. |
| `DrawDebugBox / DrawDebugSphere / DrawDebugCircle / DrawDebugCapsule` | Engine/Public/DrawDebugHelpers.h | Compiled out in Shipping. |

## Skeletal mesh + native animation runtime

| API | Header | Notes |
|---|---|---|
| `USkeletalMeshComponent::SetSkeletalMeshAsset`, `SetAnimationMode(EAnimationMode::AnimationBlueprint)`, `SetAnimInstanceClass`, `GetAnimInstance`, `VisibilityBasedAnimTickOption`, `SetTickGroup(TG_PostUpdateWork)` | Engine/Classes/Components/SkeletalMeshComponent.h, SkinnedMeshComponent.h | Tick group after the subsystem tick (LevelTick.cpp order). `SetSkeletalMeshAsset` replaced `SetSkeletalMesh` in 5.1; present in 5.8.2. |
| `UAnimInstance::CreateAnimInstanceProxy / DestroyAnimInstanceProxy`, `FAnimInstanceProxy::PreUpdate / Evaluate(FPoseContext&) -> bool` | Engine/Classes/Animation/AnimInstance.h, Engine/Public/Animation/AnimInstanceProxy.h | Pattern verified against open-source native proxies (see the stream notes). |
| `FPoseContext`, `FCompactPose::ForEachBoneIndex`, `FCompactPoseBoneIndex`, `FBoneContainer::GetReferenceSkeleton / MakeMeshPoseIndex / GetParentBoneIndex`, `FReferenceSkeleton::FindBoneIndex / GetRefBonePose` | Engine/Public/Animation/AnimNodeBase.h, BonePose.h, BoneContainer.h, Engine/Classes/Animation/AnimTypes.h | Model space built from the parent chain by hand. |
| `UAnimSequence::GetAnimationPose(FAnimationPoseData&, const FAnimExtractContext&)`, `FAnimExtractContext(double, bool, FDeltaTimeRecord, bool)`, `GetPlayLength` | Engine/Classes/Animation/AnimSequence.h, AnimationAsset.h | Constructor unchanged in 5.8.2. |
| `LoadObject<UAnimSequence / USkeletalMesh>(nullptr, Path)` | CoreUObject/Public/UObject/UObjectGlobals.h | Object paths `/Game/.../A_x.A_x`. |
| `FQuat` / `FTransform` / `FVector` math (`Slerp`, `GetNormalized`, `Inverse`, `TransformPosition`, `InverseTransformVectorNoScale`) | Core/Public/Math/*.h | |

## Slate UI (HUD, touch overlay, menus, Lab panel)

| API | Header | Notes |
|---|---|---|
| `SLeafWidget`, `SCompoundWidget`, `SLATE_BEGIN_ARGS / SLATE_ATTRIBUTE / SLATE_ARGUMENT / SLATE_EVENT`, `SNew / SAssignNew`, `TAttribute<T>::CreateLambda`, `TAttribute::Get(Default)` | SlateCore/Public/Widgets/SLeafWidget.h, SCompoundWidget.h, DeclarativeSyntaxSupport.h, Core/Public/Misc/Attribute.h | |
| `SWidget::OnPaint` (const, 7 args), `ComputeDesiredSize(float)`, `Tick(const FGeometry&, double, float)`, `ForceVolatile`, `SetCanTick`, `SetVisibility`, `HasMouseCapture`, `IsEnabled`, `SupportsKeyboardFocus` | SlateCore/Public/Widgets/SWidget.h | The UI widgets are volatile (repainted every frame). |
| `OnTouchStarted / OnTouchMoved / OnTouchEnded`, `OnMouseButtonDown / Up / Move / Wheel`, `OnMouseEnter / Leave`, `OnMouseCaptureLost(const FCaptureLostEvent&)` (`PointerIndex`) | SlateCore/Public/Widgets/SWidget.h, Input/Events.h | Touch pointer indices 0..9 (`ETouchIndex`), mouse = `FSlateApplicationBase::CursorPointerIndex`. |
| `FPointerEvent::GetPointerIndex / IsTouchEvent / GetEffectingButton / GetScreenSpacePosition`, `FGeometry::AbsoluteToLocal / GetLocalSize / Scale` | SlateCore/Public/Input/Events.h, Layout/Geometry.h | Touch events carry `EKeys::LeftMouseButton` as the effecting button. |
| `FReply::Handled / Unhandled / CaptureMouse(SharedThis(this)) / ReleaseMouseCapture` | SlateCore/Public/Input/Reply.h | Capture is per pointer for touches. |
| `FSlateDrawElement::MakeBox(List, Layer, PaintGeometry, Brush, Effects, Tint)`, `MakeLines(..., TArray<FVector2f>, Effects, Tint, bAntialias, Thickness)`, `MakeText(..., const FString&, const FSlateFontInfo&, Effects, Tint)` | SlateCore/Public/Rendering/DrawElementTypes.h | MakeBox skips a fully transparent tint unless the outline is visible (DrawElementTypes.cpp `ShouldCull`). Line thickness is in local units. Payloads copy the brush, so stack brushes are safe. Signatures unchanged in 5.8.2. |
| `FGeometry::ToPaintGeometry()`, `ToPaintGeometry(FVector2f Size, FSlateLayoutTransform(FVector2f Offset))` | SlateCore/Public/Layout/Geometry.h, Rendering/SlateLayoutTransform.h | The (offset, size, scale) overload is deprecated since 5.2 and is not used. |
| `FSlateRoundedBoxBrush(Fill, Radius, OutlineColor, OutlineWidth)`, `FSlateBrush::OutlineSettings.CornerRadii`, `FSlateNoResource` | SlateCore/Public/Brushes/SlateRoundedBoxBrush.h, Styling/SlateBrush.h | Circles, rings, pills and cards. The fill comes from the MakeBox tint. The outline is drawn inside the box. |
| `FCoreStyle::GetDefaultFontStyle(FName, float, FFontOutlineSettings)`, `FSlateFontInfo::{Size, OutlineSettings, LetterSpacing}` | SlateCore/Public/Styling/CoreStyle.h, Fonts/SlateFontInfo.h | Size is in points at 96 dpi, so px = Size * 4/3. |
| `FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(FStringView, FSlateFontInfo)` | Slate/Public/Framework/Application/SlateApplication.h, SlateCore/Public/Fonts/FontMeasure.h | |
| `FSlateApplication::Get().GetSafeZoneSize(FMargin&, FVector2f)` | SlateCore/Public/Application/SlateApplicationBase.h | Pixels, divided by the geometry scale (same as `SSafeZone`). |
| `FPlatformApplicationMisc::GetPhysicalScreenDensity(int32&)`, `EScreenPhysicalAccuracy` | ApplicationCore/Public/GenericPlatform/GenericPlatformApplicationMisc.h | iOS reports 163 x content scale. Desktop is floored (`ffg::PxPerMm`). |
| `SOverlay`, `SVerticalBox / SHorizontalBox` (`AddSlot().AutoHeight / FillHeight / FillWidth / Padding`, `NumSlots`), `SScrollBox` (`ScrollBarThickness(FVector2f)`, `ScrollDescendantIntoView`), `SBox` (`SetHAlign / SetVAlign / SetWidthOverride / SetMaxDesiredWidth / SetMaxDesiredHeight`), `SBorder` (`SetContent`, `BorderImage`, `BorderBackgroundColor_Lambda`), `SWrapBox` (`UseAllottedSize`, `InnerSlotPadding`), `STextBlock` (`Font`, `ColorAndOpacity`, `AutoWrapText`), `SSpacer` | Slate/Public/Widgets/Layout/*.h, SlateCore/Public/Widgets/SOverlay.h, SBoxPanel.h, Slate/Public/Widgets/Text/STextBlock.h | SBox overrides only size the box itself (SBox.cpp `OnArrangeChildren`), so alignment is applied one box up. |
| `FString::Printf` (literal formats only), `FString::Join`, `FText::FromString`, `TCHAR_TO_UTF8 / UTF8_TO_TCHAR` | Core/Public/Containers/UnrealString.h, Internationalization/Text.h, Containers/StringConv.h | UE5 rejects non-literal Printf formats with a static_assert. Precision variants go through a switch. |

## Risks to check first on the Mac (not covered by the header check)

1. UHT: every `UPROPERTY` / `UCLASS` specifier (the mock UHT does not validate them).
2. Linking: every engine module a symbol comes from must be in `Fourfold.Build.cs` (`Core CoreUObject Engine InputCore
   Slate SlateCore ApplicationCore FourfoldCore`).
3. The BasicShapeMaterial color parameter name (placeholder arena tint): both "Color" and "BaseColor" are set.
4. Runtime behaviour on device: safe-area insets, physical screen density, haptics and multi-touch indices.
