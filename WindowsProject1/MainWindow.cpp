#include "MainWindow.h"

using namespace std;

template <class T> void SafeRelease(T** ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
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
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &pFactory)))
        {
            return -1;
        }
        InitMessageHandlers();
        CreateDevice();
        return 0;

    case WM_DESTROY:
        DestroyMessageHandlers();
        ReleaseDevice();
        PostQuitMessage(0);
        return 0;

    case WM_PAINT:
    {
        OnPaint();
        return 0;
    }

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


    default:
        if (pMessageHandler)
            return pMessageHandler->HandleMessage(uMsg, wParam, lParam);
        else
            return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
    }
    return TRUE;
}

void MainWindow::InitMessageHandlers()
{
    pMessageHandler = new MouseMessageHandler(m_hwnd);
    pMessageHandler->SetNext(new KeyboardMessageHandler(m_hwnd));
}

void MainWindow::DestroyMessageHandlers()
{
    delete pMessageHandler;
    pMessageHandler = nullptr;
}

void MainWindow::CreateDevice()
{
    device = new DxDevice(Window());
}

void MainWindow::ReleaseDevice()
{
    delete device;
}

void MainWindow::CalculateLayout()
{
    if (pRenderTarget != NULL)
    {
        D2D1_SIZE_F size = pRenderTarget->GetSize();
        const float x = size.width / 2;
        const float y = size.height / 2;
        const float radius = min(x, y);
        ellipse = D2D1::Ellipse(D2D1::Point2F(x, y), radius, radius);
    }
}

void MainWindow::OnPaint()
{
    device->ResetCommandList();
}

void MainWindow::OnResize()
{
    // TODO
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
