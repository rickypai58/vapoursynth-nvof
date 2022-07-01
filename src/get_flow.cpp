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

#include <map>

//==============================================================================
// Filter internal data structure

struct DataGetFlow
{
	PtrNodeRef pInputNode{nullptr};
	const VSVideoInfo * cpSourceVideoInfo{nullptr};
	VSVideoInfo videoInfo{};
	int64_t delta{1};
	int64_t chromaMotion{1};
	NV_OF_PERF_LEVEL perfLevel{NV_OF_PERF_LEVEL_UNDEFINED};
	bool getCost{false};
	int64_t gpu{0};
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

	internalData.pInputNode = PtrNodeRef(a_cpVSAPI->propGetNode(a_pIn, "clip", 0, 0), FreeNodeRef{a_cpVSAPI});
	internalData.cpSourceVideoInfo = a_cpVSAPI->getVideoInfo(internalData.pInputNode.get());

	//--------------------------------------------------------------------------
	// Format check

	const VSFormat * pFormat = internalData.cpSourceVideoInfo->format;

	bool acceptableFormat = isConstantFormat(internalData.cpSourceVideoInfo) &&
		(internalData.cpSourceVideoInfo->numFrames != 0);
	if(!acceptableFormat)
	{
		a_cpVSAPI->setError(a_pOut, "nvof.getFlow: "
			"only constant format input with fixed frame number is supported.");
		NVidiaOpticalFlowAPI::destroy(errorString);
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
		NVidiaOpticalFlowAPI::destroy(errorString);
		return;
	}

	//--------------------------------------------------------------------------
	// chromaMotion

	internalData.chromaMotion = a_cpVSAPI->propGetInt(a_pIn, "chromaMotion", 0, &error);
	if(error != 0)
	{
		internalData.chromaMotion = 1;
		error = 0;
	}

	if(NVidiaOpticalFlow::bufferFormat(internalData.cpSourceVideoInfo->format,
		internalData.chromaMotion, errorString) == NV_OF_BUFFER_FORMAT_UNDEFINED)
	{
		a_cpVSAPI->setError(a_pOut, (std::string("nvof.getFlow: "
			"Source format error. ") + errorString).c_str());
			NVidiaOpticalFlowAPI::destroy(errorString);
		return;
	}

	//--------------------------------------------------------------------------
	// speed

	int64_t speed = a_cpVSAPI->propGetInt(a_pIn, "speed", 0, &error);
	if(error != 0)
	{
		speed = 0;
		error = 0;
	}

	static std::map<int64_t, NV_OF_PERF_LEVEL> perfLevels = {
		{0, NV_OF_PERF_LEVEL_SLOW},
		{1, NV_OF_PERF_LEVEL_MEDIUM},
		{2, NV_OF_PERF_LEVEL_FAST},
	};

	static std::map<int64_t, NV_OF_PERF_LEVEL>::const_iterator it =
		perfLevels.find(speed);
	if(it == perfLevels.end())
	{
		a_cpVSAPI->setError(a_pOut, "nvof.getFlow: "
			"Invalid value for the \"speed\" argument.\n"
			"Acceptable values:\n"
			"0 - slow, best quality\n"
			"1 - medium, medium quality\n"
			"2 - fast, low quality\n"
			);
		NVidiaOpticalFlowAPI::destroy(errorString);
		return;
	}

	internalData.perfLevel = it->second;

	//--------------------------------------------------------------------------
	// getCost

	internalData.getCost = a_cpVSAPI->propGetInt(a_pIn, "getCost", 0, &error);
	if(error != 0)
	{
		internalData.getCost = false;
		error = 0;
	}

	//--------------------------------------------------------------------------
	// gpu

	internalData.gpu = a_cpVSAPI->propGetInt(a_pIn, "gpu", 0, &error);
	if(error != 0)
	{
		internalData.gpu = 0;
		error = 0;
	}

	//--------------------------------------------------------------------------
	// output video info

	internalData.videoInfo = *internalData.cpSourceVideoInfo;
	internalData.videoInfo.format = a_cpVSAPI->getFormatPreset(pfYUV444PS, a_pCore);

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
	const VSFormat * cpFormat = pInternalData->videoInfo.format;

	const VSFrameRef * cpCurrentFrame = a_cpVSAPI->getFrameFilter(a_n,
		pInternalData->pInputNode.get(), a_pFrameCtx);
	const VSFrameRef * cpDeltaFrame = a_cpVSAPI->getFrameFilter(n2,
		pInternalData->pInputNode.get(), a_pFrameCtx);

	VSFrameRef * pOutFrame = a_cpVSAPI->newVideoFrame(cpFormat,
		pInternalData->videoInfo.width, pInternalData->videoInfo.height,
		nullptr, a_pCore);

	std::string errorString;

	NVidiaOpticalFlow flow(a_cpVSAPI,
		*pInternalData->cpSourceVideoInfo,
		(bool)pInternalData->chromaMotion,
		pInternalData->perfLevel,
		pInternalData->getCost,
		(int)pInternalData->gpu);

	if(!flow.init(errorString))
	{
		a_cpVSAPI->setFilterError((std::string("nvof.getFlow: "
			"failed to initialize the flow calculator. ") +
			errorString).c_str(), a_pFrameCtx);
		a_cpVSAPI->freeFrame(cpCurrentFrame);
		a_cpVSAPI->freeFrame(cpDeltaFrame);
		a_cpVSAPI->freeFrame(pOutFrame);
		return nullptr;
	}

	flow.getFlow(cpCurrentFrame, cpDeltaFrame, pOutFrame);

	a_cpVSAPI->freeFrame(cpCurrentFrame);
	a_cpVSAPI->freeFrame(cpDeltaFrame);

	return pOutFrame;
}

//==============================================================================
