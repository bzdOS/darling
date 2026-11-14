/* OpenDirectory stand-in: real root classes, so a message sent to a class
 * object reaches a method instead of code bytes. Chromium asks ODSession for
 * its default session while it reads the profile; the answer here is nil, which
 * Chromium's callers treat as "no directory service". */
__attribute__((objc_root_class)) @interface ODSession @end
@implementation ODSession
+ (id)defaultSession { return 0; }
+ (id)sessionWithOptions:(id)options error:(id *)error { if (error) *error = 0; return 0; }
@end

__attribute__((objc_root_class)) @interface ODNode @end
@implementation ODNode
+ (id)nodeWithSession:(id)session name:(id)name error:(id *)error { if (error) *error = 0; return 0; }
+ (id)nodeWithSession:(id)session type:(unsigned)type error:(id *)error { if (error) *error = 0; return 0; }
@end

__attribute__((objc_root_class)) @interface ODQuery @end
@implementation ODQuery
+ (id)queryWithNode:(id)node forRecordTypes:(id)types attribute:(id)attribute
          matchType:(unsigned)matchType queryValues:(id)values returnAttributes:(id)returnAttributes
      maximumResults:(long)maxResults error:(id *)error { if (error) *error = 0; return 0; }
@end

__attribute__((objc_root_class)) @interface ODRecord @end
@implementation ODRecord
@end
