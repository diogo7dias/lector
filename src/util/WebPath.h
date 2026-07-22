#pragma once

#include <string>
#include <string_view>

// WebPath — the single safe-path brain shared by the HTTP file server
// (CrossPointWebServer) and the WebDAV handler (WebDAVHandler). Before this
// module the "clean a request path" and "is this path off-limits" rules were
// copied across both files and quietly disagreed: the web server checked only a
// path's last segment while WebDAV checked every segment, and each kept its own
// copy of the reserved-name list. Centralising them here keeps the two servers
// from drifting again and makes the rules host-testable in isolation.
namespace WebPath {

// Canonicalise a raw request/argument path into the form the storage layer
// expects: an absolute path with a single leading '/', duplicate and trailing
// slashes removed, and '..' segments resolved (never escaping root). Empty or
// root inputs collapse to "/". Protocol-specific pre-processing (URL decoding,
// stripping scheme+host from a WebDAV Destination header) stays in the caller;
// this handles only the shared canonicalisation tail.
std::string normalize(std::string_view raw);

// True when `name` is exactly a reserved system directory the browser hides
// (case-sensitive, matching the historical exact-equals behaviour). Directory
// listers use this to skip reserved entries while keeping their own dot-file
// visibility policy (the web UI can reveal dot-files via a setting; WebDAV
// always hides them).
bool isReservedName(std::string_view name);

// True when ANY segment of `path` is off-limits: a segment that begins with '.'
// (hidden) or is a reserved system name. This is the strong per-segment rule;
// every file-operation guard in both servers routes through it, so a hidden
// folder anywhere in the path — not just a hidden leaf — blocks the operation.
bool isProtected(std::string_view path);

}  // namespace WebPath
