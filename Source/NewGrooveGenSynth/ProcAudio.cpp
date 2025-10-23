// Fill out your copyright notice in the Description page of Project Settings.

// ProcAudio.cpp
#include "ProcAudio.h"
#include "GrooveSynthComponent.h"

// These two includes aren't strictly required (they're pulled via Engine.h),
// but adding them makes intent clear and helps IDEs resolve symbols.
#include "GameFramework/PlayerController.h" // APlayerController
#include "InputCoreTypes.h"                 // EKeys

// ============================================================================
// AProcAudio
// Placeable actor that owns the GrooveSynth component and binds a few hotkeys.
// ============================================================================

// Sets default values and creates the synth component
AProcAudio::AProcAudio()
{
 	// Set this actor to call Tick() every frame. Turned off to improve performance
 	// don't need per-frame game logic for this actor.
	PrimaryActorTick.bCanEverTick = false;
	// Create the synth as a child component (also makes it visible in Details)
	Synth = CreateDefaultSubobject<UGrooveSynthComponent>(TEXT("GrooveSynth"));
	// Make the synth the root so we can just drop the actor into a level
	RootComponent = Synth;
}

// Called when the game starts or when spawned
void AProcAudio::BeginPlay()
{
	Super::BeginPlay();

	// In development builds, ensure we actually created the component.
	// If Synth were null, this logs and safely returns.
	if (!ensure(Synth != nullptr)) return;

	// Start the synth the first time play begins.
	// This constructs the ISoundGenerator and starts pulling audio buffers.
	if (Synth && !Synth->IsPlaying()) Synth->Start();  // this will spawn the FGrooveSoundGenerator

	// ---- Keyboard control without a Pawn/Character ----
	// We explicitly enable input on THIS actor using the first player controller.
	// Click the PIE viewport to ensure it has focus before trying keys.
	// Enabling input on this actor even though there’s no Pawn
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		EnableInput(PC);

		// InputComponent is created by EnableInput. Always guard against null.
		if (InputComponent)
		{
			// W/S = BPM up/down
			InputComponent->BindKey(EKeys::W,      IE_Pressed, this, &AProcAudio::OnBpmUp);
			InputComponent->BindKey(EKeys::S, IE_Pressed, this, &AProcAudio::OnBpmDown);
			// A = cycle scale; D = reseed
			InputComponent->BindKey(EKeys::A,        IE_Pressed, this, &AProcAudio::OnCycleScale);
			InputComponent->BindKey(EKeys::D,        IE_Pressed, this, &AProcAudio::OnReseed);

			// Tip: swap to EKeys::Equals / EKeys::Hyphen if you prefer +/-
			// InputComponent->BindKey(EKeys::Equals, IE_Pressed, this, &AProcAudio::OnBpmUp);
			// InputComponent->BindKey(EKeys::Hyphen, IE_Pressed, this, &AProcAudio::OnBpmDown);
		}
	}
}

// ---------------------------------------------------------------------------
// Hotkey handlers (thin wrappers that call the public BlueprintCallable funcs)
// ---------------------------------------------------------------------------
void AProcAudio::OnBpmUp()      { NudgeBPM(+2.f); }     // +2 BPM is a nice perceptible step for testing
void AProcAudio::OnBpmDown()    { NudgeBPM(-2.f); }
void AProcAudio::OnCycleScale() { CycleScale(+1); }  // +1 cycles forward through the EProcScale enum (wraps in CycleScale)
void AProcAudio::OnReseed()     { ReseedNow(); }

// ---------------------------------------------------------------------------
// Public controls (also callable from Level BP / widgets)
// ---------------------------------------------------------------------------
void AProcAudio::NudgeBPM(float Delta)
{
	if (!Synth) return;
	// Clamp to a sane musical range. Adjust to taste.
	Synth->BPM = FMath::Clamp(Synth->BPM + Delta, 60.f, 160.f);
}

void AProcAudio::CycleScale(int32 Dir)
{
	if (!Synth) return;

	// Wrap an int around the 4 enum values [0..3]
	constexpr int32 Count = 4;
	const int32 Cur   = static_cast<int32>(Synth->Scale);
	const int32 Next  = (Cur + Dir + Count) % Count;

	Synth->Scale = static_cast<EProcScale>(Next);
	// Optional: reseed so the pattern layout stays deterministic for a seed
	Synth->Reseed(Synth->Seed);
	//vvv-----old code-----vvv
	/* int32 v = (static_cast<int32>(Synth->Scale) + Dir + 4) % 4;
	// v = (v + Dir + 4) % 4;---shortened to above---^^^
	Synth->Scale = static_cast<EProcScale>(v);
	Synth->Reseed(Synth->Seed);  // keep pattern deterministic per seed */
}

void AProcAudio::ReseedNow()
{
	if (!Synth) return;
	// New random seed → new evolving pattern
	Synth->Reseed(FMath::Rand());
}

void AProcAudio::UpdateFromSpeed(float Speed, float MaxSpeed)
{
	if (!Synth) return;
	// Map an arbitrary "speed" (e.g., player velocity) to 0..1 motion amount.
	Synth->SetMotionAmount(FMath::Clamp(Speed / FMath::Max(1.f, MaxSpeed), 0.f, 1.f));
}

/*
// If you later want to drive visuals every frame, uncomment Tick here and in the header.
void AProcAudio::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Example: read Synth->AnRMS.load() and feed a material parameter.
}
*/