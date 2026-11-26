#import <Cocoa/Cocoa.h>
#import <PDFKit/PDFKit.h>
#include "ui/NativePrint.h"

namespace ostfstudio {
juce::Result printSheet(juce::Component&,const NotationScore& score,int part,StaffStyle style,
                        const MeasureNotes& notes,bool lead,const SheetOptions& options) {
    @autoreleasepool {
        juce::MemoryOutputStream out;
        auto result=writeSheetPdf(out,score,part,style,notes,lead,options);
        if (result.failed()) return result;
        NSData* data=[NSData dataWithBytes:out.getData() length:out.getDataSize()];
        PDFDocument* pdf=[[PDFDocument alloc] initWithData:data];
        NSPrintInfo* info=[[NSPrintInfo sharedPrintInfo] copy];
        info.paperSize=options.usLetter ? NSMakeSize(612,792) : NSMakeSize(595,842);
        info.orientation=options.landscape ? NSPaperOrientationLandscape : NSPaperOrientationPortrait;
        NSPrintOperation* operation=[pdf printOperationForPrintInfo:info scalingMode:kPDFPrintPageScaleDownToFit autoRotate:YES];
        if (!operation) return juce::Result::fail(TRANS("Could not open printer settings."));
        operation.showsPrintPanel=YES;
        operation.showsProgressPanel=YES;
        [operation runOperation];
        return juce::Result::ok();
    }
}
}
