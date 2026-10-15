# Drum Breaker

A VST3 / AU / Standalone drum-loop slicer built with JUCE 8. Drop in a break, it finds
the tempo and the transients, chops the loop into fragments, and lets you re-arrange
them on a magnetic, snap-to-grid waveform playlist.

## Features

- **Drag & drop loading** (WAV / AIFF / FLAC / OGG / MP3, 60 second maximum) from Explorer or Finder.
- **Automatic BPM detection**, shown to three decimal places in the visualiser badge.
  Onset-grid timing helps distinguish syncopated pulses and half/double-tempo choices.
  Evenly cut loop durations are considered only when supported by detected onset timing;
  padded audio falls back to pulse-based measurement. Decimal precision is not a guarantee
  of accuracy: unusual rhythms, swing, tempo changes, and ambiguous beat groupings can
  still require an external reference. Silent/unusable audio displays `----`.
- **Transient slicing** – the BREAK button cuts the loop just before each kick, snare, hat or fill.
- **Zoomable / scrollable waveform** (scroll to move, Ctrl/Cmd + scroll to zoom) spanning the full width.
- **Custom break display** - after BREAK, NEW BREAK reveals a horizontally centred display
  below the original, with the same height and an initial width of one third. It grows
  horizontally as fragments are added and stays centred when resized or reopened.
- **Magnetic fragment dragging** – drag a piece anywhere, even off the display; a dimmed ghost
  follows the mouse and the fragment snaps back into the nearest slot on release.
  Hovering a piece over the NEW BREAK display (including the first, empty drop) shows it
  magnetised in place: later pieces slide aside to open a gap and the display temporarily
  widens to fit it.
- **Delete / duplicate fragments** (right-click or Delete). The timeline re-compacts so no
  silence is left in the display or in the rendered output.
- **Monophonic, last-note-priority playback** with the fixed factory envelope
  (attack 0, hold 100 %, decay 0 %, sustain 3 %, release 4 %). Hold a key or PLAY to loop
  the full visible arrangement; release to stop. The playhead wraps to the beginning,
  fractional pitch/rate positions are preserved, and sample-timed filter motion stays
  aligned. WAV export remains one arrangement pass plus its effect tail, not endless loops.
- **Pitch knob** centred (pointing up) at the sample's original pitch; middle C also plays it unshifted.
- **FL-style typing keyboard** is ready when the editor opens: Q = middle C/C5
  (MIDI 60), Z = C4 (MIDI 48), S = C#4. The lower chromatic row is
  `Z S X D C V G B H N J M`; the upper row is
  `Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P`. Incoming DAW piano-roll MIDI
  and typing notes share the displayed keyboard. Held keys loop; release, leaving
  the plugin, or closing its editor releases typing notes. Text entry and modifier
  shortcuts are not intercepted. Hosts must forward typing keys to the focused
  plugin; DAW piano-roll MIDI works independently of editor focus.
- **Interactive 7-band EQ** over a frequency histogram and scrolling heat map. The response
  starts flat at 0 dB; drag numbered handles horizontally for frequency and vertically for
  gain (-18 to +18 dB). Band 4 starts at the centre of a logarithmic 20 Hz to 20 kHz scale.
  Endpoints adjust the low/high shelves; bands 2-6 are bell filters. Select a band for numeric
  gain/frequency/Q editing, scroll over a bell handle for width, or double-click to reset it.
- **EQ power lamp** beside EQ / ANALYSER starts on and subtly pulses on hover. Left-click
  bypasses only the EQ; right-click opens a hover-dismissed menu for On/Off, Reset to Default,
  and Low Pass (2 kHz), Band Pass (200 Hz-3 kHz), and High Pass (200 Hz) presets. Cut presets
  use real 12 dB/octave filters; drag their cutoff tokens horizontally. When off, reset and
  presets are disabled, the graph is dimmed and flat, and the spectrum still runs. Filter
  and reverb remain active, and restoring EQ preserves its previous settings.
- **Resonant filter** with an interactive X/Y modulation pad and 7 genre presets:
  Hip Hop, Trap, Breakcore, Jungle, Techno, House, Trance.
- **Filter power lamp** matches the smaller EQ lamp. Left-click toggles filter-only
  bypass; right-click offers On/Off, filter-only Reset to Default, and three automated
  presets: Low Pass, Band Pass, and High Pass. The modulation dropdown starts at
  **Select preset...** (manual mode) and offers the same three automated presets,
  with no Notch entry. Main and custom breaks each remember their own preset,
  including after saving and reopening the project.
  Sweep presets cycle logarithmically from 20 Hz to the safe high-frequency limit and
  back using the selected low-pass, band-pass, or high-pass shape. Each cycle
  matches the arranged sample's duration adjusted for playback pitch, restarts on a
  new note, and pauses when playback stops or filter power is off. Selecting the marked
  preset again deselects it. The X/Y pad follows actual smoothed cutoff/resonance without
  automating the stored manual values; filter-only actions do not change EQ settings.
  The four-quadrant X/Y pad controls cutoff horizontally and resonance vertically
  simultaneously. Its exact center is the neutral (0, 0) position: the filter, including
  drive, is bypassed there without bypassing EQ or reverb. New instances and filter
  resets start centered; moving away smoothly reactivates the filter. Drag into the
  small center snap zone or double-left-click the handle to recenter, cancel filter
  motion, and clear mod-wheel modulation. Motion presets and the mod wheel can still
  activate filtering independently, and the pad follows their live values.
- **Animated DRUM/BREAKER logo** retains its left-click full reset and offers a single
  Restore to default... action on right-click. Both restore factory parameters and
  forget the sample. The separate by hidden audio credit opens hiddenaudiofx.com.
- Emerald styling throughout with hover glow, styled tooltips and a hint strip that is also
  published to the host (FL Studio's hint panel).

## Logo Intro

Each plugin instance plays the embedded company MP4 once, after authoritative
license verification succeeds. The played flag is saved in the host project, so
closing/reopening the editor or reopening a saved project never replays it.
Unverified/pending/failed checks never start it. After a new activation through
the license panel, Continue dismisses the panel and starts the intro with sound;
every other playback starts muted (and falls back to muted if the OS blocks
audible autoplay).
The video fills the editor (cropping to cover different window aspect ratios).
Hovering or moving the pointer reveals **X** and **VOLUME**
in the top left; VOLUME toggles sound without skipping. Any other click, X,
Escape, or the end of the video goes directly to the existing GUI.
Typing-piano input is disabled during the intro; incoming DAW MIDI and instrument
processing are unchanged. Closing the editor stops playback and releases the player.
Intro sound uses the OS default output, separately from the plugin/DAW audio path.
Player failures/timeouts return to the GUI with a status-line error.

The HTML player and original MP4 are compiled into the plugin; no desktop file,
external video download, or loose installed asset is required. Windows uses the
Microsoft Edge WebView2 Runtime (the installer includes its signed online
bootstrapper); macOS uses WKWebView. CMake fetches the pinned WebView2 build SDK
on Windows. macOS intro playback still requires platform validation.

With tests enabled, `SessionStateTest --logo-intro-self-test` (CTest `LogoIntroUI`)
uses synthetic licensing and a real native editor/player to check the activation
gate, audible autoplay after panel activation, muted autoplay otherwise,
full-window resizing, click-to-skip, natural completion, once-per-instance
playback (including saved-project restore), player cleanup, and loss of verification. This UI test requires the
WebView2 Runtime and an interactive desktop on Windows.

## Project Sessions

Drum Breaker is registered as a synth/instrument, not a mixer effect. A newly created
instance starts with factory parameter values, no sample, an empty waveform and no BPM.
The DAW saves each instrument instance's sample reference, slices, arrangement, parameters,
and pitch/modulation-wheel values inside its own project state. Reopening that project
restores those values; another fresh instance does not inherit them.

Restoring a valid empty state clears previous sample/BPM/controller data. A missing source
file cannot leave an unrelated earlier sample loaded, and queued genre-preset actions cannot
overwrite restored custom settings. A saved arrangement with every fragment deleted stays
empty rather than silently restoring the entire sample.

Samples are currently referenced by their full source-file path, not embedded in the DAW
project. Keep source files available at that location, including when transferring projects.
Host templates, presets, and duplicated channels that contain saved instrument state restore
that state intentionally; standard VST3/AU does not identify whether a supplied state came
from a project, template, or preset.

With tests enabled, `SessionStateTest` (CTest `ProjectSessionState`) exercises actual
processor instances for these fresh-instance and state-round-trip behaviors.

## Exporting

EXPORT TO... sits before BREAK, PLAY, RESTORE and opens a menu that closes when the
pointer leaves. Export snapshots the arranged sample and current pitch/bend, seven-band
EQ/bypass, filter/bypass/motion/mod wheel, reverb, and output gain, then renders the full
C5/PLAY arrangement on a background worker. Files are 32-bit float WAVs at the current
audio sample rate and output channel count, with a four-second tail when reverb is enabled.
Bounded EQ/filter decay tails are also included when those effects remain active.
Export does not normalize, change live playback, or overwrite an existing file.

- EXPORT TO FOLDER... opens a folder chooser, saves a uniquely named WAV, and reveals it
  in Explorer/Finder after rendering.
- EXPORT TO PLAYLIST... renders a persistent WAV and shows its filename as a drag handle.
  Drag that handle to an accepting host's Playlist, Arrangement View, Tracks area, or
  edit timeline. Standard VST3/AU provides no portable way to choose an empty host track
  or automatically insert a clip, so track selection is performed by dropping the file.
  Native drag/import behavior depends on the DAW and platform and must be checked there.
  These files remain under the user's application-data folder in
  `Hidden Audio/Drum Breaker/Exports`; keep them if a DAW project references them.

`AnalyzerTest --export-self-test` (CTest `ProcessedWavExport`) checks arrangement
playback fidelity, pitch-adjusted duration, effects/tail output, float peaks, cancellation,
and protection against silent overwrites.

Export captures one PLAY-note render, not a host MIDI performance, host automation, or
other DAW insert/master effects. Closing the editor cancels unfinished exports and removes
temporary files; it does not remove completed WAVs.

## Licensing Setup (In Progress)

The intended release policy is no demo: playback and WAV export require activation.
Lemon Squeezy product ID `1412490` and variant ID `2205337` are configured in
`Source/Licensing/LicensePolicy.h`; the store's three-activation limit remains enforced
by Lemon Squeezy rather than a client-side activation counter.

The first staged component checks activation/instance-validation replies against this
product, variant, submitted key, device and instance, active status, and expiration.
`AnalyzerTest --license-policy-self-test` (CTest `LicensePolicy`) uses synthetic replies
only; it makes no network requests and consumes no activations.

The activation screen now connects to the public Lemon Squeezy License API on a
processor-owned background worker. It uses HTTPS form POSTs, checks product/variant/key
metadata before requesting a new activation, and checks the returned device instance.
The readable, monospaced key field and ACTIVATE button are disabled while a request is
running. Entered text stays visible after success so the user can verify it; closing or
reopening the panel clears the field. Keys are not logged or saved in DAW project state.
Recreating or reopening the editor does not automatically display the activation form
for an already verified processor. Automatic prompts wait for a pending startup check
and appear only if the result is unverified. ACTIVATE remains available for manual
license/status access.
Verified status is shown as ACTIVATED with a checkmark in the header. The activation
action inside the license form is hidden after successful verification, and its visible
key field becomes read-only. Pending or failed checks restore the activation/retry action;
the indicator never treats a saved credential record alone as verification.
Requests have timeouts, bounded response size, no redirect following, and no automatic
retries. Merchant API credentials are not needed and must never be embedded in the plugin.

A successful activation is saved in Windows Credential Manager or macOS Keychain for
the current OS user, with product/variant identity, device binding, key, and instance ID.
No editable local "licensed" flag is accepted as proof. Startup loads that protected
record on a background worker and validates its existing instance with Lemon Squeezy;
it never requests a new activation as a fallback for failed saved-instance validation.
Submitting the same saved key also validates that instance rather than activating again.
A cross-process coordination lock prevents simultaneous plugin instances from each
creating a new activation for the same unsaved key.

Startup verification currently requires internet access. Network errors or rejected
saved instances keep the protected record intact and do not consume replacement slots.
RETRY CHECK validates the saved instance without requiring key entry or allocating a
new instance. Storage read errors and device mismatches fail closed rather than silently
reactivating. If secure saving fails after a new activation, the UI reports that warning;
keep the instance open and contact support before restarting. Keys are never saved in
DAW project state. Windows protected-storage tests passed; macOS Keychain behavior is
implemented but has not been built or tested on this Windows development machine.

ACTIVATE now performs real requests: placeholder text is no longer a UI-only test.
Test a genuine DrumBreaker key directly in the plugin, never in chat. Each successful
new activation consumes a slot. The first activation after upgrading from the previous
memory-only build may consume one more slot because its old instance ID was not saved.
Previously consumed slots are not automatically reclaimed; manage unused instances in
Lemon Squeezy. If a reply is lost after activation, check existing activations before
retrying. Installer updates should preserve the protected credential-store entry.

`AnalyzerTest --license-persistence-self-test` (CTest `LicensePersistence`) exercises
native protected storage and mocked startup/activation requests using a unique synthetic
record, never the production record or a customer key. It checks round trips, simultaneous
clients, restart validation, offline/revoked failures, validation retry, and device binding.

No-demo enforcement is now enabled: playback stays silent and processed WAV export is
denied until authoritative online verification succeeds. Pending checks, failed checks,
and unverified clients cannot render audio. The audio callback reads an atomic gate,
not a network request or licensing mutex. Loss of verification clears active voices and
effect tails; notes received while locked are discarded rather than replayed after unlock.

PLAY/keyboard/export controls reflect this state, but protection is enforced independently
in the processor and offline renderer. Export authorization is checked before rendering,
between blocks, and before publishing the completed WAV. Cached snapshots cannot export
after authorization is lost, and failed/unlicensed renders do not publish partial WAVs.
License authorization is not a DAW parameter or project-state field. Sample loading,
editing, and project restoration are still available while playback/export are locked.

Verification is performed at startup and on requested validation; this build does not
continuously poll for later server-side revocations. There is no offline grace period.
Protected-storage, policy, processor playback, and export-gate regressions use synthetic
keys and isolated stores, never real customer credentials. A genuine-key activation was
manually verified by the user before enforcement; final locked FL Studio interaction
still requires manual testing. macOS build/Keychain behavior remains untested here.

Inno Setup packages the complete VST3 bundle; it does not add licensing security. Preserve
the protected credential-store record during updates. Before public distribution, test
the packaged build, confirm published product/variant IDs, and consider code-signing both
plugin and installer. Client-side licensing is not a guarantee against binary patching.

## Building

Requires CMake 3.22+ and a C++17 toolchain (MSVC on Windows, Xcode on macOS). JUCE is
fetched automatically.

```bash
cmake -B build
cmake --build build --config Release
```

Artefacts land in `build/DrumBreaker_artefacts/Release/`.

To also copy the plugin into the system plugin folders (needs admin rights on Windows):

```bash
cmake -B build -DDRUMBREAKER_INSTALL_AFTER_BUILD=ON
```

To build against an existing JUCE checkout instead of downloading one:

```bash
cmake -B build -DJUCE_PATH=/path/to/JUCE
```

macOS builds are universal (x86_64 + arm64) with a 10.13 deployment target; the same
source tree produces VST3, AU and Standalone on both platforms.

## Source layout

| Path | Purpose |
| --- | --- |
| `Source/Analysis/SampleAnalyzer.*` | Spectral-flux onset detection and tempo estimation |
| `Source/Audio/BreakEngine.*` | Snapshot data model, monophonic sample playback, envelope |
| `Source/Audio/ToneStack.*` | 7-band EQ + state-variable filter + reverb |
| `Source/Audio/FilterPresets.*` | The seven genre presets |
| `Source/UI/WaveformEditor.*` | Waveform playlist, dragging, ghost overlay, BPM badge |
| `Source/UI/SpectrumDisplay.*` | Histogram + heat map |
| `Source/UI/XYPad.*` | Modulation pad |
| `Source/UI/EmeraldLookAndFeel.*` | Glowing emerald styling and tooltips |
| `Source/UI/HintBroadcaster.*` | Hint strip / host hint panel plumbing |

## EQ Verification

With `DRUMBREAKER_BUILD_TESTS=ON`, the existing AnalyzerTest harness also accepts
`--eq-self-test`. The `EqualizerResponse` CTest checks logarithmic band placement,
flat response, +/-18 dB limits at multiple sample rates, and mono/stereo audio matching
the coefficients used for the displayed curve. It also covers filter-motion ranges,
cycle/retrigger timing, pause/bypass isolation, duration changes, and deselection.
Older low/mid/high parameter IDs are
retained as bands 1/4/7; all seven bands now have a 20 Hz to 20 kHz frequency range.

`AnalyzerTest --tempo-self-test` (CTest `TempoDetection`) checks straight and syncopated
167, 170, and fractional 167.375 BPM loops at 44.1/48 kHz, silence, and silence tails.
`AnalyzerTest <audio file> <expected BPM>` also verifies a reference recording and prints
six decimal places for diagnostics. The supplied 167 BPM Amen reference measured
166.999988 BPM after correction, displayed as 167.000; it previously measured 111.208.
