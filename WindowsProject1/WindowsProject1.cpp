#ifndef UNICODE
#define UNICODE
#endif 

#include <windows.h>
// #include "05/InitApp.h"
 //#include "06/BoxApp.h"
//#include "07/ShapeApp.h"
//#include "07LandAndWaves//LandAndWavesApp.h"
//#include "08LitWaves/LitWavesApp.h"
//#include "09Crate/CrateApp.h"
//#include "09TexShape/TexShapeApp.h"
//#include "09TexWaves/TexWavesApp.h"
//#include "10Blend/BlendApp.h"
//#include "11Stencil/StencilApp.h"
//#include "12TreeBillboards/TreeApp.h"
#include "13Blur/BlurApp.h"
#include "Common/DxDebug.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow)
{
    try {
        //InitApp win(hInstance);
        //BoxApp win(hInstance);
        //ShapeApp win(hInstance);
        //LandAndWavesApp win(hInstance);
        //LitWavesApp win(hInstance);
        //CrateApp win(hInstance);
        //TexShapeApp win(hInstance);
        //TexWavesApp win(hInstance);
        //BlendApp win(hInstance);
        //StencilApp win(hInstance);
        //TreeApp win(hInstance);
        BlurApp win(hInstance);

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