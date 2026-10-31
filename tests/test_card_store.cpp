// Replacing page-card files all or nothing (card_store.h): with a write,
// rename or remove failing at every step, and with a power cut at every
// step followed by the boot's recovery, a swap of two cards ends with both
// old or both new, never one page on both cards or a page lost.

#include <map>
#include <string>

#include "card_store.h"
#include "test.h"

using namespace skgauges;

namespace {

// A filesystem in memory. Only the operations that change it count as
// steps: the step `fail_at` fails once; from the step `cut_at` on, nothing
// changes any more (the power is off) until `power_on()`.
class FakeFs : public CardFs {
 public:
  std::map<std::string, std::string> files;
  int fail_at = -1;
  int cut_at = -1;
  int steps = 0;

  void power_on() {
    cut_at = -1;
    fail_at = -1;
  }

  bool exists(const std::string& path) override {
    return files.count(path) != 0;
  }
  bool read(const std::string& path, std::string* text) override {
    if (powered_off()) return false;
    auto it = files.find(path);
    if (it == files.end()) return false;
    *text = it->second;
    return true;
  }
  bool remove(const std::string& path) override {
    if (!step()) return false;
    return files.erase(path) != 0;
  }
  bool rename(const std::string& from, const std::string& to) override {
    if (!step()) return false;
    if (files.count(from) == 0 || files.count(to) != 0) return false;
    files[to] = files[from];
    files.erase(from);
    return true;
  }
  bool write(const std::string& path, const std::string& text) override {
    if (!step()) {
      // A failed write may leave part of the file.
      if (!powered_off()) files[path] = text.substr(0, text.size() / 2);
      return false;
    }
    files[path] = text;
    return true;
  }

 private:
  bool powered_off() const { return cut_at >= 0 && steps >= cut_at; }
  bool step() {
    if (powered_off()) return false;
    const int n = steps++;
    if (n == fail_at) return false;
    if (cut_at >= 0 && n >= cut_at) return false;
    return true;
  }
};

const CardFiles kCards[2] = {{"/a", "/pgnew0", "/pgold0"},
                             {"/b", "/pgnew1", "/pgold1"}};

// A swap of page A and page B; `b_saved` false: card B has no file yet.
FakeFs before_swap(bool b_saved) {
  FakeFs fs;
  fs.files["/a"] = "A";
  if (b_saved) fs.files["/b"] = "B";
  return fs;
}

std::map<std::string, std::string> old_state(bool b_saved) {
  return before_swap(b_saved).files;
}

std::map<std::string, std::string> swapped_state() {
  return {{"/a", "B2"}, {"/b", "A2"}};
}

const std::string kSwapTexts[2] = {"B2", "A2"};

// How many changing steps a whole swap takes.
int swap_steps(bool b_saved) {
  FakeFs fs = before_swap(b_saved);
  CHECK(replace_cards(fs, kCards, kSwapTexts, 2));
  return fs.steps;
}

}  // namespace

TEST(replace_writes_every_card_and_leaves_nothing_behind) {
  FakeFs fs = before_swap(true);
  CHECK(replace_cards(fs, kCards, kSwapTexts, 2));
  CHECK(fs.files == swapped_state());
  // A card that had no file.
  fs = before_swap(false);
  CHECK(replace_cards(fs, kCards, kSwapTexts, 2));
  CHECK(fs.files == swapped_state());
  // One card on its own.
  fs = before_swap(true);
  const std::string text = "A3";
  CHECK(replace_cards(fs, kCards, &text, 1));
  CHECK(fs.files.at("/a") == "A3" && fs.files.at("/b") == "B");
  CHECK(fs.files.size() == 2);
}

TEST(a_failed_step_changes_nothing) {
  for (bool b_saved : {true, false}) {
    const int total = swap_steps(b_saved);
    for (int k = 0; k < total; k++) {
      FakeFs fs = before_swap(b_saved);
      fs.fail_at = k;
      if (replace_cards(fs, kCards, kSwapTexts, 2)) {
        // Only removing a backup at the end may fail and still succeed;
        // the next boot removes what is left.
        CHECK(fs.files.at("/a") == "B2" && fs.files.at("/b") == "A2");
        recover_cards(fs, kCards, 2);
        CHECK(fs.files == swapped_state());
      } else {
        CHECK(fs.files == old_state(b_saved));
      }
    }
  }
}

TEST(a_power_cut_at_any_step_recovers_to_old_or_new) {
  for (bool b_saved : {true, false}) {
    const int total = swap_steps(b_saved);
    for (int k = 0; k <= total; k++) {
      FakeFs fs = before_swap(b_saved);
      fs.cut_at = k;
      replace_cards(fs, kCards, kSwapTexts, 2);
      fs.power_on();
      recover_cards(fs, kCards, 2);
      CHECK(fs.files == old_state(b_saved) || fs.files == swapped_state());
      // A second boot changes nothing.
      const auto after = fs.files;
      CHECK(recover_cards(fs, kCards, 2));
      CHECK(fs.files == after);
    }
  }
}

TEST(replace_refuses_an_empty_card_and_clears_a_finished_backup) {
  FakeFs fs = before_swap(true);
  const std::string empty;
  CHECK(!replace_cards(fs, kCards, &empty, 1));
  CHECK(fs.files == old_state(true));
  // A backup left by a replace that finished goes with the next one.
  fs.files["/pgold0"] = "A0";
  const std::string text = "A3";
  CHECK(replace_cards(fs, kCards, &text, 1));
  CHECK(fs.files.at("/a") == "A3" && fs.files.count("/pgold0") == 0);
  // One left by a replace that could not be undone stays for the boot.
  fs.files.erase("/a");
  fs.files["/pgold0"] = "A";
  CHECK(!replace_cards(fs, kCards, &text, 1));
  CHECK(!recover_cards(fs, kCards, 2));
  CHECK(fs.files.at("/a") == "A");
}
