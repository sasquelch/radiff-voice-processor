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

std::filesystem::path bundledResourceDirectory() {
    @autoreleasepool {
        NSBundle* bundle = [NSBundle mainBundle];
        if (bundle != nil) {
            NSString* res = bundle.resourcePath;
            NSString* exe = bundle.executablePath;
            // A bare command-line executable has a "bundle" whose resourcePath is just its
            // directory; that is also the right answer for it.
            if (res != nil && res.length > 0)
                return pathFromUtf8(std::string_view(res.UTF8String));
            if (exe != nil)
                return pathFromUtf8(std::string_view(exe.UTF8String)).parent_path();
        }
    }
    return {};
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
