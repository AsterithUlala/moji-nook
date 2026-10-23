# Changelog

Moji Nook is an early alpha. Downloads for each version are on the
[releases page](https://github.com/AsterithUlala/moji-nook/releases).

## 0.3.0-alpha

### Added

- **Start at login.** Turn on **Start Moji Nook when you log in** in
  **Settings → Practice → Schedule** to open Moji Nook in the tray when you log in.
- **Quiet hours.** Choose days and times when no cards appear, such as work
  hours or overnight. A card you haven't clicked steps aside and returns when
  quiet hours end. **Practice now** still works. See
  [everyday practice](docs/USAGE.md#everyday-practice).
- **Smarter word choice.** Moji Nook estimates how well you remember each
  reading and meaning, starting from your last WaniKani review or Anki interval.
  - A reading or meaning you answer correctly rests until tomorrow.
  - A miss comes back after at least 10 minutes, and rests until tomorrow after
    three misses in a day.
  - Burned WaniKani items get their own 10% share, with a new **Burned items**
    slider.
  - Fragile words lean toward four choices, sturdier words toward typed
    reading. Your overall format mix is unchanged.
- **Type before reveal.** Recall cards ask you to type an answer before
  revealing, then show it next to the correct answer. On by default; turn off
  **Type an answer before revealing** to reveal without typing.
- **Near-miss second try.** A typed reading one kana away from an accepted
  reading gets one more attempt without showing the answer. On by default.
- **I made a typo.** After a wrong typed reading, count the answer as correct.
- **Memory help after a miss (optional, off by default).** Shows similar-looking
  kanji and the WaniKani mnemonic under the answer.

### Changed

- Answers checked by typing count as stronger evidence than self-ratings.
  Answers rescued by a near-miss second try or a typo override count for less.
- A typed reading attempt on a recall card is shown in kana.
- WaniKani review cards keep WaniKani's rules: the near-miss second try, the
  typo override, and memory help are practice-only.

## 0.2.0-alpha

### Added

- **WaniKani review mode.** Turn on **Settings → WaniKani → Do WaniKani reviews
  here** to do your due WaniKani reviews on Moji Nook's cards. It is off by
  default. See [review mode](docs/USAGE.md#do-wanikani-reviews-in-moji-nook-optional).
  - While reviews are due, each card asks one of them before any practice card.
    When none are due, practice resumes.
  - You type every answer. Kanji and vocabulary ask the meaning and the reading;
    radicals and kana-only vocabulary ask only the meaning.
  - Answers are graded by WaniKani's rules. Small meaning typos are accepted,
    your WaniKani synonyms count, and blocked meanings are wrong. A reading typed
    as a meaning, a verb without "to", stray characters in a reading, or a kanji
    reading of the other type asks again without a miss.
  - A finished review is submitted to WaniKani, and the card shows the stage
    change, for example "Apprentice 4 → Guru 1".
  - Before submitting, Moji Nook checks that the review is still due. A review
    you already did elsewhere is dropped, not sent twice.
  - A miss is saved as soon as you answer, so quitting mid-item keeps it.
  - The mode needs a token with the `reviews:create` permission. Turning it on
    checks the token without recording anything, and explains how to fix a token
    that can't submit reviews.
  - Moji Nook never starts lessons.
- Review cards show radicals, including radicals that WaniKani only has as an
  image.

### Fixed

- Reminders no longer stop for good when the timer fires while no words are
  available. An Anki sync that fills an empty pool now starts a session.
- A double-click on **Next** no longer answers the card that replaced it.
- Changing the time between sessions no longer cancels an active snooze.
- The first card after launch appears in the saved corner instead of jumping
  there.
- The settings sample card closes when a real card arrives.
- CSV export keeps negative Anki subject IDs as numbers.
- An attempt counts on the day it was graded, not the day it was saved.
- A phrase over the speech length limit shows an error instead of
  "Preparing audio…" forever.
- **Listen** works when the saved Fast engine is no longer installed.
- Spacing and alignment fixes in spin boxes, the word-tile empty state, the
  missed-answer row, and the Insights page.
- Anki furigana in a tooltip is shown as plain text.

### Packaging

- The Arch package now depends on `qt6-svg`, which draws image-only radicals.
  The AppImage and Windows zip already include it.
- A bug report form for the GitHub repository.

### Known limitations

- For vocabulary written with a single kanji, typing that kanji's reading of the
  other type counts as a miss. WaniKani would ask again.

## 0.1.2-alpha

- Sessions can have up to 25 cards. The default is still two cards every five
  minutes.
- The snooze length is configurable (30 minutes by default).
- Quality speech has one adult and one child voice of each gender.

## 0.1.0-alpha

- First public alpha, with an Arch package, an AppImage, and an experimental
  Windows zip. 0.1.1-alpha republished the same build.
