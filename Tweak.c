/*
 * ControlCenter15 (com.ray.cc15) v1.0.2
 *
 * iOS 15 control center customization, from scratch.
 *
 * WHY THIS BUILD IS DIFFERENT (v1.0 / v1.0.1 both hit SafeMode):
 *
 *  v1.0   hooked CCUIContentModuleContainerViewController -viewDidLoad.
 *         That class does NOT define viewDidLoad on iOS 15 (it only has
 *         loadView / viewWillLayoutSubviews), so the old code had to ADD a
 *         brand new method to a live SpringBoard class. Adding methods to
 *         classes that UIKit/ControlCenterUI is actively instantiating is
 *         unsafe and was one crash source.
 *
 *  v1.0.1 hooked CCUIRoundButton -layoutSubviews and called the private
 *         -_setCornerRadius:. That private setter can call setNeedsLayout,
 *         which re-enters -layoutSubviews -> infinite layout recursion ->
 *         watchdog kills SpringBoard -> SafeMode.
 *
 *  v1.0.2 hooks exactly ONE method:
 *         CCUIContentModuleContainerView -layoutSubviews
 *         - the class defines layoutSubviews itself (verified in the iOS 15
 *           runtime index), so the superclass is never touched;
 *         - it only writes PUBLIC properties (CALayer.cornerRadius and
 *           UIView.clipsToBounds), neither of which triggers layout, so no
 *           recursion is possible;
 *         - the hook is installed lazily, only after ControlCenterUI has
 *           actually been loaded (dyld image callback), so we never message
 *           a nil class during early SpringBoard boot.
 *
 *  Everything that can fail is guarded: nil checks, own-method check with a
 *  class_addMethod fallback, and a re-entrancy guard.
 *
 *  A runtime report is written to /var/mobile/cc15.log so that if anything
 *  still goes wrong we can see exactly how far it got.
 *
 * Preferences (Filza, effective after respring):
 *   /var/jb/Library/Preferences/com.ray.cc15.plist
 *     ModuleCornerRadius  (double, default 22)
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
extern Method class_getInstanceMethod(Class, SEL);
extern IMP  method_getImplementation(Method);
extern IMP  method_setImplementation(Method, IMP);
extern Method *class_copyMethodList(Class, unsigned *outCount);
extern SEL  method_getName(Method);
extern const char *method_getTypeEncoding(Method);
extern BOOL class_addMethod(Class, SEL, IMP, const char *);
extern void _dyld_register_func_for_add_image(void (*func)(const void *mh, long slide));

/* libc, resolved from libSystem at runtime (dylib is linked with -nostdlib) */
extern unsigned long strlen(const char *);
extern int  open(const char *, int, ...);
extern long write(int, const char *, unsigned long);
extern int  close(int);

#define O_WRONLY 0x0001
#define O_CREAT  0x0200
#define O_APPEND 0x0008

/* ---------------- tiny logger ---------------- */
static void lg(const char *s) {
    int fd = open("/var/mobile/cc15.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    write(fd, s, strlen(s));
    write(fd, "\n", 1);
    close(fd);
}

/* ---------------- typed message sends ----------------
 * A variadic objc_msgSend call does NOT put a double into d0 on arm64;
 * the compiler must see the real prototype. Every call that passes or
 * returns a non-integer type goes through one of these.               */
static id send0(id o, const char *sel) {
    return ((id (*)(id, SEL))objc_msgSend)(o, sel_registerName(sel));
}
static id send_id(id o, const char *sel, id a) {
    return ((id (*)(id, SEL, id))objc_msgSend)(o, sel_registerName(sel), a);
}
static void send_d(id o, const char *sel, double v) {
    ((void (*)(id, SEL, double))objc_msgSend)(o, sel_registerName(sel), v);
}
static void send_b(id o, const char *sel, BOOL v) {
    ((void (*)(id, SEL, BOOL))objc_msgSend)(o, sel_registerName(sel), v);
}

static id mkstr(const char *s) {
    return ((id (*)(id, SEL, const char *))objc_msgSend)(
        objc_getClass("NSString"),
        sel_registerName("stringWithUTF8String:"), s);
}
static double readDouble(id dict, const char *key, double def) {
    if (!dict) return def;
    id v = send_id(dict, "objectForKey:", mkstr(key));
    if (!v) return def;
    return ((double (*)(id, SEL))objc_msgSend)(v, sel_registerName("doubleValue"));
}

/* ---------------- tunables ---------------- */
static double g_radius   = 22.0;
static int    g_prefs_ok = 0;

static void loadPrefs(void) {
    Class dcls = objc_getClass("NSDictionary");
    if (!dcls) return;
    id dict = send_id((id)dcls, "dictionaryWithContentsOfFile:",
                      mkstr("/var/jb/Library/Preferences/com.ray.cc15.plist"));
    if (!dict) return;
    g_radius = readDouble(dict, "ModuleCornerRadius", g_radius);
}

/* ---------------- the one hook ---------------- */
static IMP g_origLayout = 0;
static int g_hits = 0;
static int g_busy = 0;   /* re-entrancy guard (main-thread only, no TLV needed) */

static void cc15_layout(id self, SEL _cmd) {
    if (g_busy) return;
    g_busy = 1;

    if (g_origLayout) ((void (*)(id, SEL))g_origLayout)(self, _cmd);

    if (!g_prefs_ok) { loadPrefs(); g_prefs_ok = 1; }

    if (g_radius > 0.0 && g_radius < 500.0) {
        id layer = send0(self, "layer");
        if (layer) {
            send_d(layer, "setCornerRadius:", g_radius);
            send_b(self, "setClipsToBounds:", (BOOL)1);
        }
    }

    g_hits++;
    if (g_hits == 1) lg("hook fired for the first time (cornerRadius applied)");
    g_busy = 0;
}

/* ---------------- safe install ---------------- */
static int g_installed = 0;

static int class_defines(Class cls, SEL sel, Method *out) {
    unsigned n = 0;
    Method *list = class_copyMethodList(cls, &n);
    if (!list) return 0;
    int own = 0;
    for (unsigned i = 0; i < n; i++) {
        if (method_getName(list[i]) == sel) { own = 1; if (out) *out = list[i]; break; }
    }
    return own;
}

static void try_install(void) {
    if (g_installed) return;

    Class cls = objc_getClass("CCUIContentModuleContainerView");
    if (!cls) return;                 /* ControlCenterUI not loaded yet */

    SEL sel = sel_registerName("layoutSubviews");
    Method inherited = class_getInstanceMethod(cls, sel);
    if (!inherited) { lg("ABORT: layoutSubviews not found on container view"); g_installed = 1; return; }

    g_origLayout = method_getImplementation(inherited);

    Method own = 0;
    if (class_defines(cls, sel, &own) && own) {
        method_setImplementation(own, (IMP)cc15_layout);
        lg("installed: CCUIContentModuleContainerView.layoutSubviews (own method replaced)");
    } else {
        /* only inherits it -> add an override, superclass untouched */
        class_addMethod(cls, sel, (IMP)cc15_layout, method_getTypeEncoding(inherited));
        lg("installed: CCUIContentModuleContainerView.layoutSubviews (override added)");
    }
    g_installed = 1;
}

/* ControlCenterUI may be dlopen()ed long after we run, so retry on every
 * image the dyld adds. Naming a class that does not exist yet is harmless. */
static void on_image(const void *mh, long slide) {
    (void)mh; (void)slide;
    try_install();
}

__attribute__((constructor))
static void cc15_init(void) {
    lg("=== ControlCenter15 v1.0.2 loaded ===");
    _dyld_register_func_for_add_image(on_image);  /* also fires for already-loaded images */
    try_install();
    if (!g_installed) lg("deferred: ControlCenterUI not loaded yet, waiting for dyld callback");
}
