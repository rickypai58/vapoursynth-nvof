//==============================================================================
// Copyright (c) 2022 Aleksey [Mystery Keeper] Lyashin (mystkeeper@gmail.com)
//
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following
// conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
// WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//==============================================================================

#include "common.h"

#include <vector>

#undef min
#undef max
#include <algorithm>

//==============================================================================

struct PlaneWithCorrelation
{
	int width{0};
	int height{0};
	int subSamplingW{1};
	int subSamplingH{1};
	const uint8_t * cpSourceBytes{nullptr};
	int sourceStride{0};
	const uint8_t * cpCompensatedBytes{nullptr};
	int compensatedStride{0};
	std::vector<float> correlation;
};

//==============================================================================
// Filter internal data structure

struct DataCorrelation
{
	PtrNodeRef pInputNode{nullptr};
	PtrNodeRef pCompensatedNode{nullptr};
	const VSVideoInfo * cpSourceVideoInfo{nullptr};
	VSVideoInfo videoInfo{};
	int radius{2};
};

//==============================================================================
// Forward declarations

void VS_CC initCorrelation(VSMap * a_pIn, VSMap * a_pOut, void ** a_ppInstanceData,
	VSNode * a_pNode, VSCore * a_pCore, const VSAPI * a_cpVSAPI);

const VSFrameRef * VS_CC getFrameCorrelation(int a_n, int a_activationReason,
	void ** a_ppInstanceData, void ** a_ppFrameData,
	VSFrameContext * a_pFrameCtx, VSCore * a_pCore, const VSAPI * a_cpVSAPI);

void VS_CC freeCorrelation(void * a_pInstanceData, VSCore * a_pCore,
	const VSAPI * a_cpVSAPI);

//==============================================================================

void VS_CC createCorrelation(const VSMap * a_pIn, VSMap * a_pOut, void * a_pUserData,
	VSCore * a_pCore, const VSAPI * a_cpVSAPI)
{
	UNUSED(a_pUserData);

	DataCorrelation internalData;

	internalData.pInputNode = PtrNodeRef(a_cpVSAPI->propGetNode(a_pIn, "clip", 0, 0),
		FreeNodeRef{a_cpVSAPI});
	internalData.pCompensatedNode = PtrNodeRef(a_cpVSAPI->propGetNode(a_pIn, "compensated", 0, 0),
		FreeNodeRef{a_cpVSAPI});
	internalData.cpSourceVideoInfo = a_cpVSAPI->getVideoInfo(internalData.pInputNode.get());

	//--------------------------------------------------------------------------
	// Format check

	bool acceptableFormat = isConstantFormat(internalData.cpSourceVideoInfo) &&
		(internalData.cpSourceVideoInfo->numFrames != 0);
	if(!acceptableFormat)
	{
		a_cpVSAPI->setError(a_pOut, "nvof.correlation: "
			"unacceptable input clip format.");
		return;
	}

	const VSVideoInfo * cpCompensatedVideoInfo = a_cpVSAPI->getVideoInfo(
		internalData.pCompensatedNode.get());
	bool equalClips = isSameFormat(internalData.cpSourceVideoInfo, cpCompensatedVideoInfo) &&
		(internalData.cpSourceVideoInfo->numFrames == cpCompensatedVideoInfo->numFrames);
	if(!equalClips)
	{
		a_cpVSAPI->setError(a_pOut, "nvof.correlation: "
			"source and compensated clip format and frame number must match.");
		return;
	}

	//--------------------------------------------------------------------------
	// radius

	int error = 0;
	int64_t radius = a_cpVSAPI->propGetInt(a_pIn, "radius", 0, &error);
	if(error == 0)
		internalData.radius = (int)radius;

	//--------------------------------------------------------------------------
	// output video info

	internalData.videoInfo = *internalData.cpSourceVideoInfo;
	internalData.videoInfo.format = a_cpVSAPI->getFormatPreset(pfGrayS, a_pCore);

	//--------------------------------------------------------------------------

	DataCorrelation * pInternalData = new DataCorrelation;
	*pInternalData = std::move(internalData);
	a_cpVSAPI->createFilter(a_pIn, a_pOut, "correlation", initCorrelation,
		getFrameCorrelation, freeCorrelation, fmParallel, 0, pInternalData, a_pCore);
}

//==============================================================================

void VS_CC initCorrelation(VSMap * a_pIn, VSMap * a_pOut, void ** a_ppInstanceData,
	VSNode * a_pNode, VSCore * a_pCore, const VSAPI * a_cpVSAPI)
{
	UNUSED(a_pIn);
	UNUSED(a_pOut);
	UNUSED(a_pCore);

	DataCorrelation * pInternalData = (DataCorrelation *) *a_ppInstanceData;
	a_cpVSAPI->setVideoInfo(&pInternalData->videoInfo, 1, a_pNode);
}

//==============================================================================

// Free all allocated data on filter destruction
void VS_CC freeCorrelation(void * a_pInstanceData, VSCore * a_pCore,
	const VSAPI * a_cpVSAPI)
{
	UNUSED(a_pCore);

	DataCorrelation * pInternalData = (DataCorrelation *)a_pInstanceData;
	delete pInternalData;
}

//==============================================================================

template<typename T>
float calculateBlockCorrelation(const Sampler2D<T> & a_sampler1,
	const Sampler2D<T> & a_sampler2, int a_hMin, int a_hMax, int a_wMin, int a_wMax)
{
	int blockSize = (a_hMax - a_hMin + 1) * (a_wMax - a_wMin + 1);

	float mean1 = 0.0f;
	float mean2 = 0.0f;

	for(int h = a_hMin; h <= a_hMax; ++h)
	{
		for(int w = a_wMin; w < a_wMax; ++w)
		{
			mean1 += (float)a_sampler1[h][w];
			mean2 += (float)a_sampler2[h][w];
		}
	}

	mean1 /= blockSize;
	mean2 /= blockSize;

	std::vector<float> block1(blockSize, 0.0f);
	std::vector<float> block2(blockSize, 0.0f);

	float * pValue1 = block1.data();
	float * pValue2 = block2.data();

	for(int h = a_hMin; h <= a_hMax; ++h)
	{
		for(int w = a_wMin; w < a_wMax; ++w)
		{
			*pValue1 = (float)a_sampler1[h][w] - mean1;
			*pValue2 = (float)a_sampler2[h][w] - mean2;
			pValue1++;
			pValue2++;
		}
	}

	float correlation = 0.0f;

	for(size_t i = 0; i < blockSize; ++i)
	{
		float denom = std::max(std::abs(block1[i]), std::abs(block2[i]));
		correlation += block1[i] * block2[i] / denom / denom;
	}

	correlation /= blockSize;

	return correlation;
}

//==============================================================================

template<typename T>
void calculatePlaneCorelation(PlaneWithCorrelation & a_plane, int a_radius)
{
	int radX = a_radius >> a_plane.subSamplingW;
	int radY = a_radius >> a_plane.subSamplingH;
	
	Sampler2D<T> samplerSource{a_plane.cpSourceBytes, a_plane.sourceStride};
	Sampler2D<T> samplerCompensated{a_plane.cpCompensatedBytes, a_plane.compensatedStride};

	float * pCorrelation = a_plane.correlation.data();

	for(int h = 0; h < a_plane.width; ++h)
	{
		int hMin = std::max(h - radY, 0);
		int hMax = std::min(h + radY, a_plane.height - 1);
		int blockHeight = hMax - hMin + 1;

		for(int w = 0; w < a_plane.width; ++w)
		{
			int wMin = std::max(w - radX, 0);
			int wMax = std::min(w + radX, a_plane.width - 1);

			*pCorrelation = calculateBlockCorrelation(samplerSource, samplerCompensated,
				hMin, hMax, wMin, wMax);
			pCorrelation++;
		}
	}
}

//==============================================================================

const VSFrameRef * VS_CC getFrameCorrelation(int a_n, int a_activationReason,
	void ** a_ppInstanceData, void ** a_ppFrameData,
	VSFrameContext * a_pFrameCtx, VSCore * a_pCore, const VSAPI * a_cpVSAPI)
{
	UNUSED(a_ppFrameData);

	DataCorrelation * pInternalData = (DataCorrelation *) *a_ppInstanceData;
	int framesNumber = pInternalData->cpSourceVideoInfo->numFrames;
	int width = pInternalData->cpSourceVideoInfo->width;
	int height = pInternalData->cpSourceVideoInfo->height;

	a_n = clamp(a_n, 0, framesNumber - 1);

	if(a_activationReason == arInitial)
	{
		a_cpVSAPI->requestFrameFilter(a_n, pInternalData->pInputNode.get(), a_pFrameCtx);
		a_cpVSAPI->requestFrameFilter(a_n, pInternalData->pCompensatedNode.get(), a_pFrameCtx);
		return nullptr;
	}
	else if(a_activationReason != arAllFramesReady)
		return nullptr;

	const VSFormat * cpFormat = pInternalData->videoInfo.format;
	size_t planesNumber = cpFormat->numPlanes;

	const VSFrameRef * cpSourceFrame = a_cpVSAPI->getFrameFilter(a_n,
		pInternalData->pInputNode.get(), a_pFrameCtx);
	const VSFrameRef * cpCompensatedFrame = a_cpVSAPI->getFrameFilter(a_n,
		pInternalData->pCompensatedNode.get(), a_pFrameCtx);

	VSFrameRef * pOutFrame = a_cpVSAPI->newVideoFrame(cpFormat, width, height,
		nullptr, a_pCore);

	std::vector<PlaneWithCorrelation> planes(planesNumber);

	for(int planeNumber = 0; planeNumber < planesNumber; ++planeNumber)
	{
		PlaneWithCorrelation plane;
		plane.width = a_cpVSAPI->getFrameWidth(cpSourceFrame, planeNumber);
		plane.height = a_cpVSAPI->getFrameHeight(cpSourceFrame, planeNumber);
		plane.subSamplingW = (planeNumber == 0) ? 1 : cpFormat->subSamplingW;
		plane.subSamplingH = (planeNumber == 0) ? 1 : cpFormat->subSamplingH;
		plane.cpSourceBytes = a_cpVSAPI->getReadPtr(cpSourceFrame, planeNumber);
		plane.sourceStride = a_cpVSAPI->getStride(cpSourceFrame, planeNumber);
		plane.cpCompensatedBytes = a_cpVSAPI->getReadPtr(cpCompensatedFrame, planeNumber);
		plane.compensatedStride = a_cpVSAPI->getStride(cpCompensatedFrame, planeNumber);
		plane.correlation = std::vector<float>(plane.height * plane.width, 0.0f);

		auto calculatePlaneCorrelationType = [&](auto arg)
		{
			using T = std::decay_t<decltype(arg)>;
			calculatePlaneCorelation<T>(plane, pInternalData->radius);
		};

		if(cpFormat->sampleType == stFloat)
		{
			if(cpFormat->bytesPerSample == 4)
				calculatePlaneCorrelationType(float());
			else
				assert(false);
		}
		else
		{
			switch(cpFormat->bytesPerSample)
			{
			case 1: calculatePlaneCorrelationType(uint8_t()); break;
			case 2: calculatePlaneCorrelationType(uint16_t()); break;
			case 4: calculatePlaneCorrelationType(uint32_t()); break;
			default: assert(false);
			}
		}

		planes[planeNumber] = std::move(plane);
	}

	a_cpVSAPI->freeFrame(cpSourceFrame);
	a_cpVSAPI->freeFrame(cpCompensatedFrame);

	return pOutFrame;
}

//==============================================================================
