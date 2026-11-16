/* Security stub behaviour: constants.
 *
 * The generic stub exports every kSec* constant as a zero filled pointer. Callers put those
 * constants in dictionaries (the keychain password query of the Chromium os_crypt code uses
 * kSecClass, kSecAttrService, kSecAttrAccount, kSecMatchLimit, kSecReturnData ...); a NULL key or
 * value makes +[NSDictionary dictionaryWithObjects:forKeys:count:] throw "Tried to init dictionary
 * with nil object", which killed most Chrome runs. Each constant here is a real CFString, set by a
 * constructor before any consumer reads it: the documented value where it is known, else the
 * constant's own name, so keys stay distinct. */
typedef const void *CFStringRef;
extern CFStringRef CFStringCreateWithCString(const void *alloc, const char *s, unsigned enc);

CFStringRef kSecAttrAccessControl;
CFStringRef kSecAttrAccessGroup;
CFStringRef kSecAttrAccessible;
CFStringRef kSecAttrAccessibleAfterFirstUnlock;
CFStringRef kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly;
CFStringRef kSecAttrAccessibleWhenUnlocked;
CFStringRef kSecAttrAccessibleWhenUnlockedThisDeviceOnly;
CFStringRef kSecAttrAccount;
CFStringRef kSecAttrApplicationLabel;
CFStringRef kSecAttrApplicationTag;
CFStringRef kSecAttrCanSign;
CFStringRef kSecAttrCreationDate;
CFStringRef kSecAttrIsPermanent;
CFStringRef kSecAttrKeySizeInBits;
CFStringRef kSecAttrKeyType;
CFStringRef kSecAttrKeyTypeECSECPrimeRandom;
CFStringRef kSecAttrLabel;
CFStringRef kSecAttrModificationDate;
CFStringRef kSecAttrService;
CFStringRef kSecAttrSynchronizable;
CFStringRef kSecAttrTokenID;
CFStringRef kSecAttrTokenIDSecureEnclave;
CFStringRef kSecAttrType;
CFStringRef kSecCFErrorResourceAltered;
CFStringRef kSecCFErrorResourceMissing;
CFStringRef kSecClass;
CFStringRef kSecClassCertificate;
CFStringRef kSecClassGenericPassword;
CFStringRef kSecClassIdentity;
CFStringRef kSecClassKey;
CFStringRef kSecCodeInfoCertificates;
CFStringRef kSecCodeInfoFlags;
CFStringRef kSecCodeInfoMainExecutable;
CFStringRef kSecCodeInfoPList;
CFStringRef kSecCodeInfoUnique;
CFStringRef kSecCodeSignerEntitlements;
CFStringRef kSecCodeSignerFlags;
CFStringRef kSecCodeSignerIdentity;
CFStringRef kSecGuestAttributeAudit;
CFStringRef kSecGuestAttributeDynamicCode;
CFStringRef kSecGuestAttributeDynamicCodeInfoPlist;
CFStringRef kSecGuestAttributePid;
CFStringRef kSecKeyAlgorithmECDSASignatureDigestX962SHA1;
CFStringRef kSecKeyAlgorithmECDSASignatureDigestX962SHA256;
CFStringRef kSecKeyAlgorithmECDSASignatureDigestX962SHA384;
CFStringRef kSecKeyAlgorithmECDSASignatureDigestX962SHA512;
CFStringRef kSecKeyAlgorithmECDSASignatureMessageX962SHA256;
CFStringRef kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA1;
CFStringRef kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA256;
CFStringRef kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA384;
CFStringRef kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA512;
CFStringRef kSecKeyAlgorithmRSASignatureDigestPSSSHA256;
CFStringRef kSecKeyAlgorithmRSASignatureDigestPSSSHA384;
CFStringRef kSecKeyAlgorithmRSASignatureDigestPSSSHA512;
CFStringRef kSecKeyAlgorithmRSASignatureRaw;
CFStringRef kSecMatchLimit;
CFStringRef kSecMatchLimitAll;
CFStringRef kSecMatchLimitOne;
CFStringRef kSecPolicyAppleSSL;
CFStringRef kSecPolicyOid;
CFStringRef kSecPrivateKeyAttrs;
CFStringRef kSecReturnAttributes;
CFStringRef kSecReturnData;
CFStringRef kSecReturnRef;
CFStringRef kSecUseAuthenticationContext;
CFStringRef kSecUseDataProtectionKeychain;
CFStringRef kSecValueData;
CFStringRef kSecValueRef;

static const struct { CFStringRef *slot; const char *val; } kvals[] = {
    { &kSecAttrAccessControl, "kSecAttrAccessControl" },
    { &kSecAttrAccessGroup, "agrp" },
    { &kSecAttrAccessible, "pdmn" },
    { &kSecAttrAccessibleAfterFirstUnlock, "kSecAttrAccessibleAfterFirstUnlock" },
    { &kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly, "kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly" },
    { &kSecAttrAccessibleWhenUnlocked, "kSecAttrAccessibleWhenUnlocked" },
    { &kSecAttrAccessibleWhenUnlockedThisDeviceOnly, "kSecAttrAccessibleWhenUnlockedThisDeviceOnly" },
    { &kSecAttrAccount, "acct" },
    { &kSecAttrApplicationLabel, "klbl" },
    { &kSecAttrApplicationTag, "atag" },
    { &kSecAttrCanSign, "sign" },
    { &kSecAttrCreationDate, "cdat" },
    { &kSecAttrIsPermanent, "perm" },
    { &kSecAttrKeySizeInBits, "bsiz" },
    { &kSecAttrKeyType, "type" },
    { &kSecAttrKeyTypeECSECPrimeRandom, "kSecAttrKeyTypeECSECPrimeRandom" },
    { &kSecAttrLabel, "labl" },
    { &kSecAttrModificationDate, "mdat" },
    { &kSecAttrService, "svce" },
    { &kSecAttrSynchronizable, "sync" },
    { &kSecAttrTokenID, "kSecAttrTokenID" },
    { &kSecAttrTokenIDSecureEnclave, "kSecAttrTokenIDSecureEnclave" },
    { &kSecAttrType, "type" },
    { &kSecCFErrorResourceAltered, "kSecCFErrorResourceAltered" },
    { &kSecCFErrorResourceMissing, "kSecCFErrorResourceMissing" },
    { &kSecClass, "class" },
    { &kSecClassCertificate, "cert" },
    { &kSecClassGenericPassword, "genp" },
    { &kSecClassIdentity, "idnt" },
    { &kSecClassKey, "keys" },
    { &kSecCodeInfoCertificates, "kSecCodeInfoCertificates" },
    { &kSecCodeInfoFlags, "kSecCodeInfoFlags" },
    { &kSecCodeInfoMainExecutable, "kSecCodeInfoMainExecutable" },
    { &kSecCodeInfoPList, "kSecCodeInfoPList" },
    { &kSecCodeInfoUnique, "kSecCodeInfoUnique" },
    { &kSecCodeSignerEntitlements, "kSecCodeSignerEntitlements" },
    { &kSecCodeSignerFlags, "kSecCodeSignerFlags" },
    { &kSecCodeSignerIdentity, "kSecCodeSignerIdentity" },
    { &kSecGuestAttributeAudit, "kSecGuestAttributeAudit" },
    { &kSecGuestAttributeDynamicCode, "kSecGuestAttributeDynamicCode" },
    { &kSecGuestAttributeDynamicCodeInfoPlist, "kSecGuestAttributeDynamicCodeInfoPlist" },
    { &kSecGuestAttributePid, "kSecGuestAttributePid" },
    { &kSecKeyAlgorithmECDSASignatureDigestX962SHA1, "kSecKeyAlgorithmECDSASignatureDigestX962SHA1" },
    { &kSecKeyAlgorithmECDSASignatureDigestX962SHA256, "kSecKeyAlgorithmECDSASignatureDigestX962SHA256" },
    { &kSecKeyAlgorithmECDSASignatureDigestX962SHA384, "kSecKeyAlgorithmECDSASignatureDigestX962SHA384" },
    { &kSecKeyAlgorithmECDSASignatureDigestX962SHA512, "kSecKeyAlgorithmECDSASignatureDigestX962SHA512" },
    { &kSecKeyAlgorithmECDSASignatureMessageX962SHA256, "kSecKeyAlgorithmECDSASignatureMessageX962SHA256" },
    { &kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA1, "kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA1" },
    { &kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA256, "kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA256" },
    { &kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA384, "kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA384" },
    { &kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA512, "kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA512" },
    { &kSecKeyAlgorithmRSASignatureDigestPSSSHA256, "kSecKeyAlgorithmRSASignatureDigestPSSSHA256" },
    { &kSecKeyAlgorithmRSASignatureDigestPSSSHA384, "kSecKeyAlgorithmRSASignatureDigestPSSSHA384" },
    { &kSecKeyAlgorithmRSASignatureDigestPSSSHA512, "kSecKeyAlgorithmRSASignatureDigestPSSSHA512" },
    { &kSecKeyAlgorithmRSASignatureRaw, "kSecKeyAlgorithmRSASignatureRaw" },
    { &kSecMatchLimit, "m_Limit" },
    { &kSecMatchLimitAll, "m_LimitAll" },
    { &kSecMatchLimitOne, "m_LimitOne" },
    { &kSecPolicyAppleSSL, "kSecPolicyAppleSSL" },
    { &kSecPolicyOid, "kSecPolicyOid" },
    { &kSecPrivateKeyAttrs, "kSecPrivateKeyAttrs" },
    { &kSecReturnAttributes, "r_Attributes" },
    { &kSecReturnData, "r_Data" },
    { &kSecReturnRef, "r_Ref" },
    { &kSecUseAuthenticationContext, "kSecUseAuthenticationContext" },
    { &kSecUseDataProtectionKeychain, "kSecUseDataProtectionKeychain" },
    { &kSecValueData, "v_Data" },
    { &kSecValueRef, "v_Ref" },
};

__attribute__((constructor)) static void sec_constants_init(void)
{
    for (unsigned i = 0; i < sizeof kvals / sizeof kvals[0]; i++)
        *kvals[i].slot = CFStringCreateWithCString(0, kvals[i].val, 0x08000100);
}
