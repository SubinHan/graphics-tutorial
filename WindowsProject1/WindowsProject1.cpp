#ifndef UNICODE
#define UNICODE
#endif 

#include <windows.h>
// #include "05/InitApp.h"
 //#include "06/BoxApp.h"
//#nclude "07/ShapeApp.h"
#include "07LandAndWaves//LandAndWavesApp.h"
#include "Common/DxDebug.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow)
{
    try {
        //InitApp win(hInstance);
        //BoxApp win(hInstance);
        //ShapeApp win(hInstance);
        LandAndWavesApp win(hInstance);

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