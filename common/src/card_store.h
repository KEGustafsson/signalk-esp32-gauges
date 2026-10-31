#pragma once

// Replacing the files of one or more page cards all or nothing, so a full or
// failing filesystem, or a power cut part-way, never leaves a page lost or on
// two cards (a swap writes two). Host-safe (tests/test_card_store.cpp): the
// filesystem is the CardFs interface; gauge_settings.cpp backs it with SPIFFS.
//
// SensESP's FileSystemSaveable::save() is not enough: it removes the old file
// first and reports success whatever the write did.
//
// A replace:
//   1. writes each card's new contents to its staging file and reads them
//      back;
//   2. writes the marker file: from here until it is removed, the cards are
//      to go back to their old files whatever happens;
//   3. moves each card's file to its backup (or, if it has none, writes an
//      empty backup, which means "no file"), then each staging file into
//      place;
//   4. removes the marker (the replace is done), then the backups.
// A failure in 1-4 puts every card back; the marker goes only once that has
// worked, so a rollback that fails part-way (or a power cut) is finished by
// recover_cards() at the next boot: with the marker there, every card goes
// back to its backup; without it, leftover backups and staging files go.

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

// Replaces cards[i].path with texts[i] for i < n, all or nothing, using
// `marker` as the marker file. A card's text is never empty. False on any
// failure: nothing changed, or (if putting the cards back failed too) the
// marker stays and recover_cards() puts them back at the next boot; until
// then every replace is refused.
bool replace_cards(CardFs& fs, const std::string& marker,
                   const CardFiles* cards, const std::string* texts, size_t n);

// Finishes a replace that was cut short or could not be undone, over every
// card a replace can include. Call at boot, before the cards are loaded.
// True if nothing needed doing.
bool recover_cards(CardFs& fs, const std::string& marker,
                   const CardFiles* cards, size_t n);

}  // namespace skgauges
