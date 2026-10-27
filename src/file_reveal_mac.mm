#include "file_reveal.hpp"
#import <Cocoa/Cocoa.h>
#include <stdexcept>
namespace paint {
RevealOutcome reveal_in_file_manager(const std::string& path, const std::atomic<bool>* cancel) {
    const RevealTarget target = prepare_file_reveal(path);
    if (cancel && (*cancel).load()) {
        return RevealOutcome::Cancelled;
    }
    @autoreleasepool {
        NSString* native = [NSString stringWithUTF8String:target.path.c_str()];
        if (!native) {
            throw std::runtime_error("The selected path is not valid UTF-8.");
        }
        NSURL* url = [NSURL fileURLWithPath:native];
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ url ]];
    }
    return RevealOutcome::SelectionRequested;
}
} // namespace paint
