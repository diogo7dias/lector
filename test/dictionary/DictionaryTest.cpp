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
#include "DictionaryRegistry.h"

namespace DictionaryRegistry {
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
  void writeDictionary(const std::vector<Entry>& entries, const std::string& ifoType,
                       const std::vector<Synonym>& synonyms = {}) {
    const std::string base = root + "/dictionaries/test/test";
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
    ifo << "StarDict's dict ifo file\nversion=2.4.2\nbookname=test\nidxoffsetbits=32\n";
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
