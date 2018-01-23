//
// DirectXPage.xaml.cpp
// Implementation of the DirectXPage class.
//

#include "pch.h"
#include "DirectXPage.xaml.h"

extern "C"
{
#include <libavutil/time.h>
}

using namespace TestWin2DXAML;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Graphics::Display;
using namespace Windows::System::Threading;
using namespace Windows::UI::Core;
using namespace Windows::UI::Input;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;
using namespace concurrency;

using namespace Microsoft::Graphics::Canvas;
using namespace Windows::Media::Core;
using namespace Windows::Media::Playback;
using namespace FFmpegInterop;
using namespace Windows::Graphics::Imaging;
using namespace Microsoft::Graphics::Canvas::UI::Xaml;


DirectXPage::DirectXPage():
	m_windowVisible(true),
	m_coreInput(nullptr)
{
	InitializeComponent();

	auto avWorkItem = ref new WorkItemHandler([this](IAsyncAction^) {

		// Set FFmpeg specific options. List of options can be found in https://www.ffmpeg.org/ffmpeg-protocols.html
		PropertySet^ options = ref new PropertySet();

		// Below are some sample options that you can set to configure RTSP streaming
		//options->Insert("rtsp_flags", "prefer_tcp");
		options->Insert("rtmp_buffer", 100);
		options->Insert("rtmp_live", "live");

		FFmpegMSS = FFmpegInteropMSS::CreateFFmpegInteropMSSFromUri("rtmp://localhost:1935/live/test", false, false, options);

		mediaPlayer = ref new MediaPlayer();

		mediaPlayer->RealTimePlayback = true;
		mediaPlayer->IsVideoFrameServerEnabled = true;
		mediaPlayer->VideoFrameAvailable += ref new TypedEventHandler<MediaPlayer^, Platform::Object^>(this, &DirectXPage::mediaPlayer_VideoFrameAvailable);
		mediaPlayer->PlaybackSession->BufferingStarted += ref new TypedEventHandler<MediaPlaybackSession ^, Platform::Object^>(this, &DirectXPage::BufferingEnded);
		mediaPlayer->PlaybackSession->BufferingEnded += ref new TypedEventHandler<MediaPlaybackSession ^, Platform::Object^>(this, &DirectXPage::BufferingEnded);

		GotFirst = false;

		if (FFmpegMSS != nullptr)
		{
			MediaStreamSource^ mss = FFmpegMSS->GetMediaStreamSource();

			if (mss)
			{
				// Pass MediaStreamSource to Media Element
				//mediaElement->SetMediaStreamSource(mss);
				MediaSource^ source = MediaSource::CreateFromMediaStreamSource(mss);
				mediaPlayer->Source = source;
				mediaPlayer->Play();
			}
			else
			{
				OutputDebugString(L"Cannot open media");
			}
		}
		else
		{
			OutputDebugString(L"Cannot open media");
		}	
	
	});

	ThreadPool::RunAsync(avWorkItem, WorkItemPriority::High, WorkItemOptions::TimeSliced);
	

	// Register event handlers for page lifecycle.
	CoreWindow^ window = Window::Current->CoreWindow;
	uiDispatcher = window->Dispatcher;

	window->VisibilityChanged +=
		ref new TypedEventHandler<CoreWindow^, VisibilityChangedEventArgs^>(this, &DirectXPage::OnVisibilityChanged);

	DisplayInformation^ currentDisplayInformation = DisplayInformation::GetForCurrentView();

	currentDisplayInformation->DpiChanged +=
		ref new TypedEventHandler<DisplayInformation^, Object^>(this, &DirectXPage::OnDpiChanged);

	currentDisplayInformation->OrientationChanged +=
		ref new TypedEventHandler<DisplayInformation^, Object^>(this, &DirectXPage::OnOrientationChanged);

	DisplayInformation::DisplayContentsInvalidated +=
		ref new TypedEventHandler<DisplayInformation^, Object^>(this, &DirectXPage::OnDisplayContentsInvalidated);

	swapChainPanel->CompositionScaleChanged += 
		ref new TypedEventHandler<SwapChainPanel^, Object^>(this, &DirectXPage::OnCompositionScaleChanged);

	swapChainPanel->SizeChanged +=
		ref new SizeChangedEventHandler(this, &DirectXPage::OnSwapChainPanelSizeChanged);

	// At this point we have access to the device. 
	// We can create the device-dependent resources.
	m_deviceResources = std::make_shared<DX::DeviceResources>();
	m_deviceResources->SetSwapChainPanel(swapChainPanel);

	// Register our SwapChainPanel to get independent input pointer events
	auto workItemHandler = ref new WorkItemHandler([this] (IAsyncAction ^)
	{
		// The CoreIndependentInputSource will raise pointer events for the specified device types on whichever thread it's created on.
		m_coreInput = swapChainPanel->CreateCoreIndependentInputSource(
			Windows::UI::Core::CoreInputDeviceTypes::Mouse |
			Windows::UI::Core::CoreInputDeviceTypes::Touch |
			Windows::UI::Core::CoreInputDeviceTypes::Pen
			);

		// Register for pointer events, which will be raised on the background thread.
		m_coreInput->PointerPressed += ref new TypedEventHandler<Object^, PointerEventArgs^>(this, &DirectXPage::OnPointerPressed);
		m_coreInput->PointerMoved += ref new TypedEventHandler<Object^, PointerEventArgs^>(this, &DirectXPage::OnPointerMoved);
		m_coreInput->PointerReleased += ref new TypedEventHandler<Object^, PointerEventArgs^>(this, &DirectXPage::OnPointerReleased);

		// Begin processing input messages as they're delivered.
		m_coreInput->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessUntilQuit);
	});

	// Run task on a dedicated high priority background thread.
	m_inputLoopWorker = ThreadPool::RunAsync(workItemHandler, WorkItemPriority::High, WorkItemOptions::TimeSliced);

	m_main = std::unique_ptr<TestWin2DXAMLMain>(new TestWin2DXAMLMain(m_deviceResources));
	m_main->StartRenderLoop();
}

DirectXPage::~DirectXPage()
{
	// Stop rendering and processing events on destruction.
	m_main->StopRenderLoop();
	m_coreInput->Dispatcher->StopProcessEvents();
}

// Saves the current state of the app for suspend and terminate events.
void DirectXPage::SaveInternalState(IPropertySet^ state)
{
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->Trim();

	// Stop rendering when the app is suspended.
	m_main->StopRenderLoop();

	// Put code to save app state here.
}

// Loads the current state of the app for resume events.
void DirectXPage::LoadInternalState(IPropertySet^ state)
{
	// Put code to load app state here.

	// Start rendering when the app is resumed.
	m_main->StartRenderLoop();
}

// Window event handlers.

void DirectXPage::OnVisibilityChanged(CoreWindow^ sender, VisibilityChangedEventArgs^ args)
{
	m_windowVisible = args->Visible;
	if (m_windowVisible)
	{
		m_main->StartRenderLoop();
	}
	else
	{
		m_main->StopRenderLoop();
	}
}

// DisplayInformation event handlers.

void DirectXPage::OnDpiChanged(DisplayInformation^ sender, Object^ args)
{
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	// Note: The value for LogicalDpi retrieved here may not match the effective DPI of the app
	// if it is being scaled for high resolution devices. Once the DPI is set on DeviceResources,
	// you should always retrieve it using the GetDpi method.
	// See DeviceResources.cpp for more details.
	m_deviceResources->SetDpi(sender->LogicalDpi);
	m_main->CreateWindowSizeDependentResources();
}

void DirectXPage::OnOrientationChanged(DisplayInformation^ sender, Object^ args)
{
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->SetCurrentOrientation(sender->CurrentOrientation);
	m_main->CreateWindowSizeDependentResources();
}

void DirectXPage::OnDisplayContentsInvalidated(DisplayInformation^ sender, Object^ args)
{
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->ValidateDevice();
}

// Called when the app bar button is clicked.
void DirectXPage::AppBarButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	// Use the app bar if it is appropriate for your app. Design the app bar, 
	// then fill in event handlers (like this one).
}

void DirectXPage::OnPointerPressed(Object^ sender, PointerEventArgs^ e)
{
	// When the pointer is pressed begin tracking the pointer movement.
	m_main->StartTracking();
}

void DirectXPage::OnPointerMoved(Object^ sender, PointerEventArgs^ e)
{
	// Update the pointer tracking code.
	if (m_main->IsTracking())
	{
		m_main->TrackingUpdate(e->CurrentPoint->Position.X);
	}
}

void DirectXPage::OnPointerReleased(Object^ sender, PointerEventArgs^ e)
{
	// Stop tracking pointer movement when the pointer is released.
	m_main->StopTracking();
}

void DirectXPage::OnCompositionScaleChanged(SwapChainPanel^ sender, Object^ args)
{
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->SetCompositionScale(sender->CompositionScaleX, sender->CompositionScaleY);
	m_main->CreateWindowSizeDependentResources();
}

void DirectXPage::OnSwapChainPanelSizeChanged(Object^ sender, SizeChangedEventArgs^ e)
{
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->SetLogicalSize(e->NewSize);
	m_main->CreateWindowSizeDependentResources();
}

void DirectXPage::mediaPlayer_VideoFrameAvailable(MediaPlayer^ sender, Object^ args)
{
	if (!GotFirst) {

		double mpSeconds = av_gettime() / 1000000.0;
		wchar_t buffer[250];
		swprintf_s(buffer, L"first media player frame available at: %f", mpSeconds);
		OutputDebugStringW(buffer);

		GotFirst = true;
	}

	
	CanvasDevice^ canvasDevice = CanvasDevice::GetSharedDevice();	
	
	uiDispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([this, canvasDevice]() {

		if (m_windowVisible) {

			if (frameServerDest == nullptr) {
				//frameServerDest = ref new SoftwareBitmap(BitmapPixelFormat::Rgba8, (int)mediaImg->Width, (int)mediaImg->Height, BitmapAlphaMode::Ignore);
				frameServerDest = ref new SoftwareBitmap(BitmapPixelFormat::Rgba8, 600, 300, BitmapAlphaMode::Ignore);
			}

			if (canvasImageSource == nullptr) {
				//canvasImageSource = ref new CanvasImageSource(canvasDevice, (int)mediaImg->Width, (int)mediaImg->Height, DisplayInformation::GetForCurrentView()->LogicalDpi);//96);
				canvasImageSource = ref new CanvasImageSource(canvasDevice, 600, 300, DisplayInformation::GetForCurrentView()->LogicalDpi);//96);
			}

			mediaImg->Source = canvasImageSource;

			CanvasBitmap^ inputBitmap = CanvasBitmap::CreateFromSoftwareBitmap(canvasDevice, frameServerDest);

			mediaPlayer->CopyFrameToVideoSurface(inputBitmap);

			CanvasDrawingSession^ ds = canvasImageSource->CreateDrawingSession(Windows::UI::Colors::Black);

			ds->DrawImage(inputBitmap);
		}

		
	
	}));
	
}

void DirectXPage::BufferingStarted(MediaPlaybackSession^ sender, Object^ args)
{
	double mpSeconds = av_gettime() / 1000000.0;
	wchar_t buffer[250];
	swprintf_s(buffer, L"buffering started at: %f", mpSeconds);
	OutputDebugStringW(buffer);

}

void DirectXPage::BufferingEnded(MediaPlaybackSession^ sender, Object^ args)
{
	double mpSeconds = av_gettime() / 1000000.0;
	wchar_t buffer[250];
	swprintf_s(buffer, L"buffering ended at: %f", mpSeconds);
	OutputDebugStringW(buffer);
}