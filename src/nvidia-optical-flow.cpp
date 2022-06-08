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

NVidiaOpticalFlow::NVidiaOpticalFlow(const VSAPI * a_cpVSAPI, uint32_t a_width,
	uint32_t a_height, size_t a_planes, NV_OF_PERF_LEVEL a_perfLevel, int a_gpuID):
	  m_cpVSAPI(a_cpVSAPI)
	, m_width(a_width)
	, m_height(a_height)
	, m_planes(a_planes)
	, m_perfLevel(a_perfLevel)
	, m_gpuID(a_gpuID)
{
	assert(m_cpVSAPI);
}

//==============================================================================

NVidiaOpticalFlow::~NVidiaOpticalFlow()
{
	if(!m_initialized)
		return;

	releaseAPI();
}

//==============================================================================

bool NVidiaOpticalFlow::init(std::string & a_errorString)
{
	if(m_initialized)
		return true;

	if((m_planes != 1) && (m_planes != 3))
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

	m_initialized = true;
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
