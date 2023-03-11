#include "DxDebug.h"

DxException::DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber)
{
	errorCode = hr;
	this->functionName = functionName;
	this->filename = filename;
	this->lineNumber = lineNumber;
}

std::wstring DxException::ToString() const
{
	std::wstring text;
	text += functionName;
	text += L" failed in ";
	text += filename;
	text += L" at line ";
	text += std::to_wstring(lineNumber);
	text += L" error: ";
	_com_error err(errorCode);
	text += err.ErrorMessage();

	return text;
}
