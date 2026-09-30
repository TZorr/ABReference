//
//  PluginEditor.cpp
//  AB Reference
//

#include "PluginEditor.h"
#include "TextUtf8.h"

namespace
{
    // The panel's fixed layout, in the same numbers as the approved HTML demo
    // (Design Demo/ab-reference.html), so the two can be held side by side.
    constexpr int panelWidth  = 780;
    constexpr int panelHeight = 440;
    constexpr int margin      = 15;
}

//==============================================================================
ABReferenceEditor::ABReferenceEditor (ABReferenceProcessor& p)
    : AudioProcessorEditor (&p), plugin (p)
{
    addAndMakeVisible (aPad);
    addAndMakeVisible (bPad);

    aPad.onClick = [this] { setAB (false); };
    bPad.onClick = [this] { setAB (true); };
    aPad.setCorner ("CHAIN");

    refreshSwitchCaptions();

    for (int slot = 0; slot < ABParams::numSlots; ++slot)
    {
        auto button = std::make_unique<ReferenceSlotButton> (slot);

        button->onSelect         = [this, slot] { selectSlot (slot); };
        button->onLoadRequested  = [this, slot] { chooseReferenceFile (slot); };
        button->onClearRequested = [this, slot] { plugin.clearReference (slot); };
        button->onRevealRequested = [this, slot]
        {
            if (auto clip = plugin.getUiClip (slot))
                clip->file.revealToUser();
        };
        button->onFilesDropped = [this, slot] (const juce::StringArray& files)
        {
            if (! files.isEmpty())
            {
                plugin.loadReference (slot, juce::File (files[0]));
                selectSlot (slot);
            }
        };

        addAndMakeVisible (*button);
        slotButtons[(size_t) slot] = std::move (button);
    }

    // Load and Reset are one-shot buttons in the toggle's look: they never
    // stay lit, because they are not states.
    for (auto* button : { &loadButton, &resetButton })
    {
        button->setClickingTogglesState (false);
        addAndMakeVisible (*button);
    }

    // "+ Load" goes where a drop without a target goes: the first empty slot,
    // or the selected one once all three are full.
    loadButton.onClick  = [this] { chooseReferenceFile (plugin.getSlotForUntargetedDrop()); };
    resetButton.onClick = [this] { plugin.resetMeters(); };

    loadButton.setTooltip ("Load a reference into the first empty slot");
    resetButton.setTooltip ("Reset integrated LUFS and the held true peak");

    addAndMakeVisible (levelMatchButton);
    addAndMakeVisible (loopButton);

    monitorButtons.setLabels (ABParams::monitorNames);
    monitorButtons.onSelect = [this] (int mode) { setMonitor (mode); };
    addAndMakeVisible (monitorButtons);

    trimKnob.setTextValueSuffix (" dB");
    offsetKnob.setTextValueSuffix (" ms");
    trimKnob.setTypeable (true);
    offsetKnob.setTypeable (true);

    offsetKnob.setTooltip ("Shifts the reference along the timeline. Also the "
                           "correction to reach for when a host does not compensate "
                           "its reported position for the chain's latency. "
                           "Click the value to type one.");

    addAndMakeVisible (trimKnob);
    addAndMakeVisible (offsetKnob);

    auto& state = plugin.apvts;
    levelMatchAttachment = std::make_unique<ButtonAttachment> (state, ABParams::levelMatch, levelMatchButton);
    loopAttachment       = std::make_unique<ButtonAttachment> (state, ABParams::loop, loopButton);
    trimKnob.attach   (state, ABParams::trimDb);
    offsetKnob.attach (state, ABParams::offsetMs);

    // Opaque: it fills itself, so a playhead moving 30 times a second does not
    // also repaint the display behind it.
    waveform.setOpaque (true);
    addAndMakeVisible (waveform);

    // The component reports what the mouse did; the parameters are written here
    // and only here. One gesture spans the whole drag, so a host's undo sees one
    // edit rather than one per mouse move.
    waveform.onDragStarted = [this]
    {
        beginGesture (plugin.getLoopStartParamId());
        beginGesture (plugin.getLoopEndParamId());
    };

    waveform.onRegionChanged = [this] (double startSeconds, double endSeconds)
    {
        setLoopRegion (startSeconds, endSeconds);
    };

    waveform.onRegionCleared = [this] { setLoopRegion (0.0, 0.0); };

    waveform.onDragEnded = [this] (bool regionWasSet)
    {
        endGesture (plugin.getLoopStartParamId());
        endGesture (plugin.getLoopEndParamId());

        // Selecting a region switches Loop on. Without this you drag out a
        // section, hear nothing, and conclude the feature is broken - which is
        // the correct conclusion about a control that does nothing. Clearing a
        // region deliberately does not switch it off again: that is a state you
        // chose, and taking it away would be the panel arguing with you.
        if (regionWasSet)
        {
            if (auto* parameter = plugin.apvts.getParameter (ABParams::loop);
                parameter != nullptr && parameter->getValue() <= 0.5f)
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (1.0f);
                parameter->endChangeGesture();
            }
        }
    };

    setWantsKeyboardFocus (true);
    setSize (panelWidth, panelHeight);
    timerCallback();
    startTimerHz (30);
}

ABReferenceEditor::~ABReferenceEditor()
{
    stopTimer();
}

void ABReferenceEditor::setMonitor (int mode)
{
    if (auto* parameter = plugin.apvts.getParameter (ABParams::monitor))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) mode));
        parameter->endChangeGesture();
    }

    monitorButtons.setSelected (mode);
}

//==============================================================================
void ABReferenceEditor::setAB (bool wantB)
{
    if (auto* parameter = plugin.apvts.getParameter (ABParams::ab))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (wantB ? 1.0f : 0.0f);
        parameter->endChangeGesture();
    }
}

void ABReferenceEditor::beginGesture (const char* id)
{
    if (auto* parameter = plugin.apvts.getParameter (id))
        parameter->beginChangeGesture();
}

void ABReferenceEditor::endGesture (const char* id)
{
    if (auto* parameter = plugin.apvts.getParameter (id))
        parameter->endChangeGesture();
}

void ABReferenceEditor::setParameter (const char* id, float plainValue)
{
    if (auto* parameter = plugin.apvts.getParameter (id))
    {
        // convertTo0to1, because setValueNotifyingHost takes a normalised value.
        // Handing it 93.4 seconds instead of 0.0519 fails silently: the value
        // clamps to 1 and the loop end sits at half an hour.
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
    }
}

void ABReferenceEditor::setLoopRegion (double startSeconds, double endSeconds)
{
    setParameter (plugin.getLoopStartParamId(), (float) startSeconds);
    setParameter (plugin.getLoopEndParamId(),   (float) endSeconds);
}

void ABReferenceEditor::refreshSlots()
{
    const int selected = plugin.getSelectedSlot();

    for (int slot = 0; slot < ABParams::numSlots; ++slot)
    {
        auto& button = *slotButtons[(size_t) slot];
        button.setClip (plugin.getUiClip (slot));
        button.setSelected (slot == selected);
        button.setLoading (plugin.isLoadingReference() && plugin.getUiClip (slot) == nullptr
                             && plugin.hasPathFor (slot));
    }
}

bool ABReferenceEditor::keyPressed (const juce::KeyPress& key)
{
    // 1/2/3 pick a reference, next to the existing A and B. Cheap to add and it
    // is the one control on this panel that is worth reaching for without
    // looking, now that there are three of them.
    for (int slot = 0; slot < ABParams::numSlots; ++slot)
    {
        if (key.getTextCharacter() == (juce::juce_wchar) ('1' + slot))
        {
            selectSlot (slot);
            return true;
        }
    }

    // Space and B both flip the switch. Space because the hand is already there
    // and this is the only control on the panel worth a bare keystroke; B
    // because space is the transport in every host and muscle memory will
    // eventually send it to the wrong window.
    if (key == juce::KeyPress::spaceKey || key.getTextCharacter() == 'b' || key.getTextCharacter() == 'B')
    {
        setAB (! (plugin.apvts.getRawParameterValue (ABParams::ab)->load() > 0.5f));
        return true;
    }

    if (key.getTextCharacter() == 'a' || key.getTextCharacter() == 'A')
    {
        setAB (false);
        return true;
    }

    return false;
}

//==============================================================================
void ABReferenceEditor::selectSlot (int slot)
{
    // Selecting a reference is the statement that you want to hear it, so the
    // A/B switch follows. Getting back to A is one keystroke.
    plugin.setSelectedSlot (slot, true);
    refreshSlots();
}

void ABReferenceEditor::chooseReferenceFile (int slot)
{
    fileChooser = std::make_unique<juce::FileChooser> ("Choose a reference",
                                                       juce::File(),
                                                       plugin.getReferenceFileFilter());

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                | juce::FileBrowserComponent::canSelectFiles,
                              [this, slot] (const juce::FileChooser& chooser)
                              {
                                  const auto file = chooser.getResult();

                                  if (file != juce::File())
                                  {
                                      plugin.loadReference (slot, file);
                                      selectSlot (slot);
                                  }
                              });
}

bool ABReferenceEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (files.isEmpty())
        return false;

    const auto wildcard = plugin.getReferenceFileFilter();

    for (const auto& path : files)
        if (juce::File (path).hasFileExtension (wildcard.replace ("*.", "").replace (";", ";")))
            return true;

    // Fall back to accepting the drag and letting the loader report a format it
    // cannot read: a wildcard-matching failure and an unreadable file produce
    // the same message, and refusing the drop silently is the worse of the two.
    return true;
}

void ABReferenceEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    dragHighlight = true;
    repaint();
}

void ABReferenceEditor::fileDragExit (const juce::StringArray&)
{
    dragHighlight = false;
    repaint();
}

void ABReferenceEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHighlight = false;
    repaint();

    if (files.isEmpty())
        return;

    // The drop did not name a slot, so it goes to the first empty one: dragging
    // three files in fills 1, 2, 3 rather than overwriting the same slot twice.
    const int slot = plugin.getSlotForUntargetedDrop();
    plugin.loadReference (slot, juce::File (files[0]));
    selectSlot (slot);
}

//==============================================================================
void ABReferenceEditor::resized()
{
    // Top: the wordmark over the two pads on the left, the slot row over the
    // display on the right.
    wordmarkArea = { margin, 22, 240, 60 };

    aPad.setBounds (margin, 92, 117, 124);
    bPad.setBounds (margin + 123, 92, 117, 124);

    // The slots share the row with "+ Load". Each slot's right edge is computed
    // from the full width rather than accumulated, so integer division cannot
    // drift the last edge by a pixel or two.
    {
        juce::Rectangle<int> row (267, margin, panelWidth - margin - 267, 26);
        const int gap = 5;
        const int loadWidth = 63;
        const int slotsWidth = row.getWidth() - loadWidth - gap;

        for (int slot = 0; slot < ABParams::numSlots; ++slot)
        {
            const int x0 = row.getX() + (slotsWidth + gap) * slot / ABParams::numSlots;
            const int x1 = row.getX() + (slotsWidth + gap) * (slot + 1) / ABParams::numSlots - gap;
            slotButtons[(size_t) slot]->setBounds (x0, row.getY(), x1 - x0, row.getHeight());
        }

        loadButton.setBounds (row.removeFromRight (loadWidth));
    }

    displayArea = { 267, 49, panelWidth - margin - 267, 167 };

    // The strip inside the display, under two lines of text and over one.
    waveform.setBounds (displayArea.getX() + 14, displayArea.getY() + 54, displayArea.getWidth() - 28, 86);

    // Bottom: what the plugin measured, and what it is doing about it.
    loudnessArea = { margin, 226, 300, 182 };
    matchArea    = { margin + 310, 226, panelWidth - 2 * margin - 310, 182 };

    resetButton.setBounds (loudnessArea.getRight() - 10 - 62, loudnessArea.getY() + 6, 62, 18);
    meterTableArea = loudnessArea.withTrimmedTop (32).reduced (10, 0).withTrimmedBottom (10);

    const int column = matchArea.getX() + 10;

    levelMatchButton.setBounds (column, matchArea.getY() + 34, 150, 26);
    loopButton.setBounds       (column, matchArea.getY() + 70, 150, 26);
    monitorLabelArea = { column, matchArea.getY() + 110, 150, 12 };
    monitorButtons.setBounds   (column, matchArea.getY() + 126, 150, 26);

    trimKnob.setBounds   (column + 160, matchArea.getY() + 42, 95, 82);
    offsetKnob.setBounds (column + 255, matchArea.getY() + 42, 95, 82);

    matchReadoutArea = { matchArea.getRight() - 10 - 64, matchArea.getY() + 32, 64, 120 };

    footerArea = { margin, 418, panelWidth - 2 * margin, 14 };
}

//==============================================================================
void ABReferenceEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::body);

    paintWordmark (g);
    paintDisplay (g);

    paintSection (g, loudnessArea, "Loudness");
    paintSection (g, matchArea, "Match");

    g.setColour (Palette::inkDim);
    g.setFont (Palette::label (9.0f));
    g.drawText ("MONITOR", monitorLabelArea, juce::Justification::centredLeft);

    paintMeterTable (g);
    paintMatchReadout (g);
    paintFooter (g);

    if (dragHighlight)
    {
        g.setColour (Palette::accent);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 6.0f, 2.0f);
    }
}

void ABReferenceEditor::paintWordmark (juce::Graphics& g)
{
    // As on Kitbox and Rackbox: the first half in the accent, the rest in ink.
    // 26 px rather than their 30, because "REFERENCE" is eleven letters to
    // their six or seven and has to end before the slot row does.
    auto area = wordmarkArea.toFloat();
    const auto font = juce::Font (juce::FontOptions (26.0f, juce::Font::bold)).withExtraKerningFactor (0.14f);
    g.setFont (font);

    const auto abWidth = juce::GlyphArrangement::getStringWidth (font, "AB");
    auto nameRow = area.removeFromTop (30.0f);

    g.setColour (Palette::accent);
    g.drawText ("AB", nameRow, juce::Justification::centredLeft);
    g.setColour (Palette::ink);
    g.drawText ("REFERENCE", nameRow.withTrimmedLeft (abWidth + 1.0f), juce::Justification::centredLeft);

    area.removeFromTop (8.0f);
    g.setColour (Palette::inkDim);
    g.setFont (Palette::label (9.5f));
    g.drawText ("MASTERING A/B", area.removeFromTop (14.0f), juce::Justification::centredLeft);
}

void ABReferenceEditor::paintDisplay (juce::Graphics& g)
{
    g.setColour (Palette::display);
    g.fillRoundedRectangle (displayArea.toFloat(), 6.0f);

    auto inner = displayArea.reduced (14, 0);

    // Top line: the reference's name, and the transport on the right. Green is
    // the lamp and nothing else on the panel, so it cannot be mistaken for B.
    auto titleRow = inner.withY (displayArea.getY() + 11).withHeight (18);

    {
        auto lampRow = titleRow.removeFromRight (86);
        const auto lamp = lampRow.removeFromLeft (8).toFloat().withSizeKeepingCentre (7.0f, 7.0f);

        g.setColour (transportRunning ? Palette::lamp : Palette::displayDim);
        g.fillEllipse (lamp);

        lampRow.removeFromLeft (6);
        g.setColour (transportRunning ? Palette::displayText : Palette::displayDim);
        g.setFont (Palette::mono (10.5f));
        g.drawText (transportRunning ? "PLAYING" : "STOPPED", lampRow, juce::Justification::centredLeft);
    }

    g.setColour (Palette::displayText);
    g.setFont (Palette::mono (14.0f, true));
    g.drawText (displayTitle, titleRow.withTrimmedRight (10), juce::Justification::centredLeft, true);

    g.setColour (Palette::displayDim);
    g.setFont (Palette::mono (10.5f));
    g.drawText (displayInfo, inner.withY (displayArea.getY() + 33).withHeight (14),
                juce::Justification::centredLeft, true);

    // Under the strip: which part of it is looping, and where the playhead is.
    auto bottomRow = inner.withY (waveform.getBottom() + 6).withHeight (14);

    g.setColour (Palette::displayText);
    g.drawText (displayTime, bottomRow.removeFromRight (80), juce::Justification::centredRight);

    g.setColour (Palette::displayDim);
    g.drawText (displayLoop, bottomRow, juce::Justification::centredLeft, true);
}

void ABReferenceEditor::paintSection (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title)
{
    g.setColour (Palette::section);
    g.fillRoundedRectangle (bounds.toFloat(), 6.0f);

    g.setColour (Palette::ink);
    g.setFont (Palette::label (10.0f));
    g.drawText (title.toUpperCase(), bounds.reduced (10, 0).withHeight (28), juce::Justification::centredLeft);
}

void ABReferenceEditor::paintMeterTable (juce::Graphics& g)
{
    g.setColour (Palette::display);
    g.fillRoundedRectangle (meterTableArea.toFloat(), 6.0f);

    auto inner = meterTableArea.reduced (14, 12);

    const int labelColumn = 34;
    const int column = (inner.getWidth() - labelColumn) / 3;

    auto cell = [&] (int index, int y, int height)
    {
        return juce::Rectangle<int> (inner.getX() + labelColumn + index * column, y, column, height);
    };

    g.setColour (Palette::displayDim);
    g.setFont (Palette::mono (10.0f));

    const int headerY = inner.getY();
    g.drawText ("LUFS S", cell (0, headerY, 14), juce::Justification::centredRight);
    g.drawText ("LUFS I", cell (1, headerY, 14), juce::Justification::centredRight);
    g.drawText ("dBTP",   cell (2, headerY, 14), juce::Justification::centredRight);

    const auto clip = plugin.getSelectedClip();
    const float fileLufs = clip == nullptr ? kLoudnessSilence
                         : plugin.isMonoOutput() ? clip->integratedLufsMono : clip->integratedLufs;

    struct Row
    {
        const char* name;
        juce::Colour colour;
        float shortTerm, integrated, truePeak;
        bool valid;
    };

    const Row rows[2]
    {
        { "A", Palette::padText,
          plugin.getALufsShortTerm(), plugin.getALufsIntegrated(), plugin.getATruePeakDb(), true },
        { "B", Palette::displayText,
          plugin.getBLufsShortTerm(),
          clip != nullptr ? fileLufs + plugin.getTotalRefDb() : kLoudnessSilence,
          plugin.getBTruePeakDb(),
          clip != nullptr }
    };

    int y = headerY + 24;

    for (const auto& row : rows)
    {
        g.setColour (row.colour);
        g.setFont (Palette::mono (14.0f, true));
        g.drawText (row.name, inner.getX(), y, labelColumn, 26, juce::Justification::centredLeft);

        g.setFont (Palette::mono (17.0f, true));

        // The one number that gets its own colour is a reference pushed past
        // 0 dBTP by the level match. That is not an error - it is the plugin
        // telling you the match it was asked for cannot be delivered without
        // the reference clipping, which is itself a fact about the two masters.
        const bool clipping = row.valid && row.truePeak > 0.0f;

        g.setColour (row.valid ? row.colour : Palette::displayDim);
        g.drawText (row.valid ? formatLufs (row.shortTerm)  : juce::String ("-"), cell (0, y, 26), juce::Justification::centredRight);
        g.drawText (row.valid ? formatLufs (row.integrated) : juce::String ("-"), cell (1, y, 26), juce::Justification::centredRight);

        g.setColour (clipping ? Palette::warning : (row.valid ? row.colour : Palette::displayDim));
        g.drawText (row.valid ? formatDb (row.truePeak) : juce::String ("-"), cell (2, y, 26), juce::Justification::centredRight);

        y += 30;
    }

    // B minus A, in the display's dim tone: the difference is what you are
    // listening for, and a number is quicker to read than two.
    auto difference = [] (float a, float b, float floor)
    {
        if (a <= floor || b <= floor)
            return juce::String ("-");

        return formatDb (b - a, true);
    };

    g.setColour (Palette::displayDim);
    g.setFont (Palette::mono (14.0f));
    g.drawText (utf8 ("Δ"), inner.getX(), y, labelColumn, 26, juce::Justification::centredLeft);

    g.setFont (Palette::mono (13.0f));

    if (rows[1].valid)
    {
        g.drawText (difference (rows[0].shortTerm,  rows[1].shortTerm,  kLoudnessSilence), cell (0, y, 26), juce::Justification::centredRight);
        g.drawText (difference (rows[0].integrated, rows[1].integrated, kLoudnessSilence), cell (1, y, 26), juce::Justification::centredRight);
        g.drawText (difference (rows[0].truePeak,   rows[1].truePeak,   -200.0f),          cell (2, y, 26), juce::Justification::centredRight);
    }
}

void ABReferenceEditor::paintMatchReadout (juce::Graphics& g)
{
    g.setColour (Palette::display);
    g.fillRoundedRectangle (matchReadoutArea.toFloat(), 6.0f);

    const bool on = plugin.apvts.getRawParameterValue (ABParams::levelMatch)->load() > 0.5f;
    const bool clamped = plugin.isMatchClamped();

    auto area = matchReadoutArea.reduced (4, 14);

    g.setColour (Palette::displayDim);
    g.setFont (Palette::mono (10.0f));
    g.drawText ("MATCH", area.removeFromTop (12), juce::Justification::centred);

    area.removeFromTop (12);

    g.setColour (clamped ? Palette::warning : (on ? Palette::displayText : Palette::displayDim));
    g.setFont (Palette::mono (19.0f, true));
    g.drawText (on ? formatDb (plugin.getMatchDb(), true) : juce::String ("OFF"),
                area.removeFromTop (22), juce::Justification::centred);

    g.setFont (Palette::mono (10.0f));
    g.drawText (clamped ? "LIMIT" : "dB", area.removeFromTop (14), juce::Justification::centred);

    // What B is actually played at: the match plus the trim.
    area.removeFromTop (10);
    g.setColour (Palette::displayDim);
    g.drawText (utf8 ("→ B ") + formatDb (plugin.getTotalRefDb(), true),
                area.removeFromTop (14), juce::Justification::centred);
}

void ABReferenceEditor::paintFooter (juce::Graphics& g)
{
    g.setFont (Palette::label (9.0f));

    // The status line borrows the footer while it has something to say - a
    // load in progress, a failed load, a match at its limit - and the hints
    // come back when it has not.
    if (statusText.isNotEmpty())
    {
        g.setColour (statusWarns ? Palette::warning : Palette::inkDim);
        g.drawText (statusText, footerArea, juce::Justification::centredLeft, true);
        return;
    }

    g.setColour (Palette::inkDim);
    g.drawText (utf8 ("AB REFERENCE " JucePlugin_VersionString
                      "   \xc2\xb7   CLICK A OR B, OR PRESS SPACE   \xc2\xb7   1 / 2 / 3 PICK A REFERENCE"
                      "   \xc2\xb7   DRAG IN THE WAVEFORM TO LOOP   \xc2\xb7   DROP FILES ON A SLOT"),
                footerArea, juce::Justification::centredLeft, true);
}

//==============================================================================
juce::String ABReferenceEditor::formatLufs (float value)
{
    if (value <= kLoudnessSilence)
        return "-";

    return juce::String (value, 1);
}

juce::String ABReferenceEditor::formatDb (float value, bool withSign)
{
    if (value <= -200.0f)
        return "-";

    const juce::String number (value, 1);
    return withSign && value > 0.0f ? "+" + number : number;
}

juce::String ABReferenceEditor::describeLoopRegion() const
{
    const float startSec = plugin.apvts.getRawParameterValue (plugin.getLoopStartParamId())->load();
    const float endSec   = plugin.apvts.getRawParameterValue (plugin.getLoopEndParamId())->load();

    if (! ABParams::hasLoopRegion (startSec, endSec))
        return "whole file";

    auto timecode = [] (float seconds)
    {
        const int whole = (int) seconds;
        return juce::String (whole / 60) + ":"
                 + juce::String (whole % 60).paddedLeft ('0', 2) + "."
                 + juce::String ((int) ((seconds - (float) whole) * 10.0f));
    };

    return timecode (startSec) + utf8 (" \u2013 ") + timecode (endSec)
             + utf8 ("  ·  ") + juce::String (endSec - startSec, 1) + " s";
}


juce::String ABReferenceEditor::formatTime (double seconds)
{
    const int tenths = juce::jmax (0, (int) (seconds * 10.0));
    const int whole  = tenths / 10;

    return juce::String (whole / 60) + ":" + juce::String (whole % 60).paddedLeft ('0', 2)
             + "." + juce::String (tenths % 10);
}

juce::String ABReferenceEditor::describeLoop() const
{
    const bool loopOn = plugin.apvts.getRawParameterValue (ABParams::loop)->load() > 0.5f;
    return (loopOn ? juce::String ("LOOP ") : utf8 ("LOOP OFF  \xc2\xb7  ")) + describeLoopRegion();
}

juce::String ABReferenceEditor::describeReference() const
{
    const auto clip = plugin.getSelectedClip();

    if (clip == nullptr)
        return utf8 ("No reference loaded — choose a file, or drag one onto this window");

    juce::StringArray parts;

    parts.add ("SLOT " + juce::String (plugin.getSelectedSlot() + 1));

    if (clip->wasResampled())
        parts.add (juce::String (clip->sourceSampleRate / 1000.0, 1) + utf8 (" → ")
                     + juce::String (clip->playbackSampleRate / 1000.0, 1) + " kHz");
    else
        parts.add (juce::String (clip->playbackSampleRate / 1000.0, 1) + " kHz");

    const int seconds = (int) clip->getLengthSeconds();
    parts.add (juce::String (seconds / 60) + ":" + juce::String (seconds % 60).paddedLeft ('0', 2));

    // On a mono bus the reference is heard folded, and its loudness is quoted
    // for the fold - that is the figure the level match works from.
    const bool monoOut = plugin.isMonoOutput();

    if (monoOut)
        parts.add ("folded to mono");
    else if (clip->sourceChannels == 1)
        parts.add (utf8 ("mono → stereo"));
    else if (clip->sourceChannels > 2)
        parts.add (juce::String (clip->sourceChannels) + utf8 (" channels → first 2"));

    parts.add ("file " + formatLufs (monoOut ? clip->integratedLufsMono : clip->integratedLufs) + " LUFS");
    parts.add (formatDb (clip->truePeakDb) + " dBTP");

    return parts.joinIntoString (utf8 (" \xc2\xb7 "));
}

void ABReferenceEditor::refreshSwitchCaptions()
{
    // Under each letter, what it means at the moment: A is always your own
    // chain; B is whichever reference is selected, by name, so that switching
    // slots changes the switch you are about to press as well.
    const auto clip = plugin.getSelectedClip();

    aPad.setText ("Your chain");
    bPad.setText (clip != nullptr ? clip->file.getFileNameWithoutExtension() : juce::String ("No reference"));
    bPad.setCorner ("SLOT " + juce::String (plugin.getSelectedSlot() + 1));
}

//==============================================================================
void ABReferenceEditor::timerCallback()
{
    const bool onB = plugin.apvts.getRawParameterValue (ABParams::ab)->load() > 0.5f;

    aPad.setLit (! onB);
    bPad.setLit (onB);

    const auto clip = plugin.getSelectedClip();
    refreshSlots();
    refreshSwitchCaptions();

    monitorButtons.setSelected ((int) plugin.apvts.getRawParameterValue (ABParams::monitor)->load());

    waveform.setActive (onB);
    waveform.setClip (clip);
    waveform.setRegion (plugin.apvts.getRawParameterValue (plugin.getLoopStartParamId())->load(),
                        plugin.apvts.getRawParameterValue (plugin.getLoopEndParamId())->load());
    waveform.setPlayhead (plugin.getReferencePositionSeconds(), plugin.isTransportRunning());

    // The display's text, repainted only when some of it changed.
    {
        const auto title   = clip != nullptr ? clip->file.getFileNameWithoutExtension() : juce::String ("NO REFERENCE");
        const auto info    = describeReference();
        const auto loop    = describeLoop();
        const auto time    = clip != nullptr ? formatTime (plugin.getReferencePositionSeconds()) : juce::String();
        const bool running = plugin.isTransportRunning();

        if (title != displayTitle || info != displayInfo || loop != displayLoop
            || time != displayTime || running != transportRunning)
        {
            displayTitle = title;
            displayInfo  = info;
            displayLoop  = loop;
            displayTime  = time;
            transportRunning = running;
            repaint (displayArea);
        }
    }

    const bool clamped = plugin.isMatchClamped();

    juce::String status = plugin.getStatusMessage();

    // The status colour comes from a flag, not from matching words inside the
    // message. Colouring by substring works right up until somebody rewords the
    // string, and then it fails silently and in the safe-looking direction.
    bool warn = plugin.didLoadFail();

    if (plugin.isLoadingReference())
    {
        status = "Loading ...";
        warn = false;
    }
    else if (status.isEmpty() && clamped)
    {
        status = "Level match is at its limit: the reference is "
                   + juce::String (std::abs (plugin.getDesiredMatchDb()), 1)
                   + " dB louder than A, and only 12 dB of that is applied";
        warn = true;
    }
    else if (status.isEmpty() && clip != nullptr && plugin.getBTruePeakDb() > 0.0f)
    {
        status = "At this gain the reference exceeds 0 dBTP";
        warn = true;
    }

    if (status != statusText || warn != statusWarns)
    {
        statusText  = status;
        statusWarns = warn;
        repaint (footerArea);
    }

    repaint (meterTableArea);
    repaint (matchReadoutArea);
}
