#define MemoryBlock DumbBlock
#define Point CarbonDummyPointName
#define Component CarbonDummyCompName
#import <Cocoa/Cocoa.h>
#undef Point
#undef Component
#undef MemoryBlock
#include "juce/JuceLibraryCode/JuceHeader.h"

void makeWindowFloatingPanel(Component *aComponent)
{
    jassert(aComponent);
    
    ComponentPeer *componentPeer=aComponent->getPeer();
    jassert(componentPeer);
    componentPeer->setAlwaysOnTop(true);
    NSView* const peer = (NSView*) (componentPeer->getNativeHandle());
    jassert(peer);
    NSWindow *window=[peer window];
    jassert(window);
    [window setHidesOnDeactivate:YES];
}