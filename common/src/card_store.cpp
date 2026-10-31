#include "card_store.h"

namespace skgauges {

namespace {

// Puts a card's backup back in place of whatever is at its path. True if the
// card is back as it was (or was never touched).
bool restore(CardFs& fs, const CardFiles& card) {
  if (!fs.exists(card.backup)) return true;  // Its file never moved
  std::string old_text;
  if (!fs.read(card.backup, &old_text)) return false;
  if (fs.exists(card.path) && !fs.remove(card.path)) return false;
  if (old_text.empty()) return fs.remove(card.backup);  // It had no file
  return fs.rename(card.backup, card.path);
}

void discard_staging(CardFs& fs, const CardFiles* cards, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (fs.exists(cards[i].staging)) fs.remove(cards[i].staging);
  }
}

// Undoes a replace that failed: every card back to its backup.
void undo(CardFs& fs, const CardFiles* cards, size_t n) {
  for (size_t i = 0; i < n; i++) restore(fs, cards[i]);
  discard_staging(fs, cards, n);
}

}  // namespace

bool replace_cards(CardFs& fs, const CardFiles* cards, const std::string* texts,
                   size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (texts[i].empty()) return false;  // Empty means "no file" in a backup
    if (fs.exists(cards[i].backup)) {
      // Left by an earlier replace: one that finished (its file is there)
      // goes; one that could not be undone is left for recover_cards().
      if (!fs.exists(cards[i].path) || !fs.remove(cards[i].backup)) {
        return false;
      }
    }
  }
  discard_staging(fs, cards, n);

  // 1. The new contents, checked.
  for (size_t i = 0; i < n; i++) {
    std::string back;
    if (!fs.write(cards[i].staging, texts[i]) ||
        !fs.read(cards[i].staging, &back) || back != texts[i]) {
      discard_staging(fs, cards, n);
      return false;
    }
  }
  // 2. The old files out of the way, kept.
  for (size_t i = 0; i < n; i++) {
    const bool kept = fs.exists(cards[i].path)
                          ? fs.rename(cards[i].path, cards[i].backup)
                          : fs.write(cards[i].backup, "");
    if (!kept) {
      undo(fs, cards, n);
      return false;
    }
  }
  // 3. The new files in place.
  for (size_t i = 0; i < n; i++) {
    if (!fs.rename(cards[i].staging, cards[i].path)) {
      undo(fs, cards, n);
      return false;
    }
  }
  // 4. Done: the backups go (one left behind is removed by the next replace
  // of that card, or at boot).
  for (size_t i = 0; i < n; i++) fs.remove(cards[i].backup);
  return true;
}

bool recover_cards(CardFs& fs, const CardFiles* cards, size_t n) {
  bool clean = true;
  bool cut = false;  // Cut before every new file was in place
  for (size_t i = 0; i < n; i++) {
    if (fs.exists(cards[i].staging)) {
      fs.remove(cards[i].staging);
      clean = false;
    }
    if (fs.exists(cards[i].backup) && !fs.exists(cards[i].path)) cut = true;
  }
  for (size_t i = 0; i < n; i++) {
    if (!fs.exists(cards[i].backup)) continue;
    clean = false;
    if (cut) {
      restore(fs, cards[i]);
    } else {
      fs.remove(cards[i].backup);
    }
  }
  return clean;
}

}  // namespace skgauges
