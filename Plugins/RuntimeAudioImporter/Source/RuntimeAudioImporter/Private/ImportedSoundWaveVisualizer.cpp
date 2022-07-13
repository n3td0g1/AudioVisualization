// Georgy Treshchev 2022.

#include "ImportedSoundWaveVisualizer.h"

#include "DynamicTexture.h"
#include "ImportedSoundWave.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/UserInterfaceSettings.h"
#include "Slate/SlateTextures.h"

/** The maximum number of channels we support */
static constexpr int32 MaxSupportedChannels = 2;
/** The number of pixels between which to place control points for cubic interpolation */
static constexpr int32 SmoothingAmount = 6;
/** The size of the sroked border of the audio wave */
static constexpr int32 StrokeBorderSize = 2;

/** A specific sample from the audio, specifying peak and average amplitude over the sample's range */
struct FAudioSample
{
	FAudioSample() : RMS(0.f), Peak(0), NumSamples(0) {}

	float RMS;
	int32 Peak;
	int32 NumSamples;
};

/** A segment in a cubic spline */
struct FSplineSegment
{
	FSplineSegment() : A(0.f), B(0.f), C(0.f), D(0.f), SampleSize(0), Position(0)
	{
	}

	/** Cubic polynomial coefficients for the equation f(x) = A + Bx + Cx^2 + Dx^3*/
	float A, B, C, D;
	/** The width of this segment */
	float SampleSize;
	/** The x-position of this segment */
	float Position;
};

/**
 * The audio thumbnail, which holds a texture which it can pass back to a viewport to render
 */
class FAudioThumbnail
	: public TSharedFromThis<FAudioThumbnail>
{
public:
	FAudioThumbnail(const FLinearColor& BaseColor, const UImportedSoundWave* SoundWave);
	~FAudioThumbnail();

	/** Generates the waveform preview and dumps it out to the OutBuffer */
	void GenerateWaveformPreview(TRange<float> DrawRange, float DisplayScale, const UImportedSoundWave* SoundWave, UDynamicTexture* DynamicTexture);
	
private:	

	/** Sample the audio data at the given lookup position. Appends the sample result to the Samples array */
	void SampleAudio(int32 NumChannels, const int16* LookupData, int32 LookupStartIndex, int32 LookupEndIndex, int32 MaxAmplitude);

	/** Generate a natural cubic spline from the sample buffer */
	void GenerateSpline(int32 NumChannels, int32 SamplePositionOffset);

private:

	TArray<int16> LookupDataArray;
	int32 LookupSize;
	
	/** Accumulation of audio samples for each channel */
	TArray<FAudioSample> Samples[MaxSupportedChannels];

	/** Spline segments generated from the above Samples array */
	TArray<FSplineSegment> SplineSegments[MaxSupportedChannels];

	/** Waveform colors */
	FLinearColor BoundaryColorHSV;
	FLinearColor FillColor_A, FillColor_B;
};

float Modulate(float Value, float Delta, float Range)
{
	Value = FMath::Fmod(Value + Delta, Range);
	if (Value < 0.0f)
	{
		Value += Range;
	}
	return Value;
}

FAudioThumbnail::FAudioThumbnail(const FLinearColor& BaseColor, const UImportedSoundWave* SoundWave)
{	
	const FLinearColor BaseHSV = BaseColor.LinearRGBToHSV();

	const float BaseValue = FMath::Min(BaseHSV.B, .5f) * BaseHSV.A;
	const float BaseSaturation = FMath::Max((BaseHSV.G - .45f), 0.f) * BaseHSV.A;

	FillColor_A = FLinearColor(Modulate(BaseHSV.R, -2.5f, 360), BaseSaturation + .35f, BaseValue);
	FillColor_B = FLinearColor(Modulate(BaseHSV.R,  2.5f, 360), BaseSaturation + .4f, BaseValue + .15f);

	BoundaryColorHSV = FLinearColor(BaseHSV.R, BaseSaturation, BaseValue + .35f);
	LookupDataArray.Empty();	

	if(SoundWave)
	{
		const float* TempLookupData = reinterpret_cast<float*>(SoundWave->PCMBufferInfo.PCMData.GetView().GetData());
		LookupSize = SoundWave->PCMBufferInfo.PCMData.GetView().Num() * sizeof(uint8) / sizeof(float);

		if (!TempLookupData)
		{
			return;
		}
		
		LookupDataArray.Reserve(LookupSize);

		for(int32 i = 0; i < LookupSize; ++i)
		{
			LookupDataArray.Add(FMath::CeilToInt(TempLookupData[i] * TNumericLimits<int16>::Max()));
		}
	}
	else
	{
		LookupSize = 0;
	}
}


FAudioThumbnail::~FAudioThumbnail()
{

}


//FIntPoint FAudioThumbnail::GetSize() const {return FIntPoint(TextureSize, /*Section.GetTypedOuter<UMovieSceneAudioTrack>()->GetRowHeight());*/ 50.0f); }

/** Lerp between 2 HSV space colors */
FLinearColor LerpHSV(const FLinearColor& A, const FLinearColor& B, float Alpha)
{
	float SrcHue = A.R;
	float DestHue = B.R;

	// Take the shortest path to the new hue
	if (FMath::Abs(SrcHue - DestHue) > 180.0f)
	{
		if (DestHue > SrcHue)
		{
			SrcHue += 360.0f;
		}
		else
		{
			DestHue += 360.0f;
		}
	}

	float NewHue = FMath::Fmod(FMath::Lerp(SrcHue, DestHue, Alpha), 360.0f);
	if (NewHue < 0.0f)
	{
		NewHue += 360.0f;
	}

	return FLinearColor(
		NewHue,
		FMath::Lerp(A.G, B.G, Alpha),
		FMath::Lerp(A.B, B.B, Alpha),
		FMath::Lerp(A.A, B.A, Alpha)
		);
}

void FAudioThumbnail::GenerateWaveformPreview(const TRange<float> DrawRange, const float DisplayScale, const UImportedSoundWave* SoundWave, UDynamicTexture* DynamicTexture)
{
	if(!DynamicTexture)
	{
		return;
	}
	
	DynamicTexture->Clear();
	
	if(!SoundWave)
	{
		return;
	}
	
	if(SoundWave->NumChannels != 1 && SoundWave->NumChannels != 2)
	{
		return;
	}

	if(LookupDataArray.IsEmpty())
	{
		return;
	}

	for(int32 i = 0; i < MaxSupportedChannels; ++i)
	{
		Samples[i].Empty();
		SplineSegments[i].Empty();
	}
	
	const int16* LookupData = LookupDataArray.GetData();

	FFrameRate TestFrameRate(24000, 1);

	FFrameRate FrameRate = TestFrameRate;//            = AudioSection->GetTypedOuter<UMovieScene>()->GetTickResolution(); TODO
	float      PitchMultiplierValue = 1.0f; // AudioSection->GetPitchMultiplierChannel().GetDefault().Get(1.f); TODO
	double     SectionStartTime     = 0.0; //AudioSection->GetInclusiveStartFrame() / FrameRate; TODO

	// @todo Sequencer This fixes looping drawing by pretending we are only dealing with a SoundWave
	const TRange<float> AudioTrueRange = TRange<float>(
		SectionStartTime,// - FrameRate.AsSeconds(AudioSection->GetStartOffset()), TODO
		SectionStartTime + SoundWave->Duration * (1.0f / PitchMultiplierValue));// - FrameRate.AsSeconds(AudioSection->GetStartOffset()) + DeriveUnloopedDuration(AudioSection) * (1.0f / PitchMultiplierValue)); TODO

	const float TrueRangeSize = AudioTrueRange.Size<float>();
	const float DrawRangeSize = DrawRange.Size<float>();	

	const FIntPoint ThumbnailSize(DynamicTexture->GetWidth(), DynamicTexture->GetHeight());
	
	const int32 MaxAmplitude = SoundWave->NumChannels == 1 ? ThumbnailSize.Y : ThumbnailSize.Y / 2;
	const int32 DrawOffsetPx = FMath::Max(FMath::RoundToInt((DrawRange.GetLowerBoundValue() - SectionStartTime) / DisplayScale), 0);

	const int32 SampleLockOffset = DrawOffsetPx % SmoothingAmount;
	
	const int32 FirstSample = -2.f * SmoothingAmount - SampleLockOffset;
	const int32 LastSample = ThumbnailSize.X + 2.f * SmoothingAmount;

	// Sample the audio one pixel to the left and right
	for (int32 X = FirstSample; X < LastSample; ++X)
	{
		const float LookupTime = ((X - 0.5f) / static_cast<float>(ThumbnailSize.X)) * DrawRangeSize + DrawRange.GetLowerBoundValue();
		const float LookupFraction = (LookupTime - AudioTrueRange.GetLowerBoundValue()) / TrueRangeSize;
		const float LookupFractionLooping = FMath::Fmod(LookupFraction, 1.f);
		const int32 LookupIndex = FMath::TruncToInt(LookupFractionLooping * LookupSize);
		
		const float NextLookupTime = ((X + 0.5f) / static_cast<float>(ThumbnailSize.X)) * DrawRangeSize + DrawRange.GetLowerBoundValue();
		const float NextLookupFraction = (NextLookupTime - AudioTrueRange.GetLowerBoundValue()) / TrueRangeSize;
		const float NextLookupFractionLooping = FMath::Fmod(NextLookupFraction, 1.f);
		const int32 NextLookupIndex = FMath::TruncToInt(NextLookupFractionLooping * LookupSize);
		
		if(LookupFraction > 1.f)
		{
			break;
		}

		SampleAudio(SoundWave->NumChannels, LookupData, LookupIndex, NextLookupIndex, MaxAmplitude);
	}

	// Generate a spline
	GenerateSpline(SoundWave->NumChannels, FirstSample);

	// Now draw the spline
	const int32 Width = ThumbnailSize.X;
	const int32 Height = ThumbnailSize.Y;

	for (int32 ChannelIndex = 0; ChannelIndex < SoundWave->NumChannels; ++ChannelIndex)
	{
		int32 SplineIndex = 0;

		for (int32 X = 0; X < Width; ++X)
		{
			bool bOutOfRange = SplineIndex >= SplineSegments[ChannelIndex].Num();
			while (!bOutOfRange && X >= SplineSegments[ChannelIndex][SplineIndex].Position+SplineSegments[ChannelIndex][SplineIndex].SampleSize)
			{
				++SplineIndex;
				bOutOfRange = SplineIndex >= SplineSegments[ChannelIndex].Num();
			}
			
			if (bOutOfRange)
			{
				break;
			}

			// Evaluate the spline
			const float DistBetweenPts = (X-SplineSegments[ChannelIndex][SplineIndex].Position)/SplineSegments[ChannelIndex][SplineIndex].SampleSize;
			const float Amplitude = 
				SplineSegments[ChannelIndex][SplineIndex].A +
				SplineSegments[ChannelIndex][SplineIndex].B * DistBetweenPts +
				SplineSegments[ChannelIndex][SplineIndex].C * FMath::Pow(DistBetweenPts, 2) +
				SplineSegments[ChannelIndex][SplineIndex].D * FMath::Pow(DistBetweenPts, 3);

			// @todo: draw border according to gradient of curve to prevent aliasing on steep gradients? This would be non-trivial...
			const float BoundaryStart = Amplitude - StrokeBorderSize * 0.5f;
			const float BoundaryEnd = Amplitude + StrokeBorderSize * 0.5f;

			const FAudioSample& Sample = Samples[ChannelIndex][X - FirstSample];

			for (int32 PixelIndex = 0; PixelIndex < MaxAmplitude; ++PixelIndex)
			{
				const float PixelCenter = PixelIndex + 0.5f;

				const float Dither = FMath::FRand() * .025f - .0125f;
				const float GradLerp = FMath::Clamp(static_cast<float>(PixelIndex) / MaxAmplitude + Dither, 0.f, 1.f);
				FLinearColor SolidFilledColor = LerpHSV(FillColor_A, FillColor_B, GradLerp);

				float BorderBlend = 1.f;
				if (PixelIndex <= FMath::TruncToInt(BoundaryStart))
				{
					BorderBlend = 1.f - FMath::Clamp(BoundaryStart - PixelIndex, 0.f, 1.f);
				}
				
				FLinearColor Color = PixelIndex == Sample.Peak ? FillColor_B.HSVToLinearRGB() : LerpHSV(SolidFilledColor, BoundaryColorHSV, BorderBlend).HSVToLinearRGB();

				// Calculate alpha based on how far from the boundary we are
				const float Alpha = FMath::Max(FMath::Clamp(BoundaryEnd - PixelCenter, 0.f, 1.f), FMath::Clamp(static_cast<float>(Sample.Peak) - PixelIndex + 0.25f, 0.f, 1.f));
				if (Alpha <= 0.f)
				{
					break;
				}

				Color.A = Alpha;
				Color.R *= Color.R * Alpha;
				Color.G *= Color.G * Alpha;
				Color.B *= Color.B * Alpha;

				int32 Y = Height - PixelIndex - 1;
				if (SoundWave->NumChannels == 2)
				{
					Y = ChannelIndex == 0 ? Height/2 - PixelIndex : Height/2 + PixelIndex;
				}

				DynamicTexture->SetPixel(X, Y, Color);

				// Slate viewports must have pre-multiplied alpha
				/**Pixel++ = Color.B*Alpha*255;
				*Pixel++ = Color.G*Alpha*255;
				*Pixel++ = Color.R*Alpha*255;
				*Pixel++ = Alpha*255;*/
			}
		}
	}
}

void FAudioThumbnail::GenerateSpline(int32 NumChannels, int32 SamplePositionOffset)
{
	// Generate a cubic polynomial spline interpolating the samples
	for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
	{
		TArray<FSplineSegment>& Segments = SplineSegments[ChannelIndex];

		const int32 NumSamples = Samples[ChannelIndex].Num();

		struct FControlPoint
		{
			float Value;
			float Position;
			int32 SampleSize;
		};
		TArray<FControlPoint> ControlPoints;

		for (int SampleIndex = 0; SampleIndex < NumSamples; SampleIndex += SmoothingAmount)
		{
			float RMS = 0.f;
			int32 NumAvgs = FMath::Min(SmoothingAmount, NumSamples - SampleIndex);
			
			for (int32 SubIndex = 0; SubIndex < NumAvgs; ++SubIndex)
			{
				RMS += FMath::Pow(Samples[ChannelIndex][SampleIndex + SubIndex].RMS, 2);
			}

			const int32 SegmentSize2 = NumAvgs / 2;
			const int32 SegmentSize1 = NumAvgs - SegmentSize2;

			RMS = FMath::Sqrt(RMS / NumAvgs);

			FControlPoint& StartPoint = ControlPoints[ControlPoints.AddZeroed()];
			StartPoint.Value = Samples[ChannelIndex][SampleIndex].RMS;
			StartPoint.SampleSize = SegmentSize1;
			StartPoint.Position = SampleIndex + SamplePositionOffset;

			if (SegmentSize2 > 0)
			{
				FControlPoint& MidPoint = ControlPoints[ControlPoints.AddZeroed()];
				MidPoint.Value = RMS;
				MidPoint.SampleSize = SegmentSize2;
				MidPoint.Position = SampleIndex + SamplePositionOffset + SegmentSize1;
			}
		}

		if (ControlPoints.Num() <= 1)
		{
			continue;
		}

		const int32 LastIndex = ControlPoints.Num() - 1;

		// Perform gaussian elimination on the following tridiagonal matrix that defines the piecewise cubic polynomial
		// spline for n control points, given f(x), f'(x) and f''(x) continuity. Imposed boundary conditions are f''(0) = f''(n) = 0.
		//	(D[i] = f[i]'(x))
		//	1	2						D[i]	= 3(y[1] - y[0])
		//	1	4	1					D[i+1]	= 3(y[2] - y[1])
		//		1	4	1				|		|
		//		\	\	\	\	\		|		|
		//					1	4	1	|		= 3(y[n-1] - y[n-2])
		//						1	2	D[n]	= 3(y[n] - y[n-1])
		struct FMinimalMatrixComponent
		{
			float DiagComponent;
			float KnownConstant;
		};

		TArray<FMinimalMatrixComponent> GaussianCoefficients;
		GaussianCoefficients.AddZeroed(ControlPoints.Num());

		// Setup the top left of the matrix
		GaussianCoefficients[0].KnownConstant = 3.f * (ControlPoints[1].Value - ControlPoints[0].Value);
		GaussianCoefficients[0].DiagComponent = 2.f;

		// Calculate the diagonal component of each row, based on the eliminated value of the last
		for (int32 Index = 1; Index < GaussianCoefficients.Num() - 1; ++Index)
		{
			GaussianCoefficients[Index].KnownConstant = (3.f * (ControlPoints[Index+1].Value - ControlPoints[Index-1].Value)) - (GaussianCoefficients[Index-1].KnownConstant / GaussianCoefficients[Index-1].DiagComponent);
			GaussianCoefficients[Index].DiagComponent = 4.f - (1.f / GaussianCoefficients[Index-1].DiagComponent);
		}
		
		// Setup the bottom right of the matrix
		GaussianCoefficients[LastIndex].KnownConstant = (3.f * (ControlPoints[LastIndex].Value - ControlPoints[LastIndex-1].Value)) - (GaussianCoefficients[LastIndex-1].KnownConstant / GaussianCoefficients[LastIndex-1].DiagComponent);
		GaussianCoefficients[LastIndex].DiagComponent = 2.f - (1.f / GaussianCoefficients[LastIndex-1].DiagComponent);

		// Now we have an upper triangular matrix, we can use reverse substitution to calculate D[n] -> D[0]

		TArray<float> FirstOrderDerivatives;
		FirstOrderDerivatives.AddZeroed(GaussianCoefficients.Num());

		FirstOrderDerivatives[LastIndex] = GaussianCoefficients[LastIndex].KnownConstant / GaussianCoefficients[LastIndex].DiagComponent;

		for (int32 Index = GaussianCoefficients.Num() - 2; Index >= 0; --Index)
		{
			FirstOrderDerivatives[Index] = (GaussianCoefficients[Index].KnownConstant - FirstOrderDerivatives[Index+1]) / GaussianCoefficients[Index].DiagComponent;
		}

		// Now we know the first-order derivatives of each control point, calculating the interpolating polynomial is trivial
		// f(x) = a + bx + cx^2 + dx^3
		//	a = y
		//	b = D[i]
		//	c = 3(y[i+1] - y[i]) - 2D[i] - D[i+1]
		//	d = 2(y[i] - y[i+1]) + 2D[i] + D[i+1]
		for (int32 Index = 0; Index < FirstOrderDerivatives.Num() - 2; ++Index)
		{
			Segments.Emplace();
			Segments.Last().A = ControlPoints[Index].Value;
			Segments.Last().B = FirstOrderDerivatives[Index];
			Segments.Last().C = 3.f*(ControlPoints[Index+1].Value - ControlPoints[Index].Value) - 2*FirstOrderDerivatives[Index] - FirstOrderDerivatives[Index+1];
			Segments.Last().D = 2.f*(ControlPoints[Index].Value - ControlPoints[Index+1].Value) + FirstOrderDerivatives[Index] + FirstOrderDerivatives[Index+1];

			Segments.Last().Position = ControlPoints[Index].Position;
			Segments.Last().SampleSize = ControlPoints[Index].SampleSize;
		}
	}
}

namespace AnimatableAudioEditorConstants
{
	// Optimization - maximum samples per pixel this sound allows
	constexpr uint32 MaxSamplesPerPixel = 60;
}

void FAudioThumbnail::SampleAudio(const int32 NumChannels, const int16* LookupData, int32 LookupStartIndex, int32 LookupEndIndex, const int32 MaxAmplitude)
{
	LookupStartIndex = NumChannels == 2 ? (LookupStartIndex % 2 == 0 ? LookupStartIndex : LookupStartIndex - 1) : LookupStartIndex;
	LookupEndIndex = FMath::Max(LookupEndIndex, LookupStartIndex + 1);

	const int32 StepSize = NumChannels;

	// optimization - don't take more than a maximum number of samples per pixel
	const int32 Range = LookupEndIndex - LookupStartIndex;
	const int32 SampleCount = Range / StepSize;
	constexpr int32 MaxSampleCount = AnimatableAudioEditorConstants::MaxSamplesPerPixel;
	int32 ModifiedStepSize = StepSize;
	
	if (SampleCount > MaxSampleCount)
	{
		// Always start from a common multiple
		const int32 Adjustment = LookupStartIndex % MaxSampleCount;
		LookupStartIndex = FMath::Clamp(LookupStartIndex - Adjustment, 0, LookupSize);
		LookupEndIndex = FMath::Clamp(LookupEndIndex - Adjustment, 0, LookupSize);
		ModifiedStepSize *= (SampleCount / MaxSampleCount);
	}

	for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
	{
		FAudioSample& NewSample = Samples[ChannelIndex][Samples[ChannelIndex].Emplace()];

		for (int32 Index = LookupStartIndex; Index < LookupEndIndex; Index += ModifiedStepSize)
		{
			if (Index < 0 || Index >= LookupSize)
			{
				NewSample.RMS += 0.f;
				++NewSample.NumSamples;
				continue;
			}

			const int32 DataPoint = LookupData[Index + ChannelIndex];
			const int32 Sample = FMath::Clamp(FMath::TruncToInt(FMath::Abs(DataPoint) / 32768.f * MaxAmplitude), 0, MaxAmplitude - 1);

			NewSample.RMS += FMath::Pow(Sample, 2.f);
			NewSample.Peak = FMath::Max(NewSample.Peak, Sample);
			++NewSample.NumSamples;
		}

		if (NewSample.NumSamples)
		{
			NewSample.RMS = (FMath::Sqrt(NewSample.RMS / NewSample.NumSamples));
		}
	}
}

UImportedSoundWaveVisualizer::UImportedSoundWaveVisualizer()
{
	CurrentSoundWave = nullptr;
}

void UImportedSoundWaveVisualizer::SetAudioWave(UImportedSoundWave* SoundWave)
{
	CurrentSoundWave = SoundWave;
	StartTime = 0.0f;
	CurrentScale = 1.0f;
	ActualScale = 1.0f;
	
	if(IsValid(CurrentSoundWave))
	{		
		WaveformThumbnail.Reset();
	}
	
	UpdateTexture();
}

float UImportedSoundWaveVisualizer::GetMaxOffset() const
{
	if(!IsValid(CurrentSoundWave))
	{
		return 0.0f;
	}
	
	return CurrentSoundWave->Duration * (1.0f - ActualScale);
}

void UImportedSoundWaveVisualizer::SetOffset(float NewOffset)
{
	StartTime = FMath::Clamp(NewOffset, 0.0f, GetMaxOffset());

	UpdateTexture();
}

void UImportedSoundWaveVisualizer::AddScale(float DeltaScale)
{
	CurrentScale = FMath::Clamp(CurrentScale + DeltaScale, 1.0f, MaxScale);
	
	ActualScale = 1.0f / CurrentScale;

	StartTime = FMath::Clamp(StartTime, 0.0f, GetMaxOffset());

	UpdateTexture();
}

void UImportedSoundWaveVisualizer::UpdateTexture()
{
	if(!IsValid(CurrentSoundWave))
	{
		WaveformThumbnail.Reset();
		return;
	}
	
	float EndTime = StartTime + CurrentSoundWave->Duration * ActualScale;
	EndTime = FMath::Min(EndTime, CurrentSoundWave->Duration);

	if(!DynamicTexture)
	{
		DynamicTexture = NewObject<UDynamicTexture>(this);
		const FIntPoint TextureSize = GetDynamicTextureSize();
		DynamicTexture->Initialize(TextureSize.X, TextureSize.Y, FLinearColor::Transparent, TextureFilter::TF_Nearest);
	}

	const TRange<float> DrawRange = TRange<float>(
		StartTime,
		EndTime
		);
	const float DisplayScale = (EndTime - StartTime) / static_cast<float>(DynamicTexture->GetWidth());

	if(!WaveformThumbnail.IsValid())
	{
		WaveformThumbnail = MakeShareable(new FAudioThumbnail(ColorTint, CurrentSoundWave));
	}

	
	WaveformThumbnail->GenerateWaveformPreview(DrawRange, DisplayScale, CurrentSoundWave, DynamicTexture);
	DynamicTexture->UpdateTexture();
	SetBrushFromTexture(DynamicTexture->GetTextureResource(), true);
}

FIntPoint UImportedSoundWaveVisualizer::GetDynamicTextureSize() const
{
	if (const UCanvasPanelSlot* LocalCanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		float DPIScale = 1.0f;
		if (GEngine && IsValid(GEngine->GameViewport))
		{
			FVector2D ViewportSize;
			GEngine->GameViewport->GetViewportSize(ViewportSize);
			DPIScale = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(ViewportSize.X, ViewportSize.Y));
		}

		const FVector2D LocalSize = LocalCanvasSlot->GetSize() * DPIScale;

		return FIntPoint{
			FMath::Clamp(static_cast<int32>(LocalSize.X), 1, 4096),
			FMath::Clamp(static_cast<int32>(LocalSize.Y), 1, 4096),
		};
	}

	// In case CanvasSlot is not valid
	const float Size = FMath::Min(GSystemResolution.ResX, GSystemResolution.ResY);
	return FIntPoint{
		FMath::Clamp(static_cast<int32>(Size), 1, 4096),
		FMath::Clamp(static_cast<int32>(Size), 1, 4096),
	};
}
