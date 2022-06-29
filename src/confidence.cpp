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

//==============================================================================
// Filter internal data structure

struct DataConfidence
{
	PtrNodeRef pInputNode{nullptr};
	PtrNodeRef pCompensatedNode{nullptr};
	const VSVideoInfo * cpSourceVideoInfo{nullptr};
	VSVideoInfo videoInfo{};
	int64_t radius{2};
};

//==============================================================================
// Forward declarations

void VS_CC initConfidence(VSMap * a_pIn, VSMap * a_pOut, void ** a_ppInstanceData,
	VSNode * a_pNode, VSCore * a_pCore, const VSAPI * a_cpVSAPI);

const VSFrameRef * VS_CC getFrameConfidence(int a_n, int a_activationReason,
	void ** a_ppInstanceData, void ** a_ppFrameData,
	VSFrameContext * a_pFrameCtx, VSCore * a_pCore, const VSAPI * a_cpVSAPI);

void VS_CC freeConfidence(void * a_pInstanceData, VSCore * a_pCore,
	const VSAPI * a_cpVSAPI);

//==============================================================================

void VS_CC createConfidence(const VSMap * a_pIn, VSMap * a_pOut, void * a_pUserData,
	VSCore * a_pCore, const VSAPI * a_cpVSAPI)
{
	UNUSED(a_pUserData);

	DataConfidence internalData;

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
		a_cpVSAPI->setError(a_pOut, "nvof.confidence: "
			"unacceptable input clip format.");
		return;
	}

	const VSVideoInfo * cpCompensatedVideoInfo = a_cpVSAPI->getVideoInfo(
		internalData.pCompensatedNode.get());
	bool equalClips = isSameFormat(internalData.cpSourceVideoInfo, cpCompensatedVideoInfo) &&
		(internalData.cpSourceVideoInfo->numFrames == cpCompensatedVideoInfo->numFrames);
	if(!equalClips)
	{
		a_cpVSAPI->setError(a_pOut, "nvof.confidence: "
			"source and compensated clip format and frame number must match.");
		return;
	}

	//--------------------------------------------------------------------------
	// radius

	int error = 0;
	int64_t radius = a_cpVSAPI->propGetInt(a_pIn, "radius", 0, &error);
	if(error == 0)
		internalData.radius = radius;

	//--------------------------------------------------------------------------
	// output video info

	internalData.videoInfo = *internalData.cpSourceVideoInfo;
	internalData.videoInfo.format = a_cpVSAPI->getFormatPreset(pfGrayS, a_pCore);

	//--------------------------------------------------------------------------

	DataConfidence * pInternalData = new DataConfidence;
	*pInternalData = std::move(internalData);
	a_cpVSAPI->createFilter(a_pIn, a_pOut, "confidence", initConfidence,
		getFrameConfidence, freeConfidence, fmParallel, 0, pInternalData, a_pCore);
}

//==============================================================================

void VS_CC initConfidence(VSMap * a_pIn, VSMap * a_pOut, void ** a_ppInstanceData,
	VSNode * a_pNode, VSCore * a_pCore, const VSAPI * a_cpVSAPI)
{
	UNUSED(a_pIn);
	UNUSED(a_pOut);
	UNUSED(a_pCore);

	DataConfidence * pInternalData = (DataConfidence *) *a_ppInstanceData;
	a_cpVSAPI->setVideoInfo(&pInternalData->videoInfo, 1, a_pNode);
}

//==============================================================================

// Free all allocated data on filter destruction
void VS_CC freeConfidence(void * a_pInstanceData, VSCore * a_pCore,
	const VSAPI * a_cpVSAPI)
{
	UNUSED(a_pCore);

	DataConfidence * pInternalData = (DataConfidence *)a_pInstanceData;
	delete pInternalData;
}

//==============================================================================

const VSFrameRef * VS_CC getFrameConfidence(int a_n, int a_activationReason,
	void ** a_ppInstanceData, void ** a_ppFrameData,
	VSFrameContext * a_pFrameCtx, VSCore * a_pCore, const VSAPI * a_cpVSAPI)
{
	UNUSED(a_ppFrameData);

	DataConfidence * pInternalData = (DataConfidence *) *a_ppInstanceData;
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

	const VSFrameRef * cpSourceFrame = a_cpVSAPI->getFrameFilter(a_n,
		pInternalData->pInputNode.get(), a_pFrameCtx);
	const VSFrameRef * cpCompensatedFrame = a_cpVSAPI->getFrameFilter(a_n,
		pInternalData->pCompensatedNode.get(), a_pFrameCtx);

	VSFrameRef * pOutFrame = a_cpVSAPI->newVideoFrame(cpFormat, width, height,
		nullptr, a_pCore);


	a_cpVSAPI->freeFrame(cpSourceFrame);
	a_cpVSAPI->freeFrame(cpCompensatedFrame);

	return pOutFrame;
}

//==============================================================================
