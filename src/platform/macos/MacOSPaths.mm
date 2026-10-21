#include "radiff/platform/Paths.h"

#import <Foundation/Foundation.h>

namespace radiff::platform {

std::filesystem::path userDataDirectory() {
    @autoreleasepool {
        NSArray<NSURL*>* urls = [[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory
                                                                        inDomains:NSUserDomainMask];
        if (urls.count > 0) {
            NSString* path = urls[0].path;
            return pathFromUtf8(std::string_view(path.UTF8String)) / "RaDiff Voice Processor";
        }
    }
    return std::filesystem::temp_directory_path() / "RaDiff Voice Processor";
}

std::filesystem::path temporaryDirectory() {
    @autoreleasepool {
        NSString* tmp = NSTemporaryDirectory();
        if (tmp != nil)
            return pathFromUtf8(std::string_view(tmp.UTF8String));
    }
    return std::filesystem::temp_directory_path();
}

} // namespace radiff::platform
