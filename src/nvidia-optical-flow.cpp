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
#include <vector>

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
	if(!m_initialized)
		return;

	destroyBuffers();
	destroyNVOF();
	destroyContext();
	releaseAPI();
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
	if(m_initialized)
		return true;

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

	if(!createBuffers(a_errorString))
	{
		destroyNVOF();
		releaseAPI();
		return false;
	}

	m_initialized = true;

	return true;
}

//==============================================================================

void NVidiaOpticalFlow::getFlow(const VSFrameRef * a_cpCurrentFrame,
	const VSFrameRef * a_cpDeltaFrame, VSFrameRef * a_pOutFrame)
{
	const VSFormat * cpCurrentFormat = m_cpVSAPI->getFrameFormat(a_cpCurrentFrame);
	const VSFormat * cpDeltaFormat = m_cpVSAPI->getFrameFormat(a_cpDeltaFrame);
	if((m_sourceVideoInfo.format != cpCurrentFormat) || (cpCurrentFormat != cpDeltaFormat))
		return;

	if((cpCurrentFormat->colorFamily == cmYUV) && (cpCurrentFormat->subSamplingW == 1) &&
		(cpCurrentFormat->subSamplingH == 1))
	{
		loadYUV420toBuffer(a_cpCurrentFrame, m_hSourceFrame);
		loadYUV420toBuffer(a_cpDeltaFrame, m_hDeltaFrame);
	}
	
	NV_OF_EXECUTE_INPUT_PARAMS inputParams = {
		m_hSourceFrame,
		m_hDeltaFrame,
		nullptr,
		NV_OF_TRUE,
		0,
		nullptr,
		0,
		0,
		nullptr
	};

	NV_OF_EXECUTE_OUTPUT_PARAMS outputParams = {
		m_hFlow,
		m_hCost,
		nullptr,
		nullptr,
		nullptr,
		nullptr
	};

	cuCtxPushCurrent(m_context);
	NV_OF_STATUS result = NVidiaOpticalFlowAPI::getAPI()->nvOFExecute(m_handle,
		&inputParams, &outputParams);
	cuCtxPopCurrent(nullptr);
	if(result != NV_OF_SUCCESS)
		return;

	downloadFlow(a_pOutFrame);
}

//==============================================================================

void NVidiaOpticalFlow::releaseAPI()
{
	std::string destroyError;
	NVidiaOpticalFlowAPI::destroy(destroyError);
}

//==============================================================================

void NVidiaOpticalFlow::destroyContext()
{
	if(!m_context)
		return;

	CUresult cuResult = cuCtxDestroy(m_context);
	if(cuResult != CUDA_SUCCESS)
		return;

	m_context = nullptr;
}

//==============================================================================

bool NVidiaOpticalFlow::initNVOF(std::string & a_errorString)
{
	m_sourceBufferFormat = bufferFormat(m_sourceVideoInfo.format,
		m_chromaMotion, a_errorString);
	if(m_sourceBufferFormat == NV_OF_BUFFER_FORMAT_UNDEFINED)
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
		m_sourceBufferFormat
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
		return;

	m_handle = nullptr;
}

//==============================================================================

bool NVidiaOpticalFlow::createBuffers(std::string & a_errorString)
{
	if(!m_handle)
	{
		a_errorString = "Failed to create CUDA buffers. "
			"NVidiaOpticalFlow is not initialized.";
		return false;
	}

	NV_OF_BUFFER_DESCRIPTOR descriptor{(uint32_t)m_sourceVideoInfo.width,
		(uint32_t)m_sourceVideoInfo.height, NV_OF_BUFFER_USAGE_UNDEFINED,
		NV_OF_BUFFER_FORMAT_UNDEFINED};

	struct BufferInitInfo
	{
		NvOFGPUBufferHandle * pHandle{nullptr};
		NV_OF_BUFFER_USAGE usage{NV_OF_BUFFER_USAGE_UNDEFINED};
		NV_OF_BUFFER_FORMAT format{NV_OF_BUFFER_FORMAT_UNDEFINED};
	};

	BufferInitInfo buffersToInit[] = {
		{&m_hSourceFrame, NV_OF_BUFFER_USAGE_INPUT, m_sourceBufferFormat},
		{&m_hDeltaFrame, NV_OF_BUFFER_USAGE_INPUT, m_sourceBufferFormat},
		{&m_hFlow, NV_OF_BUFFER_USAGE_OUTPUT, NV_OF_BUFFER_FORMAT_SHORT2},
		{&m_hCost, NV_OF_BUFFER_USAGE_COST, NV_OF_BUFFER_FORMAT_UINT8},
	};

	for(const BufferInitInfo & info : buffersToInit)
	{
		descriptor.bufferUsage = info.usage;
		descriptor.bufferFormat = info.format;
		NV_OF_STATUS result = NVidiaOpticalFlowAPI::getAPI()->nvOFCreateGPUBufferCuda(
			m_handle, &descriptor, NV_OF_CUDA_BUFFER_TYPE_CUDEVICEPTR, info.pHandle);
		if(result != NV_OF_SUCCESS)
		{
			a_errorString = std::string("Failed to create a CUDA buffer. ") +
				NVidiaOpticalFlowAPI::getErrorDescription(result);
			destroyBuffers();
			return false;
		}
	}

	return true;
}

//==============================================================================

void NVidiaOpticalFlow::destroyBuffers()
{
	NvOFGPUBufferHandle * buffers[] = {&m_hSourceFrame, &m_hDeltaFrame,
		&m_hFlow, &m_hCost};
	for(NvOFGPUBufferHandle * pHandle : buffers)
	{
		if(!(*pHandle))
			continue;
		NVidiaOpticalFlowAPI::getAPI()->nvOFDestroyGPUBufferCuda(*pHandle);
		*pHandle = nullptr;
	}
}

//==============================================================================

void NVidiaOpticalFlow::loadYUV420toBuffer(const VSFrameRef * a_cpFrame, NvOFGPUBufferHandle a_buffer)
{
	const size_t width = m_sourceVideoInfo.width;
	const size_t height = m_sourceVideoInfo.height;
	const size_t tempBufferYSize = width * height;
	const size_t tempBufferUVSize = width * height / 2;
	const size_t tempBufferSize = tempBufferYSize + tempBufferUVSize;

	std::vector<uint8_t> tempBuffer(tempBufferSize, 0);

	const int yStride = m_cpVSAPI->getStride(a_cpFrame, 0);
	const uint8_t * cpYRow = m_cpVSAPI->getReadPtr(a_cpFrame, 0);
	uint8_t * pDest = tempBuffer.data();

	for(size_t h = 0; h < height; ++h)
	{
		memcpy(pDest, cpYRow, width);
		cpYRow += yStride;
		pDest += width;
	}

	const int uStride = m_cpVSAPI->getStride(a_cpFrame, 1);
	const uint8_t * cpURow = m_cpVSAPI->getReadPtr(a_cpFrame, 1);
	const int vStride = m_cpVSAPI->getStride(a_cpFrame, 2);
	const uint8_t * cpVRow = m_cpVSAPI->getReadPtr(a_cpFrame, 2);

	for(size_t h = 0; h < (height / 2); ++h)
	{
		for(size_t w = 0; w < (width / 2); ++w)
		{
			*pDest = cpURow[w];
			pDest++;
			*pDest = cpVRow[w];
			pDest++;
		}
		cpURow += uStride;
		cpVRow += vStride;
	}

	cuCtxPushCurrent(m_context);

	CUdeviceptr devicePtr = NVidiaOpticalFlowAPI::getAPI()->nvOFGPUBufferGetCUdeviceptr(a_buffer);

	NV_OF_CUDA_BUFFER_STRIDE_INFO strideInfo{};
	NVidiaOpticalFlowAPI::getAPI()->nvOFGPUBufferGetStrideInfo(a_buffer, &strideInfo);

	CUDA_MEMCPY2D cuCopy2d;
	memset(&cuCopy2d, 0, sizeof(cuCopy2d));
	cuCopy2d.WidthInBytes = width;
	cuCopy2d.srcMemoryType = CU_MEMORYTYPE_HOST;
	cuCopy2d.srcHost = tempBuffer.data();
	cuCopy2d.srcPitch = width;
	cuCopy2d.dstMemoryType = CU_MEMORYTYPE_DEVICE;
	cuCopy2d.dstDevice = devicePtr;
	cuCopy2d.dstPitch = strideInfo.strideInfo[0].strideXInBytes;
	cuCopy2d.Height = height + height / 2;

	cuMemcpy2D(&cuCopy2d);
	cuCtxPopCurrent(nullptr);
}

//==============================================================================

void NVidiaOpticalFlow::downloadFlow(VSFrameRef * a_pFrame)
{
	cuCtxPushCurrent(m_context);

	const size_t width = m_sourceVideoInfo.width;
	const size_t height = m_sourceVideoInfo.height;

	size_t flowTempBufferSize = width * height * 2;
	size_t flowTempBufferStride = width * sizeof(uint16_t) * 2;
	std::vector<uint16_t> flowTempBuffer(flowTempBufferSize, 0);
	std::vector<uint8_t> costTempBuffer(width * height, 0);

	CUdeviceptr flowDevicePtr = NVidiaOpticalFlowAPI::getAPI()->nvOFGPUBufferGetCUdeviceptr(m_hFlow);
	CUdeviceptr costDevicePtr = NVidiaOpticalFlowAPI::getAPI()->nvOFGPUBufferGetCUdeviceptr(m_hCost);

	NV_OF_CUDA_BUFFER_STRIDE_INFO strideInfo{};
	NVidiaOpticalFlowAPI::getAPI()->nvOFGPUBufferGetStrideInfo(m_hFlow, &strideInfo);

	CUDA_MEMCPY2D cuCopy2d;
	memset(&cuCopy2d, 0, sizeof(cuCopy2d));
	cuCopy2d.WidthInBytes = flowTempBufferStride;
	cuCopy2d.dstMemoryType = CU_MEMORYTYPE_HOST;
	cuCopy2d.dstHost = flowTempBuffer.data();
	cuCopy2d.dstPitch = flowTempBufferStride;
	cuCopy2d.srcMemoryType = CU_MEMORYTYPE_DEVICE;
	cuCopy2d.srcDevice = flowDevicePtr;
	cuCopy2d.srcPitch = strideInfo.strideInfo[0].strideXInBytes;
	cuCopy2d.Height = height;

	NVidiaOpticalFlowAPI::getAPI()->nvOFGPUBufferGetStrideInfo(m_hCost, &strideInfo);

	cuMemcpy2D(&cuCopy2d);

	cuCopy2d.WidthInBytes = width;
	cuCopy2d.dstHost = costTempBuffer.data();
	cuCopy2d.dstPitch = width;
	cuCopy2d.srcDevice = costDevicePtr;
	cuCopy2d.srcPitch = strideInfo.strideInfo[0].strideXInBytes;

	cuMemcpy2D(&cuCopy2d);

	cuCtxPopCurrent(nullptr);

	const int yStride = m_cpVSAPI->getStride(a_pFrame, 0);
	uint8_t * pDestRow = m_cpVSAPI->getWritePtr(a_pFrame, 0);
	const uint8_t * pSrcRow = costTempBuffer.data();
	for(size_t h = 0; h < height; ++h)
	{
		for(size_t w = 0; w < width; ++w)
			pDestRow[w << 1] = pSrcRow[w];
		pDestRow += yStride;
		pSrcRow += width;
	}

	const int uStride = m_cpVSAPI->getStride(a_pFrame, 1);
	const int vStride = m_cpVSAPI->getStride(a_pFrame, 2);
	uint8_t * pDestURow = m_cpVSAPI->getWritePtr(a_pFrame, 1);
	uint8_t * pDestVRow = m_cpVSAPI->getWritePtr(a_pFrame, 2);
	const size_t flowStrideInElements = width * 2;
	const uint16_t * cpSrcRow = flowTempBuffer.data();
	for(size_t h = 0; h < height; ++h)
	{
		uint16_t * pURow16 = (uint16_t *)pDestURow;
		uint16_t * pVRow16 = (uint16_t *)pDestVRow;
		for(size_t w = 0; w < width; ++w)
		{
			pURow16[w] = cpSrcRow[w << 1];
			pVRow16[w] = cpSrcRow[(w << 1) | 1];
		}
		pDestURow += uStride;
		pDestVRow += vStride;
		cpSrcRow += flowStrideInElements;
	}
}

//==============================================================================
