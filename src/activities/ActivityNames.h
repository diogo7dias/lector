#pragma once

// Activity names that code branches on. Each screen passes its constant to the
// Activity constructor and every check compares against the same constant, so
// renaming one is a compile error rather than a silently missed match. The string
// values show up in logs and perf records: change a value only on purpose.
namespace activity_name {

inline constexpr char kHome[] = "Home";
inline constexpr char kFileBrowser[] = "FileBrowser";
inline constexpr char kSettings[] = "Settings";
inline constexpr char kQuotesViewer[] = "QuotesViewer";
inline constexpr char kInstalledFonts[] = "InstalledFonts";
inline constexpr char kFontPicker[] = "FontPicker";
inline constexpr char kFontDownload[] = "FontDownload";
inline constexpr char kReader[] = "Reader";
inline constexpr char kEpubReader[] = "EpubReader";
inline constexpr char kTxtReader[] = "TxtReader";
inline constexpr char kXtcReader[] = "XtcReader";
inline constexpr char kCrossPointWebServer[] = "CrossPointWebServer";
inline constexpr char kOpdsBookBrowser[] = "OpdsBookBrowser";
inline constexpr char kOpdsServerList[] = "OpdsServerList";
inline constexpr char kWifiSelection[] = "WifiSelection";
inline constexpr char kCalibreConnect[] = "CalibreConnect";
inline constexpr char kOtaUpdate[] = "OtaUpdate";
inline constexpr char kNearbyFileTransfer[] = "NearbyFileTransfer";
inline constexpr char kNearbyPositionSync[] = "NearbyPositionSync";
inline constexpr char kKOReaderSync[] = "KOReaderSync";
inline constexpr char kKOReaderAuth[] = "KOReaderAuth";
inline constexpr char kSleep[] = "Sleep";
inline constexpr char kBoot[] = "Boot";

}  // namespace activity_name
