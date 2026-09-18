// Source audit: a TLS transfer in the font flow must run with the framebuffer lent.
//
// With WiFi up on an ESP32-C3 the heap cannot hold a 16 KB TLS record buffer
// (16640 bytes contiguous), and GitHub's release-asset host ignores the 2 KB
// max_fragment_length the reader asks for, so every fetch from it needs one.
// GfxRenderer::FrameBufferLoan plus tls_scratch::Session route that allocation
// into the framebuffer instead, and tls_heap::canStartTls refuses a handshake
// the heap still cannot carry -- wolfSSL on a starved heap does not fail
// cleanly, it spins until the watchdog resets the device.
//
// Three call sites got that treatment one at a time as each was found to fail
// on an X3/X4 (the per-file font download, the OTA release check, the font
// manifest). This audit is what stops a fourth from being written without it.
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef TLS_LOAN_AUDIT_SOURCES
#error "TLS_LOAN_AUDIT_SOURCES must be defined by the build system"
#endif

namespace {

struct Function {
  std::string path;
  std::string signature;
  int startLine = 0;
  bool fetches = false;  // calls HttpDownloader
  bool lends = false;    // constructs a GfxRenderer::FrameBufferLoan
  bool scratch = false;  // constructs a tls_scratch::Session
  bool gates = false;    // consults tls_heap::canStartTls
  int lendLine = 0;
  int scratchLine = 0;
};

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

// A definition starts in column 0 and names the class; it ends at the closing
// brace in column 0. Good enough for this file, and it fails loudly (no
// functions found) rather than quietly if the style ever changes.
std::vector<Function> functions(const std::string& path, const char* className) {
  std::vector<Function> found;
  std::ifstream file(path);
  EXPECT_TRUE(file.is_open()) << "cannot open " << path;
  std::string line;
  int number = 0;
  bool inside = false;
  while (std::getline(file, line)) {
    number++;
    if (!inside) {
      if (!line.empty() && line[0] != ' ' && line[0] != '}' && line[0] != '#' && line[0] != '/' &&
          contains(line, className) && contains(line, "(")) {
        found.push_back(Function{path, line, number, false, false, false, false, 0, 0});
        inside = true;
      }
      continue;
    }
    if (line == "}") {
      inside = false;
      continue;
    }
    Function& fn = found.back();
    if (contains(line, "HttpDownloader::downloadToFile(") || contains(line, "HttpDownloader::fetchUrl(")) {
      fn.fetches = true;
    }
    if (contains(line, "GfxRenderer::FrameBufferLoan") && !contains(line, "//")) {
      fn.lends = true;
      if (fn.lendLine == 0) fn.lendLine = number;
    }
    if (contains(line, "tls_scratch::Session") && !contains(line, "//")) {
      fn.scratch = true;
      if (fn.scratchLine == 0) fn.scratchLine = number;
    }
    if (contains(line, "tls_heap::canStartTls")) fn.gates = true;
  }
  return found;
}

std::vector<Function> auditedFunctions() {
  std::vector<Function> all;
  std::stringstream paths(TLS_LOAN_AUDIT_SOURCES);
  std::string path;
  while (std::getline(paths, path, '|')) {
    if (path.empty()) continue;
    const std::string className = "FontDownloadActivity::";
    for (const Function& fn : functions(path, className.c_str())) all.push_back(fn);
  }
  return all;
}

}  // namespace

TEST(TlsLoanAudit, TheAuditCanSeeTheSource) {
  const auto all = auditedFunctions();
  ASSERT_FALSE(all.empty()) << "no function definitions found — the audit is not reading the source";
  int fetching = 0;
  for (const auto& fn : all) {
    if (fn.fetches) fetching++;
  }
  // The manifest fetch and the per-file download. If this ever reads 0 the
  // audit has stopped matching the code and everything below passes vacuously.
  EXPECT_EQ(fetching, 2) << "the number of TLS fetches in the font flow changed; re-check the audit";
}

TEST(TlsLoanAudit, EveryFetchLendsTheFramebuffer) {
  for (const auto& fn : auditedFunctions()) {
    if (!fn.fetches) continue;
    EXPECT_TRUE(fn.lends) << fn.path << ":" << fn.startLine << " fetches over TLS without a "
                          << "GfxRenderer::FrameBufferLoan; the 16640-byte record buffer will come off the heap";
    EXPECT_TRUE(fn.scratch) << fn.path << ":" << fn.startLine << " lends the framebuffer but never constructs a "
                            << "tls_scratch::Session, so wolfSSL never sees the lent block";
  }
}

TEST(TlsLoanAudit, EveryFetchGatesTheHandshake) {
  for (const auto& fn : auditedFunctions()) {
    if (!fn.fetches) continue;
    EXPECT_TRUE(fn.gates) << fn.path << ":" << fn.startLine << " starts a handshake without tls_heap::canStartTls; "
                          << "on a starved heap wolfSSL hangs until the watchdog resets the device";
  }
}

TEST(TlsLoanAudit, TheScratchSessionComesAfterTheLoan) {
  for (const auto& fn : auditedFunctions()) {
    if (!fn.scratch) continue;
    ASSERT_TRUE(fn.lends) << fn.path << ":" << fn.scratchLine << " opens a scratch session with no loan to claim";
    EXPECT_LT(fn.lendLine, fn.scratchLine)
        << fn.path << ":" << fn.scratchLine << " constructs the scratch session before the loan, so claim() finds "
        << "nothing and every allocation falls back to the heap";
  }
}
