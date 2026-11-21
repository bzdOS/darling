// Placeholder URL loading classes for Foundation.
// macOS binaries (chrome_crashpad_handler) bind these classes to Foundation at load time;
// without them dyld aborts the process ("Symbol not found: _OBJC_CLASS_$_NSMutableURLRequest").
// The real implementations live in src/external/cfnetwork, which is not built on FreeBSD yet.
// Here the classes only exist: creating a request works, any network operation returns nil.
#import <objc/NSObject.h>

@class NSURL, NSString, NSData, NSDictionary;

@interface NSURLRequest : NSObject { NSURL *_url; }
@end
@implementation NSURLRequest
+ (instancetype)requestWithURL:(NSURL *)url { NSURLRequest *r = [[self alloc] init]; r->_url = url; return r; }
- (instancetype)initWithURL:(NSURL *)url { if ((self = [self init])) _url = url; return self; }
- (NSURL *)URL { return _url; }
- (NSString *)HTTPMethod { return nil; }
- (NSData *)HTTPBody { return nil; }
- (NSDictionary *)allHTTPHeaderFields { return nil; }
@end

@interface NSMutableURLRequest : NSURLRequest
@end
@implementation NSMutableURLRequest
- (void)setURL:(NSURL *)url { _url = url; }
- (void)setHTTPMethod:(NSString *)m { (void)m; }
- (void)setHTTPBody:(NSData *)b { (void)b; }
- (void)setHTTPBodyStream:(id)s { (void)s; }
- (void)setValue:(NSString *)v forHTTPHeaderField:(NSString *)f { (void)v; (void)f; }
- (void)addValue:(NSString *)v forHTTPHeaderField:(NSString *)f { (void)v; (void)f; }
- (void)setTimeoutInterval:(double)t { (void)t; }
- (void)setCachePolicy:(unsigned long)p { (void)p; }
@end

@interface NSURLResponse : NSObject
@end
@implementation NSURLResponse
@end

@interface NSHTTPURLResponse : NSURLResponse
@end
@implementation NSHTTPURLResponse
- (long)statusCode { return 0; }
@end

@interface NSURLSessionConfiguration : NSObject
@end
@implementation NSURLSessionConfiguration
+ (NSURLSessionConfiguration *)defaultSessionConfiguration { return [[self alloc] init]; }
+ (NSURLSessionConfiguration *)ephemeralSessionConfiguration { return [[self alloc] init]; }
@end

@interface NSURLSession : NSObject
@end
@implementation NSURLSession
+ (NSURLSession *)sharedSession { return nil; }
+ (NSURLSession *)sessionWithConfiguration:(id)c { (void)c; return nil; }
+ (NSURLSession *)sessionWithConfiguration:(id)c delegate:(id)d delegateQueue:(id)q { (void)c; (void)d; (void)q; return nil; }
@end
