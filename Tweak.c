/*
 * ControlCenter15 (com.ray.cc15) — a from-scratch iOS 15 control center
 * customization tweak. It hooks ONLY classes that really exist on iOS 15
 * (CCUIContentModuleContainerViewController / CCUIRoundButton /
 *  CCUIModuleCollectionViewController), so it cannot crash SpringBoard the
 * way an iOS-18-only tweak would.
 *
 * Two crash-safety rules baked in:
 *  1) Any method that takes a floating-point argument is called through a
 *     TYPED function pointer (not the variadic objc_msgSend), otherwise the
 *     double would land in an integer register on arm64 and CALayer would
 *     get a garbage/NaN cornerRadius -> CoreAnimation crash -> SafeMode.
 *  2) A method is only replaced on the class that actually defines it; if the
 *     class only inherits it, we ADD an override with class_addMethod so we
 *     never mutate UIViewController's own method for every VC in SpringBoard.
 *
 * No static "other tweaks incompatible" banner, no version gate.
 *
 * Built with: clang -target arm64-apple-ios15.0 -dynamiclib -nostdlib
 *             -undefined dynamic_lookup -fuse-ld=lld -Wl,-adhoc_codesign
 *
 * Optional preferences file (edit with Filza, effective after next respring):
 *   /var/jb/Library/Preferences/com.ray.cc15.plist
 *   ModuleCornerRadius (double, default 22)
 *   ButtonCornerRadius (double, default 34)
 *   ModuleBgRed/Green/Blue/Alpha (0..1, optional module tint)
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
extern Method *class_copyMethodList(Class, unsigned *outCount);
extern const char *method_getTypeEncoding(Method);
extern SEL  method_getName(Method);
extern BOOL  class_addMethod(Class, SEL, IMP, const char *);
extern Class class_getSuperclass(Class);

/* generic object/integer-arg sends (variadic ABI is correct for these) */
#define MSG(o, s, ...)  objc_msgSend((o), sel_registerName(s), ##__VA_ARGS__)

/* typed float-arg sends (REQUIRED for double/CGFloat params on arm64) */
static void send_cornerRadius(id o, const char *sel, double v) {
    void (*f)(id, SEL, double) = (void *)objc_msgSend;
    f(o, sel_registerName(sel), v);
}
static id send_color_rgba(double r, double g, double b, double a) {
    id (*f)(id, SEL, double, double, double, double) = (void *)objc_msgSend;
    return f(objc_getClass("UIColor"),
             sel_registerName("colorWithRed:green:blue:alpha:"),
             r, g, b, a);
}

/* ---- tunables (overridden by the plist if present) ---- */
static double g_modRadius = 22.0;
static double g_btnRadius = 34.0;
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

/* ---- round every module tile ---- */
static void new_ModuleContainerDidLoad(id self, SEL _cmd) {
    if (orig_ModuleContainerDidLoad) orig_ModuleContainerDidLoad(self, _cmd);
    if (!(g_modRadius > 0.0 && g_modRadius < 1000.0)) return;
    id view = MSG(self, "view");
    if (!view) return;
    id layer = MSG(view, "layer");
    if (!layer) return;
    send_cornerRadius(layer, "setCornerRadius:", g_modRadius);
    MSG(view, "setClipsToBounds:", (BOOL)1);
    if (g_bgA > 0.0 && g_bgA <= 1.0) {
        id color = send_color_rgba(g_bgR, g_bgG, g_bgB, g_bgA);
        if (color) MSG(view, "setBackgroundColor:", color);
    }
}

/* ---- make the round toggles more circular / larger radius ---- */
static void new_RoundButtonLayout(id self, SEL _cmd) {
    if (orig_RoundButtonLayout) orig_RoundButtonLayout(self, _cmd);
    if (g_btnRadius > 0.0 && g_btnRadius < 1000.0)
        send_cornerRadius(self, "_setCornerRadius:", g_btnRadius);
}

/* ---- keep the grid itself square; tiles do the rounding ---- */
static void new_CollectionDidLoad(id self, SEL _cmd) {
    if (orig_CollectionDidLoad) orig_CollectionDidLoad(self, _cmd);
    id view = MSG(self, "view");
    if (!view) return;
    id layer = MSG(view, "layer");
    if (!layer) return;
    send_cornerRadius(layer, "setCornerRadius:", 0.0);
}

/*
 * Safe swizzle: only replace the method on the class that defines it.
 * If the class only inherits the method, add an override (class_addMethod)
 * so UIViewController (and friends) are never touched.
 */
static void safe_hook(const char *clsName, const char *selName,
                      IMP replacement, IMP *origOut) {
    Class cls = objc_getClass(clsName);
    if (!cls || !replacement) return;
    SEL sel = sel_registerName(selName);
    Method m = class_getInstanceMethod(cls, sel);
    if (!m) return;

    unsigned count = 0;
    Method *list = class_copyMethodList(cls, &count); /* not freed: tiny one-time leak OK */
    int own = 0;
    for (unsigned i = 0; i < count; i++) {
        if (method_getName(list[i]) == sel) { own = 1; m = list[i]; break; }
    }

    const char *enc = method_getTypeEncoding(m);
    IMP orig = method_getImplementation(m);
    if (origOut) *origOut = orig;

    if (own) {
        method_setImplementation(m, replacement);
    } else {
        class_addMethod(cls, sel, replacement, enc);
    }
}

__attribute__((constructor))
static void cc15_init(void) {
    loadPrefs();
    safe_hook("CCUIContentModuleContainerViewController", "viewDidLoad",
              (IMP)new_ModuleContainerDidLoad, (IMP *)&orig_ModuleContainerDidLoad);
    safe_hook("CCUIRoundButton", "layoutSubviews",
              (IMP)new_RoundButtonLayout, (IMP *)&orig_RoundButtonLayout);
    safe_hook("CCUIModuleCollectionViewController", "viewDidLoad",
              (IMP)new_CollectionDidLoad, (IMP *)&orig_CollectionDidLoad);
}
