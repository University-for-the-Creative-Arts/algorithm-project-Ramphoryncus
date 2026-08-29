// Fill out your copyright notice in the Description page of Project Settings.

// GrooveSynthComponent.cpp
#include "GrooveSynthComponent.h"
#include "Sound/SoundGenerator.h"  // FSoundGenerator / ISoundGeneratorPtr
//#include <cmath>

// ============================================================================
// UGrooveSynthComponent (host component)
// ============================================================================

UGrooveSynthComponent::UGrooveSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)    // UObject-style ctor; required for components
{
    PrimaryComponentTick.bCanEverTick = false;   // no per-frame game thread tick needed
}

void UGrooveSynthComponent::Reseed(int32 NewSeed)
{
	// Update the Seed the generator will read next audio block.
	// (Generator already checks SeedShadow vs Seed and re-initializes RNG.)
	Seed = NewSeed;
}

void UGrooveSynthComponent::SetMotionAmount(float Normalized01)
{
	// Motion is a simple 0..1 control (e.g., from player speed). 
	// The generator reads this once per audio block.
    Motion = FMath::Clamp(Normalized01, 0.f, 1.f);
}

// ============================================================================
// Audio Generator (runs on Unreal's audio render thread)
// ============================================================================

/** One synthesizer "voice": oscillator + simple ADSR + stereo pan */
struct FGrooveVoice
{
	// Oscillator state
	double Phase = 0.0, Phase2 = 0.0; // detuned copy

	// Pitch
	double Freq = 220.0;

	// Envelope state (ADSR), values are seconds for A/D/R and unit level for S
	double Env = 0.0, EnvTime = 0.0;
	double A=0.005, D=0.12, S=0.35, R=0.40;  // Attack Decay Sustain Release (seconds/level)

	// Stereo pan -1..+1 (L..R)
	float Pan = 0.f;

	// Retrigger helper
	FORCEINLINE void Trigger() { Env = 1.0; EnvTime = 0.0;}
};

/** Your actual audio producer. Unreal will call OnGenerateAudio(...) repeatedly. */
class FGrooveSoundGenerator final : public ISoundGenerator
{
public:
    FGrooveSoundGenerator(const FSoundGeneratorInitParams& Init, TWeakObjectPtr<UGrooveSynthComponent> InOwner)
        : Owner(InOwner)
        , SampleRate(Init.SampleRate > 0 ? FMath::RoundToInt(Init.SampleRate) : 48000)
	// Can use "sensible clamp"-> SampleRate(FMath::Clamp(FMath::RoundToInt(Init.SampleRate), 8000, 192000))
        , Channels(Init.NumChannels > 0 ? Init.NumChannels : 2)
    {
    	// Snapshot initial parameters from the component (game thread state)
    	if (const auto* C = Owner.Get())
    	{
    		BPM        = C->BPM;
    		RootMidi   = C->RootMidi;
    		Scale      = C->Scale;
    		Density    = C->Density;
    		Brightness = C->Brightness;
    		SeedShadow = C->Seed;
    		bArpOn     = C->bArpOn;
    		bPadOn     = C->bPadOn;
    		bPercOn    = C->bPercOn;
    	}

    	// Initialize musical state
    	Rng.Initialize(SeedShadow);
    	RebuildScale();
    	UpdateTiming();

    	// Different character for each voice
    	// Give the two voices different feels
    	Arp.A=0.08; Arp.D=0.10; Arp.S=0.30; Arp.R=0.20; Arp.Pan=-0.2f;
    	Pad.A=0.20; Pad.D=0.50; Pad.S=0.60; Pad.R=0.80; Pad.Pan=+0.2f;
    }

	// Mixer asks for NumSamples interleaved float samples. Return count written.
    virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
    {
    	// Pull fresh component parameters once per block (no locks)
    	if (auto* C = Owner.Get())
    	{
    		BPM        = C->BPM;
    		RootMidi   = C->RootMidi;
    		Scale      = C->Scale;
    		Density    = C->Density;
    		Brightness = C->Brightness;
    		Motion     = C->Motion;

    		bArpOn     = C->bArpOn;
    		bPadOn     = C->bPadOn;
    		bPercOn    = C->bPercOn;

    		// Detect seed change and reseed RNG deterministically
    		if (SeedShadow != C->Seed)
    		{
    			SeedShadow = C->Seed;
    			Rng.Initialize(SeedShadow);
    		}
    	}

    	// If BPM or Scale changed, recompute derived timings/scale
    	UpdateTimingIfChanged();

    	// Motion modulations (slightly brighten + increase perc density)
    	const float Bright = FMath::Clamp(Brightness + 0.30f * Motion, 0.f,1.f);
    	const float PercPr = FMath::Clamp(Density    + 0.20f * Motion, 0.f,1.f);

    	// Accumulate energy for RMS metering
    	float blockSumSq = 0.f;

    	// Tiny one-pole filter constants (compile-time constants)
    	constexpr float kALP = 0.0025f; // constexpr is a variable or function that (CONSTANT expression)
    	constexpr float kAHP = 0.02f;   // can be evaluated entirely (known) at compile time
		// const float (constant value set at runtime) CONSTANT after initialization depends on runtime data.

    	// Render loop: interleaved LR samples
    	for (int32 i = 0; i < NumSamples; i += Channels)
    	{
    		// Tick musical clocks in "frames" of one sample per channel
    		// --- rhythmic grid clocks (sample-accurate counters) ---
    		if (++Sixteenth >= SixteenthPeriod) { Sixteenth -= SixteenthPeriod; if (bArpOn) TriggerArp();}
    		if (++Eighth    >= EighthPeriod)    { Eighth    -= EighthPeriod;    if (bPercOn && (Rng.GetFraction() < PercPr)) TriggerPerc();}
    		if (++PadGate   >= PadPeriod)       { PadGate   -= PadPeriod;       if (bPadOn) TriggerPad();}

    		// --- voice mixing into L/R ---
    		float L = 0.f, R = 0.f;
    		if (bPadOn) StepVoice(Pad, Bright * 0.6f, L, R);
    		if (bArpOn) StepVoice(Arp, Bright,        L, R);
    		if (bPercOn) StepPerc(L, R);

    		// tiny feedback reverb/echo for vibe
    		const float fb = 0.12f  + 0.25f * Bright;  // more bright -> more feedback
    		const float dl = L + ReverbL * fb;
    		const float dr = R + ReverbR * fb;
    		ReverbL = dl; ReverbR = dr;

    		// --- crude 3-band split (for visual meters only) ---
    		const float mono = 0.5f * (dl + dr);
    		// const float aLP = 0.0025f, aHP = 0.02f; --changed to constexpr, outside loop----^^
    		LP += kALP * (mono - LP);        // low-pass follow
    		HP += kAHP * (mono - HP);        // high-pass follow
    		BP += mono - LP - (HP - mono);   // band-pass residual
    		blockSumSq += mono * mono;

    		//--- write interleaved output ---
    		OutAudio[i + 0] = dl;
    		if (Channels > 1) OutAudio[i + 1] = dr;
    	}
    	
    	// --- publish smoothed meters for visuals (lock-free atomics) ---
    	if (auto* C = Owner.Get())
    	{
    		const float rms = FMath::Sqrt(blockSumSq / FMath::Max(1, NumSamples / Channels));
    		constexpr float kMeterSmoothing = 0.20f;  // simple one-pole smoothing
    		//const float s = 0.20f;---changed to constexpr--^^

    		C->AnRMS.store(     (1 - kMeterSmoothing) * C->AnRMS.load()     + kMeterSmoothing * rms );
    		C->AnArpEnv.store(  (1 - kMeterSmoothing) * C->AnArpEnv.load()  + kMeterSmoothing * static_cast<float>(Arp.Env) );
    		C->AnPadEnv.store(  (1 - kMeterSmoothing) * C->AnPadEnv.load()  + kMeterSmoothing * static_cast<float>(Pad.Env) );
    		C->AnPercEnv.store( (1 - kMeterSmoothing) * C->AnPercEnv.load() + kMeterSmoothing * PercEnv );
    		C->AnBass.store(    (1 - kMeterSmoothing) * C->AnBass.load()    + kMeterSmoothing * FMath::Abs(LP) );
    		C->AnMid.store(     (1 - kMeterSmoothing) * C->AnMid.load()     + kMeterSmoothing * FMath::Abs(BP) );
    		C->AnTreble.store(  (1 - kMeterSmoothing) * C->AnTreble.load()  + kMeterSmoothing * FMath::Abs(HP) );
    	}

    	return NumSamples;
        // Minimal silent stub to prove compile; replace with DSP
        /* FMemory::Memzero(OutAudio, sizeof(float) * NumSamples);
        return NumSamples; */
    }

    // Optional: request a specific callback size if you want
    // virtual int32 GetDesiredNumSamplesToRenderPerCallback() const override { return 1024; }
private:
	// ------------------------------------------------------------------------
	// Helpers (musical math + per-voice/percussion DSP)
	// ------------------------------------------------------------------------

	// MIDI note to frequency (A4 = 440 Hz).
	static double MidiToHz(int32 M) { return 440.0 * FMath::Pow(2.0, (M - 69) / 12.0); }

	// Soft saturation (cubic) to tame peaks
	static float  SoftClip(float x) { return FMath::Clamp(x - (x*x*x)/3.f, -1.f, 1.f); }

	// Build the semitone offsets for the current scale
	void RebuildScale()
	{
		ScaleSemis.Reset();
		switch (Scale)
		{
			case EProcScale::Ionian:          ScaleSemis = {0,2,4,5,7,9,11};  break;
			case EProcScale::Dorian:          ScaleSemis = {0,2,3,5,7,9,10};  break;
			case EProcScale::MinorPentatonic: ScaleSemis = {0,3,5,7,10};      break;
			case EProcScale::HarmonicMinor:   ScaleSemis = {0,2,3,5,7,8,11};  break;
		}
		// Start walker somewhere in the scale

		Walker = ScaleSemis.Num() ? Rng.RandRange(0, ScaleSemis.Num()-1) : 0;
	}

	// Recompute sample counts for musical periods from BPM
	void UpdateTiming()
	{
		const float SafeBPM = FMath::Clamp(BPM, 20.f, 300.f);
		SamplesPerBeat  = (SampleRate * 60.0) / SafeBPM;
		SixteenthPeriod = SamplesPerBeat / 4.0;
		EighthPeriod    = SamplesPerBeat / 2.0;
		PadPeriod       = SamplesPerBeat * 2.0;
	}

	// If BPM or Scale changed since last block, rebuild derived state
	void UpdateTimingIfChanged()
	{
		if (!FMath::IsNearlyEqual(BPMShadow, BPM, 1e-4f)) { BPMShadow = BPM; UpdateTiming(); }
		if (ScaleShadow != Scale) { ScaleShadow = Scale; RebuildScale(); }
	}

	// Arpeggio trigger on sixteenth grid
	void TriggerArp()
	{
		const int32 Step = Rng.RandRange(-1, 1);
		Walker = FMath::Clamp(Walker + Step, 0, ScaleSemis.Num()-1);
		const int32 Midi = RootMidi + ScaleSemis[Walker] + 12;
		Arp.Freq = MidiToHz(Midi);
		Arp.Trigger();
	}

	// Pad trigger on 2-beat gate
	void TriggerPad()
	{
		const int32 i0 = Walker;
		const int32 Midi = RootMidi + ScaleSemis[i0];
		Pad.Freq = MidiToHz(Midi);

		// Reset detuned oscillators for click-free attacks
		Pad.Phase2 = Arp.Phase2 = 0.0; // clean attacks
		Pad.Trigger();
	}

	// Short noise burst with exponential decay
	void TriggerPerc() { PercEnv = 1.0f; }

	// Per-voice oscillator + envelope + pan, mixed into L/R
	FORCEINLINE void StepVoice(FGrooveVoice& V, float Bright, float& L, float& R) const
	{
		// Two saws detuned in cents -> beating richness
		const double DetuneCents = 10.0 + 60.0 * Bright;
		const double Ratio = FMath::Pow(2.0, DetuneCents / 1200.0);
		const double f1 = V.Freq, f2 = V.Freq * Ratio;

		// Advance phases
		V.Phase  += (2.0 * PI) * (f1 / SampleRate);
		V.Phase2 += (2.0 * PI) * (f2 / SampleRate);

		// Cheap band-limited-ish saw (triangle-like fold via fractional trick)
		const double s1 = 2.0 * ( V.Phase  / (2.0*PI) - floor(V.Phase  /(2.0*PI) + 0.5) );
		const double s2 = 2.0 * ( V.Phase2 / (2.0*PI) - floor(V.Phase2 /(2.0*PI) + 0.5) );
		const double s  = 0.5 * (s1 + s2);

		// Envelope update (simple smoothing toward target + slow release bleed)
		V.EnvTime += 1.0;
		const double AtkS = V.A * SampleRate, RelS = FMath::Max(1.0, V.R * SampleRate);
		const double target = (V.EnvTime < AtkS) ? 1.0 : V.S;
		const double alpha  = 0.001 + 0.007 * Bright;  // brighter → snappier
		V.Env += (target - V.Env) * alpha;
		V.Env *= (1.0 - 1.0 / RelS);

		// Brightness shapes from bipolar saw → rectified for brighter tone
		const double brightShape = FMath::Clamp(Brightness + 0.25f * Motion, 0.f, 1.f);
		const double shaped = (1.0 - brightShape) * s + brightShape * fabs(s);
		const float out = SoftClip(static_cast<float>(V.Env * shaped * 0.25));

		// Equal-power-ish pan law approximation
		L += out * (0.5f * (1.f - V.Pan));
		R += out * (0.5f * (1.f + V.Pan));
	}

	// Percussion: filtered noise blip with fast decay
	FORCEINLINE void StepPerc(float& L, float& R)
	{
		if (PercEnv <= 1e-5f) return;
		const float n = (Rng.GetFraction() * 2.f - 1.f);                          // white noise
		const float cf = 2000.f + 4000.f * (0.4f + 0.6f * Brightness);            // brighter → higher cutoff
		const float a  = FMath::Clamp(cf / SampleRate, 0.f, 0.25f);   // simple one-pole coefficient

		// Two 1-pole stages to get a rough band-pass hit
		LP = LP + a * (n - LP);
		HP = HP + a * (LP - HP);
		const float v  = FMath::Clamp(HP, -1.f, 1.f) * PercEnv * 0.6f;

		// Mix centered
		L += v * 0.35f; R += v * 0.35f;

		// Exponential decay toward silence (~40 ms)
		PercEnv *= FMath::Pow(0.001f, 1.f / (0.04f * SampleRate)); // ~40ms decay
	}
	
private:

	// ------------------------------------------------------------------------
	// Internal state (lives on audio thread)
	// ------------------------------------------------------------------------
	
    TWeakObjectPtr<UGrooveSynthComponent> Owner;  // safe cross-thread access to component

	// Parameters (copied from component once per block)
	float BPM=100.f, BPMShadow=-1.f, Brightness=0.5f, Density=0.35f, Motion=0.f;
	int32 RootMidi=60, SeedShadow=12345;
	EProcScale Scale=EProcScale::Ionian, ScaleShadow=EProcScale::Ionian;
	bool bArpOn=true, bPadOn=true, bPercOn=true;

	// Timing (sample counts for note intervals)
	int32 SampleRate=48000, Channels=2;
	double SamplesPerBeat=48000, SixteenthPeriod=12000, EighthPeriod=24000, PadPeriod=96000;
	double Sixteenth=0, Eighth=0, PadGate=0;

	// musical state
	TArray<int32> ScaleSemis; int32 Walker=0;
	FGrooveVoice Arp, Pad; float PercEnv=0.f; FRandomStream Rng;

	// meters/fx Simple analysis state + tiny feedback delay
	float LP=0, BP=0, HP=0, ReverbL=0, ReverbR=0;
};

// ============================================================================
// Factory: Unreal calls this to get a generator for the component instance
// ============================================================================
// Factory: return a thread-safe shared pointer (per typedef)
ISoundGeneratorPtr UGrooveSynthComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
    // Note the ESPMode::ThreadSafe to match typedef---
    // Engine typedef is TSharedPtr<ISoundGenerator, ESPMode::ThreadSafe>
    return MakeShared<FGrooveSoundGenerator, ESPMode::ThreadSafe>(InParams, this);
}

