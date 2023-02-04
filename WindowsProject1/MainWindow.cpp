#include "MainWindow.h"

#include <WindowsX.h>

using namespace std;

template <class T> void SafeRelease(T** ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

MainWindow::MainWindow(HINSTANCE hInstance) : BaseWindow(hInstance),
    pMessageHandler(nullptr),
    pFactory(nullptr),
    device(nullptr)
{
}

float MainWindow::AspectRatio() const
{
    return static_cast<float>(device->GetClientWidth()) / device->GetClientHeight();
}

int MainWindow::Run()
{
    ShowWindow(Window(), SW_SHOWNORMAL);

    // Run the message loop.
    bool bGotMsg;
    MSG  msg;
    timer.Reset();
    msg.message = WM_NULL;
    PeekMessage(&msg, NULL, 0U, 0U, PM_NOREMOVE);

    while (WM_QUIT != msg.message)
    {
        // Process window events.
        // Use PeekMessage() so we can use idle time to render the scene. 
        bGotMsg = PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE);

        if (bGotMsg)
        {
            // Translate and dispatch the message
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

		//// Update the scene.
		//renderer->Update();

		//// Render frames during idle time (when no messages are waiting).
		//renderer->Render();

		//// Present the frame to the screen.
		//deviceResources->Present();

		timer.Tick();

        if (isPaused)
        {
            Sleep(100);
            continue;
        }

		CalculateFrameStats();
		Update(timer);
		Draw(timer);
    }

    return 0;
}

bool MainWindow::Initialize()
{
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &pFactory)))
    {
        return false;
    }
    CreateDevice();
    
    return true;
}

PCWSTR MainWindow::ClassName() const
{
    return L"Sample Window Class";
}

LRESULT MainWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
        if (!Initialize())
            return -1;
        return 0;

    case WM_DESTROY:
        ReleaseDevice();
        PostQuitMessage(0);
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE)
        {
            isPaused = true;
            timer.Stop();
            return 0;
        }

        isPaused = false;
        timer.Start();
        return 0;

    case WM_SIZE:
        // Save the new client area dimensions.
        device->SetClientWidth(LOWORD(lParam));
        device->SetClientHeight(HIWORD(lParam));
        if (device->GetD3DDevice())
        {
            if (wParam == SIZE_MINIMIZED)
            {
                isPaused = true;
                isMinimized = true;
                isMaximized = false;
            }
            else if (wParam == SIZE_MAXIMIZED)
            {
                isPaused = false;
                isMinimized = false;
                isMaximized = true;
                OnResize();
            }
            else if (wParam == SIZE_RESTORED)
            {

                // Restoring from minimized state?
                if (isMinimized)
                {
                    isPaused = false;
                    isMinimized = false;
                    OnResize();
                }

                // Restoring from maximized state?
                else if (isMaximized)
                {
                    isPaused = false;
                    isMaximized = false;
                    OnResize();
                }
                else if (isResizing)
                {
                    // If user is dragging the resize bars, we do not resize 
                    // the buffers here because as the user continuously 
                    // drags the resize bars, a stream of WM_SIZE messages are
                    // sent to the window, and it would be pointless (and slow)
                    // to resize for each WM_SIZE message received from dragging
                    // the resize bars.  So instead, we reset after the user is 
                    // done resizing the window and releases the resize bars, which 
                    // sends a WM_EXITSIZEMOVE message.
                }
                else // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
                {
                    OnResize();
                }
            }
        }
        return 0;

    case WM_ENTERSIZEMOVE:
        isPaused = true;
        isResizing = true;
        timer.Stop();
        return 0;

    case WM_EXITSIZEMOVE:
        isPaused = false;
        isResizing = false;
        timer.Start();
        OnResize();
        return 0;

    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
        ((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
        return 0;

    case WM_LBUTTONDOWN:
    {
        int xPos = GET_X_LPARAM(lParam);
        int yPos = GET_Y_LPARAM(lParam);
        int keyState = GET_KEYSTATE_WPARAM(wParam);
        LeftDown(xPos, yPos, keyState);
        break;
    }
    case WM_LBUTTONUP:
    {
        int xPos = GET_X_LPARAM(lParam);
        int yPos = GET_Y_LPARAM(lParam);
        int keyState = GET_KEYSTATE_WPARAM(wParam);
        LeftUp(xPos, yPos, keyState);
        break;
    }
    case WM_MBUTTONDOWN:
    {
        int xPos = GET_X_LPARAM(lParam);
        int yPos = GET_Y_LPARAM(lParam);
        int keyState = GET_KEYSTATE_WPARAM(wParam);
        MiddleDown(xPos, yPos, keyState);
        break;
    }
    case WM_MBUTTONUP:
    {
        int xPos = GET_X_LPARAM(lParam);
        int yPos = GET_Y_LPARAM(lParam);
        int keyState = GET_KEYSTATE_WPARAM(wParam);
        MiddleUp(xPos, yPos, keyState);
        break;
    }
    case WM_RBUTTONDOWN:
    {
        int xPos = GET_X_LPARAM(lParam);
        int yPos = GET_Y_LPARAM(lParam);
        int keyState = GET_KEYSTATE_WPARAM(wParam);
        RightDown(xPos, yPos, keyState);
        break;
    }
    case WM_RBUTTONUP:
    {
        int xPos = GET_X_LPARAM(lParam);
        int yPos = GET_Y_LPARAM(lParam);
        int keyState = GET_KEYSTATE_WPARAM(wParam);
        RightUp(xPos, yPos, keyState);
        break;
    }
    case WM_XBUTTONDOWN:
    {
        int xPos = GET_X_LPARAM(lParam);
        int yPos = GET_Y_LPARAM(lParam);
        int keyState = GET_KEYSTATE_WPARAM(wParam);
        XDown(xPos, yPos, keyState);
        break;
    }
    case WM_XBUTTONUP:
    {
        int xPos = GET_X_LPARAM(lParam);
        int yPos = GET_Y_LPARAM(lParam);
        int keyState = GET_KEYSTATE_WPARAM(wParam);
        XUp(xPos, yPos, keyState);
        break;
    }
    case WM_MOUSEWHEEL:
    {
        int xPos = GET_X_LPARAM(lParam);
        int yPos = GET_Y_LPARAM(lParam);
        int keyState = GET_KEYSTATE_WPARAM(wParam);
        MouseWheel(GET_WHEEL_DELTA_WPARAM(wParam), keyState);
        break;
}
    case WM_MOUSEHOVER:
    {
        int xPos = GET_X_LPARAM(lParam);
        int yPos = GET_Y_LPARAM(lParam);
        int keyState = GET_KEYSTATE_WPARAM(wParam);
        MouseHover(xPos, yPos);
        break;
    }
    case WM_MOUSELEAVE:
    {
        int xPos = GET_X_LPARAM(lParam);
        int yPos = GET_Y_LPARAM(lParam);
        int keyState = GET_KEYSTATE_WPARAM(wParam);
        MouseLeave();
        break;
    }
    case WM_MOUSEMOVE:
    {
        int xPos = GET_X_LPARAM(lParam);
        int yPos = GET_Y_LPARAM(lParam);
        int keyState = GET_KEYSTATE_WPARAM(wParam);
        MouseMove(xPos, yPos, keyState);
        break;
    }

    default:
        return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
    }

    return TRUE;
}

void MainWindow::CreateDevice()
{
    device = new DxDevice(Window());
    OnResize();
}

void MainWindow::ReleaseDevice()
{
    delete device;
}

void MainWindow::OnResize()
{
    device->FlushCommandQueue();
    auto commandList = device->GetCommandList();
    auto commandQueue = device->GetCommandQueue();

    ThrowIfFailed(device->GetCommandList()->Reset(device->GetCommandListAllocator().Get(), nullptr));

    device->ResetAllSwapChainBuffers();
    device->ResetDepthStencilBuffer();
    device->ResizeBuffers();
    device->CreateDepthStencilView();

    ThrowIfFailed(commandList->Close());
    ID3D12CommandList* cmdsLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    device->FlushCommandQueue();

    device->InitScreenViewport();
    device->InitScissorRect();
}

void MainWindow::CalculateFrameStats()
{
    static int frameCnt = 0;
    static float timeElapsed = 0.0f;

    frameCnt++;
    if (timer.GameTime() - timeElapsed >= 1.0f)
    {
        float fps = (float)frameCnt;
        float mspf = 1000.0f / fps;

        wstring fpsStr = to_wstring(fps);
        wstring mspfStr = to_wstring(mspf);

        wstring windowText = L"  fps: " + fpsStr +
            L"  mspf: " + mspfStr;

        SetWindowText(m_hwnd, windowText.c_str());

        frameCnt = 0;
        timeElapsed += 1.0f;
    }
}
