#include "ui/AnalysisService.h"
#include "ui/StudioPaths.h"
#if JUCE_WINDOWS && defined(_MSC_VER)
#include <windows.data.pdf.h>
#include <thread>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <shcore.h>
#include <shlwapi.h>
#include <wincodec.h>
#elif JUCE_MAC
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

namespace ostfstudio {
#if JUCE_WINDOWS && defined(_MSC_VER)
namespace {
int renderWindowsPage(const juce::File& source, const juce::File& directory, int pageIndex) {
    namespace pdf = ABI::Windows::Data::Pdf;
    using ABI::Windows::Storage::Streams::IRandomAccessStream;
    using Microsoft::WRL::ComPtr;
    using Microsoft::WRL::Wrappers::HStringReference;
    Microsoft::WRL::Wrappers::RoInitializeWrapper apartment(RO_INIT_MULTITHREADED);
    if (FAILED(apartment)) return 1;
    // Release idle COM renderers while the desktop graphics runtime is still available.
    struct ReleaseRenderers { ~ReleaseRenderers() { CoFreeUnusedLibrariesEx(0, 0); } } releaseRenderers;
    auto fileStream = [](const juce::File& file, bool write, ComPtr<IRandomAccessStream>& stream) {
        ComPtr<IStream> bytes;
        const DWORD mode = write ? STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE : STGM_READ | STGM_SHARE_DENY_WRITE;
        return SUCCEEDED(SHCreateStreamOnFileEx(file.getFullPathName().toWideCharPointer(), mode,
                   FILE_ATTRIBUTE_NORMAL, write, nullptr, &bytes))
            && SUCCEEDED(CreateRandomAccessStreamOverStream(bytes.Get(), BSOS_DEFAULT, IID_PPV_ARGS(&stream)));
    };
    auto complete = [](const auto& op) {
        ComPtr<IAsyncInfo> info;
        if (FAILED(op.As(&info))) return false;
        AsyncStatus state{};
        auto* job = juce::ThreadPoolJob::getCurrentThreadPoolJob();
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 120000;
        while (SUCCEEDED(info->get_Status(&state)) && state == AsyncStatus::Started) {
            if ((job && job->shouldExit()) || juce::Time::getMillisecondCounterHiRes() > deadline) {
                info->Cancel();
                return false;
            }
            juce::Thread::sleep(10);
        }
        return state == AsyncStatus::Completed;
    };
    ComPtr<IRandomAccessStream> input;
    ComPtr<pdf::IPdfDocumentStatics> factory;
    ComPtr<ABI::Windows::Foundation::IAsyncOperation<pdf::PdfDocument*>> loading;
    ComPtr<pdf::IPdfDocument> doc;
    UINT32 count = 0;
    if (!fileStream(source, false, input) ||
        FAILED(RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Data_Pdf_PdfDocument).Get(), IID_PPV_ARGS(&factory))) ||
        FAILED(factory->LoadFromStreamAsync(input.Get(), &loading)) || !complete(loading) || FAILED(loading->GetResults(&doc)) ||
        FAILED(doc->get_PageCount(&count)) || !count) return 1;
    const auto png = directory.getChildFile("page.png");
    if (pageIndex < 0 || static_cast<UINT32>(pageIndex) >= count) return 1;
    {
        ComPtr<pdf::IPdfPage> page;
        ABI::Windows::Foundation::Size size{};
        ComPtr<IInspectable> object;
        ComPtr<pdf::IPdfPageRenderOptions> options;
        if (FAILED(doc->GetPage(static_cast<UINT32>(pageIndex), &page)) || FAILED(page->get_Size(&size)) || size.Width <= 0 || size.Height <= 0 ||
            FAILED(RoActivateInstance(HStringReference(RuntimeClass_Windows_Data_Pdf_PdfPageRenderOptions).Get(), &object)) ||
            FAILED(object.As(&options)) || FAILED(options->put_BitmapEncoderId(CLSID_WICPngEncoder)) ||
            FAILED(size.Width >= size.Height ? options->put_DestinationWidth(2000) : options->put_DestinationHeight(2000))) return 1;
        ComPtr<IRandomAccessStream> output;
        ComPtr<ABI::Windows::Foundation::IAsyncAction> render;
        if (!fileStream(png, true, output) ||
            FAILED(page->RenderWithOptionsToStreamAsync(output.Get(), options.Get(), &render)) ||
            !complete(render) || FAILED(render->GetResults())) return 1;
        ComPtr<ABI::Windows::Foundation::IClosable> closable;
        if (SUCCEEDED(page.As(&closable))) closable->Close();
    }
    if (pageIndex == 0 && !directory.getChildFile("page-count").replaceWithText(juce::String(count))) return 1;
    return 0;
}
}

void runWindowsScorePageRenderer(const juce::File& source, const juce::File& directory, int pageIndex) {
    // JUCE's message thread can already be in an STA. WinRT PDF uses an MTA here.
    int exitCode = 1;
    std::thread worker([&] { exitCode = renderWindowsPage(source, directory, pageIndex); });
    worker.join();
    // The page, streams and COM apartment have been closed. Windows.Data.Pdf
    // can create a D3D device from its DLL detach handler during ExitProcess.
    // Stop just this disposable worker without running process-detach handlers.
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(exitCode));
}
#endif

juce::Result readScorePages(const juce::File& source, const juce::File& directory,
                            const std::function<juce::Result(const juce::File&)>& readPage) {
    auto failed = [] { return juce::Result::fail(TRANS("Could not read the selected PDF.")); };
#if JUCE_WINDOWS && defined(_MSC_VER)
    const auto executable = studioToolsExecutable();
    if (!executable.existsAsFile()) return juce::Result::fail(TRANS("Install OSTF Studio to import PDF pages."));
    const auto png = directory.getChildFile("page.png");
    int pageCount = 1;
    for (int i = 0; i < pageCount; ++i) {
        if (auto r = runAnalysisTool({executable.getFullPathName(), "--render-score-page", source.getFullPathName(),
                                      directory.getFullPathName(), juce::String(i)}, 180000); r.failed()) return failed();
        if (i == 0) {
            pageCount = directory.getChildFile("page-count").loadFileAsString().getIntValue();
            if (pageCount < 1) return failed();
        }
        if (!png.existsAsFile()) return failed();
        if (auto r = readPage(png); r.failed()) return r;
    }
    return juce::Result::ok();
#elif JUCE_MAC
    auto fileUrl = [](const juce::File& f) {
        const auto path = f.getFullPathName();
        return CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path.toRawUTF8()), path.getNumBytesAsUTF8(), false);
    };
    const auto url = fileUrl(source);
    if (!url) return failed();
    const auto doc = CGPDFDocumentCreateWithURL(url);
    CFRelease(url);
    if (!doc) return failed();
    struct Document { CGPDFDocumentRef doc; ~Document() { CGPDFDocumentRelease(doc); } } holder{doc};
    const auto count = CGPDFDocumentGetNumberOfPages(doc);
    if (!count) return failed();
    for (size_t i = 1; i <= count; ++i) {
        const auto page = CGPDFDocumentGetPage(doc, i);
        if (!page) return failed();
        const auto box = CGPDFPageGetBoxRect(page, kCGPDFCropBox);
        if (CGRectIsEmpty(box)) return failed();
        const auto scale = 2000. / std::max(box.size.width, box.size.height);
        auto w = std::max(1, juce::roundToInt(box.size.width * scale));
        auto h = std::max(1, juce::roundToInt(box.size.height * scale));
        if ((CGPDFPageGetRotationAngle(page) / 90) % 2) std::swap(w,h);
        const auto colours = CGColorSpaceCreateDeviceRGB();
        auto canvas = CGBitmapContextCreate(nullptr,w,h,8,0,colours,kCGImageAlphaNoneSkipLast);
        CGColorSpaceRelease(colours);
        if (!canvas) return failed();
        const auto rect = CGRectMake(0,0,w,h);
        CGContextSetRGBFillColor(canvas,1,1,1,1);
        CGContextFillRect(canvas,rect);
        CGContextConcatCTM(canvas, CGPDFPageGetDrawingTransform(page,kCGPDFCropBox,rect,0,true));
        CGContextDrawPDFPage(canvas,page);
        auto image = CGBitmapContextCreateImage(canvas);
        CGContextRelease(canvas);
        if (!image) return failed();
        const auto png = directory.getChildFile("page.png");
        auto destUrl = fileUrl(png);
        auto dest = destUrl ? CGImageDestinationCreateWithURL(destUrl, CFSTR("public.png"), 1, nullptr) : nullptr;
        if (destUrl) CFRelease(destUrl);
        bool ok = false;
        if (dest) { CGImageDestinationAddImage(dest,image,nullptr); ok = CGImageDestinationFinalize(dest); CFRelease(dest); }
        CGImageRelease(image);
        if (!ok) return failed();
        if (auto r = readPage(png); r.failed()) return r;
    }
    return juce::Result::ok();
#else
    auto render = analysisTool("pdftoppm");
    if (!render.existsAsFile()) return juce::Result::fail(TRANS("AI score reading requires Poppler pdftoppm on PATH."));
    if (auto r = runAnalysisTool({render.getFullPathName(), "-png", "-scale-to", "2000", source.getFullPathName(),
                                  directory.getChildFile("page").getFullPathName()}, 180000); r.failed()) return r;
    auto pages = directory.findChildFiles(juce::File::findFiles, false, "page-*.png");
    std::sort(pages.begin(), pages.end(), [](const auto& a, const auto& b) {
        return a.getFileNameWithoutExtension().getTrailingIntValue() < b.getFileNameWithoutExtension().getTrailingIntValue();
    });
    if (pages.isEmpty()) return failed();
    for (const auto& page : pages) if (auto r = readPage(page); r.failed()) return r;
    return juce::Result::ok();
#endif
}
}
