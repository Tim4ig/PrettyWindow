
#include "PrettyWindow.hpp"
#include <iostream>	

constexpr auto WND_CLASS_NAME = L"PrettyWindowClassNameByTim4ig";

PrettyWindow::PrettyWindow()
{
	m_screen = { GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
}

PrettyWindow::~PrettyWindow()
{
}

bool PrettyWindow::OpenAsync()
{
	if (m_isRunning) return 0;
	if (m_wndFuture.valid()) m_wndFuture.wait();

	bool isFailed = false;

	auto OpenSync = [this, &isFailed]()
		{
			WNDCLASS wc = {};
			wc.lpfnWndProc = m_WndProc;
			wc.hInstance = GetModuleHandle(nullptr);
			wc.lpszClassName = WND_CLASS_NAME;
			wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
			wc.style = CS_HREDRAW | CS_VREDRAW;
			RegisterClassW(&wc);

			m_hwnd = CreateWindowExW(
				WS_EX_COMPOSITED | WS_EX_LAYERED,
				wc.lpszClassName,
				m_text.c_str(),
				WS_OVERLAPPEDWINDOW,
				CW_USEDEFAULT,
				CW_USEDEFAULT,
				CW_USEDEFAULT,
				CW_USEDEFAULT,
				nullptr,
				nullptr,
				wc.hInstance,
				nullptr
			);

			SetWindowLongPtr(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
			SetLayeredWindowAttributes(m_hwnd, 0xffffff, 0xff, LWA_COLORKEY | LWA_ALPHA);
			
			if (!this->m_ApplyBackdrop()) isFailed = true;
			if (!this->m_ApplyTheme()) isFailed = true;
			if (!this->m_InitDComp()) isFailed = true;
			if (isFailed) return;

			ShowWindow(m_hwnd, SW_SHOW);

			m_isRunning = true;
			MSG msg = {};
			while (GetMessage(&msg, nullptr, 0, 0) && m_isRunning)
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			m_isRunning = false;
		};

	m_wndFuture = std::async(std::launch::async, OpenSync);
	while (!m_isRunning) std::this_thread::sleep_for(std::chrono::milliseconds(1));
	return !isFailed;
}

void PrettyWindow::Close()
{
	m_isRunning = false;
}

ID2D1DeviceContext* PrettyWindow::BeginDraw()
{
	HRESULT hr = S_OK;
	POINT offset = { 0 };
	ComPtr<IDXGISurface> dxgiSurface;
	ComPtr<ID2D1Bitmap1> d2dTargetBitmap;

	if (m_isResized) if (!this->m_Resize()) return nullptr;
	hr = m_dcompSurface->BeginDraw(nullptr, IID_PPV_ARGS(&dxgiSurface), &offset);
	if (FAILED(hr)) throw std::runtime_error("m_dcompSurface->BeginDraw error");
	hr = m_d2dContext->CreateBitmapFromDxgiSurface(dxgiSurface.Get(), nullptr, &d2dTargetBitmap);
	if (FAILED(hr)) throw std::runtime_error("m_d2dContext->CreateBitmapFromDxgiSurface error");

	auto size = d2dTargetBitmap->GetSize();
	m_d2dContext->SetTarget(d2dTargetBitmap.Get());
	m_d2dContext->BeginDraw();

	return m_d2dContext.Get();
}

HRESULT PrettyWindow::EndDraw()
{
	HRESULT hr = S_OK;
	hr = m_d2dContext->EndDraw();
	if (FAILED(hr)) return hr;
	hr = m_dcompSurface->EndDraw();
	if (FAILED(hr)) return hr;
	hr = m_dcompDevice->Commit();
	if (FAILED(hr)) return hr;
	hr = DwmFlush();
	if (FAILED(hr)) return hr;

	return S_OK;
}

void PrettyWindow::SetTheme(WindowTheme theme)
{
	m_theme = theme;

	if (!m_isRunning) return;
	this->m_ApplyTheme();
}

void PrettyWindow::SetBackdrop(WindowBackdrop backdrop)
{
	m_backdrop = backdrop;

	if (!m_isRunning) return;
	this->m_ApplyBackdrop();
}

void PrettyWindow::SetText(const std::wstring& text)
{
	m_text = text;

	if (!m_isRunning) return;
	SetWindowTextW(m_hwnd, m_text.c_str());
}

void PrettyWindow::SetMinSize(POINT size)
{
	m_minSize = size;
}

bool PrettyWindow::IsRunning() const
{
	return m_isRunning;
}

bool PrettyWindow::m_InitDComp()
{
	HRESULT hr = S_OK;

	// init d3d
	ComPtr<IDXGIDevice> dxgiDevice;
	ComPtr<ID3D11Device> d3dDevice;
	hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &d3dDevice, nullptr, nullptr);
	if (FAILED(hr)) return false;
	hr = d3dDevice.As(&dxgiDevice);
	if (FAILED(hr)) return false;

	// init d2d
	ComPtr<ID2D1Device> d2dDevice;
	ComPtr<ID2D1Factory2> d2dFactory;
	hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory.GetAddressOf());
	if (FAILED(hr)) return false;
	hr = d2dFactory->CreateDevice(dxgiDevice.Get(), d2dDevice.GetAddressOf());
	if (FAILED(hr)) return false;
	hr = d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_d2dContext);
	if (FAILED(hr)) return false;

	// init dcomp
	hr = DCompositionCreateDevice3(dxgiDevice.Get(), IID_PPV_ARGS(&m_dcompDevice));
	if (FAILED(hr)) return false;
	hr = m_dcompDevice->CreateTargetForHwnd(m_hwnd, TRUE, &m_dcompTarget);
	if (FAILED(hr)) return false;
	hr = m_dcompDevice->CreateSurface(m_screen.x, m_screen.y, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &m_dcompSurface);
	if (FAILED(hr)) return false;
	this->m_Resize();

	return true;
}

bool PrettyWindow::m_ApplyBackdrop()
{
	HRESULT hr = S_OK;
	DWM_SYSTEMBACKDROP_TYPE backdropType = DWM_SYSTEMBACKDROP_TYPE::DWMSBT_TABBEDWINDOW;

	if (m_backdrop == WindowBackdrop::ACRYLIC) backdropType = DWM_SYSTEMBACKDROP_TYPE::DWMSBT_TRANSIENTWINDOW;
	else if (m_backdrop == WindowBackdrop::MICA) backdropType = DWM_SYSTEMBACKDROP_TYPE::DWMSBT_MAINWINDOW;
	
	hr = DwmSetWindowAttribute(m_hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType));
	return SUCCEEDED(hr);
}

bool PrettyWindow::m_ApplyTheme()
{
	HRESULT hr = S_OK;
	BOOL useDarkMode = (m_theme == WindowTheme::DARK || (m_theme == WindowTheme::AUTO && m_IsSystemDark()));
	hr = DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));
	return SUCCEEDED(hr);
}

bool PrettyWindow::m_IsSystemDark()
{
	HKEY hKey;
	DWORD appsUseLightTheme = 0;
	DWORD dataSize = sizeof(appsUseLightTheme);
	std::wstring subKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

	if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS)
	{
		if (RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr, reinterpret_cast<LPBYTE>(&appsUseLightTheme), &dataSize) == ERROR_SUCCESS)
		{
			RegCloseKey(hKey);
			return appsUseLightTheme == 0;
		}
		RegCloseKey(hKey);
	}
	return false;
}

bool PrettyWindow::m_Resize()
{
	HRESULT hr = S_OK;

	RECT rc = {};
	GetClientRect(m_hwnd, &rc);
	int newWidth = rc.right - rc.left;
	int newHeight = rc.bottom - rc.top;
	float scaleX = static_cast<float>(newWidth) / m_screen.x;
	float scaleY = static_cast<float>(newHeight) / m_screen.y;

	ComPtr<IDCompositionVisual2> dcompVisual;
	ComPtr<IDCompositionScaleTransform> scaleTransform;
	hr = m_dcompDevice->CreateScaleTransform(&scaleTransform);
	if (FAILED(hr)) return false;

	hr = scaleTransform->SetScaleX(scaleX);
	if (FAILED(hr)) return false;
	hr = scaleTransform->SetScaleY(scaleY);
	if (FAILED(hr)) return false;
	hr = m_dcompDevice->CreateVisual(&dcompVisual);
	if (FAILED(hr)) return false;
	hr = dcompVisual->SetContent(m_dcompSurface.Get());
	if (FAILED(hr)) return false;
	hr = dcompVisual->SetTransform(scaleTransform.Get());
	if (FAILED(hr)) return false;
	hr = m_dcompTarget->SetRoot(dcompVisual.Get());
	if (FAILED(hr)) return false;

	m_isResized = false;
	return true;
}

LRESULT __stdcall PrettyWindow::m_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	auto self = reinterpret_cast<PrettyWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

	// common messages
	switch (msg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	if (!self) return DefWindowProc(hwnd, msg, wParam, lParam);

	// self messages
	switch (msg)
	{
	case WM_SIZE:
		self->m_isResized = true;
		return 0;
	case WM_SETTINGCHANGE:
		self->m_ApplyTheme();
		return 0;
	case WM_GETMINMAXINFO:
	{
		MINMAXINFO* pMinMaxInfo = (MINMAXINFO*)lParam;
		pMinMaxInfo->ptMinTrackSize.x = self->m_minSize.x;
		pMinMaxInfo->ptMinTrackSize.y = self->m_minSize.y;
		return 0;
	}
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}
