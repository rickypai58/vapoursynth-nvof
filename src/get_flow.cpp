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
#include "nvidia-optical-flow.h"

#include <vector>
#include <memory>

//==============================================================================
// Filter internal data structure

struct DataGetFlow
{
	VSNodeRef * pInputNode;
	const VSVideoInfo * pSourceVideoInfo;
	VSVideoInfo videoInfo;
	int64_t delta;
	std::unique_ptr<NVidiaOpticalFlow> pFlow;
};

//==============================================================================
// Forward declarations

void VS_CC initGetFlow(VSMap * a_pIn, VSMap * a_pOut, void ** a_ppInstanceData,
	VSNode * a_pNode, VSCore * a_pCore, const VSAPI * a_cpVSAPI);

const VSFrameRef * VS_CC getFrameGetFlow(int a_n, int a_activationReason,
	void ** a_ppInstanceData, void ** a_ppFrameData,
	VSFrameContext * a_pFrameCtx, VSCore * a_pCore, const VSAPI * a_cpVSAPI);

void VS_CC freeGetFlow(void * a_pInstanceData, VSCore * a_pCore,
	const VSAPI * a_cpVSAPI);

//==============================================================================

void VS_CC createGetFlow(const VSMap * a_pIn, VSMap * a_pOut, void * a_pUserData,
	VSCore * a_pCore, const VSAPI * a_cpVSAPI)
{
	UNUSED(a_pUserData);

	std::string errorString;
	bool success = NVidiaOpticalFlowAPI::create(errorString);
	if(!success)
	{
		a_cpVSAPI->setError(a_pOut, (std::string("nvof.getFlow: ") + errorString).c_str());
		return;
	}

	DataGetFlow internalData;

	internalData.pInputNode = a_cpVSAPI->propGetNode(a_pIn, "clip", 0, 0);
	internalData.pSourceVideoInfo = a_cpVSAPI->getVideoInfo(internalData.pInputNode);

	//--------------------------------------------------------------------------
	// Format check

	const VSFormat * pFormat = internalData.pSourceVideoInfo->format;

	bool acceptableFormat = isConstantFormat(internalData.pSourceVideoInfo) &&
		(internalData.pSourceVideoInfo->numFrames != 0);
	if(!acceptableFormat)
	{
		a_cpVSAPI->setError(a_pOut, "nvof.getFlow: "
			"only constant format input with fixed frame number is supported.");
		a_cpVSAPI->freeNode(internalData.pInputNode);
		return;
	}

	//--------------------------------------------------------------------------
	// delta

	int error = 0;
	internalData.delta = a_cpVSAPI->propGetInt(a_pIn, "delta", 0, &error);
	if(error != 0)
	{
		a_cpVSAPI->setError(a_pOut, "nvof.getFlow: "
			"failed to initialize the \"delta\" argument.");
		a_cpVSAPI->freeNode(internalData.pInputNode);
		return;
	}

	//--------------------------------------------------------------------------
	// output video info

	internalData.videoInfo = *internalData.pSourceVideoInfo;
	internalData.videoInfo.width *= 2;

	//--------------------------------------------------------------------------
	// flow calculator

	internalData.pFlow = std::make_unique<NVidiaOpticalFlow>(
		a_cpVSAPI,
		internalData.videoInfo.width,
		internalData.videoInfo.height,
		internalData.videoInfo.format->numPlanes);

	if(!internalData.pFlow->init(errorString))
	{
		a_cpVSAPI->setError(a_pOut, (std::string("nvof.getFlow: "
			"failed to initialize the flow calculator. ") + errorString).c_str());
		a_cpVSAPI->freeNode(internalData.pInputNode);
		return;
	}

	//--------------------------------------------------------------------------

	DataGetFlow * pInternalData = new DataGetFlow;
	*pInternalData = std::move(internalData);
	a_cpVSAPI->createFilter(a_pIn, a_pOut, "getFlow", initGetFlow,
		getFrameGetFlow, freeGetFlow, fmParallel, 0, pInternalData, a_pCore);
}

//==============================================================================

void VS_CC initGetFlow(VSMap * a_pIn, VSMap * a_pOut, void ** a_ppInstanceData,
	VSNode * a_pNode, VSCore * a_pCore, const VSAPI * a_cpVSAPI)
{
	UNUSED(a_pIn);
	UNUSED(a_pOut);
	UNUSED(a_pCore);

	DataGetFlow * pInternalData = (DataGetFlow *) *a_ppInstanceData;
	a_cpVSAPI->setVideoInfo(&pInternalData->videoInfo, 1, a_pNode);
}

//==============================================================================

// Free all allocated data on filter destruction
void VS_CC freeGetFlow(void * a_pInstanceData, VSCore * a_pCore,
	const VSAPI * a_cpVSAPI)
{
	UNUSED(a_pCore);

	DataGetFlow * pInternalData = (DataGetFlow *)a_pInstanceData;
	a_cpVSAPI->freeNode(pInternalData->pInputNode);
	delete pInternalData;

	std::string errorString;
	bool success = NVidiaOpticalFlowAPI::destroy(errorString);
}

//==============================================================================

const VSFrameRef * VS_CC getFrameGetFlow(int a_n, int a_activationReason,
	void ** a_ppInstanceData, void ** a_ppFrameData,
	VSFrameContext * a_pFrameCtx, VSCore * a_pCore, const VSAPI * a_cpVSAPI)
{
	UNUSED(a_ppFrameData);

	DataGetFlow * pInternalData = (DataGetFlow *) *a_ppInstanceData;
	int framesNumber = pInternalData->pSourceVideoInfo->numFrames;

	a_n = clamp(a_n, 0, framesNumber - 1);
	int n2 = clamp(a_n + pInternalData->delta, 0, framesNumber - 1);

	if(a_activationReason == arInitial)
	{
		a_cpVSAPI->requestFrameFilter(a_n, pInternalData->pInputNode, a_pFrameCtx);
		a_cpVSAPI->requestFrameFilter(n2, pInternalData->pInputNode, a_pFrameCtx);
	}
	else if(a_activationReason == arAllFramesReady)
	{
		const VSFormat * cpSourceFormat = pInternalData->pSourceVideoInfo->format;
		const VSFormat * cpFormat = pInternalData->videoInfo.format;

		const VSFrameRef * cpCurrentFrame = a_cpVSAPI->getFrameFilter(a_n,
			pInternalData->pInputNode, a_pFrameCtx);
		const VSFrameRef * cpDeltaFrame = a_cpVSAPI->getFrameFilter(n2,
			pInternalData->pInputNode, a_pFrameCtx);

		std::vector<const VSFrameRef *> planeSources(cpSourceFormat->numPlanes, nullptr);
		std::vector<int> planes(cpSourceFormat->numPlanes, 0);

		VSFrameRef * pOutFrame = a_cpVSAPI->newVideoFrame2(cpFormat,
			pInternalData->videoInfo.width, pInternalData->videoInfo.height,
			planeSources.data(), planes.data(), cpCurrentFrame, a_pCore);

		const size_t sampleSize = cpFormat->bytesPerSample;
		
		for(int plane = 0; plane < cpSourceFormat->numPlanes; ++plane)
		{
			size_t sourceStride = a_cpVSAPI->getStride(cpCurrentFrame, plane);
			size_t sourceWidth = a_cpVSAPI->getFrameWidth(cpCurrentFrame, plane);
			size_t sourceRowSize = sampleSize * sourceWidth;
			size_t height = a_cpVSAPI->getFrameHeight(cpCurrentFrame, plane);
			size_t outStride = a_cpVSAPI->getStride(pOutFrame, plane);

			const uint8_t * cpCurrentRow = a_cpVSAPI->getReadPtr(cpCurrentFrame, plane);
			const uint8_t * cpDeltaRow = a_cpVSAPI->getReadPtr(cpDeltaFrame, plane);
			uint8_t * pOutRow = a_cpVSAPI->getWritePtr(pOutFrame, plane);

			for(size_t h = 0; h < height; ++h)
			{
				memcpy(pOutRow, cpCurrentRow, sourceRowSize);
				memcpy(pOutRow + sourceRowSize, cpDeltaRow, sourceRowSize);
				cpCurrentRow += sourceStride;
				cpDeltaRow += sourceStride;
				pOutRow += outStride;
			}
		}

		a_cpVSAPI->freeFrame(cpCurrentFrame);
		a_cpVSAPI->freeFrame(cpDeltaFrame);

		return pOutFrame;
	}

	return nullptr;
}

//==============================================================================
