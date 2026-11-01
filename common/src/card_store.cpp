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

// Every card back to its backup, then the marker goes; it stays if any card
// could not be put back, for recover_cards() to finish.
bool roll_back(CardFs& fs, const std::string& marker, const CardFiles* cards,
               size_t n) {
  bool all_back = true;
  for (size_t i = 0; i < n; i++) {
    if (!restore(fs, cards[i])) all_back = false;
  }
  discard_staging(fs, cards, n);
  if (!all_back) return false;
  return !fs.exists(marker) || fs.remove(marker);
}

}  // namespace

bool replace_cards(CardFs& fs, const std::string& marker,
                   const CardFiles* cards, const std::string* texts,
                   size_t n) {
  // A rollback that did not finish: nothing more until the boot finishes it.
  if (fs.exists(marker)) return false;
  for (size_t i = 0; i < n; i++) {
    if (texts[i].empty()) return false;  // Empty means "no file" in a backup
    // Without the marker, a backup is left by a replace that finished.
    if (fs.exists(cards[i].backup) && !fs.remove(cards[i].backup)) {
      return false;
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
  // 2. From here, a failure puts every card back.
  if (!fs.write(marker, "replace")) {
    if (fs.exists(marker)) fs.remove(marker);  // Nothing has moved yet
    discard_staging(fs, cards, n);
    return false;
  }
  // 3. The old files kept as backups, the new ones in place.
  for (size_t i = 0; i < n; i++) {
    const bool kept = fs.exists(cards[i].path)
                          ? fs.rename(cards[i].path, cards[i].backup)
                          : fs.write(cards[i].backup, "");
    if (!kept) {
      roll_back(fs, marker, cards, n);
      return false;
    }
  }
  for (size_t i = 0; i < n; i++) {
    if (!fs.rename(cards[i].staging, cards[i].path)) {
      roll_back(fs, marker, cards, n);
      return false;
    }
  }
  // 4. Done once the marker is gone; then the backups go (one left behind
  // goes with the card's next replace, or at boot).
  if (!fs.remove(marker)) {
    roll_back(fs, marker, cards, n);
    return false;
  }
  for (size_t i = 0; i < n; i++) fs.remove(cards[i].backup);
  return true;
}

bool recover_cards(CardFs& fs, const std::string& marker,
                   const CardFiles* cards, size_t n) {
  if (fs.exists(marker)) {
    // Cut short, or its rollback was: every card back to its old file.
    roll_back(fs, marker, cards, n);
    return false;
  }
  bool clean = true;
  for (size_t i = 0; i < n; i++) {
    if (fs.exists(cards[i].staging)) {
      fs.remove(cards[i].staging);
      clean = false;
    }
    if (fs.exists(cards[i].backup)) {  // Left by a replace that finished
      fs.remove(cards[i].backup);
      clean = false;
    }
  }
  return clean;
}

}  // namespace skgauges
