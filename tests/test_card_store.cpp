// Replacing page-card files all or nothing (card_store.h): with a read,
// write, rename or remove failing at any one or two steps (so a rollback can
// fail too), and with a power cut at every step, then the boot's recovery, a
// swap of two cards ends with both old or both new, never one page on both
// cards or a page lost.

#include <map>
#include <set>
#include <string>

#include "card_store.h"
#include "test.h"

using namespace skgauges;

namespace {

// A filesystem in memory. Every read, write, rename and remove is a step:
// the steps in `fail` fail; from the step `cut_at` on, nothing works any
// more (the power is off) until `power_on()`.
class FakeFs : public CardFs {
 public:
  std::map<std::string, std::string> files;
  std::set<int> fail;
  int cut_at = -1;
  int steps = 0;

  void power_on() {
    cut_at = -1;
    fail.clear();
  }

  bool exists(const std::string& path) override {
    return files.count(path) != 0;
  }
  bool read(const std::string& path, std::string* text) override {
    if (!step()) return false;
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
    if (fail.count(n) != 0) return false;
    if (cut_at >= 0 && n >= cut_at) return false;
    return true;
  }
};

const std::string kMarker = "/pgtx";
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
  CHECK(replace_cards(fs, kMarker, kCards, kSwapTexts, 2));
  return fs.steps;
}

}  // namespace

TEST(replace_writes_every_card_and_leaves_nothing_behind) {
  FakeFs fs = before_swap(true);
  CHECK(replace_cards(fs, kMarker, kCards, kSwapTexts, 2));
  CHECK(fs.files == swapped_state());
  // A card that had no file.
  fs = before_swap(false);
  CHECK(replace_cards(fs, kMarker, kCards, kSwapTexts, 2));
  CHECK(fs.files == swapped_state());
  // One card on its own.
  fs = before_swap(true);
  const std::string text = "A3";
  CHECK(replace_cards(fs, kMarker, kCards, &text, 1));
  CHECK(fs.files.at("/a") == "A3" && fs.files.at("/b") == "B");
  CHECK(fs.files.size() == 2);
}

// After a failed replace and the boot's recovery: both old.
void check_failed_then_recovered(FakeFs& fs, bool replaced, bool b_saved) {
  fs.power_on();
  recover_cards(fs, kMarker, kCards, 2);
  if (replaced) {
    CHECK(fs.files == swapped_state());
  } else {
    CHECK(fs.files == old_state(b_saved));
  }
  CHECK(recover_cards(fs, kMarker, kCards, 2));  // A second boot: nothing
}

TEST(a_failed_step_changes_nothing) {
  for (bool b_saved : {true, false}) {
    const int total = swap_steps(b_saved);
    for (int k = 0; k < total; k++) {
      FakeFs fs = before_swap(b_saved);
      fs.fail = {k};
      const bool replaced = replace_cards(fs, kMarker, kCards, kSwapTexts, 2);
      if (replaced) {
        // Only removing a backup at the end may fail and still succeed.
        CHECK(fs.files.at("/a") == "B2" && fs.files.at("/b") == "A2");
      } else {
        CHECK(fs.files == old_state(b_saved));  // Put back at once
      }
      check_failed_then_recovered(fs, replaced, b_saved);
    }
  }
}

TEST(a_failed_rollback_is_finished_at_boot) {
  // Two failures: the second can hit the rollback of the first (a read of a
  // backup, a remove, a rename). Until the boot, every replace is refused.
  for (bool b_saved : {true, false}) {
    const int total = swap_steps(b_saved) + 12;  // Room for rollback steps
    for (int j = 0; j < total; j++) {
      for (int k = j + 1; k < total; k++) {
        FakeFs fs = before_swap(b_saved);
        fs.fail = {j, k};
        const bool replaced =
            replace_cards(fs, kMarker, kCards, kSwapTexts, 2);
        if (!replaced && fs.files.count(kMarker) != 0) {
          const std::string text = "A3";
          CHECK(!replace_cards(fs, kMarker, kCards, &text, 1));
        }
        check_failed_then_recovered(fs, replaced, b_saved);
      }
    }
  }
}

TEST(a_power_cut_at_any_step_recovers_to_old_or_new) {
  for (bool b_saved : {true, false}) {
    const int total = swap_steps(b_saved) + 12;
    // A cut at step k, after a failure at step j (none for j = -1): the cut
    // can land in the rollback too.
    for (int j = -1; j < total; j++) {
      for (int k = j + 1; k <= total; k++) {
        FakeFs fs = before_swap(b_saved);
        if (j >= 0) fs.fail = {j};
        fs.cut_at = k;
        replace_cards(fs, kMarker, kCards, kSwapTexts, 2);
        fs.power_on();
        recover_cards(fs, kMarker, kCards, 2);
        CHECK(fs.files == old_state(b_saved) || fs.files == swapped_state());
        // A second boot changes nothing.
        const auto after = fs.files;
        CHECK(recover_cards(fs, kMarker, kCards, 2));
        CHECK(fs.files == after);
      }
    }
  }
}

TEST(replace_refuses_an_empty_card_and_clears_a_finished_backup) {
  FakeFs fs = before_swap(true);
  const std::string empty;
  CHECK(!replace_cards(fs, kMarker, kCards, &empty, 1));
  CHECK(fs.files == old_state(true));
  // A backup left by a replace that finished goes with the next one.
  fs.files["/pgold0"] = "A0";
  const std::string text = "A3";
  CHECK(replace_cards(fs, kMarker, kCards, &text, 1));
  CHECK(fs.files.at("/a") == "A3" && fs.files.count("/pgold0") == 0);
  // An unfinished rollback (the marker is there): refused until the boot
  // puts the card back.
  fs.files["/pgtx"] = "replace";
  fs.files["/pgold0"] = "A";
  fs.files["/a"] = "A3";
  CHECK(!replace_cards(fs, kMarker, kCards, &text, 1));
  CHECK(!recover_cards(fs, kMarker, kCards, 2));
  CHECK(fs.files == old_state(true));
}
