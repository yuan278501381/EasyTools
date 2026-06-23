#pragma once
#include <windows.h>
#include <d2d1.h>
#include <wrl/client.h>
#include <opencv2/opencv.hpp>
#include <memory>
#include <string>

class PinnedWindow {
public:
    PinnedWindow(const cv::Mat& image, int x, int y);
    ~PinnedWindow();

    void show();
    void destroy();

    static void registerClass(HINSTANCE hInstance);
    static void closeAllWindows();

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    bool initD2D();
    void render();
    void updateScale(float deltaScale, POINT cursorPt);
    void updateAlpha(int deltaAlpha);

    HWND m_hwnd = nullptr;
    cv::Mat m_image;
    
    // D2D resources
    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_d2dBitmap;

    float m_scale = 1.0f;
    int m_alpha = 255;

    // Drag state
    bool m_isDragging = false;
    POINT m_lastMousePos{};
    
    // Original dimensions
    int m_origWidth = 0;
    int m_origHeight = 0;
};
