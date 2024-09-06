
#include "PrettyWindow.hpp"

#include <iostream>

int main() try
{
	PrettyWindow wnd;
	if (!wnd.OpenAsync()) return 1;

	D2D1_GRADIENT_STOP gradientStops[3];
	gradientStops[0].color = D2D1::ColorF(1.0f, 0.0f, 0.5f);
	gradientStops[0].position = .2f; 

	gradientStops[1].color = D2D1::ColorF(0.0f, 1.0f, 0.0f);
	gradientStops[1].position = .5f;

	gradientStops[2].color = D2D1::ColorF(0.0f, 0.0f, 1.0f);
	gradientStops[2].position = .8f; 

	auto context = wnd.BeginDraw();

	ComPtr<ID2D1GradientStopCollection> gradientStopCollection;
	context->CreateGradientStopCollection(
		gradientStops,
		ARRAYSIZE(gradientStops),
		&gradientStopCollection
	);
	wnd.EndDraw();

	ComPtr<ID2D1LinearGradientBrush> brush;
	context->CreateLinearGradientBrush(
		D2D1::LinearGradientBrushProperties(
			D2D1::Point2F(0, 0),            
			D2D1::Point2F(2560, 1440)       
		),
		gradientStopCollection.Get(),
		&brush
	);

	wnd.EndDraw();

	while (wnd.IsRunning())
	{
		if (GetAsyncKeyState('1')) wnd.SetBackdrop(WindowBackdrop::MICA);
		if (GetAsyncKeyState('2')) wnd.SetBackdrop(WindowBackdrop::ACRYLIC);
		if (GetAsyncKeyState('3')) wnd.SetBackdrop(WindowBackdrop::MICAALT);

		auto d2d = wnd.BeginDraw();
		d2d->Clear(D2D1::ColorF(D2D1::ColorF::White, 0));
		auto s = d2d->GetPixelSize();
		d2d->DrawLine(D2D1::Point2F(0, 0), D2D1::Point2F(s.width, s.height), brush.Get(), 5);
		d2d->DrawLine(D2D1::Point2F(s.width, 0), D2D1::Point2F(0, s.height), brush.Get(), 5);
		wnd.EndDraw();
		std::this_thread::sleep_for(std::chrono::milliseconds(16));
	}
	return 0;
}
catch (const std::exception& e)
{
	std::cerr << e.what() << '\n';
	return 1;
}
