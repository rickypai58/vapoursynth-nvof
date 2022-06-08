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

#include "nvidia-optical-flow.h"
#include <cassert>

//==============================================================================

NVidiaOpticalFlow::NVidiaOpticalFlow(const VSAPI * a_cpVSAPI,
	const VSVideoInfo & a_sourceVideoInfo, bool a_chromaMotion,
	NV_OF_PERF_LEVEL a_perfLevel, int a_gpuID):
	  m_cpVSAPI(a_cpVSAPI)
	, m_sourceVideoInfo(a_sourceVideoInfo)
	, m_chromaMotion(a_chromaMotion)
	, m_perfLevel(a_perfLevel)
	, m_gpuID(a_gpuID)
{
	assert(m_cpVSAPI);
}

//==============================================================================

NVidiaOpticalFlow::~NVidiaOpticalFlow()
{
	m_cpVSAPI->logMessage(mtDebug, "NVidiaOpticalFlow: Cleaning up.");
	if(!m_initialized)
	{
		m_cpVSAPI->logMessage(mtDebug, "NVidiaOpticalFlow: Nothing to clean up.");
		return;
	}

	destroyNVOF();
	destroyContext();
	releaseAPI();
	m_cpVSAPI->logMessage(mtDebug, "NVidiaOpticalFlow: Cleaning up finished.");
}

//==============================================================================

NV_OF_BUFFER_FORMAT NVidiaOpticalFlow::bufferFormat(const VSFormat * a_cpFormat,
	bool chromaMotion, std::string & a_errorString)
{
	const int planes = a_cpFormat->numPlanes;
	const VSColorFamily family = (VSColorFamily)a_cpFormat->colorFamily;
	const int ssW = a_cpFormat->subSamplingW;
	const int ssH = a_cpFormat->subSamplingH;

	if((a_cpFormat->bitsPerSample != 8) || ((planes != 1) && (planes != 3)))
	{
		a_errorString = "Only 8 bit per sample formats with 1 or 3 planes "
			"are supported.";
		return NV_OF_BUFFER_FORMAT_UNDEFINED;
	}

	if(planes == 1)
		return NV_OF_BUFFER_FORMAT_GRAYSCALE8;

	if(((family == cmYUV) || (family == cmYCoCg)) && (!chromaMotion))
		return NV_OF_BUFFER_FORMAT_GRAYSCALE8;

	if(((family == cmYUV) || (family == cmYCoCg)) && (ssH == 1) && (ssW == 1))
		return NV_OF_BUFFER_FORMAT_NV12;

	if((ssH != 0) || (ssW != 0))
	{
		a_errorString = "Subsampling only supported for YUV420P8 format.";
		return NV_OF_BUFFER_FORMAT_UNDEFINED;
	}

	return NV_OF_BUFFER_FORMAT_ABGR8;
}

//==============================================================================

bool NVidiaOpticalFlow::init(std::string & a_errorString)
{
	m_cpVSAPI->logMessage(mtDebug, "NVidiaOpticalFlow: Initializing.");
	if(m_initialized)
	{
		m_cpVSAPI->logMessage(mtDebug, "NVidiaOpticalFlow: Already initialized.");
		return true;
	}

	int planes = m_sourceVideoInfo.format->numPlanes;
	if((planes != 1) && (planes != 3))
	{
		a_errorString = "Only 1 and 3 planes are supported.";
		return false;
	}

	if(!NVidiaOpticalFlowAPI::create(a_errorString))
		return false;

	CUdevice device;
	CUresult cuResult = cuDeviceGet(&device, m_gpuID);
	if(cuResult != CUDA_SUCCESS)
	{
		a_errorString = NVidiaOpticalFlowAPI::getErrorDescription(cuResult);
		releaseAPI();
		return false;
	}

	cuResult = cuCtxCreate(&m_context, 0, device);
	if(cuResult != CUDA_SUCCESS)
	{
		a_errorString = NVidiaOpticalFlowAPI::getErrorDescription(cuResult);
		releaseAPI();
		return false;
	}

	if(!initNVOF(a_errorString))
	{
		releaseAPI();
		return false;
	}

	m_initialized = true;

	m_cpVSAPI->logMessage(mtDebug, "NVidiaOpticalFlow: Initializing complete.");

	return true;
}

//==============================================================================

void NVidiaOpticalFlow::releaseAPI()
{
	std::string destroyError;
	if(!NVidiaOpticalFlowAPI::destroy(destroyError))
		m_cpVSAPI->logMessage(mtWarning, (std::string("NVidiaOpticalFlow: "
			"Failed to release the API. ") + destroyError).c_str());
}

//==============================================================================

void NVidiaOpticalFlow::destroyContext()
{
	if(!m_context)
		return;

	CUresult cuResult = cuCtxDestroy(m_context);
	if(cuResult != CUDA_SUCCESS)
	{
		m_cpVSAPI->logMessage(mtWarning, (std::string("NVidiaOpticalFlow: "
			"Failed to destroy CUDA context. ") +
			NVidiaOpticalFlowAPI::getErrorDescription(cuResult)).c_str());
		return;
	}
	m_context = nullptr;
}

//==============================================================================

bool NVidiaOpticalFlow::initNVOF(std::string & a_errorString)
{
	NV_OF_BUFFER_FORMAT sourceBufferFormat = bufferFormat(m_sourceVideoInfo.format,
		m_chromaMotion, a_errorString);
	if(sourceBufferFormat == NV_OF_BUFFER_FORMAT_UNDEFINED)
		return false;

	NV_OF_STATUS result = NVidiaOpticalFlowAPI::getAPI()->nvCreateOpticalFlowCuda(
		m_context, &m_handle);
	if(result != NV_OF_SUCCESS)
	{
		a_errorString = "NVidiaOpticalFlow: Failed to create CUDA flow. ";
		a_errorString += NVidiaOpticalFlowAPI::getErrorDescription(result);
		return false;
	}

	assert(m_handle);

	NV_OF_INIT_PARAMS params{
		(uint32_t)m_sourceVideoInfo.width,
		(uint32_t)m_sourceVideoInfo.height,
		NV_OF_OUTPUT_VECTOR_GRID_SIZE_1,
		NV_OF_HINT_VECTOR_GRID_SIZE_UNDEFINED,
		NV_OF_MODE_OPTICALFLOW,
		m_perfLevel,
		NV_OF_FALSE,
		NV_OF_TRUE,
		0,
		NV_OF_STEREO_DISPARITY_RANGE_UNDEFINED,
		NV_OF_FALSE,
		NV_OF_PRED_DIRECTION_FORWARD,
		NV_OF_FALSE,
		sourceBufferFormat
	};

	result = NVidiaOpticalFlowAPI::getAPI()->nvOFInit(m_handle, &params);
	if(result != NV_OF_SUCCESS)
	{
		a_errorString = "NVidiaOpticalFlow: Failed to initialize CUDA flow. ";
		a_errorString += NVidiaOpticalFlowAPI::getErrorDescription(result);
		destroyNVOF();
		return false;
	}

	return true;
}

//==============================================================================

void NVidiaOpticalFlow::destroyNVOF()
{
	if(!m_handle)
		return;

	NV_OF_STATUS result = NVidiaOpticalFlowAPI::getAPI()->nvOFDestroy(m_handle);
	if(result != NV_OF_SUCCESS)
	{
		m_cpVSAPI->logMessage(mtWarning, (std::string("NVidiaOpticalFlow: "
			"Failed to destroy CUDA flow. ") +
			NVidiaOpticalFlowAPI::getErrorDescription(result)).c_str());
		return;
	}
	m_handle = nullptr;
}

//==============================================================================
