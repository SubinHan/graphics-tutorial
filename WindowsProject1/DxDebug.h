#pragma once
#include <Windows.h>
#include <string>
#include <comdef.h>

class DxException
{
public:
	DxException() = default;
	DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber);
	std::wstring ToString() const;

	HRESULT errorCode = S_OK;
	std::wstring functionName;
	std::wstring filename;
	int lineNumber = -1;
};

#ifndef ThrowIfFailed
#define ThrowIfFailed(x) \
{ \
	HRESULT hr__ = (x); \
	std::wstring wfn = AnsiToWString(__FILE__); \
	if(FAILED(hr__)) { throw DxException(hr__, L#x, wfn, __LINE__); } \
}
#endif