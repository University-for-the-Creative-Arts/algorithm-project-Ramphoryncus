// Fill out your copyright notice in the Description page of Project Settings.

// ProcAudio.cpp
#include "ProcAudio.h"
#include "GrooveSynthComponent.h"

#include "Engine/Engine.h"                     // GEngine and on-screen messages
#include "GameFramework/PlayerController.h"   // APlayerController
#include "InputCoreTypes.h"                   // EKeys

// ============================================================================
// AProcAudio
//
// Placeable actor that owns the procedural synth component and provides
// keyboard interaction without requiring a Pawn or Character.
// ============================================================================

AProcAudio::AProcAudio()
{
    // This actor does not require per-frame game-thread processing.
    PrimaryActorTick.bCanEverTick = false;

    // Create the procedural synthesizer as an actor component.
    Synth = CreateDefaultSubobject<UGrooveSynthComponent>(TEXT("GrooveSynth"));

    // Making the synth the root allows this actor to be placed directly
    // into a level without requiring another scene component.
    RootComponent = Synth;
}

void AProcAudio::BeginPlay()
{
    Super::BeginPlay();

    // Verify that the synth component was created successfully.
    if (!ensure(Synth != nullptr))
    {
        return;
    }

    // Start procedural audio generation when play begins.
    if (!Synth->IsPlaying())
    {
        Synth->Start();
    }

    // Enable keyboard input directly on this actor.
    if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
    {
        EnableInput(PlayerController);

        if (InputComponent)
        {
            InputComponent->BindKey(
                EKeys::W,
                IE_Pressed,
                this,
                &AProcAudio::OnBpmUp);

            InputComponent->BindKey(
                EKeys::S,
                IE_Pressed,
                this,
                &AProcAudio::OnBpmDown);

            InputComponent->BindKey(
                EKeys::A,
                IE_Pressed,
                this,
                &AProcAudio::OnCycleScale);

            InputComponent->BindKey(
                EKeys::D,
                IE_Pressed,
                this,
                &AProcAudio::OnReseed);
        }
    }

    // Display the controls when the prototype starts.
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(
            -1,
            8.0f,
            FColor::Cyan,
            TEXT("PROCEDURAL SYNTH\nW/S: Change BPM\nA: Change Scale\nD: Generate New Pattern"));
    }
}

// ============================================================================
// Keyboard handlers
// ============================================================================

void AProcAudio::OnBpmUp()
{
    NudgeBPM(+2.0f);
}

void AProcAudio::OnBpmDown()
{
    NudgeBPM(-2.0f);
}

void AProcAudio::OnCycleScale()
{
    CycleScale(+1);
}

void AProcAudio::OnReseed()
{
    ReseedNow();
}

// ============================================================================
// Public procedural controls
// ============================================================================

void AProcAudio::NudgeBPM(float Delta)
{
    if (!Synth)
    {
        return;
    }

    // Keep the generated music within a practical tempo range.
    Synth->BPM = FMath::Clamp(Synth->BPM + Delta, 60.0f, 160.0f);

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(
            -1,
            2.0f,
            FColor::Green,
            FString::Printf(TEXT("BPM: %.0f"), Synth->BPM));
    }
}

void AProcAudio::CycleScale(int32 Direction)
{
    if (!Synth)
    {
        return;
    }

    // Wrap around the four values in EProcScale.
    constexpr int32 ScaleCount = 4;

    const int32 CurrentScale = static_cast<int32>(Synth->Scale);
    const int32 NextScale =
        (CurrentScale + Direction + ScaleCount) % ScaleCount;

    Synth->Scale = static_cast<EProcScale>(NextScale);

    // Human-readable scale name for demonstration feedback.
    const TCHAR* ScaleName = TEXT("Unknown");

    switch (Synth->Scale)
    {
        case EProcScale::Ionian:
            ScaleName = TEXT("Ionian");
            break;

        case EProcScale::Dorian:
            ScaleName = TEXT("Dorian");
            break;

        case EProcScale::MinorPentatonic:
            ScaleName = TEXT("Minor Pentatonic");
            break;

        case EProcScale::HarmonicMinor:
            ScaleName = TEXT("Harmonic Minor");
            break;
    }

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(
            -1,
            2.0f,
            FColor::Yellow,
            FString::Printf(TEXT("Scale: %s"), ScaleName));
    }
}

void AProcAudio::ReseedNow()
{
    if (!Synth)
    {
        return;
    }

    // A new random seed causes the procedural generator to produce
    // a different sequence of musical decisions.
    const int32 NewSeed = FMath::Rand();
    Synth->Reseed(NewSeed);

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(
            -1,
            2.0f,
            FColor::Magenta,
            FString::Printf(TEXT("New procedural seed: %d"), NewSeed));
    }
}

void AProcAudio::UpdateFromSpeed(float Speed, float MaxSpeed)
{
    if (!Synth)
    {
        return;
    }

    // Convert an external gameplay value into a normalised 0–1 control.
    const float NormalisedSpeed =
        FMath::Clamp(
            Speed / FMath::Max(1.0f, MaxSpeed),
            0.0f,
            1.0f);

    Synth->SetMotionAmount(NormalisedSpeed);
}