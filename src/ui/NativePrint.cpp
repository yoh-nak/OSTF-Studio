#include "ui/NativePrint.h"
#if JUCE_WINDOWS
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
namespace ostfstudio {
juce::Result printSheet(juce::Component& owner,const NotationScore& score,int part,StaffStyle style,
                        const MeasureNotes& notes,bool lead,const SheetOptions& options) {
    PRINTDLGW dialog{};
    dialog.lStructSize=sizeof(dialog);
    dialog.hwndOwner=static_cast<HWND>(owner.getWindowHandle());
    dialog.Flags=PD_RETURNDEFAULT;
    if (PrintDlgW(&dialog) && dialog.hDevMode) {
        auto* mode=static_cast<DEVMODEW*>(GlobalLock(dialog.hDevMode));
        if (mode) {
            mode->dmFields |= DM_ORIENTATION|DM_PAPERSIZE;
            mode->dmPaperSize=options.usLetter ? DMPAPER_LETTER : DMPAPER_A4;
            mode->dmOrientation=options.landscape ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
            GlobalUnlock(dialog.hDevMode);
        }
    }
    dialog.Flags=PD_RETURNDC|PD_NOSELECTION|PD_USEDEVMODECOPIESANDCOLLATE|PD_HIDEPRINTTOFILE;
    const int pages=sheetPages(score,lead,style,options);
    dialog.nMinPage=dialog.nFromPage=1;
    dialog.nMaxPage=dialog.nToPage=WORD(std::min(65535,pages));
    auto result=juce::Result::ok();
    if (PrintDlgW(&dialog)) {
        DOCINFOW doc{}; doc.cbSize=sizeof(doc); doc.lpszDocName=score.title.toWideCharPointer();
        const auto dc=dialog.hDC;
        const bool started=StartDocW(dc,&doc)>0;
        bool ok=started;
        const auto size=options.paperPoints();
        const auto extent=juce::Rectangle<float>(float(GetDeviceCaps(dc,PHYSICALWIDTH)),float(GetDeviceCaps(dc,PHYSICALHEIGHT)));
        const auto pageRect=juce::RectanglePlacement(juce::RectanglePlacement::centred).appliedTo(
            juce::Rectangle<float>(float(size.x),float(size.y)),extent).toNearestInt()
            .translated(-GetDeviceCaps(dc,PHYSICALOFFSETX),-GetDeviceCaps(dc,PHYSICALOFFSETY));
        const bool ranged=(dialog.Flags & PD_PAGENUMS)!=0;
        for (int i=ranged ? dialog.nFromPage-1 : 0; ok && i<(ranged ? dialog.nToPage : pages); ++i) {
            juce::Image image(juce::Image::ARGB,size.x*3,size.y*3,true,juce::SoftwareImageType{});
            { juce::Graphics g(image); g.addTransform(juce::AffineTransform::scale(3.f)); drawSheetPage(g,score,part,style,notes,lead,i,options); }
            image.desaturate();
            const juce::Image::BitmapData pixels(image,juce::Image::BitmapData::readOnly);
            BITMAPINFO bmp{}; bmp.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            bmp.bmiHeader.biWidth=image.getWidth(); bmp.bmiHeader.biHeight=-image.getHeight();
            bmp.bmiHeader.biPlanes=1; bmp.bmiHeader.biBitCount=32; bmp.bmiHeader.biCompression=BI_RGB;
            ok=StartPage(dc)>0;
            if (ok) {
                SetStretchBltMode(dc,HALFTONE);
                const auto lines=StretchDIBits(dc,pageRect.getX(),pageRect.getY(),pageRect.getWidth(),pageRect.getHeight(),
                    0,0,image.getWidth(),image.getHeight(),pixels.data,&bmp,DIB_RGB_COLORS,SRCCOPY);
                ok=lines!=GDI_ERROR && lines!=0 && EndPage(dc)>0;
            }
        }
        if (ok) ok=EndDoc(dc)>0; else if (started) AbortDoc(dc);
        if (!ok) result=juce::Result::fail(TRANS("Printing failed."));
    } else if (CommDlgExtendedError()) result=juce::Result::fail(TRANS("Could not open printer settings."));
    if (dialog.hDC) DeleteDC(dialog.hDC);
    if (dialog.hDevMode) GlobalFree(dialog.hDevMode);
    if (dialog.hDevNames) GlobalFree(dialog.hDevNames);
    return result;
}
}
#endif
