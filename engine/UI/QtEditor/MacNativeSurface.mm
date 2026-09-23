#include "MacNativeSurface.h"

#include <Cocoa/Cocoa.h>
#include <QuartzCore/CAMetalLayer.h>
#include <QWindow>

namespace Hephaestus::QtEditor {

void* GetMacMetalLayer(QWindow* window) {
    if (!window) {
        return nullptr;
    }

    NSView* view = reinterpret_cast<NSView*>(window->winId());
    if (!view) {
        return nullptr;
    }

    [view setWantsLayer:YES];
    if (![view layer] || ![[view layer] isKindOfClass:[CAMetalLayer class]]) {
        [view setLayer:[CAMetalLayer layer]];
    }
    return [view layer];
}

} // namespace Hephaestus::QtEditor
