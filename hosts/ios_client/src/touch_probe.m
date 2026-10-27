// Trial only: every window of the client given an opaque black background,
// so its layer tree has content where its Metal layer shows nothing, for
// whether the simulator then routes a touch to the client.
#import <UIKit/UIKit.h>

static void Paint(int left)
{
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
    {
        if (![scene isKindOfClass:[UIWindowScene class]])
        {
            continue;
        }
        for (UIWindow* window in ((UIWindowScene*)scene).windows)
        {
            if (window.backgroundColor == nil)
            {
                window.backgroundColor = UIColor.blackColor;
                fprintf(stderr, "touch_probe: window %p painted black\n", (void*)window);
            }
        }
    }
    if (left > 0)
    {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)NSEC_PER_SEC), dispatch_get_main_queue(), ^{
          Paint(left - 1);
        });
    }
}

__attribute__((constructor)) static void Probe(void)
{
    dispatch_async(dispatch_get_main_queue(), ^{
      Paint(120);
    });
}
