#include <Arduino.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "DictZip.h"
#include "Dictionary.h"
#include "DictionaryFailure.h"
#include "DictionaryRegistry.h"
#include "VocabExport.h"

namespace DictionaryRegistry {
static std::vector<DictionaryEntry> g_stubDictionaries;
void discover(std::vector<DictionaryEntry>& out) { out = g_stubDictionaries; }
bool resolveBasePath(const char* folderName, std::string& basePathOut) {
  basePathOut = std::string("/dictionaries/") + folderName + "/" + folderName;
  return true;
}
}  // namespace DictionaryRegistry

namespace DictZip {
bool extractEntry(const char*, uint32_t, uint32_t, HalFile&, ExtractError* error) {
  if (error) *error = ExtractError::ReadError;
  return false;
}
}  // namespace DictZip

namespace {

struct Entry {
  std::string word;
  std::string definition;
};
struct Synonym {
  std::string word;
  uint32_t ordinal;
};

void appendBe32(std::string& out, const uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<char>(value >> shift));
}

class DictionaryTest : public testing::Test {
 protected:
  void SetUp() override {
    root = (std::filesystem::temp_directory_path() / ("lector-dict-" + std::to_string(::getpid()) + "-" +
                                                      testing::UnitTest::GetInstance()->current_test_info()->name()))
               .string();
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root + "/dictionaries/test");
    testCardRoot = root;
    ESP.maxAllocHeap = 1024 * 1024;
  }
  void TearDown() override { std::filesystem::remove_all(root); }

  // Entries must already be in StarDict order. An empty ifoType leaves sametypesequence out.
  void writeDictionaryIn(const std::string& folder, const std::vector<Entry>& entries, const std::string& ifoType,
                         const std::vector<Synonym>& synonyms = {}) {
    std::filesystem::create_directories(root + "/dictionaries/" + folder);
    const std::string base = root + "/dictionaries/" + folder + "/" + folder;
    std::string idx, dict;
    for (const auto& entry : entries) {
      idx += entry.word;
      idx.push_back('\0');
      appendBe32(idx, static_cast<uint32_t>(dict.size()));
      appendBe32(idx, static_cast<uint32_t>(entry.definition.size()));
      dict += entry.definition;
    }
    std::ofstream(base + ".idx", std::ios::binary) << idx;
    std::ofstream(base + ".dict", std::ios::binary) << dict;
    std::ofstream ifo(base + ".ifo");
    ifo << "StarDict's dict ifo file\nversion=2.4.2\nbookname=" << folder << "\nidxoffsetbits=32\n";
    if (!ifoType.empty()) ifo << "sametypesequence=" << ifoType << "\n";
    ifo.close();
    if (synonyms.empty()) return;
    std::string syn;
    for (const auto& synonym : synonyms) {
      syn += synonym.word;
      syn.push_back('\0');
      appendBe32(syn, synonym.ordinal);
    }
    std::ofstream(base + ".syn", std::ios::binary) << syn;
  }

  void writeDictionary(const std::vector<Entry>& entries, const std::string& ifoType,
                       const std::vector<Synonym>& synonyms = {}) {
    writeDictionaryIn("test", entries, ifoType, synonyms);
  }

  void openAndIndex() {
    ASSERT_TRUE(dictionary.open("test"));
    ASSERT_TRUE(dictionary.buildIndex());
  }

  std::string root;
  Dictionary dictionary;
  std::string definition;
  std::string headword;
  Dictionary::LookupResult result = Dictionary::LookupResult::NotFound;
};

std::string fillerWord(const int i) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%c%05d", i < 300 ? 'a' : 'z', i);
  return buf;
}

}  // namespace

TEST_F(DictionaryTest, PrepareRemembersItsAnswerUntilTheIndexIsBuilt) {
  writeDictionary({{"cat", "an animal"}}, "m");
  EXPECT_EQ(dictionary.prepare("test"), Dictionary::Readiness::NeedsIndex);
  EXPECT_EQ(dictionary.prepare("test"), Dictionary::Readiness::NeedsIndex);
  ASSERT_TRUE(dictionary.buildIndex());
  EXPECT_EQ(dictionary.prepare("test"), Dictionary::Readiness::Ready);
}

TEST_F(DictionaryTest, PrepareReportsAMissingDictionaryAsUnavailable) {
  EXPECT_EQ(dictionary.prepare("absent"), Dictionary::Readiness::Unavailable);
  EXPECT_EQ(dictionary.prepare("absent"), Dictionary::Readiness::Unavailable);
}

TEST_F(DictionaryTest, AFailedReopenDoesNotKeepTheOldAnswer) {
  writeDictionary({{"cat", "an animal"}}, "m");
  openAndIndex();
  ASSERT_EQ(dictionary.prepare("test"), Dictionary::Readiness::Ready);
  EXPECT_FALSE(dictionary.open("absent"));
  EXPECT_EQ(dictionary.prepare("absent"), Dictionary::Readiness::Unavailable);
}

TEST_F(DictionaryTest, OneEntryKeepsItsDefinitionUntouched) {
  writeDictionary({{"bank", "a river side"}, {"cat", "an animal"}}, "m");
  openAndIndex();
  ASSERT_TRUE(dictionary.lookup("cat", definition, headword, &result));
  EXPECT_EQ(result, Dictionary::LookupResult::Found);
  EXPECT_EQ(definition, "an animal");
  EXPECT_EQ(headword, "cat");
}

TEST_F(DictionaryTest, EveryEntryOfAHomographIsShownLabelled) {
  writeDictionary({{"bank", "a river side"}, {"bank", "money house"}, {"bank", "to tilt"}, {"cat", "an animal"}}, "m");
  openAndIndex();
  ASSERT_TRUE(dictionary.lookup("bank", definition, headword, &result));
  EXPECT_EQ(definition, "bank\na river side<p>bank\nmoney house<p>bank\nto tilt");
  EXPECT_EQ(headword, "bank");
}

TEST_F(DictionaryTest, HomographRunAcrossAnIndexSampleKeepsItsFirstEntries) {
  // Samples fall on every 256th entry; the run sits on both sides of the one at entry 256.
  std::vector<Entry> entries;
  for (int i = 0; i < 254; ++i) entries.push_back({fillerWord(i), "filler"});
  for (const char* meaning : {"one", "two", "three", "four"}) entries.push_back({"bank", meaning});
  for (int i = 300; i < 600; ++i) entries.push_back({fillerWord(i), "filler"});
  writeDictionary(entries, "m");
  openAndIndex();
  ASSERT_TRUE(dictionary.lookup("bank", definition, headword, &result));
  EXPECT_EQ(definition, "bank\none<p>bank\ntwo<p>bank\nthree<p>bank\nfour");
}

TEST_F(DictionaryTest, EmptyEntryIsLeftOutOfAHomograph) {
  writeDictionary({{"bank", "same"}, {"bank", ""}}, "m");
  openAndIndex();
  ASSERT_TRUE(dictionary.lookup("bank", definition, headword, &result));
  EXPECT_EQ(definition, "same");
}

TEST_F(DictionaryTest, SynonymsPointingAtOneEntryShowItOnce) {
  writeDictionary({{"go", "to move"}, {"run", "to race"}}, "m", {{"went", 0}, {"went", 0}});
  openAndIndex();
  ASSERT_TRUE(dictionary.lookup("went", definition, headword, &result));
  EXPECT_EQ(definition, "to move");
  EXPECT_EQ(headword, "go");
}

TEST_F(DictionaryTest, HtmlLabelsAreEscapedHeadings) {
  writeDictionary({{"a&b", "<i>one</i>"}, {"a&b", "<i>two</i>"}}, "h");
  openAndIndex();
  EXPECT_TRUE(dictionary.definitionsAreHtml());
  ASSERT_TRUE(dictionary.lookup("a&b", definition, headword, &result));
  EXPECT_EQ(definition, "<h2>a&amp;b</h2><i>one</i><h2>a&amp;b</h2><i>two</i>");
}

TEST_F(DictionaryTest, SynonymWithOneHeadwordOpensThatHeadword) {
  writeDictionary({{"go", "to move"}, {"run", "to race"}}, "m", {{"went", 0}});
  openAndIndex();
  ASSERT_TRUE(dictionary.lookup("went", definition, headword, &result));
  EXPECT_EQ(definition, "to move");
  EXPECT_EQ(headword, "go");
}

TEST_F(DictionaryTest, SynonymWithSeveralHeadwordsShowsThemAll) {
  writeDictionary({{"go", "to move"}, {"run", "to race"}}, "m", {{"ran", 0}, {"ran", 1}});
  openAndIndex();
  ASSERT_TRUE(dictionary.lookup("ran", definition, headword, &result));
  EXPECT_EQ(definition, "go\nto move<p>run\nto race");
  EXPECT_EQ(headword, "ran");
}

TEST_F(DictionaryTest, DefinitionsPastTheByteCapAreLeftOut) {
  const std::string large(Dictionary::MAX_DEFINITION_BYTES - 100, 'x');
  writeDictionary({{"bank", large}, {"bank", std::string(200, 'y')}, {"bank", "short"}}, "m");
  openAndIndex();
  ASSERT_TRUE(dictionary.lookup("bank", definition, headword, &result));
  EXPECT_EQ(definition, "bank\n" + large + "<p>bank\nshort");
}

TEST_F(DictionaryTest, LowHeapIsReportedBeforeCombining) {
  writeDictionary({{"bank", "one"}, {"bank", "two"}}, "m");
  openAndIndex();
  ESP.maxAllocHeap = 100;
  EXPECT_FALSE(dictionary.lookup("bank", definition, headword, &result));
  EXPECT_EQ(result, Dictionary::LookupResult::LowMemory);
}

TEST(DictionaryCleanWord, StripsCjkBracketsAndQuotes) {
  EXPECT_EQ(Dictionary::cleanWord("\xE3\x80\x8C\xEC\x82\xAC\xEA\xB8\xB0\xE3\x80\x8D"), "\xEC\x82\xAC\xEA\xB8\xB0");
  EXPECT_EQ(Dictionary::cleanWord("\xE3\x80\x8A\xEC\x82\xAC\xEA\xB8\xB0\xE3\x80\x8B\xEF\xBC\x8C"),
            "\xEC\x82\xAC\xEA\xB8\xB0");
  EXPECT_EQ(Dictionary::cleanWord("\xE3\x80\x8E\xE3\x80\x8C"), "");
}

TEST(DictionaryCleanWord, KeepsWordsAndInnerPunctuation) {
  EXPECT_EQ(Dictionary::cleanWord("\xEC\x82\xAC\xEA\xB8\xB0"), "\xEC\x82\xAC\xEA\xB8\xB0");
  EXPECT_EQ(Dictionary::cleanWord("\xC3\xA9t\xC3\xA9"), "\xC3\xA9t\xC3\xA9");
  EXPECT_EQ(Dictionary::cleanWord("(can't)"), "can't");
  EXPECT_EQ(Dictionary::cleanWord("garage\xE2\x80\x9D"), "garage");
}

TEST(DictionaryCleanWord, StripsHebrewNiqqudAndArabicVowelMarks) {
  EXPECT_EQ(Dictionary::cleanWord("\xD7\xA9\xD6\xB8\xD7\x9C\xD7\x95\xD6\xB9\xD7\x9D"),
            "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D");
  // Cantillation and the Unicode 18.0 marks U+05C8/U+05C9 go too.
  EXPECT_EQ(Dictionary::cleanWord("\xD7\x90\xD6\x91\xD7\x91"), "\xD7\x90\xD7\x91");
  EXPECT_EQ(Dictionary::cleanWord("\xD7\x90\xD7\x88\xD7\x89\xD7\x91"), "\xD7\x90\xD7\x91");
  // Fatha, kasra, sukun and superscript alef go; shaddah stays significant.
  EXPECT_EQ(Dictionary::cleanWord("\xD8\xB4\xD9\x8E\xD9\x83\xD9\x90\xD9\x84"), "\xD8\xB4\xD9\x83\xD9\x84");
  EXPECT_EQ(Dictionary::cleanWord("\xD8\xB4\xD9\xB0\xD9\x83"), "\xD8\xB4\xD9\x83");
  EXPECT_EQ(Dictionary::cleanWord("\xD8\xB4\xD9\x91\xD9\x83"), "\xD8\xB4\xD9\x91\xD9\x83");
  // A mark between a letter and trailing punctuation must not hide the punctuation.
  EXPECT_EQ(Dictionary::cleanWord("\xD7\x90\xD6\xB8."), "\xD7\x90");
}

TEST_F(DictionaryTest, InlineTextFieldIsDecodedAndEscaped) {
  writeDictionary({{"bank", std::string("m<a&b>\0", 7)}}, "");
  openAndIndex();
  EXPECT_TRUE(dictionary.definitionsAreHtml());
  ASSERT_TRUE(dictionary.lookup("bank", definition, headword, &result));
  EXPECT_EQ(definition, "<pre>&lt;a&amp;b&gt;</pre>");
}

TEST_F(DictionaryTest, InlineMarkupFieldsPassThroughAndBinaryFieldsAreSkipped) {
  std::string fields("h<b>one</b>\0", 12);
  fields += "W";
  appendBe32(fields, 3);
  fields += "abc";
  fields += std::string("x<i>two</i>\0", 12);
  writeDictionary({{"bank", fields}}, "");
  openAndIndex();
  ASSERT_TRUE(dictionary.lookup("bank", definition, headword, &result));
  EXPECT_EQ(definition, "<b>one</b><i>two</i>");
}

TEST_F(DictionaryTest, InlineFieldsOfSeveralEntriesCombine) {
  writeDictionary({{"bank", std::string("mone\0", 5)}, {"bank", std::string("htwo\0", 5)}}, "");
  openAndIndex();
  ASSERT_TRUE(dictionary.lookup("bank", definition, headword, &result));
  EXPECT_EQ(definition, "<h2>bank</h2><pre>one</pre><h2>bank</h2>two");
}

TEST_F(DictionaryTest, DamagedInlineEntryIsAReadError) {
  writeDictionary({{"bank", std::string("\x01junk", 5)}}, "");
  openAndIndex();
  EXPECT_FALSE(dictionary.lookup("bank", definition, headword, &result));
  EXPECT_EQ(result, Dictionary::LookupResult::ReadError);
}

TEST_F(DictionaryTest, FixedTypeSequenceLeftAlone) {
  writeDictionary({{"bank", "mnot a type byte"}}, "m");
  openAndIndex();
  EXPECT_FALSE(dictionary.definitionsAreHtml());
  ASSERT_TRUE(dictionary.lookup("bank", definition, headword, &result));
  EXPECT_EQ(definition, "mnot a type byte");
}

TEST_F(DictionaryTest, LongIfoDescriptionDoesNotHideTheTypeSequence) {
  writeDictionary({{"bank", "plain"}}, "");
  std::ofstream ifo(root + "/dictionaries/test/test.ifo");
  ifo << "StarDict's dict ifo file\nversion=2.4.2\ndescription=" << std::string(4000, 'd')
      << "\nsametypesequence=m\nidxoffsetbits=32\n";
  ifo.close();
  openAndIndex();
  EXPECT_FALSE(dictionary.definitionsAreHtml());
  ASSERT_TRUE(dictionary.lookup("bank", definition, headword, &result));
  EXPECT_EQ(definition, "plain");
}

// A touch selection looks up the passage as the quote joins it: words spaced, edge
// punctuation still attached.
TEST_F(DictionaryTest, PhraseWithSpacesAndEdgePunctuationFindsItsHeadword) {
  writeDictionary({{"kick", "strike"}, {"kick the bucket", "die"}, {"kickback", "a bribe"}}, "m");
  openAndIndex();
  ASSERT_TRUE(
      dictionary.lookup("\xE2\x80\x9C"
                        "kick the bucket,",
                        definition, headword, &result));
  EXPECT_EQ(definition, "die");
  EXPECT_EQ(headword, "kick the bucket");
}

TEST_F(DictionaryTest, ElidedPrefixIsDroppedAfterTheWholeWordMisses) {
  writeDictionary({{"homme", "man"}, {"il", "he"}, {"l'homme", "whole"}}, "m");
  openAndIndex();
  ASSERT_TRUE(dictionary.lookup("l'homme", definition, headword, &result));
  EXPECT_EQ(headword, "l'homme");  // the whole word still wins

  writeDictionary({{"homme", "man"}, {"il", "he"}}, "m");
  openAndIndex();
  ASSERT_TRUE(dictionary.lookup("l'homme", definition, headword, &result));
  EXPECT_EQ(headword, "homme");
  ASSERT_TRUE(dictionary.lookup("qu'il", definition, headword, &result));
  EXPECT_EQ(headword, "il");
  // Curly apostrophe U+2019.
  ASSERT_TRUE(dictionary.lookup("l\xE2\x80\x99homme", definition, headword, &result));
  EXPECT_EQ(headword, "homme");
}

TEST_F(DictionaryTest, ElisionTriesTheLongestRemainderFirstAndSkipsLongPrefixes) {
  writeDictionary({{"aujourd'hui", "today"}, {"hui", "x"}}, "m");
  openAndIndex();
  ASSERT_TRUE(dictionary.lookup("l'aujourd'hui", definition, headword, &result));
  EXPECT_EQ(headword, "aujourd'hui");

  writeDictionary({{"word", "w"}}, "m");
  openAndIndex();
  EXPECT_FALSE(dictionary.lookup("abcdefgh'word", definition, headword, &result));  // prefix over 7 bytes
  EXPECT_EQ(result, Dictionary::LookupResult::NotFound);
}

TEST_F(DictionaryTest, PrepareSwitchesFoldersOnSameDictionaryObject) {
  writeDictionaryIn("primary", {{"alpha", "first"}}, "m");
  writeDictionaryIn("secondary", {{"beta", "second"}}, "m");

  ASSERT_EQ(dictionary.prepare("primary"), Dictionary::Readiness::NeedsIndex);
  ASSERT_TRUE(dictionary.buildIndex());
  ASSERT_TRUE(dictionary.lookup("alpha", definition, headword, &result));
  EXPECT_EQ(definition, "first");

  // Reusing the same Dictionary object for another folder must reopen and re-check its index.
  ASSERT_EQ(dictionary.prepare("secondary"), Dictionary::Readiness::NeedsIndex);
  ASSERT_TRUE(dictionary.buildIndex());
  ASSERT_TRUE(dictionary.lookup("beta", definition, headword, &result));
  EXPECT_EQ(definition, "second");
}

TEST_F(DictionaryTest, FallbackLookupSearchesOtherDictionariesOnNotFound) {
  writeDictionaryIn("primary", {{"alpha", "first"}}, "m");
  writeDictionaryIn("secondary", {{"beta", "second"}}, "m");
  writeDictionaryIn("tertiary", {{"gamma", "third"}}, "m");
  DictionaryRegistry::g_stubDictionaries = {
      {"primary", "primary"}, {"secondary", "secondary"}, {"tertiary", "tertiary"}};

  auto noopBusy = [](void*, StrId) {};
  dict_failure::Message msg;

  // Toggle off: misses on primary without searching fallbacks.
  EXPECT_FALSE(dict_failure::lookUpWithFallbacks(dictionary, "primary", false, "gamma", definition, headword, msg,
                                                 noopBusy, nullptr));
  EXPECT_EQ(msg.id, StrId::STR_DICT_NOT_FOUND);
  EXPECT_FALSE(msg.isError);

  // Toggle on: falls through primary -> secondary -> tertiary reusing the same Dictionary object.
  ASSERT_TRUE(dict_failure::lookUpWithFallbacks(dictionary, "primary", true, "gamma", definition, headword, msg,
                                                noopBusy, nullptr));
  EXPECT_EQ(headword, "gamma");
  EXPECT_EQ(definition, "third");

  // Subsequent lookup still checks primary first and finds primary entries cleanly.
  ASSERT_TRUE(dict_failure::lookUpWithFallbacks(dictionary, "primary", true, "alpha", definition, headword, msg,
                                                noopBusy, nullptr));
  EXPECT_EQ(headword, "alpha");
  EXPECT_EQ(definition, "first");

  // Genuine miss across all dictionaries reports NotFound.
  EXPECT_FALSE(dict_failure::lookUpWithFallbacks(dictionary, "primary", true, "missing", definition, headword, msg,
                                                 noopBusy, nullptr));
  EXPECT_EQ(msg.id, StrId::STR_DICT_NOT_FOUND);
  EXPECT_FALSE(msg.isError);
}

TEST_F(DictionaryTest, FallbackLookupDoesNotHidePrimaryUnavailableError) {
  writeDictionaryIn("secondary", {{"beta", "second"}}, "m");
  DictionaryRegistry::g_stubDictionaries = {{"secondary", "secondary"}};

  auto noopBusy = [](void*, StrId) {};
  dict_failure::Message msg;
  EXPECT_FALSE(dict_failure::lookUpWithFallbacks(dictionary, "missing_primary", true, "beta", definition, headword, msg,
                                                 noopBusy, nullptr));
  EXPECT_TRUE(msg.isError);
  EXPECT_EQ(msg.id, StrId::STR_DICT_ERROR);
}

TEST(VocabExport, FormatsSanitizedAnkiTsvRowAndStripsHtml) {
  const std::string snippet = vocab_export::makeSnippet("<p><b>quick</b>\tbrown\nfox</p>", true);
  EXPECT_EQ("quick brown fox", snippet);

  const std::string row = vocab_export::formatTsvRow("ran\tfast", "run\n", snippet, "Dune\t1", "2026-11-28");
  EXPECT_EQ("ran fast\trun\tquick brown fox\tDune 1\t2026-11-28\n", row);

  // Snippet caps cleanly on UTF-8 boundary without splitting multi-byte codepoints.
  std::string utf8;
  for (int i = 0; i < 80; ++i) utf8 += "\xC3\xA9";  // 160 bytes of 'é'
  const std::string capped = vocab_export::makeSnippet(utf8, false, 11);
  EXPECT_EQ(10u, capped.size());  // 5 complete 2-byte codepoints, never 11
}
