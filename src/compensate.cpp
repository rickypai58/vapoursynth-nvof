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
#include <map>

//==============================================================================
// Filter internal data structure

struct DataCompensate
{
	PtrNodeRef pInputNode{nullptr};
	PtrNodeRef pFlowNode{nullptr};
	const VSVideoInfo * cpSourceVideoInfo{nullptr};
	int64_t delta{0};
	uint8_t costThreshold{255};
};

//==============================================================================
// Forward declarations

void VS_CC initCompensate(VSMap * a_pIn, VSMap * a_pOut, void ** a_ppInstanceData,
	VSNode * a_pNode, VSCore * a_pCore, const VSAPI * a_cpVSAPI);

const VSFrameRef * VS_CC getFrameCompensate(int a_n, int a_activationReason,
	void ** a_ppInstanceData, void ** a_ppFrameData,
	VSFrameContext * a_pFrameCtx, VSCore * a_pCore, const VSAPI * a_cpVSAPI);

void VS_CC freeCompensate(void * a_pInstanceData, VSCore * a_pCore,
	const VSAPI * a_cpVSAPI);

//==============================================================================

void VS_CC createCompensate(const VSMap * a_pIn, VSMap * a_pOut, void * a_pUserData,
	VSCore * a_pCore, const VSAPI * a_cpVSAPI)
{
	UNUSED(a_pUserData);

	DataCompensate internalData;

	internalData.pInputNode = PtrNodeRef(a_cpVSAPI->propGetNode(a_pIn, "clip", 0, 0), FreeNodeRef{a_cpVSAPI});
	internalData.pFlowNode = PtrNodeRef(a_cpVSAPI->propGetNode(a_pIn, "flow", 0, 0), FreeNodeRef{a_cpVSAPI});
	
	internalData.cpSourceVideoInfo = a_cpVSAPI->getVideoInfo(internalData.pInputNode.get());

	//--------------------------------------------------------------------------
	// Format check

	bool acceptableFormat = isConstantFormat(internalData.cpSourceVideoInfo) &&
		(internalData.cpSourceVideoInfo->numFrames != 0);
	if(!acceptableFormat)
	{
		a_cpVSAPI->setError(a_pOut, "nvof.Compensate: "
			"only constant format input with fixed frame number is supported.");
		return;
	}

	const VSVideoInfo * cpFlowInfo = a_cpVSAPI->getVideoInfo(internalData.pFlowNode.get());
	const VSFormat * cpFlowFormat = internalData.cpSourceVideoInfo->format;

	acceptableFormat = isConstantFormat(cpFlowInfo) &&
		(cpFlowFormat == a_cpVSAPI->getFormatPreset(pfYUV444P16, a_pCore));
	if(!acceptableFormat)
	{
		a_cpVSAPI->setError(a_pOut, "nvof.show: "
			"flow clip format is invalid.");
		return;
	}

	bool match = (internalData.cpSourceVideoInfo->width == cpFlowInfo->width) &&
		(internalData.cpSourceVideoInfo->height == cpFlowInfo->height) &&
		(internalData.cpSourceVideoInfo->numFrames == cpFlowInfo->numFrames);
	if(!match)
	{
		a_cpVSAPI->setError(a_pOut, "nvof.show: "
			"input clip and flow don't match in size or length.");
		return;
	}

	//--------------------------------------------------------------------------
	// delta

	int error = 0;
	internalData.delta = a_cpVSAPI->propGetInt(a_pIn, "delta", 0, &error);
	if(error != 0)
	{
		a_cpVSAPI->setError(a_pOut, "nvof.Compensate: "
			"failed to initialize the \"delta\" argument.");
		return;
	}

	//--------------------------------------------------------------------------
	// cost_threshold
	
	int64_t tempCostThreshold = a_cpVSAPI->propGetInt(a_pIn, "cost_threshold", 0, &error);
	if(error == 0)
	{
		if((tempCostThreshold < 0) || (tempCostThreshold > 255))
		{
			a_cpVSAPI->setError(a_pOut, "nvof.Compensate: "
				"cost_threshold is out of bounds (0 to 255).");
			return;
		}
		internalData.costThreshold = (uint8_t)tempCostThreshold;
	}

	//--------------------------------------------------------------------------

	DataCompensate * pInternalData = new DataCompensate;
	*pInternalData = std::move(internalData);
	a_cpVSAPI->createFilter(a_pIn, a_pOut, "Compensate", initCompensate,
		getFrameCompensate, freeCompensate, fmParallel, 0, pInternalData, a_pCore);
}

//==============================================================================

void VS_CC initCompensate(VSMap * a_pIn, VSMap * a_pOut, void ** a_ppInstanceData,
	VSNode * a_pNode, VSCore * a_pCore, const VSAPI * a_cpVSAPI)
{
	UNUSED(a_pIn);
	UNUSED(a_pOut);
	UNUSED(a_pCore);

	DataCompensate * pInternalData = (DataCompensate *) *a_ppInstanceData;
	a_cpVSAPI->setVideoInfo(pInternalData->cpSourceVideoInfo, 1, a_pNode);
}

//==============================================================================

// Free all allocated data on filter destruction
void VS_CC freeCompensate(void * a_pInstanceData, VSCore * a_pCore,
	const VSAPI * a_cpVSAPI)
{
	UNUSED(a_pCore);

	DataCompensate * pInternalData = (DataCompensate *)a_pInstanceData;
	delete pInternalData;
}

//==============================================================================

const VSFrameRef * VS_CC getFrameCompensate(int a_n, int a_activationReason,
	void ** a_ppInstanceData, void ** a_ppFrameData,
	VSFrameContext * a_pFrameCtx, VSCore * a_pCore, const VSAPI * a_cpVSAPI)
{
	UNUSED(a_ppFrameData);

	DataCompensate * pInternalData = (DataCompensate *) *a_ppInstanceData;
	int framesNumber = pInternalData->cpSourceVideoInfo->numFrames;

	a_n = clamp(a_n, 0, framesNumber - 1);
	int n2 = clamp(a_n + (int)pInternalData->delta, 0, framesNumber - 1);

	if(a_activationReason == arInitial)
	{
		a_cpVSAPI->requestFrameFilter(a_n, pInternalData->pInputNode.get(), a_pFrameCtx);
		a_cpVSAPI->requestFrameFilter(n2, pInternalData->pInputNode.get(), a_pFrameCtx);
		return nullptr;
	}
	else if(a_activationReason != arAllFramesReady)
		return nullptr;

	const VSFormat * cpSourceFormat = pInternalData->cpSourceVideoInfo->format;

	const VSFrameRef * cpCurrentFrame = a_cpVSAPI->getFrameFilter(a_n,
		pInternalData->pInputNode.get(), a_pFrameCtx);
	const VSFrameRef * cpDeltaFrame = a_cpVSAPI->getFrameFilter(n2,
		pInternalData->pInputNode.get(), a_pFrameCtx);

	VSFrameRef * pOutFrame = a_cpVSAPI->newVideoFrame(cpSourceFormat,
		pInternalData->cpSourceVideoInfo->width, pInternalData->cpSourceVideoInfo->height,
		cpCurrentFrame, a_pCore);


	a_cpVSAPI->freeFrame(cpCurrentFrame);
	a_cpVSAPI->freeFrame(cpDeltaFrame);

	return pOutFrame;
}

//==============================================================================
