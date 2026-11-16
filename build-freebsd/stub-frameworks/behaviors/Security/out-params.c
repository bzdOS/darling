/* Security stub behaviour: out-parameters.
 *
 * The generic stub is `xorl %eax,%eax; retq`: it reports success and leaves every
 * out-parameter as the caller initialised it, usually NULL. A caller that checks only
 * the status then uses the NULL (CFArrayGetCount(NULL) in Chromium's trust-store code).
 * Each function here that has an out-parameter either reports the failure the real API
 * reports for "nothing there" and writes NULL to every output, or, where the contract
 * is "an array on success", succeeds with an empty array.
 * Functions whose result is the return value (a ref or NULL) keep the generic stub. */
typedef int OSStatus;
typedef void *Ref;
typedef unsigned char Boolean;

extern Ref CFArrayCreate(Ref alloc, const void **values, long n, const void *callbacks);
extern const char kCFTypeArrayCallBacks[];

enum {
    errSecUnimplemented = -4,
    errSecNotAvailable = -25291,
    errSecNoSuchKeychain = -25294,
    errSecItemNotFound = -25300,
    errSecNoDefaultKeychain = -25307,
    errSecNoTrustSettings = -25263,
    errSecCSUnsigned = -67062,
    errSecCSNoSuchCode = -67065,
    errAuthorizationDenied = -60005,
    errAuthorizationInternal = -60008
};

#define OUT(p) do { if (p) *(p) = 0; } while (0)

static Ref empty_array(void) { return CFArrayCreate(0, 0, 0, kCFTypeArrayCallBacks); }

/* Authorization */
OSStatus AuthorizationCreate(const void *r, const void *e, unsigned f, Ref *auth)
{ (void)r; (void)e; (void)f; OUT(auth); return errAuthorizationInternal; }
OSStatus AuthorizationCopyRights(Ref a, const void *r, const void *e, unsigned f, void **out)
{ (void)a; (void)r; (void)e; (void)f; OUT(out); return errAuthorizationDenied; }
OSStatus AuthorizationRightGet(const char *n, Ref *def)
{ (void)n; OUT(def); return errAuthorizationDenied; }

/* ACL / access */
OSStatus SecACLCopyContents(Ref acl, Ref *apps, Ref *desc, void *sel)
{ (void)acl; OUT(apps); OUT(desc); if (sel) *(unsigned short *)sel = 0; return errSecUnimplemented; }
OSStatus SecAccessCreate(Ref d, Ref t, Ref *access)
{ (void)d; (void)t; OUT(access); return errSecUnimplemented; }
OSStatus SecAccessCopyACLList(Ref access, Ref *list)
{ (void)access; if (!list) return errSecUnimplemented; *list = empty_array(); return 0; }

/* code signing */
OSStatus SecCodeCopySelf(unsigned f, Ref *self)
{ (void)f; OUT(self); return errSecCSUnsigned; }
OSStatus SecCodeCopyGuestWithAttributes(Ref h, Ref a, unsigned f, Ref *g)
{ (void)h; (void)a; (void)f; OUT(g); return errSecCSNoSuchCode; }
OSStatus SecCodeCopySigningInformation(Ref c, unsigned f, Ref *info)
{ (void)c; (void)f; OUT(info); return errSecCSUnsigned; }
OSStatus SecCodeCopyDesignatedRequirement(Ref c, unsigned f, Ref *req)
{ (void)c; (void)f; OUT(req); return errSecCSUnsigned; }
OSStatus SecCodeSignerCreate(Ref p, unsigned f, Ref *s)
{ (void)p; (void)f; OUT(s); return errSecUnimplemented; }
OSStatus SecStaticCodeCreateWithPath(Ref path, unsigned f, Ref *code)
{ (void)path; (void)f; OUT(code); return errSecUnimplemented; }
OSStatus SecRequirementCreateWithString(Ref s, unsigned f, Ref *req)
{ (void)s; (void)f; OUT(req); return errSecUnimplemented; }
OSStatus SecRequirementCopyString(Ref r, unsigned f, Ref *s)
{ (void)r; (void)f; OUT(s); return errSecUnimplemented; }
OSStatus SecTaskGetCodeSignStatus(Ref t, unsigned *status)
{ (void)t; if (status) *status = 0; return 0; }
Ref SecTaskCopyValueForEntitlement(Ref t, Ref e, Ref *err)
{ (void)t; (void)e; OUT(err); return 0; }

/* identity / items */
OSStatus SecIdentityCopyCertificate(Ref i, Ref *c)
{ (void)i; OUT(c); return errSecItemNotFound; }
OSStatus SecIdentityCopyPrivateKey(Ref i, Ref *k)
{ (void)i; OUT(k); return errSecItemNotFound; }
OSStatus SecItemCopyMatching(Ref q, Ref *result)
{ (void)q; OUT(result); return errSecItemNotFound; }
OSStatus SecItemAdd(Ref a, Ref *result)
{ (void)a; if (!result) return 0; *result = 0; return errSecNotAvailable; }

/* keychain */
OSStatus SecKeychainCopyDefault(Ref *kc)
{ OUT(kc); return errSecNoDefaultKeychain; }
OSStatus SecKeychainGetStatus(Ref kc, unsigned *status)
{ (void)kc; if (status) *status = 0; return errSecNoSuchKeychain; }
OSStatus SecKeychainGetUserInteractionAllowed(Boolean *allowed)
{ if (allowed) *allowed = 0; return 0; }
OSStatus SecKeychainItemCopyKeychain(Ref item, Ref *kc)
{ (void)item; OUT(kc); return errSecNoSuchKeychain; }
OSStatus SecKeychainItemCreateFromContent(unsigned c, void *a, unsigned n, const void *d, Ref kc, Ref acc, Ref *item)
{ (void)c; (void)a; (void)n; (void)d; (void)kc; (void)acc; OUT(item); return errSecNotAvailable; }
OSStatus SecKeychainFindGenericPassword(Ref kc, unsigned sl, const char *s, unsigned al, const char *a,
                                        unsigned *plen, void **pdata, Ref *item)
{ (void)kc; (void)sl; (void)s; (void)al; (void)a; if (plen) *plen = 0; OUT(pdata); OUT(item); return errSecItemNotFound; }

/* trust */
OSStatus SecTrustCreateWithCertificates(Ref certs, Ref policies, Ref *trust)
{ (void)certs; (void)policies; OUT(trust); return errSecUnimplemented; }
Ref SecTrustCopyCertificateChain(Ref trust)
{ (void)trust; return empty_array(); }
OSStatus SecTrustSettingsCopyCertificates(int domain, Ref *certs)
{ (void)domain; OUT(certs); return errSecNoTrustSettings; }
OSStatus SecTrustSettingsCopyTrustSettings(Ref cert, int domain, Ref *settings)
{ (void)cert; (void)domain; OUT(settings); return errSecItemNotFound; }
