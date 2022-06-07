#ifndef NVIDIA_OPTICAL_FLOW_API_H
#define NVIDIA_OPTICAL_FLOW_API_H

#include "common.h"
#include "nvOpticalFlowCuda.h"

#include <string>
#include <mutex>

class NVidiaOpticalFlowAPI
{
public:
	static bool create(std::string & a_errorString);
	static bool destroy(std::string& a_errorString);
	static const NV_OF_CUDA_API_FUNCTION_LIST * getAPI();
private:
	static HMODULE m_hLibNVOF;
	static HMODULE m_hLibNVCUDA;
	static NV_OF_CUDA_API_FUNCTION_LIST m_API;
	static size_t m_referencesNumber;
	static std::mutex m_lock;
};

#endif
