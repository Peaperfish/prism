#include "macos_metal_layer.hpp"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

namespace prism::gui_app {

void reset_metal_layer(void* metal_layer) {
    @autoreleasepool {
        CAMetalLayer* layer = (__bridge CAMetalLayer*)metal_layer;
        if (layer == nil) {
            return;
        }
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.colorspace = nil;
        layer.wantsExtendedDynamicRangeContent = NO;
    }
}

}  // namespace prism::gui_app
