# Using Moji Nook

[Install and launch Moji Nook](INSTALL.md) first. To explore before connecting
anything, run `moji-nook --demo` (or `./build/moji-nook --demo` from a source
build); sample words use a separate profile. Quit the demo and launch without
`--demo` when you want your real practice profile.

## Connect your practice source

Moji Nook is primarily a WaniKani companion; start with WaniKani if that is where
you study. Compatible Anki decks also work, with the limitations described below.

In **Settings → Practice → Practice with**, choose **WaniKani**, **Anki**, or
**Both sources**. Start with one source for the simplest setup. Both sources
can contain the same words; Moji Nook keeps their records separate and does not
deduplicate vocabulary across them.

### WaniKani

1. Open **Settings → WaniKani → Open WaniKani token settings**, or visit
   [WaniKani's personal access token page](https://www.wanikani.com/settings/personal_access_tokens).
2. Create a personal API token. Moji Nook only reads data; it needs no write permissions.
3. Paste the token into **WaniKani API token** and choose **Save token and sync**.
4. Wait for sync to finish, then choose **Practice now**.

Use your own token, not a WaniKani password. WaniKani documents personal-token
access in its [API authentication guide](https://docs.api.wanikani.com/20170710/#authentication).
The first sync downloads a baseline; later syncs reuse the cache and request changes.

Eligible items are started, nonhidden kanji, vocabulary, and kana vocabulary
within the account's cached access level. Unstarted lessons and radicals are
excluded. An account with no eligible learned items cannot supply practice yet.
Moji Nook does not submit lessons or reviews and never advances your WaniKani SRS stage.

Automatic refresh is due 24 hours after the last successful sync, checked at
startup and each minute while running. Temporary failures retain the previous
cache; automatic retries are spaced out. You can also sync manually. Cached
practice works offline, though it reflects the last successful sync and its
account-access information. Moji Nook cannot notice a subscription change while
offline; reconnect and refresh when your account access changes.

### Anki

Anki support is secondary and less developed than the WaniKani integration.
The steps below describe what works today.

1. Install desktop Anki and open the profile containing your vocabulary deck.
2. Install [AnkiConnect](https://ankiweb.net/shared/info/2055492159) using Anki's
   **Tools → Add-ons → Get Add-ons** and code **2055492159**, then restart Anki.
   See Anki's [add-on instructions](https://docs.ankiweb.net/addons.html).
3. Keep Anki open. Moji Nook connects to AnkiConnect on localhost, port **8765**.
4. Choose **Anki** in Moji Nook's practice source selector.
5. In **Settings → Anki**, choose **Find decks**, check the decks you want,
   and choose **Sync selected decks**.
6. Choose **Practice now**. Anki can be closed after a successful import;
   deck recordings and images still need to remain installed locally.

The current adapter supports **Kaishi-style vocabulary notes**, not arbitrary
Anki templates. It requires fields named exactly `Word`, `Word Reading`, and
`Word Meaning`, a usable kana reading, and the first card template. Only cards
that have been studied are eligible; new, suspended, and buried cards are excluded.
Unsupported layouts are skipped. Cloze, reversed, listening-only, and generic
sentence-mining templates are not currently adapted.

If decks are not found, check the active Anki profile, that AnkiConnect is
installed, and that Anki is open. Moji Nook's UI does not currently support a custom
AnkiConnect endpoint or API key. An import requires AnkiConnect's read actions,
but never grades cards, edits notes, invokes AnkiWeb sync, or changes scheduling.

Enabled Anki practice attempts a daily source refresh. Failed imports keep the
previous snapshot. Source edits, suspensions, and deletions take effect on the
next successful sync, rather than continuously during offline practice.

### Both sources

**Both sources** reveals an adjustable Anki share, initially 30% Anki / 70%
WaniKani. If one source has no eligible items, the other supplies practice.
Selection priorities run within the chosen source; “Newest 30” is per source.
Switching sources preserves cached items and past answers. A card already on
screen remains until answered or dismissed.

## Everyday practice

The default session is **two cards**, with a **five-minute** reminder interval.
Session size is adjustable from one to five. The next interval starts after
completing or ending the session, not after revealing an answer. Busy time does
not accumulate a backlog of cards.

Cards appear in your chosen display and corner without requesting keyboard
focus. Click a card to interact. **Identify displays** shows numbered markers;
changing the display or corner shows an independent sample. A disconnected
saved display falls back to the primary display without forgetting your choice.
**Preview a sample card** never changes real history or the reminder timer.

There are three challenge formats:

- **Recall:** try the prompted reading or meaning, reveal, then honestly select
  **Remembered** or **Missed**. The rating applies to the prompted skill only.
- **Typed reading:** enter kana, katakana, or romaji and check the answer.
  A kana preview helps catch conversion mistakes. Use `n'` before a vowel when
  you intend ん, such as `shin'you`. Spell long vowels with kana spelling,
  such as `gakkou`, rather than `gakkō`.
- **Four choices:** select a reading or meaning from plausible learned alternatives.
  When suitable readings or three distractors are unavailable, Moji Nook falls back to recall.

The default requested mix is 50% recall, 25% typed reading, and 25% choices.
Selection defaults to 30% Newest 30, 25% recent troubles, 20% persistent troubles,
and 25% broad recall. Adjust these in **Fine-tune practice (optional)**.
The realized mix can differ when eligible items or formats are unavailable.
Distractors are heuristic and are not guaranteed to be ideal linguistic alternatives.

Kanji prompts identify the requested primary reading type when available.
A known alternate kanji reading can receive a retry prompt instead of an
immediate miss; it is not accepted as the correct vocabulary reading. Turn off
**Retry alternate kanji readings** for strict grading.
Unrelated wrong answers are graded normally. Incorrect answers remain visible
alongside the correct answer until you advance or dismiss the card.

After activating the card, use **Tab** to move between controls, **Enter** to
check a typed answer and then finish, and **Escape** to close. Shortcuts are
local to the card. Closing before grading records a dismissal without scoring;
closing after grading keeps the result. Pausing hides a pending card, and
resuming brings it back. **Snooze 30 minutes** dismisses it and delays the next session.

[Example checked card](previews/card-answer.png) uses synthetic practice data.

## Dashboard, tray, and appearance

The main destinations are **Practice**, **Words**, **Progress**, **Focus**, and
**Settings**. **More** contains **History** and **Quit**. Practice brings the
next session, pause/snooze controls, and earned word tiles together.
The tray icon opens the dashboard; its menu provides practice, settings,
pause, snooze, WaniKani sync, and quit. Anki sync is in Settings → Anki.
Closing the dashboard keeps Moji Nook running. **Quit** stops reminders and exits.

**Settings → Appearance** provides Ink and Paper families, each with light and
dark appearances. The header **Dark** toggle switches within the selected
family. Feedback tones, pronunciation, and reduced motion are configurable.
Answer icons and text remain usable without sound or animation. Japanese text
stays large; long options wrap and tall cards scroll.

## Japanese pronunciation

On a build with the bundled voices, **Sound & voice → Japanese voice** offers:

- **Quality:** offline VOICEVOX voices, Shikoku Metan, Zundamon, and Kasukabe Tsumugi.
- **Fast:** offline Open JTalk voices, Mei, Takumi, and Tohoku.

Quality is the default when available. Voice choice is random per new card by
default; **Listen** reuses that card's voice. Choose a fixed voice, autoplay,
volume, and speed (0.75×–1.25×), or disable pronunciation. **Try voice** auditions
the current settings. Each mode remembers its own voice choice.

Moji Nook speaks an accepted kana reading after reveal or checking, including
meaning questions. Enable **Use deck recordings when available** to prefer local Anki recordings,
with synthesis as the fallback. This option is off by default in new profiles.
Recordings retain their original voice and speed.
Missing speech or media does not prevent practice. Playback is canceled when
the card is closed or hidden. Voices are pronunciation aids, not authoritative
pitch-accent instruction. See the [speech notes](SPEECH.md) for assets and attribution.

## Progress and word tiles

Progress shows activity and accuracy, with reading/meaning and challenge-type
breakdowns. Accuracy includes both checked and self-rated answers, with their
categories distinguishable in the details. A **comeback** is a correct answer
whose previous graded attempt for the same item and skill was a miss.
The WaniKani-stage chart excludes Anki cards. Daily practice is a 28-day heatmap;
charts use three, two, or one column to fit the available width.

**Words** grows a composition through ordinary practice. Use the search field
to filter tiles as you type part of a Japanese word, reading in kana or romaji,
or meaning. For example, `chu` matches readings containing `ちゅ`, including
`うちゅう`. Hiragana and katakana readings match each other.
Type `kanji` or `vocab` (also `vocabulary`) to show only WaniKani tiles with
that tag. Searches are case-insensitive; `vocab` includes kana vocabulary.
Clear the search to restore the full collection; click a matching tile to explore
its details. A tile
settles when both reading and meaning have successful evidence on at least
three different days, spanning two weeks and including a week between
successful encounters. Only the first graded daily encounter for each skill
counts. Honest self-rating contributes; same-day retries do not accelerate it.
Qualifying past Moji Nook history contributes. Click a tile, or use the arrow keys,
to expand its reading, meaning, successful practice days, source, and challenge
state in place. Tiles stay on their row while neighboring tiles narrow as needed
to make room. Click it again, or press Space/Enter, to collapse it.

WaniKani items show a **Vocab** or **Kanji** label with a faint purple or pink
tint on the label and the edges of the word surface.

Completed tiles have a gold border and remain earned after mistakes or time away.
A compact red-orange **!!** mark (explained on hover) marks
sustained difficulty in a skill (misses on three of its last six practice days),
and clears after three later successful days in that skill. This invites extra
attention without suspending the word. Occasional passive five-second recaps
acknowledge meaningful changes; intentional hover lets you read longer, and × closes the recap early.
These are product goals, not a calibrated estimate of retention or permanent mastery.
See a [sample word collection](previews/word-tiles.png).

**Focus → Second Chances** offers up to five eligible items
missed on reading questions in the last seven days. Its results are stored
separately from ordinary accuracy, targeting, and CSV export, and do not change
the regular timer or ordinary word-tile evidence.

## Storage, backups, and privacy

Moji Nook uses Qt's per-user application-data location. On Linux the profile is
`~/.local/share/MojiNook/Moji Nook/`, or `$XDG_DATA_HOME/MojiNook/Moji Nook/` if you set
`XDG_DATA_HOME`. The profile contains:

- `token`: your WaniKani API token, if connected.
- `cache.json`: WaniKani cache and last successful sync information.
- `practice.sqlite`: practice history, the Anki snapshot, and progression data.
- `settings.ini`: source, reminders, placement, appearance, and voice preferences.
- `app.lock`: prevents two instances opening the same profile.

On Linux, the profile directory and sensitive files have owner-restricted
permissions. The token is plaintext, not encrypted. Windows follows the user's
application-data permissions; credential-vault integration is not implemented.
Anki media remains in Anki's media directory, outside the profile backup.

For a backup, **quit Moji Nook**, then copy the entire profile to a private backup
location. Restore with Moji Nook stopped too. History's **CSV export** is useful
for analysis, but is not a full backup: it excludes settings, cache, progression
state, and separate focused-practice results. CSV duration starts at first
interaction and includes subsequent idle time.

Use a separate profile when switching WaniKani accounts so their histories do
not mix. For example:

```sh
moji-nook --data-dir "$HOME/.local/share/moji-nook-profiles/second-account"
```

Demo and preview modes add `-demo` to their profile directory. Do not point a
test build at your real data.
Do not share a live SQLite database between running instances.

Moji Nook has no cloud progress account or telemetry service. Source refreshes
contact WaniKani over HTTPS or local AnkiConnect over HTTP. WaniKani requests
are GET-only; AnkiConnect uses POST as the transport for read-only RPC actions.
Build-time speech downloads are separate from runtime source connections.
Never attach tokens, private caches, decks, recordings, or your practice database
to a public bug report. Screenshots can expose personal study content too.
