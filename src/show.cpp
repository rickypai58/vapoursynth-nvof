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

#include <memory>
#include <map>

//==============================================================================

const double pi = 3.14159265358979323846;

struct RGB{
	double r;       // ¸ [0, 1]
	double g;       // ¸ [0, 1]
	double b;       // ¸ [0, 1]
};

struct RGB8{
	uint8_t r;       // ¸ [0, 255]
	uint8_t g;       // ¸ [0, 255]
	uint8_t b;       // ¸ [0, 255]
};

struct HSV{
	double h;       // ¸ [0, 2 * pi]
	double s;       // ¸ [0, 1]
	double v;       // ¸ [0, 1]
};

RGB hsv2rgb(HSV hsv)
{
	RGB rgb;
	double h = hsv.h;
	double s = hsv.s;
	double v = hsv.v;

	while(h >= 2. * pi)
		h -= 2. * pi;
	while(h < 0.)
		h += 2. * pi;

	h /= (pi / 3.);
	double fract = h - floor(h);

	double P = v * (1. - s);
	double Q = v * (1. - s * fract);
	double T = v * (1. - s * (1. - fract));

	if(0. <= h && h < 1.)
		rgb = RGB{v, T, P};
	else if (1. <= h && h < 2.)
		rgb = RGB{Q, v, P};
	else if (2. <= h && h < 3.)
		rgb = RGB{P, v, T};
	else if (3. <= h && h < 4.)
		rgb = RGB{P, Q, v};
	else if (4. <= h && h < 5.)
		rgb = RGB{T, P, v};
	else if (5. <= h && h < 6.)
		rgb = RGB{v, P, Q};
	else
		rgb = RGB{0., 0., 0.};

	return rgb;
}

RGB8 fromUV(int16_t u16, int16_t v16, double amplify = 1.)
{
	const double maxLen = 1024. * sqrt(2.);

	double u = (double)u16 / (1 << 5);
	double v = (double)v16 / (1 << 5);

	double h = atan2(v, u);
	double s = sqrt(u * u + v * v) / maxLen * amplify;
	if(s > 1.)
		s = 1.;

	RGB rgb = hsv2rgb({h, s, 0.5});

	uint8_t r8 = uint8_t(rgb.r * 255.);
	uint8_t g8 = uint8_t(rgb.g * 255.);
	uint8_t b8 = uint8_t(rgb.b * 255.);

	return RGB8{r8, g8, b8};
}

//==============================================================================

enum class ShowMode
{
	Flow,
	Cost,
	COUNT,
};

//==============================================================================
// Filter internal data structure

struct DataShow
{
	VSNodeRef * pInputNode{nullptr};
	const VSVideoInfo * pSourceVideoInfo{nullptr};
	VSVideoInfo videoInfo{};
	ShowMode mode{0};
	double amplify{1.};
};

//==============================================================================
// Forward declarations

void VS_CC initShow(VSMap * a_pIn, VSMap * a_pOut, void ** a_ppInstanceData,
	VSNode * a_pNode, VSCore * a_pCore, const VSAPI * a_cpVSAPI);

const VSFrameRef * VS_CC getFrameShow(int a_n, int a_activationReason,
	void ** a_ppInstanceData, void ** a_ppFrameData,
	VSFrameContext * a_pFrameCtx, VSCore * a_pCore, const VSAPI * a_cpVSAPI);

void VS_CC freeShow(void * a_pInstanceData, VSCore * a_pCore,
	const VSAPI * a_cpVSAPI);

//==============================================================================

void VS_CC createShow(const VSMap * a_pIn, VSMap * a_pOut, void * a_pUserData,
	VSCore * a_pCore, const VSAPI * a_cpVSAPI)
{
	UNUSED(a_pUserData);

	DataShow internalData;

	internalData.pInputNode = a_cpVSAPI->propGetNode(a_pIn, "clip", 0, 0);
	internalData.pSourceVideoInfo = a_cpVSAPI->getVideoInfo(internalData.pInputNode);

	//--------------------------------------------------------------------------
	// Format check

	const VSFormat * pFormat = internalData.pSourceVideoInfo->format;

	bool acceptableFormat = isConstantFormat(internalData.pSourceVideoInfo) &&
		(internalData.pSourceVideoInfo->numFrames != 0) && 
		(pFormat == a_cpVSAPI->getFormatPreset(pfYUV444P16, a_pCore));
	if(!acceptableFormat)
	{
		a_cpVSAPI->setError(a_pOut, "nvof.show: "
			"input clip format doesn't match the NVOF flow format.");
		a_cpVSAPI->freeNode(internalData.pInputNode);

		return;
	}

	//--------------------------------------------------------------------------
	// mode

	int error = 0;
	int64_t mode = a_cpVSAPI->propGetInt(a_pIn, "mode", 0, &error);
	if(error != 0)
	{
		a_cpVSAPI->setError(a_pOut, "nvof.Show: "
			"failed to initialize the \"mode\" argument.");
		a_cpVSAPI->freeNode(internalData.pInputNode);
		return;
	}

	if((mode < 0) || (mode >= (int64_t)ShowMode::COUNT))
	{
		a_cpVSAPI->setError(a_pOut, "nvof.Show: "
			"invalid mode.\n"
			"Supported modes are:\n"
			"0: Flow\n"
			"1: Cost");
		a_cpVSAPI->freeNode(internalData.pInputNode);
		return;
	}

	internalData.mode = (ShowMode)mode;

	//--------------------------------------------------------------------------
	// amplify

	double amplify = a_cpVSAPI->propGetFloat(a_pIn, "amplify", 0, &error);
	if(error == 0)
		internalData.amplify = amplify;

	//--------------------------------------------------------------------------
	// output video info

	static std::map<ShowMode, const VSFormat *> formats{
		{ShowMode::Flow, a_cpVSAPI->getFormatPreset(pfRGB24, a_pCore)},
		{ShowMode::Cost, a_cpVSAPI->getFormatPreset(pfGray8, a_pCore)},
	};
	std::map<ShowMode, const VSFormat *>::const_iterator it =
		formats.find(internalData.mode);
	if(it == formats.end())
	{
		a_cpVSAPI->setError(a_pOut, "nvof.Show: "
			"no output format for selected mode. Kick the programmer.");
		a_cpVSAPI->freeNode(internalData.pInputNode);
		return;
	}

	internalData.videoInfo = *internalData.pSourceVideoInfo;
	internalData.videoInfo.format = it->second;

	//--------------------------------------------------------------------------

	DataShow * pInternalData = new DataShow;
	*pInternalData = std::move(internalData);
	a_cpVSAPI->createFilter(a_pIn, a_pOut, "show", initShow,
		getFrameShow, freeShow, fmParallel, 0, pInternalData, a_pCore);
}

//==============================================================================

void VS_CC initShow(VSMap * a_pIn, VSMap * a_pOut, void ** a_ppInstanceData,
	VSNode * a_pNode, VSCore * a_pCore, const VSAPI * a_cpVSAPI)
{
	UNUSED(a_pIn);
	UNUSED(a_pOut);
	UNUSED(a_pCore);

	DataShow * pInternalData = (DataShow *) *a_ppInstanceData;
	a_cpVSAPI->setVideoInfo(&pInternalData->videoInfo, 1, a_pNode);
}

//==============================================================================

// Free all allocated data on filter destruction
void VS_CC freeShow(void * a_pInstanceData, VSCore * a_pCore,
	const VSAPI * a_cpVSAPI)
{
	UNUSED(a_pCore);

	DataShow * pInternalData = (DataShow *)a_pInstanceData;
	a_cpVSAPI->freeNode(pInternalData->pInputNode);
	delete pInternalData;
}

//==============================================================================

const VSFrameRef * VS_CC getFrameShow(int a_n, int a_activationReason,
	void ** a_ppInstanceData, void ** a_ppFrameData,
	VSFrameContext * a_pFrameCtx, VSCore * a_pCore, const VSAPI * a_cpVSAPI)
{
	UNUSED(a_ppFrameData);

	DataShow * pInternalData = (DataShow *) *a_ppInstanceData;
	int framesNumber = pInternalData->pSourceVideoInfo->numFrames;
	int width = pInternalData->pSourceVideoInfo->width;
	int height = pInternalData->pSourceVideoInfo->height;

	a_n = clamp(a_n, 0, framesNumber - 1);

	if(a_activationReason == arInitial)
	{
		a_cpVSAPI->requestFrameFilter(a_n, pInternalData->pInputNode, a_pFrameCtx);
		return nullptr;
	}
	else if(a_activationReason != arAllFramesReady)
		return nullptr;

	const VSFormat * cpFormat = pInternalData->videoInfo.format;

	const VSFrameRef * cpFlowFrame = a_cpVSAPI->getFrameFilter(a_n,
		pInternalData->pInputNode, a_pFrameCtx);

	VSFrameRef * pOutFrame = a_cpVSAPI->newVideoFrame(cpFormat, width, height,
		cpFlowFrame, a_pCore);

	if(pInternalData->mode == ShowMode::Flow)
	{
		int srcUStride = a_cpVSAPI->getStride(cpFlowFrame, 1);
		const uint8_t * cpSrcURow = a_cpVSAPI->getReadPtr(cpFlowFrame, 1);
		int srcVStride = a_cpVSAPI->getStride(cpFlowFrame, 2);
		const uint8_t * cpSrcVRow = a_cpVSAPI->getReadPtr(cpFlowFrame, 2);

		int dstRStride = a_cpVSAPI->getStride(pOutFrame, 0);
		uint8_t * pDstRRow = a_cpVSAPI->getWritePtr(pOutFrame, 0);
		int dstGStride = a_cpVSAPI->getStride(pOutFrame, 1);
		uint8_t * pDstGRow = a_cpVSAPI->getWritePtr(pOutFrame, 1);
		int dstBStride = a_cpVSAPI->getStride(pOutFrame, 2);
		uint8_t * pDstBRow = a_cpVSAPI->getWritePtr(pOutFrame, 2);

		for(int h = 0; h < height; ++h)
		{
			const uint16_t * cpSrcU16Row = (const uint16_t *)cpSrcURow;
			const uint16_t * cpSrcV16Row = (const uint16_t *)cpSrcVRow;
			for(int w = 0; w < width; ++w)
			{
				RGB8 rgb = fromUV(cpSrcU16Row[w], cpSrcV16Row[w], pInternalData->amplify);
				pDstRRow[w] = rgb.r;
				pDstGRow[w] = rgb.g;
				pDstBRow[w] = rgb.b;
			}
			
			cpSrcURow += srcUStride;
			cpSrcVRow += srcVStride;
			pDstRRow += dstRStride;
			pDstGRow += dstGStride;
			pDstBRow += dstBStride;
		}
	}
	else if(pInternalData->mode == ShowMode::Cost)
	{
		int srcStride = a_cpVSAPI->getStride(cpFlowFrame, 0);
		const uint8_t * cpSrcRow = a_cpVSAPI->getReadPtr(cpFlowFrame, 0);
		int dstStride = a_cpVSAPI->getStride(pOutFrame, 0);
		uint8_t * pDstRow = a_cpVSAPI->getWritePtr(pOutFrame, 0);

		for(int h = 0; h < height; ++h)
		{
			for(int w = 0; w < width; ++w)
				pDstRow[w] = cpSrcRow[w * 2];
			cpSrcRow += srcStride;
			pDstRow += dstStride;
		}
	}

	a_cpVSAPI->freeFrame(cpFlowFrame);

	return pOutFrame;
}

//==============================================================================
