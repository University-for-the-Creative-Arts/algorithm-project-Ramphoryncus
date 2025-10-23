// Fill out your copyright notice in the Description page of Project Settings.
// ProcAudio.h
#pragma once
// ^ Standard "include guard" for Unreal headers. Ensures the file is compiled only once per TU.
#include "CoreMinimal.h"          // Core UE types/macros (FString, FVector, UE_LOG, etc.)
#include "GameFramework/Actor.h"  // Base class for AActor
#include "ProcAudio.generated.h"  // Must be the LAST include in a UCLASS header (UHT needs it)

// Forward declaration avoids pulling the whole synth header here.
// We only need the pointer type in this header; the cpp will include the full header.
class UGrooveSynthComponent;

/**
 * AProcAudio
 * ----------
 * A simple **placeable actor** that owns your UGrooveSynthComponent.
 * - Makes the synth component the root so you can drop it into a level.
 * - Provides a few convenience functions (BPM change, reseed, cycle scale).
 * - Binds a handful of hotkeys (implemented in the .cpp) so you can control
 *   the synth without a Pawn/Character.
 */
UCLASS()
class NEWGROOVEGENSYNTH_API AProcAudio : public AActor
{
	GENERATED_BODY() // Expands to boilerplate needed for reflection/replication/etc.
	
public:
	/** Default constructor: called when the actor is spawned or placed. 
		We create the synth component in the .cpp (constructor body). */
	// Sets default values for this actor's properties
	AProcAudio();
	
	/** Pointer to the owned synth component.
		Visible in the Details panel (but not editable), so you can find it at runtime.
		- VisibleAnywhere: shows up on placed instances and asset defaults.
		- BlueprintReadOnly: can be read in BP but not set. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="ProcAudio")
	UGrooveSynthComponent* Synth = nullptr;

	/** Increase/decrease BPM by 'Delta'. 
		Marked BlueprintCallable so you can call it from Level BP or widgets. */
	UFUNCTION(BlueprintCallable, Category="ProcAudio") void NudgeBPM(float Delta);
	/** Cycle the musical scale. Dir = +1 next, -1 previous. */
	UFUNCTION(BlueprintCallable, Category="ProcAudio") void CycleScale(int32 Dir);
	/** Generate a new random seed (changes patterns). */
	UFUNCTION(BlueprintCallable, Category="ProcAudio") void ReseedNow();
	/** Map some gameplay "speed" into the synth's Motion (0..1). */
	UFUNCTION(BlueprintCallable, Category="ProcAudio") void UpdateFromSpeed(float Speed, float MaxSpeed=600.f);
	

protected:
	// Called when the game starts or when spawned
	/** Called when play starts (BeginPlay in PIE or spawned at runtime).
		We start the synth and bind hotkeys here. */
	virtual void BeginPlay() override;

private:
	// ---- Hotkey handlers (bound in BeginPlay) ----
	/** 'W' key: BPM increase. */
	void OnBpmUp();
	/** 'S' key: BPM decrease. */
	void OnBpmDown();
	/** 'A' key: cycle through scales. */
	void OnCycleScale();
	/** 'D' key: reseed the generator. */
	void OnReseed();
	
	// If you ever need per-frame logic for visuals, uncomment Tick in both .h and .cpp.
	// virtual void Tick(float DeltaTime) override;vvv
/* public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;
*/
};
