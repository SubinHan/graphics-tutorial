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
        return 0;

    case WM_DESTROY:
        DestroyMessageHandlers();
        PostQuitMessage(0);
        return 0;

    case WM_PAINT:
    {
        OnPaint();
        return 0;
    }

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

HRESULT MainWindow::CreateGraphicsResources()
{
    HRESULT hr = S_OK;
    if (pRenderTarget == NULL)
    {
        RECT rc;
        GetClientRect(m_hwnd, &rc);

        D2D1_SIZE_U size = D2D1::SizeU(rc.right, rc.bottom);

        hr = pFactory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(m_hwnd, size),
            &pRenderTarget);

        if (SUCCEEDED(hr))
        {
            const D2D1_COLOR_F color = D2D1::ColorF(1.0f, 1.0f, 0);
            hr = pRenderTarget->CreateSolidColorBrush(color, &pBrush);

            if (SUCCEEDED(hr))
            {
                CalculateLayout();
            }
        }
    }
    return hr;
}

void MainWindow::DiscardGraphicsResources()
{
    SafeRelease(&pRenderTarget);
    SafeRelease(&pBrush);
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
    HRESULT hr = CreateGraphicsResources();
    if (SUCCEEDED(hr))
    {
        PAINTSTRUCT ps;
        BeginPaint(m_hwnd, &ps);

        pRenderTarget->BeginDraw();

        pRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        pRenderTarget->SetTransform(
            D2D1::Matrix3x2F::Skew(20, 40, ellipse.point)
        );
        pRenderTarget->FillEllipse(ellipse, pBrush);
        pRenderTarget->SetTransform(D2D1::Matrix3x2F::Identity());

        hr = pRenderTarget->EndDraw();
        if (FAILED(hr) || hr == D2DERR_RECREATE_TARGET)
        {
            DiscardGraphicsResources();
        }

        EndPaint(m_hwnd, &ps);
    }
}
