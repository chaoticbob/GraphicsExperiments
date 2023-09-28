/*
See LICENSE folder for this sample’s licensing information.

Abstract:
Implementation of the cross-platform view controller
*/

#import "AAPLViewController.h"

#if TARGET_IOS || TARGET_TVOS
#import "ios/UIKit/AAPLUIView.h"
#else
#import "AAPLNSView.h"
#endif

#import <QuartzCore/CAMetalLayer.h>

CAMetalLayer *gMetalLayer = NULL;

@implementation AAPLViewController
{
}

- (void)viewDidLoad
{
    [super viewDidLoad];
}

- (void)drawableResize:(CGSize)size
{
}

- (void)renderToMetalLayer:(nonnull CAMetalLayer *)layer
{
   gMetalLayer = layer;
}

@end

void* getAAPLViewControllerMetalLayer()
{
   return (void*)gMetalLayer;
}
