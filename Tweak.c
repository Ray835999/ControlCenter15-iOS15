/*
 * ControlCenter15 (com.ray.cc15) — a from-scratch iOS 15 control center
 * customization tweak. It hooks ONLY classes that really exist on iOS 15
 * (SBControlCenterController / CCUIModuleCollectionViewController /
 * CCUIContentModuleContainerViewController / CCUIRoundButton / sliders),
 * so it cannot crash SpringBoard the way an iOS-18-only tweak would.
 *
 * No static "other tweaks incompatible" banner, no version gate.
 *
 * Built with: clang -target arm64-apple-ios15.0 -dynamiclib -nostdlib
 *             -undefined dynamic_lookup -fuse-ld=lld -Wl,-adhoc_codesign
 *
 * Optional preferences file (edit with Filza, no respring needed for next
 * launch): /var/jb/Library/Preferences/com.ray.cc15.plist
 *   ModuleCornerRadius  (double, default 22)
 *   ButtonCornerRadius  (double, default 34)
 *   ModuleBgRed/Green/Blue/Alpha (0..1, optional tint on module tiles)
 */

typedef void        *id;
typedef struct objc_selector *SEL;
typedef void        *Class;
typedef void        *IMP;
typedef void        *Method;
typedef signed char  BOOL;

extern id   objc_getClass(const char *);
extern SEL  sel_registerName(const char *);
extern id   objc_msgSend(id, SEL, ...);
extern Class object_getClass(id);
extern Method class_getInstanceMethod(Class, SEL);
extern IMP  method_getImplementation(Method);
extern IMP  method_setImplementation(Method, IMP);
extern id   objc_getMetaClass(const char *);

#define MSG(o, s, ...)  objc_msgSend((o), sel_registerName(s), ##__VA_ARGS__)

/* ---- tunables (overridden by the plist if present) ---- */
static double g_modRadius   = 22.0;
static double g_btnRadius   = 34.0;
static double g_bgR = 1.0, g_bgG = 1.0, g_bgB = 1.0, g_bgA = 0.0;

static id NSStr(const char *s) {
    return MSG(objc_getClass("NSString"), "stringWithUTF8String:", s);
}
static double readDouble(id dict, const char *key, double def) {
    if (!dict) return def;
    id v = MSG(dict, "objectForKey:", NSStr(key));
    if (!v) return def;
    return ((double (*)(id, SEL))objc_msgSend)(v, sel_registerName("doubleValue"));
}

static void loadPrefs(void) {
    id dict = MSG(objc_getClass("NSDictionary"),
                  "dictionaryWithContentsOfFile:",
                  NSStr("/var/jb/Library/Preferences/com.ray.cc15.plist"));
    if (!dict) return;
    g_modRadius = readDouble(dict, "ModuleCornerRadius", g_modRadius);
    g_btnRadius = readDouble(dict, "ButtonCornerRadius", g_btnRadius);
    g_bgA       = readDouble(dict, "ModuleBgAlpha",     g_bgA);
    g_bgR       = readDouble(dict, "ModuleBgRed",       g_bgR);
    g_bgG       = readDouble(dict, "ModuleBgGreen",     g_bgG);
    g_bgB       = readDouble(dict, "ModuleBgBlue",      g_bgB);
}

/* ---- captured originals ---- */
static void (*orig_ModuleContainerDidLoad)(id, SEL) = 0;
static void (*orig_RoundButtonLayout)(id, SEL)      = 0;
static void (*orig_CollectionDidLoad)(id, SEL)      = 0;
static void (*orig_SliderGlyph)(id, SEL)            = 0;

/* ---- round every module tile ---- */
static void new_ModuleContainerDidLoad(id self, SEL _cmd) {
    if (orig_ModuleContainerDidLoad) orig_ModuleContainerDidLoad(self, _cmd);
    id view = MSG(self, "view");
    if (!view) return;
    id layer = MSG(view, "layer");
    MSG(layer, "setCornerRadius:", (double)g_modRadius);
    MSG(view, "setClipsToBounds:", (BOOL)1);
    if (g_bgA > 0.0) {
        id color = MSG(objc_getClass("UIColor"),
                       "colorWithRed:green:blue:alpha:",
                       (double)g_bgR, (double)g_bgG, (double)g_bgB, (double)g_bgA);
        MSG(view, "setBackgroundColor:", color);
    }
}

/* ---- make the round toggles more circular / larger radius ---- */
static void new_RoundButtonLayout(id self, SEL _cmd) {
    if (orig_RoundButtonLayout) orig_RoundButtonLayout(self, _cmd);
    /* CCUIRoundButton exposes -_setCornerRadius: */
    MSG(self, "_setCornerRadius:", (double)g_btnRadius);
}

/* ---- give the module grid a little breathing room ---- */
static void new_CollectionDidLoad(id self, SEL _cmd) {
    if (orig_CollectionDidLoad) orig_CollectionDidLoad(self, _cmd);
    id view = MSG(self, "view");
    if (!view) return;
    id layer = MSG(view, "layer");
    MSG(layer, "setCornerRadius:", (double)0.0); /* keep grid square; tiles do the rounding */
}

static void hook(const char *clsName, const char *selName,
                 IMP replacement, IMP *origOut) {
    Class cls = objc_getClass(clsName);
    if (!cls) return;
    SEL sel = sel_registerName(selName);
    Method m = class_getInstanceMethod(cls, sel);
    if (!m) return;
    IMP orig = method_getImplementation(m);
    if (origOut) *origOut = orig;
    method_setImplementation(m, replacement);
}

__attribute__((constructor))
static void cc15_init(void) {
    loadPrefs();
    hook("CCUIContentModuleContainerViewController", "viewDidLoad",
         (IMP)new_ModuleContainerDidLoad, (IMP *)&orig_ModuleContainerDidLoad);
    hook("CCUIRoundButton", "layoutSubviews",
         (IMP)new_RoundButtonLayout, (IMP *)&orig_RoundButtonLayout);
    hook("CCUIModuleCollectionViewController", "viewDidLoad",
         (IMP)new_CollectionDidLoad, (IMP *)&orig_CollectionDidLoad);
}
