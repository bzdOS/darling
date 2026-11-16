"""stub_data_rules.py -- what is data, and how a stub generator emits it.

Shared by check-data-as-code.py (the build-time gate) and every generator that
emits stub dylibs (gen-cg-supl.py, rebuild-stub-data.py, build-extras-objc-classes.py),
so the gate and the generators cannot disagree about a name.
"""
import re

OBJC = re.compile(r"^_OBJC_(CLASS|METACLASS|IVAR|EHTYPE)_\$_")
BLOCKS = re.compile(r"^_?_NSConcrete")
DATA_NAMES = re.compile(
    r"^(_k(CG|CM|CA|CV|CI|MT|OD|Sec|CF|UT|AX|NS|IO|LS|AV|UN)[A-Z]"
    r"|_vm_page_(size|mask|shift)$|___DefaultRuneLocale$|__DefaultRuneLocale$"
    r"|_environ$|_NXArg[cv]$|___stack_chk_guard$|_opterr$|_optind$|_optarg$|_optopt$"
    r"|_optreset$|_sys_errlist$|_sys_nerr$|_sys_siglist$|_tzname$|_daylight$|_timezone$"
    r"|_stdin$|_stdout$|_stderr$|___stdinp$|___stdoutp$|___stderrp$"
    r"|_mach_task_self_$|_bootstrap_port$|_mach_host_self_$|_signgam$"
    r"|_OBJC_|_NSApp$|_NSFoundationVersionNumber$"
    r"|_NS[A-Z][A-Za-z]*(Notification|PboardType|Key|Attribute[A-Za-z]*|Option)$)")

# Size of a zero-filled data object a stub emits for a name whose real size is
# unknown. The largest constants the consumers read are structs (CMTime is 24
# bytes), so 64 covers them; the object is always zero (NULL pointer / 0).
DATA_SIZE = 64

# Every stub that defines a class gets this one, hidden, so the root classes
# do not bind to (or shadow) libobjc's own copy.
EMPTY_CACHE_S = """\
\t.section\t__DATA,__data
\t.private_extern\t__objc_empty_cache
\t.globl\t__objc_empty_cache
\t.p2align\t3
__objc_empty_cache:
\t.quad\t0
\t.quad\t0
"""


def _load_listed():
    import os
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stub-data-names.txt")
    try:
        return {l.strip() for l in open(path) if l.strip() and not l.startswith("#")}
    except OSError:
        return set()


# names measured to be data (build-freebsd/stub-data-names.txt): data in every stub image
LISTED = _load_listed()


def is_objc_symbol(name):
    return bool(OBJC.match(name))


def is_known_data(name):
    """Rules a and b of check-data-as-code.py."""
    return bool(OBJC.match(name) or BLOCKS.match(name) or DATA_NAMES.match(name)
                or name in LISTED)


def class_names(names):
    """Class names behind the _OBJC_CLASS_$_X / _OBJC_METACLASS_$_X symbols."""
    out = set()
    for n in names:
        m = re.match(r"^_OBJC_(?:CLASS|METACLASS)_\$_(.+)$", n)
        if m:
            out.add(m.group(1))
    return sorted(out)


# Names of the string constants (CFStringRef / NSString *): the object is a pointer to a constant
# CFString whose text is the name, not NULL. A NULL key or value crashes the callers that put a
# constant in a dictionary (+[NSDictionary dictionaryWithObjects:forKeys:count:] throws, CFDictionaryGetValue
# hashes NULL). Struct constants (CMTime) and objects (kCGColor*, callbacks) are not in this set.
STRING_CONST = re.compile(
    r"^_k(SC|Sec|LS|UT|AX|AV|UN|CT|VT|CMF|CMS|CVI|CVP|CVV|CFP|CFN|CFStream|CGColorSpace|CGImage|CGDisplay|CGWindow|CGPDF|CAGravity|CAFilter|CAMedia|CATransition)[A-Za-z]"
    r"|^_NS[A-Z][A-Za-z]*(Notification|PboardType|Key|Attribute[A-Za-z]*|Option)$")
NOT_STRING = re.compile(r"(CallBacks|Callbacks|Callback)$")


def is_string_const(name):
    return bool(STRING_CONST.match(name)) and not NOT_STRING.search(name)


def data_asm(name, size=DATA_SIZE, strings=False):
    """Assembly for one exported data object: zero filled, or for a string constant a pointer to a
    constant CFString (text = the name without its underscore). The CFString isa is
    ___CFConstantStringClassReference, found by flat lookup: only for an image linked with
    -undefined dynamic_lookup and no dependency that would make the linker bind the isa two-level
    (strings=True); anything else keeps zero filled objects."""
    if strings and is_string_const(name):
        lab = re.sub(r"[^A-Za-z0-9_]", "_", name)
        return [".section __TEXT,__cstring,cstring_literals", "L_s_%s:" % lab,
                '    .asciz "%s"' % name[1:],
                ".section __DATA,__cfstring", ".p2align 3", "L_cf_%s:" % lab,
                "    .quad ___CFConstantStringClassReference", "    .long 0x7c8", "    .long 0",
                "    .quad L_s_%s" % lab, "    .quad %d" % (len(name) - 1),
                ".section __DATA,__data", ".globl " + name, ".p2align 4", name + ":",
                "    .quad L_cf_%s" % lab, ".space %d, 0" % (size - 8)]
    return [".section __DATA,__data", ".globl " + name, ".p2align 4", name + ":",
            ".space %d, 0" % size]


def classes_m(classes):
    """Objective-C source: one real root class per name (valid isa, metaclass).

    Root classes with their own methods: inheriting NSObject would add a load-time
    bind to _OBJC_METACLASS_$_NSObject, which the guest refuses at image load.
    +alloc/+new return nil, so the usual `[[X alloc] init...]` is a message to nil.
    """
    out = []
    for c in classes:
        out.append("__attribute__((objc_root_class))\n@interface %s\n@end\n"
                   "@implementation %s\n"
                   "+ (id)alloc { return 0; }\n+ (id)allocWithZone:(void *)z { (void)z; return 0; }\n"
                   "+ (id)new { return 0; }\n"
                   "- (id)description { return self; }\n"
                   "- (id)copyWithZone:(void *)zone { (void)zone; return self; }\n"
                   "- (id)copy { return self; }\n"
                   "- (id)class { return self; }\n"
                   "@end\n" % (c, c))
    return "".join(out)
