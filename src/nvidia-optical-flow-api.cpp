#include "nvidia-optical-flow-api.h"

#ifdef _WIN32
	const wchar_t NVOF_LIBRARY_NAME[] =
	#ifdef _WIN64
		L"nvofapi64.dll";
	#else
		L"nvofapi.dll";
	#endif

	const char NVCUDA_LIBRARY_NAME[] = "nvcuda.dll";

	#define LOAD_LIBRARY(x) LoadLibraryW(x);
	#define FREE_LIBRARY(x) FreeLibrary(x);
#else
	#include <dlfcn.h>
	const char NVOF_LIBRARY_NAME[] = "libnvidia-opticalflow.so.1";

	#define LOAD_LIBRARY(x) dlopen(x, RTLD_LAZY);
	#define FREE_LIBRARY(x) dlclose(x);
#endif

//=============================================================================

HMODULE NVidiaOpticalFlowAPI::m_hLibNVOF = nullptr;
HMODULE NVidiaOpticalFlowAPI::m_hLibNVCUDA = nullptr;
NV_OF_CUDA_API_FUNCTION_LIST NVidiaOpticalFlowAPI::m_API = { nullptr };
size_t NVidiaOpticalFlowAPI::m_referencesNumber = 0;
std::mutex NVidiaOpticalFlowAPI::m_lock;

//=============================================================================

bool NVidiaOpticalFlowAPI::create(std::string & a_errorString)
{
	std::lock_guard<std::mutex> lock(m_lock);
	
	if(m_referencesNumber > 0)
	{
		m_referencesNumber++;
		return true;
	}

	m_hLibNVOF = LOAD_LIBRARY(NVOF_LIBRARY_NAME);
	if(m_hLibNVOF == nullptr)
	{
		a_errorString = "Failed to load the NVIDIA Optical Flow library";
		return false;
	}

	m_referencesNumber++;

	return true;
}

//=============================================================================

bool NVidiaOpticalFlowAPI::destroy(std::string & a_errorString)
{
	std::lock_guard<std::mutex> lock(m_lock);
	if(m_referencesNumber == 0)
	{
		a_errorString = "Cannot destroy API. There are no active API references.";
		return false;
	}

	m_referencesNumber--;

	if(m_referencesNumber > 0)
		return true;

	FREE_LIBRARY(m_hLibNVOF);

	return true;
}

const NV_OF_CUDA_API_FUNCTION_LIST * NVidiaOpticalFlowAPI::getAPI()
{
	return &m_API;
}

//=============================================================================
