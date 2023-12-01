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
//#include "13Blur/BlurApp.h"
//#include "14BasicTessellation/BasicTessellationApp.h"
//#include "14Bezier/BezierApp.h"
//#include "15Camera/CameraApp.h"
//#include "16InstancingAndCulling/InstancingAndCullingApp.h"
//#include "17Picking/PickingApp.h"
//#include "18CubeMapping/CubeMapApp.h"
//#include "19NormalMapping/NormalMapApp.h"
//#include "19DisplacementMapping/DisplacementMapApp.h"
//#include "20ShadowMapping/ShadowMapApp.h"
//#include "21AmbientOcclusion/SsaoApp.h"
//#include "22Quaternion/QuaternionApp.h"
//#include "23Skinning/SkinningApp.h"
#include "24Ocean/OceanApp.h"
#include "Common/DxDebug.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow){
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
        //BlurApp win(hInstance);
        //BasicTessellationApp win(hInstance);
        //BezierApp win(hInstance);
        //CameraApp win(hInstance);
        //InstancingAndCullingApp win(hInstance);
        //PickingApp win(hInstance);
        //CubeMapApp win(hInstance);
        //NormalMapApp win(hInstance);
        //DisplacementMapApp win(hInstance);
        //ShadowMapApp win(hInstance);
        //SsaoApp win(hInstance);
        //QuaternionApp win(hInstance);
        //SkinningApp win(hInstance);
        OceanApp win(hInstance);

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