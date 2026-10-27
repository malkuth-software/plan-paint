#include "codecs.hpp"
#include "desktop.hpp"
#include "platform.hpp"
#import <Cocoa/Cocoa.h>
#include <stdexcept>
namespace paint {

void set_wallpaper(const std::string& path) {
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    NSDictionary* options = @{NSWorkspaceDesktopImageScalingKey : @(NSImageScaleAxesIndependently)};
    NSError* error = nil;
    if (![[NSWorkspace sharedWorkspace] setDesktopImageURL:url
                                                 forScreen:[NSScreen mainScreen]
                                                   options:options
                                                     error:&error]) {
        throw std::runtime_error(error ? [[error localizedDescription] UTF8String]
                                       : "The desktop background could not be changed.");
    }
}
bool acquire_picture(std::string&) {
    NSURL* app =
        [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:@"com.apple.Image_Capture"];
    if (!app || ![[NSWorkspace sharedWorkspace] openURL:app]) {
        throw std::runtime_error("Image Capture is unavailable. Open an image file or use Paste from.");
    }
    return false;
}
void copy_to_clipboard(const Image& image) {
    std::vector<std::uint8_t> bytes = encode_png(image);
    NSData* data = [NSData dataWithBytes:bytes.data() length:bytes.size()];
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard clearContents];
    if (![pasteboard setData:data forType:NSPasteboardTypePNG]) {
        throw std::runtime_error("The clipboard could not accept this image.");
    }
}
bool paste_from_clipboard(Image& image) {
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    NSData* data = [pasteboard dataForType:NSPasteboardTypePNG];
    if (!data) {
        NSImage* native_image = [[NSImage alloc] initWithPasteboard:pasteboard];
        if (!native_image) {
            return false;
        }
        NSBitmapImageRep* bitmap = [NSBitmapImageRep imageRepWithData:[native_image TIFFRepresentation]];
        data = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        [native_image release];
    }
    if (!data) {
        return false;
    }
    image = decode_image([data bytes], [data length]);
    return true;
}
bool print_image(const Image& image) {
    std::vector<std::uint8_t> bytes = encode_png(image);
    NSData* data = [NSData dataWithBytes:bytes.data() length:bytes.size()];
    NSImage* native_image = [[NSImage alloc] initWithData:data];
    NSImageView* view = [[NSImageView alloc] initWithFrame:NSMakeRect(0, 0, image.width, image.height)];
    [view setImage:native_image];
    NSPrintInfo* info = [[NSPrintInfo sharedPrintInfo] copy];
    [info setHorizontalPagination:NSPrintingPaginationModeFit];
    [info setVerticalPagination:NSPrintingPaginationModeFit];
    NSPrintOperation* operation = [NSPrintOperation printOperationWithView:view printInfo:info];
    bool completed = [operation runOperation];
    [info release];
    [view release];
    [native_image release];
    return completed;
}
void page_setup() {
    [[NSPageLayout pageLayout] runModalWithPrintInfo:[NSPrintInfo sharedPrintInfo]];
}
std::string default_font_path(bool mono, bool bold, bool italic) {
    std::string name = mono ? "Courier New" : "Arial";
    if (bold && italic) {
        name += " Bold Italic";
    } else if (bold) {
        name += " Bold";
    } else if (italic) {
        name += " Italic";
    }
    return "/System/Library/Fonts/Supplemental/" + name + ".ttf";
}
} // namespace paint
