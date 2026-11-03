#include "MacNativeSurface.h"

#include <Cocoa/Cocoa.h>
#include <QuartzCore/CAMetalLayer.h>
#include <QWindow>

namespace Hephaestus::QtEditor {

namespace {
CAMetalLayer* FindMetalLayer(CALayer* layer) {
    if ([layer isKindOfClass:[CAMetalLayer class]]) {
        return static_cast<CAMetalLayer*>(layer);
    }
    // Qt 6.10+ wraps the view's content layer in a QContainerLayer.
    for (CALayer* sublayer in [layer sublayers]) {
        if ([sublayer isKindOfClass:[CAMetalLayer class]]) {
            return static_cast<CAMetalLayer*>(sublayer);
        }
    }
    return nil;
}
} // namespace

void* GetMacMetalLayer(QWindow* window) {
    if (!window) {
        return nullptr;
    }

    NSView* view = reinterpret_cast<NSView*>(window->winId());
    if (!view) {
        return nullptr;
    }

    [view setWantsLayer:YES];
    if (!FindMetalLayer([view layer])) {
        [view setLayer:[CAMetalLayer layer]];
    }
    return FindMetalLayer([view layer]);
}

} // namespace Hephaestus::QtEditor
