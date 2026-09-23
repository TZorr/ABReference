# Third-party notices

AB Reference is © 2026 T'Zorr and is distributed under the GNU Affero
General Public License, version 3 (AGPLv3) — see [LICENSE](LICENSE). This
follows from the one third-party component it links against.

## JUCE

- **What:** the JUCE 9 framework (audio plugin client, DSP, GUI). Not
  included in this repository — `CMakeLists.txt` expects it at `~/JUCE` (or
  a path passed with `-DJUCE_PATH`) and links it as an external framework.
- **Copyright:** © Raw Material Software Limited — <https://juce.com>.
- **License:** JUCE 9 is dual-licensed. This project uses JUCE's free tier,
  the GNU Affero General Public License, version 3 (AGPLv3). Because JUCE is
  AGPLv3 here and AB Reference links it, AB Reference is itself AGPLv3 in
  turn — the AGPL's own requirement for any work that incorporates
  AGPL-licensed code. The full JUCE licence terms are at
  <https://github.com/juce-framework/JUCE/blob/master/LICENSE.md>.
- **Why AGPLv3 and not plain MIT:** AB Reference's own source is written by
  T'Zorr, but a plugin built against JUCE's AGPLv3 modules is a combined work
  under AGPL's terms, and can only be distributed under AGPLv3 or a licence
  compatible with it. A commercial JUCE licence would remove this
  requirement; this build does not use one.
- **What AGPLv3 asks of anyone who distributes this plugin (including a
  built AU/VST3, not only source):** make the complete corresponding source
  available under the same licence, and preserve the copyright and licence
  notices. This repository already is that source.

## Everything else

The DSP (loudness metering, true-peak metering, the A/B engine, reference
loading and playback) is AB Reference's own code.
