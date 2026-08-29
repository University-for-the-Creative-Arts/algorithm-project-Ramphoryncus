// Fill out your copyright notice in the Description page of Project Settings.
// GrooveSynthComponent.h
#pragma once
// ^ Standard guard; Unreal uses pragma-once everywhere.
#include "CoreMinimal.h"
#include "Components/SynthComponent.h" // correct #include for USynthComponent
#include "Sound/SoundGenerator.h" // defines Audio::ISoundGeneratorPtr etc.
// NOTE: Forward-declaring the generator types below and include the real
//       header in the .cpp to keep this header light (faster builds).

// Forward declarations (to match Engine's global types)
class ISoundGenerator;  // interface for audio generators
struct FSoundGeneratorInitParams;   // construction params for generator

#include <atomic>  // lock-free meters for the visualizer
#include "GrooveSynthComponent.generated.h"  // MUST be the last include in a UCLASS header

// ---------- Musical scale exposed to Blueprints ----------
UENUM(BlueprintType)
enum class EProcScale : uint8
{
    Ionian,
    Dorian,
    MinorPentatonic,
    HarmonicMinor
};

// ---------- Main component: drives the procedural audio ----------
UCLASS(ClassGroup=Audio, meta=(BlueprintSpawnableComponent))
class NEWGROOVEGENSYNTH_API UGrooveSynthComponent : public USynthComponent
{
    GENERATED_BODY()
public:
	// Use the ObjectInitializer constructor for UObjects/components.
    UGrooveSynthComponent(const FObjectInitializer& ObjectInitializer);

	// ---- User parameters (editable in Details / BP) ----
	// The generator reads these once per audio block (no locks needed).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProcAudio", meta=(ClampMin="60.0", ClampMax="200.0"))
	float BPM = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProcAudio")
	int32 RootMidi = 60;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProcAudio")
	EProcScale Scale = EProcScale::Ionian;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProcAudio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Density = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProcAudio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Brightness = 0.5f;
	// Seed controls determinism of the pattern/RNG
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProcAudio")
	int32 Seed = 12345;
	// Feature toggles
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProcAudio") bool bArpOn = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProcAudio") bool bPadOn = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProcAudio") bool bPercOn = true;

	// ---- Blueprint controls ----
	// Reseed pattern on demand (e.g., bound to a key/button)
    UFUNCTION(BlueprintCallable, Category="ProcAudio")
    void Reseed(int32 NewSeed);
	// Drive extra motion from gameplay (0..1), e.g., player speed
    UFUNCTION(BlueprintCallable, Category="ProcAudio")
    void SetMotionAmount(float Normalized01);

protected:
    // ✔ Match your engine: shared pointer + global params
	// UE audio entry point: return an audio generator instance for this component.
	// In your Engine build, ISoundGeneratorPtr is a TSharedPtr<ISoundGenerator, ThreadSafe>.
    virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;

private:
	// Motion is set on the game thread and read by the audio thread once per block.
    float Motion = 0.f;
	// Let the generator access private fields without getters (purely convenience).
	friend class FGrooveSoundGenerator;  // allows generator to read members
public:
	// ---- Lock-free meters used by your visualizer ----
	// Not UPROPERTY on purpose (atomics aren’t UObjects/GC’d; read from game thread).
    std::atomic<float> AnRMS{0.f},
						AnArpEnv{0.f},
						AnPadEnv{0.f},
						AnPercEnv{0.f},
						AnBass{0.f},
						AnMid{0.f},
						AnTreble{0.f};
};

