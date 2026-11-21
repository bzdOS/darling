"""stub_data_rules.py -- what is data, and how a stub generator emits it.

Shared by check-data-as-code.py (the build-time gate) and every generator that
emits stub dylibs (gen-cg-supl.py, rebuild-stub-data.py, build-extras-objc-classes.py),
so the gate and the generators cannot disagree about a name.
"""
import re

OBJC = re.compile(r"^_OBJC_(CLASS|METACLASS|IVAR|EHTYPE)_\$_")
BLOCKS = re.compile(r"^_?_NSConcrete")
# DATA_NAMES (k-prefixes, whole names, patterns) is generated from [stubs] of darling.toml by gen-from-schema.py
from stub_data_names import DATA_NAMES  # noqa: E402,F401

# Size of a zero-filled data object a stub emits for a name whose real size is
# unknown. The largest constants the consumers read are structs (CMTime is 24
# bytes), so 64 covers them; the object is always zero (NULL pointer / 0).
DATA_SIZE = 64

# image that defines ___CFConstantStringClassReference: a CFString constant binds its isa to it two-level
CF_INSTALL = "/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation"

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
# CFString, not NULL. A NULL key or value crashes the callers that put a constant in a dictionary
# (+[NSDictionary dictionaryWithObjects:forKeys:count:] throws, CFDictionaryGetValue hashes NULL).
# Struct constants (CMTime, CATransform3D, callbacks), numbers (kCLLocationAccuracy*) and
# non-string objects (UTType *) are not in this set.
STRING_CONST = re.compile(
    r"^_k(SC|Sec|LS|UT|AX|AV|UN|CT|VT|CMF|CMS|CVI|CVP|CVV|CFP|CFN|CFStream|CFProxy|MA|CGColorSpace|CGImage|CGDisplay|CGWindow|CGPDF|CAGravity|CAFilter|CAMedia|CATransition|CATransaction)[A-Za-z]"
    r"|^_k[A-Z][A-Za-z0-9_]*(Notification|NotificationName|Key|Domain)$"
    r"|^_NS[A-Z][A-Za-z]*(Notification|PboardType|Key|Attribute[A-Za-z]*|Option)$"
    # constants of the frameworks the Chrome framework imports (string typed in their headers)
    r"|^_(GCController[A-Za-z]*Notification|VNBarcodeSymbology|UTTagClass|MPMediaItemProperty|MPNowPlayingInfoProperty|GCHapticsLocality"
    r"|MTLCommonCounter|MTLDevice[A-Za-z]*Notification|AVCaptureDeviceType|AVCaptureDevice[A-Za-z]*Notification"
    r"|AVCaptureSession[A-Za-z]*(Key|Notification)|AVMediaType|AVVideo|AVLayerVideoGravity"
    r"|CBAdvertisementData|CBCentralManagerScanOption|CHHaptic(Dynamic|Event)|CIDetector(Accuracy|Type)"
    r"|CADynamicRange|ASAuthorization|ASWebAuthenticationSession|AXPrefers|NSAccessibility(SearchDirection|[A-Za-z]*Subrole)"
    r"|NSTouchBarItemIdentifier|NSUserActivityType|UNNotification(Attachment|Default|Dismiss))"
    r"|^_k(TISProperty|IOSurface|MIDIProperty|MDItem|DADiskDescriptionWatch)[A-Za-z]"
    r"|^__kLSOpen[A-Za-z]")
NOT_STRING = re.compile(r"(CallBacks|Callbacks|Callback)$|^_AVSpeechUtterance|^_kIOMainPortDefault$|^_UTType[A-Z]")

# {name: (text, source)}; source is cocotron-code, sdk-doc-from-memory or (absent from the table) symbol-name.
# Text of a string constant whose value is not its name. Priority: the cocotron definition
# (src/external/cocotron: only the CATransaction key is defined there, and its text is the name
# without the underscore, the same as the default), then the macOS value from the SDK headers or
# documentation, else the name without its underscore.
STRING_VALUES = {
    "_kCATransactionDisableActions": ("kCATransactionDisableActions", "cocotron-code"),
    "_kIOSurfaceAllocSize": ("IOSurfaceAllocSize", "sdk-doc-from-memory"),
    "_kIOSurfaceBytesPerElement": ("IOSurfaceBytesPerElement", "sdk-doc-from-memory"),
    "_kIOSurfaceBytesPerRow": ("IOSurfaceBytesPerRow", "sdk-doc-from-memory"),
    "_kIOSurfaceHeight": ("IOSurfaceHeight", "sdk-doc-from-memory"),
    "_kIOSurfacePixelFormat": ("IOSurfacePixelFormat", "sdk-doc-from-memory"),
    "_kIOSurfacePlaneBytesPerElement": ("IOSurfacePlaneBytesPerElement", "sdk-doc-from-memory"),
    "_kIOSurfacePlaneBytesPerRow": ("IOSurfacePlaneBytesPerRow", "sdk-doc-from-memory"),
    "_kIOSurfacePlaneHeight": ("IOSurfacePlaneHeight", "sdk-doc-from-memory"),
    "_kIOSurfacePlaneInfo": ("IOSurfacePlaneInfo", "sdk-doc-from-memory"),
    "_kIOSurfacePlaneOffset": ("IOSurfacePlaneOffset", "sdk-doc-from-memory"),
    "_kIOSurfacePlaneSize": ("IOSurfacePlaneSize", "sdk-doc-from-memory"),
    "_kIOSurfacePlaneWidth": ("IOSurfacePlaneWidth", "sdk-doc-from-memory"),
    "_kIOSurfaceWidth": ("IOSurfaceWidth", "sdk-doc-from-memory"),
    "_kTISPropertyInputSourceID": ("TISPropertyInputSourceID", "sdk-doc-from-memory"),
    "_kTISPropertyUnicodeKeyLayoutData": ("TISPropertyUnicodeKeyLayoutData", "sdk-doc-from-memory"),
    "_kMIDIPropertyDisplayName": ("displayName", "sdk-doc-from-memory"),
    "_kMIDIPropertyManufacturer": ("manufacturer", "sdk-doc-from-memory"),
    "_kMIDIPropertyUniqueID": ("uniqueID", "sdk-doc-from-memory"),
    "_kMIDIPropertyDriverVersion": ("driver version", "sdk-doc-from-memory"),
    "_AVMediaTypeAudio": ("soun", "sdk-doc-from-memory"),
    "_AVMediaTypeVideo": ("vide", "sdk-doc-from-memory"),
    "_AVMediaTypeMuxed": ("muxx", "sdk-doc-from-memory"),
    "_AVVideoCodecTypeJPEG": ("jpeg", "sdk-doc-from-memory"),
    "_UTTagClassFilenameExtension": ("public.filename-extension", "sdk-doc-from-memory"),
    "_UTTagClassMIMEType": ("public.mime-type", "sdk-doc-from-memory"),
    "_UNNotificationDefaultActionIdentifier": ("com.apple.UNNotificationDefaultActionIdentifier", "sdk-doc-from-memory"),
    "_UNNotificationDismissActionIdentifier": ("com.apple.UNNotificationDismissActionIdentifier", "sdk-doc-from-memory"),
    "_ASAuthorizationErrorDomain": ("com.apple.AuthenticationServices.AuthorizationError", "sdk-doc-from-memory"),
    "_ASWebAuthenticationSessionErrorDomain": ("com.apple.AuthenticationServices.WebAuthenticationSession", "sdk-doc-from-memory"),
    "_NSAccessibilityTabButtonSubrole": ("AXTabButton", "sdk-doc-from-memory"),
    "_kSecAttrAccount": ("acct", "sdk-doc-from-memory"),
    "_kSecAttrApplicationTag": ("atag", "sdk-doc-from-memory"),
    "_kSecAttrKeyType": ("type", "sdk-doc-from-memory"),
    "_kSecAttrType": ("type", "sdk-doc-from-memory"),
    "_kSecAttrLabel": ("labl", "sdk-doc-from-memory"),
    "_kSecAttrTokenID": ("tkid", "sdk-doc-from-memory"),
    "_kSecAttrAccessibleWhenUnlockedThisDeviceOnly": ("aku", "sdk-doc-from-memory"),
    "_kSecClassCertificate": ("cert", "sdk-doc-from-memory"),
    "_kSecMatchLimitAll": ("m_LimitAll", "sdk-doc-from-memory"),
    "_kSecPrivateKeyAttrs": ("private", "sdk-doc-from-memory"),
    "_kSecReturnAttributes": ("r_Attributes", "sdk-doc-from-memory"),
    "_kSecReturnData": ("r_Data", "sdk-doc-from-memory"),
    "_kSecValueData": ("v_Data", "sdk-doc-from-memory"),
    "_kSecCodeInfoCertificates": ("certificates", "sdk-doc-from-memory"),
    "_kSecKeyAlgorithmECDSASignatureDigestX962SHA1": ("algid:sign:ECDSA:digest-X962:SHA1", "sdk-doc-from-memory"),
    "_kSecKeyAlgorithmECDSASignatureMessageX962SHA256": ("algid:sign:ECDSA:message-X962:SHA256", "sdk-doc-from-memory"),
    "_kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA512": ("algid:sign:RSA:digest-PKCS1v15:SHA512", "sdk-doc-from-memory"),
    "_kSecKeyAlgorithmRSASignatureDigestPSSSHA384": ("algid:sign:RSA:digest-PSS:SHA384:SHA384", "sdk-doc-from-memory"),
}


def is_string_const(name):
    return bool(STRING_CONST.match(name)) and not NOT_STRING.search(name)


def string_value(name):
    """text of the CFString a string constant points to"""
    return STRING_VALUES[name][0] if name in STRING_VALUES else name[1:]


def string_source(name):
    """where the text of a string constant comes from"""
    return STRING_VALUES[name][1] if name in STRING_VALUES else "symbol-name"


# Images no stub generator rewrites (they have their own generator or are real system libraries);
# rebuild-stub-data.py skips them, so the caller passes the whole stub list.
# ImageIO.framework: the real implementation (src/frameworks/ImageIO on Onyx2D, build-imageio.sh) is not shipped yet;
# return it here (add the ImageIO.framework alternative back) when IMAGEIO-DECODE is green.
EXCLUDE_IMAGES = re.compile(
    r"(/usr/lib/(suppl|cg-supl|SystemConfigurationExtras)\.dylib$"
    r"|/OpenDirectory\.framework/|/usr/lib/system/)")


def is_excluded_image(path):
    return bool(EXCLUDE_IMAGES.search(path))


# Data names the injected image (darling-extras.dylib) must not export: the framework that owns the
# constant is a stand-in of its own (OpenDirectory) and exports it first-class; the injected image comes
# first in the flat lookup order and its zero object would hide that one. Data counterpart of
# SHADOW_FROM in rebuild-stub-data.py.
INJECTED_DATA_SHADOW = re.compile(
    r"^_k(ODAttributeType(AllAttributes|AltSecurityIdentities|MetaRecordName|RecordName)|ODRecordTypeUsers)$")


def is_injected_data_shadow(name):
    return bool(INJECTED_DATA_SHADOW.match(name))


def data_asm(name, size=DATA_SIZE, strings=False):
    """Assembly for one exported data object: zero filled, or for a string constant a pointer to a
    constant CFString (text = string_value(name)). The CFString isa is
    ___CFConstantStringClassReference; the image must be linked against CF_INSTALL (-needed_library)
    so the isa is a two-level bind (strings=True); anything else keeps zero filled objects."""
    if strings and is_string_const(name):
        lab = re.sub(r"[^A-Za-z0-9_]", "_", name)
        text = string_value(name)
        return [".section __TEXT,__cstring,cstring_literals", "L_s_%s:" % lab,
                '    .asciz "%s"' % text,
                ".section __DATA,__cfstring", ".p2align 3", "L_cf_%s:" % lab,
                "    .quad ___CFConstantStringClassReference", "    .long 0x7c8", "    .long 0",
                "    .quad L_s_%s" % lab, "    .quad %d" % len(text),
                ".section __DATA,__data", ".globl " + name, ".p2align 4", name + ":",
                "    .quad L_cf_%s" % lab, ".space %d, 0" % (size - 8)]
    return [".section __DATA,__data", ".globl " + name, ".p2align 4", name + ":",
            ".space %d, 0" % size]


# Per-class additions to the template (classes_m): ivars and methods appended verbatim,
# only for the named class. NSConstantDictionary answers its nine measured selectors
# itself instead of the global forward handler: build-freebsd/SERIES-FIX24-DIAG.md
# measured all nine as OBJC-FWD-MISS in every fix24 run (41 % of the hits), and the
# forward handler's nil/0 is the empty-dictionary answer the rc133 class hangs on.
# Its instances are the constant records the linker emits into __objc_dictobj: measured
# over the six records of one framework binary, the record is
# {isa, word, count, keys, values}, 40 bytes, the key/value arrays holding `count`
# pointers each. length answers the stored pair count; _fastCharacterContents answers
# NULL — the stored pairs have no flat character buffer (the tree's own
# -[NSString _fastCharacterContents] answers NULL the same way, NSString.m). The six
# isX__ are the NSObject-level NO (NSObject (__NSIsKinds) in
# src/external/corefoundation/NSObject.m); release is a no-op on a static record.
# The generated file is compiled without an include path, so BOOL/NO come from this
# typedef: the runtime's x86_64 BOOL is signed char (objc.h).
_NSCT_PRELUDE = "typedef signed char BOOL;\n#define NO ((BOOL)0)\n\n"
_NSCT_IVARS = ("    unsigned long _flags;\n    unsigned long _count;\n"
               "    id *_keys;\n    id *_values;\n")
_NSCT_METHODS = (
    "- (unsigned long)length { return _count; }\n"
    "- (const unsigned short *)_fastCharacterContents { return 0; }\n"
    "- (BOOL)isNSArray__ { return NO; }\n"
    "- (BOOL)isNSData__ { return NO; }\n"
    "- (BOOL)isNSDate__ { return NO; }\n"
    "- (BOOL)isNSDictionary__ { return NO; }\n"
    "- (BOOL)isNSNumber__ { return NO; }\n"
    "- (BOOL)isNSString__ { return NO; }\n"
    "- (void)release { }\n")
EXTRAS = {"NSConstantDictionary": (_NSCT_PRELUDE, _NSCT_IVARS, _NSCT_METHODS)}


# Contract: classes_m emits the seven root-class methods every stub class gets; a class
# named in EXTRAS additionally gets its ivars in the @interface and its own methods after
# the seven, and its prelude (typedefs the header-free generated file cannot include).
# Without an entry the output is byte-identical to the seven-method template.
# START_CLASSES_M_EXTRAS
def classes_m(classes):
    """Objective-C source: one real root class per name (valid isa, metaclass).

    Root classes with their own methods: inheriting NSObject would add a load-time
    bind to _OBJC_METACLASS_$_NSObject, which the guest refuses at image load.
    +alloc/+new return nil, so the usual `[[X alloc] init...]` is a message to nil.
    """
    out = []
    for c in classes:
        prelude, ivars, extra = EXTRAS.get(c, ("", "", ""))
        if prelude:
            out.append(prelude)
        body = ("{\n" + ivars + "}") if ivars else ""
        out.append("__attribute__((objc_root_class))\n@interface %s%s\n@end\n"
                   "@implementation %s\n"
                   "+ (id)alloc { return 0; }\n+ (id)allocWithZone:(void *)z { (void)z; return 0; }\n"
                   "+ (id)new { return 0; }\n"
                   "- (id)description { return self; }\n"
                   "- (id)copyWithZone:(void *)zone { (void)zone; return self; }\n"
                   "- (id)copy { return self; }\n"
                   "- (id)class { return self; }\n"
                   "%s"
                   "@end\n" % (c, body, c, extra))
    return "".join(out)
# END_CLASSES_M_EXTRAS
