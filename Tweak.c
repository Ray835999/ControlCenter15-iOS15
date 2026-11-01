/*
 * ControlCenter15 (com.ray.cc15) v2.0.0
 *
 * iOS 15 control center customization, from scratch.
 *
 * History:
 *   v1.0   hooked a -viewDidLoad that CCUIContentModuleContainerViewController
 *          does not even define on iOS 15 -> SafeMode.
 *   v1.0.1 hooked CCUIRoundButton -layoutSubviews and called private
 *          -_setCornerRadius: -> setNeedsLayout -> layout recursion -> SafeMode.
 *   v1.0.2 hooked CCUIContentModuleContainerView -layoutSubviews with radius 22.
 *          Did NOT crash, but 22pt is exactly what iOS 15 already draws for
 *          module tiles, so nothing looked different.
 *   v2.0.0 same safe target, but:
 *            - radius raised to 40 (a 2x2 tile is ~70pt wide, so it becomes
 *              visibly round instead of a slightly-rounded square);
 *            - add a hairline border so even large tiles show a change;
 *            - also hook the controller's -viewWillLayoutSubviews as a second,
 *              independent entry point, in case the container view's own
 *              -layoutSubviews is not the path iOS 15 actually uses;
 *            - dump the module view hierarchy to /var/mobile/cc15.log once, so
 *              if there is still no visible change the log says exactly which
 *              views exist and what they are called.
 *
 * Safety rules kept from v1.0.2:
 *   - only public properties are written (CALayer.cornerRadius / borderWidth /
 *     borderColor, UIView.clipsToBounds); none of them triggers layout, so no
 *     recursion is possible;
 *   - a method is only replaced on the class that defines it, otherwise
 *     class_addMethod is used so UIViewController is never touched;
 *   - hooks are installed lazily from a dyld image callback, i.e. only after
 *     ControlCenterUI has been loaded, so no nil class is ever messaged;
 *   - all message sends that pass non-integer values use typed function
 *     pointers (arm64 floating-point ABI).
 *
 * Preferences (Filza, effective after respring):
 *   /var/jb/Library/Preferences/com.ray.cc15.plist
 *     ModuleCornerRadius (double, default 40)
 *     ModuleBorderWidth  (double, default 1.5)
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

extern unsigned long strlen(const char *);
extern int  open(const char *, int, ...);
extern long write(int, const char *, unsigned long);
extern int  close(int);

#define O_WRONLY 0x0001
#define O_CREAT  0x0200
#define O_APPEND 0x0008

/* ---------------- logging ---------------- */
static void lg(const char *s) {
    int fd = open("/var/mobile/cc15.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    write(fd, s, strlen(s));
    write(fd, "\n", 1);
    close(fd);
}
static void lg_tree(int depth, const char *name) {
    int fd = open("/var/mobile/cc15.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    for (int i = 0; i < depth; i++) write(fd, "    ", 4);
    write(fd, "- ", 2);
    write(fd, name, strlen(name));
    write(fd, "\n", 1);
    close(fd);
}

/* ---------------- typed message sends ---------------- */
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
static unsigned long send_count(id o) {
    return ((unsigned long (*)(id, SEL))objc_msgSend)(o, sel_registerName("count"));
}
static id send_at(id o, unsigned long i) {
    return ((id (*)(id, SEL, unsigned long))objc_msgSend)(o, sel_registerName("objectAtIndex:"), i);
}
static const char *cls_name(id o) {
    id c = o ? send0(o, "class") : 0;
    id d = c ? send0(c, "description") : 0;
    if (!d) return "?";
    return ((const char *(*)(id, SEL))objc_msgSend)(d, sel_registerName("UTF8String"));
}

static id mkstr(const char *s) {
    return ((id (*)(id, SEL, const char *))objc_msgSend)(
        objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), s);
}
extern char *strstr(const char *, const char *);
static int has_word(const char *s, const char *w) {
    return (s && w) ? (strstr(s, w) != 0) : 0;
}
static double readDouble(id dict, const char *key, double def) {
    if (!dict) return def;
    id v = send_id(dict, "objectForKey:", mkstr(key));
    if (!v) return def;
    return ((double (*)(id, SEL))objc_msgSend)(v, sel_registerName("doubleValue"));
}

/* ---------------- tunables ---------------- */
static double g_radius = 40.0;
static double g_border = 1.5;
static int    g_prefs_ok = 0;

static void loadPrefs(void) {
    Class dcls = objc_getClass("NSDictionary");
    if (!dcls) return;
    id dict = send_id((id)dcls, "dictionaryWithContentsOfFile:",
                      mkstr("/var/jb/Library/Preferences/com.ray.cc15.plist"));
    if (!dict) return;
    g_radius = readDouble(dict, "ModuleCornerRadius", g_radius);
    g_border = readDouble(dict, "ModuleBorderWidth",  g_border);
}

/* ---------------- view styling ---------------- */
static id whiteCG(double alpha) {
    id col = ((id (*)(id, SEL, double, double))objc_msgSend)(
        objc_getClass("UIColor"), sel_registerName("colorWithWhite:alpha:"), 1.0, alpha);
    if (!col) return 0;
    return ((id (*)(id, SEL))objc_msgSend)(col, sel_registerName("CGColor"));
}

static int g_dumped = 0;
static void dump_tree(id view, int depth) {
    if (!view || depth > 2) return;
    id subs = send0(view, "subviews");
    if (!subs) return;
    unsigned long n = send_count(subs);
    for (unsigned long i = 0; i < n && i < 16; i++) {
        id s = send_at(subs, i);
        if (!s) continue;
        lg_tree(depth, cls_name(s));
        dump_tree(s, depth + 1);
    }
}

static id grayFill(double alpha) {
    return ((id (*)(id, SEL, double, double))objc_msgSend)(
        objc_getClass("UIColor"), sel_registerName("colorWithWhite:alpha:"), 1.0, alpha);
}

/* iOS 18 module cards are dark translucent plates. On iOS 15 the plate is a
 * blur/material subview, NOT the container itself, so styling the container
 * alone changes nothing. This walks one level down and styles the actual
 * background views. */
static void style_children(id view) {
    id subs = send0(view, "subviews");
    if (!subs) return;
    unsigned long n = send_count(subs);
    for (unsigned long i = 0; i < n && i < 24; i++) {
        id s = send_at(subs, i);
        if (!s) continue;
        const char *nm = cls_name(s);
        if (!(has_word(nm, "Effect") || has_word(nm, "Material") ||
              has_word(nm, "Background") || has_word(nm, "Blur")))
            continue;
        id sl = send0(s, "layer");
        if (sl && g_radius > 0.0 && g_radius < 500.0) {
            send_d(sl, "setCornerRadius:", g_radius);
            send_b(s, "setClipsToBounds:", (BOOL)1);
        }
        /* UIVisualEffectView paints through its contentView */
        id cv = send0(s, "contentView");
        id target = cv ? cv : s;
        id fill = grayFill(0.10);
        if (fill) send_id(target, "setBackgroundColor:", fill);
    }
}

static void style_view(id view) {
    if (!view) return;
    id layer = send0(view, "layer");
    if (layer) {
        if (g_radius > 0.0 && g_radius < 500.0) {
            send_d(layer, "setCornerRadius:", g_radius);
            send_b(view, "setClipsToBounds:", (BOOL)1);
        }
        if (g_border > 0.0 && g_border < 20.0) {
            send_d(layer, "setBorderWidth:", g_border);
            id cg = whiteCG(0.28);
            if (cg) send_id(layer, "setBorderColor:", cg);
        }
    }
    style_children(view);
    if (!g_dumped) {
        g_dumped = 1;
        lg("--- module view hierarchy ---");
        lg(cls_name(view));
        dump_tree(view, 1);
        lg("--- end ---");
    }
}

/* ---------------- hooks ---------------- */
static IMP g_origLayout = 0;
static IMP g_origWillLayout = 0;
static int g_hits = 0;
static int g_busy = 0;

/* entry point 1: the container view's own -layoutSubviews */
static void cc15_layout(id self, SEL _cmd) {
    if (g_busy) return;
    g_busy = 1;
    if (g_origLayout) ((void (*)(id, SEL))g_origLayout)(self, _cmd);
    if (!g_prefs_ok) { loadPrefs(); g_prefs_ok = 1; }
    style_view(self);
    g_hits++;
    if (g_hits == 1) lg("hook fired: container view layoutSubviews");
    g_busy = 0;
}

/* entry point 2: the controller's -viewWillLayoutSubviews (independent path) */
static void cc15_willLayout(id self, SEL _cmd) {
    if (g_busy) return;
    g_busy = 1;
    if (g_origWillLayout) ((void (*)(id, SEL))g_origWillLayout)(self, _cmd);
    if (!g_prefs_ok) { loadPrefs(); g_prefs_ok = 1; }
    id v = send0(self, "view");
    if (v) style_view(v);
    g_hits++;
    if (g_hits == 1) lg("hook fired: controller viewWillLayoutSubviews");
    g_busy = 0;
}

/* entry point 3: guaranteed to run once when the module appears, even if the
 * layout callbacks are not the path iOS 15 uses for module containers */
static IMP g_origWillAppear = 0;
static void cc15_willAppear(id self, SEL _cmd, BOOL animated) {
    if (g_busy) return;
    g_busy = 1;
    if (g_origWillAppear) ((void (*)(id, SEL, BOOL))g_origWillAppear)(self, _cmd, animated);
    if (!g_prefs_ok) { loadPrefs(); g_prefs_ok = 1; }
    id v = send0(self, "view");
    if (v) style_view(v);
    g_hits++;
    if (g_hits == 1) lg("hook fired: controller viewWillAppear:");
    g_busy = 0;
}

/* ============================================================
 *  FORCED PASS
 *  The module layout callbacks are not a reliable entry point on
 *  iOS 15. Instead we wait until Control Center has actually been
 *  presented (SBControlCenterController -_didPresent), then walk
 *  the whole view tree and style every module container we find.
 *  This does not depend on any particular internal layout path.
 * ============================================================ */
static void walk_apply(id v, int depth) {
    if (!v || depth > 14) return;
    const char *nm = cls_name(v);
    if (has_word(nm, "ContentModuleContainerView"))
        style_view(v);
    id subs = send0(v, "subviews");
    if (!subs) return;
    unsigned long n = send_count(subs);
    for (unsigned long i = 0; i < n && i < 64; i++) {
        id s = send_at(subs, i);
        if (s) walk_apply(s, depth + 1);
    }
}

static id cc_root_view(id self) {
    id vc = send0(self, "controlCenterViewController");
    if (vc) {
        id v = send0(vc, "view");
        if (v) return v;
    }
    Class appc = objc_getClass("UIApplication");
    if (appc) {
        id app = ((id (*)(id, SEL))objc_msgSend)((id)appc,
                     sel_registerName("sharedApplication"));
        if (app) {
            id win = send0(app, "keyWindow");
            if (win) return win;
        }
    }
    return 0;
}

static IMP g_origDidPresent = 0;
static void cc15_didPresent(id self, SEL _cmd) {
    if (g_origDidPresent) ((void (*)(id, SEL))g_origDidPresent)(self, _cmd);
    if (!g_prefs_ok) { loadPrefs(); g_prefs_ok = 1; }
    id root = cc_root_view(self);
    if (root) {
        walk_apply(root, 0);
        lg("forced walk applied on _didPresent");
    } else {
        lg("forced walk: no root view found");
    }
}

/* every layout pass of the CC layout view, belt and braces */
static IMP g_origLayoutView = 0;
static void cc15_layoutView(id self, SEL _cmd) {
    if (g_origLayoutView) ((void (*)(id, SEL))g_origLayoutView)(self, _cmd);
    if (!g_prefs_ok) { loadPrefs(); g_prefs_ok = 1; }
    id subs = send0(self, "subviewsToLayout");
    if (!subs) return;
    unsigned long n = send_count(subs);
    for (unsigned long i = 0; i < n && i < 64; i++) {
        id s = send_at(subs, i);
        if (!s) continue;
        if (has_word(cls_name(s), "ContentModuleContainerView")) style_view(s);
    }
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

static int install_one(const char *clsName, const char *selName,
                       IMP replacement, IMP *origOut) {
    Class cls = objc_getClass(clsName);
    if (!cls) return 0;
    SEL sel = sel_registerName(selName);
    Method inherited = class_getInstanceMethod(cls, sel);
    if (!inherited) { lg("skip: method not found"); return 1; }
    *origOut = method_getImplementation(inherited);

    Method own = 0;
    if (class_defines(cls, sel, &own) && own) {
        method_setImplementation(own, replacement);
    } else {
        class_addMethod(cls, sel, replacement, method_getTypeEncoding(inherited));
    }
    return 1;
}

static void try_install(void) {
    if (g_installed) return;

    Class scc = objc_getClass("SBControlCenterController");   /* always present in SpringBoard */
    Class cv  = objc_getClass("CCUIContentModuleContainerView");
    Class vc  = objc_getClass("CCUIContentModuleContainerViewController");
    Class lvv = objc_getClass("CCUILayoutView");

    if (!scc && !cv && !vc) return;          /* ControlCenterUI not loaded yet */
    if (!g_prefs_ok) { loadPrefs(); g_prefs_ok = 1; }

    if (scc) install_one("SBControlCenterController", "_didPresent",
                         (IMP)cc15_didPresent, &g_origDidPresent);
    if (lvv) install_one("CCUILayoutView", "layoutSubviews",
                         (IMP)cc15_layoutView, &g_origLayoutView);
    if (cv)  install_one("CCUIContentModuleContainerView", "layoutSubviews",
                         (IMP)cc15_layout, &g_origLayout);
    if (vc) {
        install_one("CCUIContentModuleContainerViewController", "viewWillLayoutSubviews",
                    (IMP)cc15_willLayout, &g_origWillLayout);
        install_one("CCUIContentModuleContainerViewController", "viewWillAppear:",
                    (IMP)cc15_willAppear, &g_origWillAppear);
    }

    if (scc || cv || vc) {
        lg("installed: didPresent + layoutView + container + controller");
        g_installed = 1;
    }
}

static void on_image(const void *mh, long slide) {
    (void)mh; (void)slide;
    try_install();
}

__attribute__((constructor))
static void cc15_init(void) {
    lg("=== ControlCenter15 v2.0.0 loaded ===");
    _dyld_register_func_for_add_image(on_image);
    try_install();
    if (!g_installed) lg("deferred: waiting for ControlCenterUI");
}
