/* Constant-only CFNetwork.h stub.
 *
 * CFNetwork.framework does not exist in this project's vendored tree at
 * all (no submodule, no symlink anywhere resolves to one — a structural
 * gap, same class as CoreGraphics — see tests/vendor/fakesdk/CoreGraphics/).
 * Foundation/NSURLError.h pulls this in purely to reuse these error-code
 * *values* for its own NSURLError* enum (NSURLErrorCancelled =
 * kCFURLErrorCancelled, etc.) — no CFNetwork function is ever called here.
 * These are Apple's real, public, unchanged-for-years CFNetworkErrors
 * values (documented in CFNetwork/CFNetworkErrors.h). */
#ifndef CFNETWORK_H_
#define CFNETWORK_H_

enum {
    kCFURLErrorCancelled                        = -999,
    kCFURLErrorBadURL                           = -1000,
    kCFURLErrorTimedOut                         = -1001,
    kCFURLErrorUnsupportedURL                    = -1002,
    kCFURLErrorCannotFindHost                    = -1003,
    kCFURLErrorCannotConnectToHost                = -1004,
    kCFURLErrorNetworkConnectionLost              = -1005,
    kCFURLErrorDNSLookupFailed                    = -1006,
    kCFURLErrorHTTPTooManyRedirects               = -1007,
    kCFURLErrorResourceUnavailable                = -1008,
    kCFURLErrorNotConnectedToInternet             = -1009,
    kCFURLErrorRedirectToNonExistentLocation      = -1010,
    kCFURLErrorBadServerResponse                  = -1011,
    kCFURLErrorUserCancelledAuthentication        = -1012,
    kCFURLErrorUserAuthenticationRequired         = -1013,
    kCFURLErrorZeroByteResource                   = -1014,
    kCFURLErrorCannotDecodeRawData                = -1015,
    kCFURLErrorCannotDecodeContentData            = -1016,
    kCFURLErrorCannotParseResponse                = -1017,
    kCFURLErrorInternationalRoamingOff            = -1018,
    kCFURLErrorCallIsActive                       = -1019,
    kCFURLErrorDataNotAllowed                     = -1020,
    kCFURLErrorRequestBodyStreamExhausted         = -1021,
    kCFURLErrorFileDoesNotExist                   = -1100,
    kCFURLErrorFileIsDirectory                    = -1101,
    kCFURLErrorNoPermissionsToReadFile            = -1102,
    kCFURLErrorDataLengthExceedsMaximum           = -1103,
    kCFURLErrorSecureConnectionFailed             = -1200,
    kCFURLErrorServerCertificateHasBadDate        = -1201,
    kCFURLErrorServerCertificateUntrusted         = -1202,
    kCFURLErrorServerCertificateHasUnknownRoot    = -1203,
    kCFURLErrorServerCertificateNotYetValid       = -1204,
    kCFURLErrorClientCertificateRejected          = -1205,
    kCFURLErrorClientCertificateRequired          = -1206,
    kCFURLErrorCannotLoadFromNetwork              = -2000,
    kCFURLErrorCannotCreateFile                   = -3000,
    kCFURLErrorCannotOpenFile                     = -3001,
    kCFURLErrorCannotCloseFile                    = -3002,
    kCFURLErrorCannotWriteToFile                  = -3003,
    kCFURLErrorCannotRemoveFile                   = -3004,
    kCFURLErrorCannotMoveFile                     = -3005,
    kCFURLErrorDownloadDecodingFailedMidStream    = -3006,
    kCFURLErrorDownloadDecodingFailedToComplete   = -3007,
};

#endif /* CFNETWORK_H_ */
