#pragma once

// Replacing the files of one or more page cards all or nothing, so a full or
// failing filesystem, or a power cut part-way, never leaves a page lost or on
// two cards (a swap writes two). Host-safe (tests/test_card_store.cpp): the
// filesystem is the CardFs interface; gauge_settings.cpp backs it with SPIFFS.
//
// SensESP's FileSystemSaveable::save() is not enough: it removes the old file
// first and reports success whatever the write did.
//
// A replace, for every card in it:
//   1. writes the new contents to the card's staging file and reads it back;
//   2. moves the card's file to its backup (or, if it has none, writes an
//      empty backup, which means "no file");
//   3. moves the staging file into place;
//   4. removes the backups.
// A failure in 1-3 undoes every card's steps. A power cut is undone, or
// finished, by recover_cards() at the next boot: while any card has a backup
// but no file, the replace was cut before step 3 ended, and every card goes
// back to its backup; otherwise step 3 ended and the backups go.

#include <stddef.h>

#include <string>

namespace skgauges {

class CardFs {
 public:
  virtual ~CardFs() = default;
  virtual bool exists(const std::string& path) = 0;
  virtual bool remove(const std::string& path) = 0;
  // Fails if `to` exists.
  virtual bool rename(const std::string& from, const std::string& to) = 0;
  // Creates or truncates `path` with `text`; true only if it all got there.
  virtual bool write(const std::string& path, const std::string& text) = 0;
  virtual bool read(const std::string& path, std::string* text) = 0;
};

struct CardFiles {
  std::string path;     // The card's file, as load() finds it
  std::string staging;  // Its new contents, until they are in place
  std::string backup;   // Its old file, until every card is replaced
};

// Replaces cards[i].path with texts[i] for i < n, all or nothing. A card's
// text is never empty. False (and nothing changed) on any failure.
bool replace_cards(CardFs& fs, const CardFiles* cards, const std::string* texts,
                   size_t n);

// Finishes or undoes a replace a power cut interrupted, over every card a
// replace can include. Call at boot, before the cards are loaded. True if
// nothing needed doing.
bool recover_cards(CardFs& fs, const CardFiles* cards, size_t n);

}  // namespace skgauges
