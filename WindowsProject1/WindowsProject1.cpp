#ifndef UNICODE
#define UNICODE
#endif 

#include <windows.h>
#include "BoxApp.h"
#include "DxDebug.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow)
{
    try {

        BoxApp win(hInstance);

        if (!win.Create(L"Learn to Program Windows", WS_OVERLAPPEDWINDOW))
        {
            return 0;
        }

        win.Run();
    }
    catch (DxException& e)
    {
        OutputDebugString(L"Exception:");
        OutputDebugString(e.ToString().c_str());
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
    }

    return 0;
}