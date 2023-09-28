/*
See LICENSE folder for this sample’s licensing information.

Abstract:
Header for the cross-platform view controller
*/

#if defined(TARGET_IOS) || defined(TARGET_TVOS)
//@import UIKit;
#include <UIKit/UIKit.h>
#define PlatformViewController UIViewController
#else
@import AppKit;
#define PlatformViewController NSViewController
#endif

#import "AAPLView.h"

@interface AAPLViewController : PlatformViewController <AAPLViewDelegate>

@end

void* getAAPLViewControllerMetalLayer();
