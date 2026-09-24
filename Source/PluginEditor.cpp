//
//  PluginEditor.cpp
//  AB Reference
//

#include "PluginEditor.h"
#include "Palette.h"
#include "TextUtf8.h"

using namespace ABLook;

//==============================================================================
ABReferenceEditor::ABReferenceEditor (ABReferenceProcessor& p)
    : AudioProcessorEditor (&p), plugin (p)
{
    setLookAndFeel (&lookAndFeel);

    auto setupButton = [this] (juce::Button& b) { addAndMakeVisible (b); };

    setupButton (aButton);
    setupButton (bButton);

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
    setupButton (resetButton);
    setupButton (levelMatchButton);
    setupButton (monoButton);
    setupButton (loopButton);

    aButton.onClick = [this] { setAB (false); };
    bButton.onClick = [this] { setAB (true); };
    resetButton.onClick = [this] { plugin.resetMeters(); };

    aButton.setColour (juce::TextButton::buttonOnColourId, accentA);
    bButton.setColour (juce::TextButton::buttonOnColourId, accentB);
    aButton.setClickingTogglesState (false);
    bButton.setClickingTogglesState (false);

    for (auto* button : { &aButton, &bButton })
        button->getProperties().set (ABLookAndFeel::abSwitch, true);

    refreshSwitchCaptions();

    resetButton.setTooltip ("Reset integrated LUFS and the held true peak");

    addAndMakeVisible (trimSlider);
    addAndMakeVisible (offsetSlider);
    trimSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 78, 22);
    offsetSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 78, 22);
    trimSlider.setTextValueSuffix (" dB");
    offsetSlider.setTextValueSuffix (" ms");
    offsetSlider.setTooltip ("Shifts the reference along the timeline. Also the "
                             "correction to reach for when a host does not compensate "
                             "its reported position for the chain's latency.");

    for (auto* label : { &trimLabel, &offsetLabel, &matchLabel, &loopRegionLabel,
                         &infoLabel, &statusLabel })
    {
        addAndMakeVisible (*label);
        label->setColour (juce::Label::textColourId, dimText);
        label->setFont (uiFont (13.0f));
    }

    infoLabel.setFont (uiFont (12.0f));
    statusLabel.setFont (uiFont (12.0f));
    matchLabel.setFont (numberFont (13.0f));
    matchLabel.setColour (juce::Label::textColourId, text);
    matchLabel.setJustificationType (juce::Justification::centredLeft);

    loopRegionLabel.setFont (numberFont (12.0f));
    loopRegionLabel.setJustificationType (juce::Justification::centredRight);

    auto& state = plugin.apvts;
    levelMatchAttachment = std::make_unique<ButtonAttachment> (state, ABParams::levelMatch, levelMatchButton);
    monoAttachment       = std::make_unique<ButtonAttachment> (state, ABParams::monoSum, monoButton);
    loopAttachment       = std::make_unique<ButtonAttachment> (state, ABParams::loop, loopButton);
    trimAttachment       = std::make_unique<SliderAttachment> (state, ABParams::trimDb, trimSlider);
    offsetAttachment     = std::make_unique<SliderAttachment> (state, ABParams::offsetMs, offsetSlider);

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
    setSize (780, 440);
    startTimerHz (30);
}

ABReferenceEditor::~ABReferenceEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
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
    auto area = getLocalBounds().reduced (16);

    headerArea = area.removeFromTop (28);   // title and transport, painted
    area.removeFromTop (12);

    statusLabel.setBounds (area.removeFromBottom (18));
    area.removeFromBottom (6);

    auto cards = area.removeFromBottom (124);
    area.removeFromBottom (14);

    // The switch gets a column of its own, A over B, laid out once the right
    // side has settled so it can span exactly from the slots to the waveform.
    auto switchColumn = area.removeFromLeft (150);
    area.removeFromLeft (12);

    auto fileRow = area.removeFromTop (28);

    // Three equal slots across the full width, with a gap between them. There is
    // no eject button beside them: with three slots one shared clear button has
    // to mean "the selected one", which is a rule you have to know rather than
    // see, and it spends panel width saying so. Clearing lives on each slot's
    // own right-click menu, where it can only mean that slot.
    //
    // Integer division would drift the last edge by a pixel or two, so each
    // slot's right edge is computed from the full width rather than accumulated.
    const int gap = 6;
    const int total = fileRow.getWidth();

    for (int slot = 0; slot < ABParams::numSlots; ++slot)
    {
        const int x0 = fileRow.getX() + (total + gap) * slot / ABParams::numSlots;
        const int x1 = fileRow.getX() + (total + gap) * (slot + 1) / ABParams::numSlots - gap;
        slotButtons[(size_t) slot]->setBounds (x0, fileRow.getY(), x1 - x0, fileRow.getHeight());
    }

    area.removeFromTop (8);

    // What the waveform is of goes under it on the left, and what part of it is
    // looping goes under it on the right - both are facts about the picture
    // directly above them.
    auto infoRow = area.removeFromBottom (18);
    loopRegionLabel.setBounds (infoRow.removeFromRight (210));
    infoLabel.setBounds (infoRow);

    area.removeFromBottom (6);

    waveform.setBounds (area);

    // Still the largest thing on the panel - it is the control that gets hit a
    // few hundred times an evening. Laid out larger than it looks, by the inset
    // the look-and-feel draws it at, so the glow has room and the visible edges
    // line up with the slots above and the waveform beside it.
    switchColumn = switchColumn.withTop (fileRow.getY()).withBottom (waveform.getBottom());

    const int inset = ABLookAndFeel::switchInset;
    const int half  = switchColumn.getHeight() / 2;

    aButton.setBounds (switchColumn.withHeight (half - 5).expanded (inset));
    bButton.setBounds (switchColumn.withTrimmedTop (half + 5).expanded (inset));

    // The two cards: what the plugin measured, and what it is doing about it.
    meterArea = cards.removeFromLeft (300);
    cards.removeFromLeft (12);
    controlsArea = cards;

    auto meterInner = meterArea.reduced (12);
    resetButton.setBounds (meterInner.removeFromTop (22).removeFromRight (60));

    auto controls = controlsArea.reduced (14, 12);

    auto toggleRow = controls.removeFromTop (24);
    levelMatchButton.setBounds (toggleRow.removeFromLeft (124));
    loopButton.setBounds (toggleRow.removeFromRight (70));
    toggleRow.removeFromRight (12);
    monoButton.setBounds (toggleRow.removeFromRight (74));

    // Directly under the switch that produces it, indented to the switch's
    // label rather than to its track, so it reads as that control's value.
    matchLabel.setBounds (controls.removeFromTop (22).withTrimmedLeft (38));

    controls.removeFromTop (2);

    auto trimRow = controls.removeFromTop (26);
    trimLabel.setBounds (trimRow.removeFromLeft (56));
    trimSlider.setBounds (trimRow);

    auto offsetRow = controls.removeFromTop (26);
    offsetLabel.setBounds (offsetRow.removeFromLeft (56));
    offsetSlider.setBounds (offsetRow);
}

//==============================================================================
void ABReferenceEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    paintHeader (g);

    paintCard (g, meterArea);
    paintCard (g, controlsArea);

    paintMeterTable (g);

    if (dragHighlight)
    {
        g.setColour (accentB.withAlpha (0.8f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (4.0f), cardRadius, 2.0f);
    }
}

void ABReferenceEditor::paintHeader (juce::Graphics& g)
{
    g.setColour (text);
    g.setFont (uiFont (15.0f, juce::Font::bold).withExtraKerningFactor (0.08f));
    g.drawText ("AB REFERENCE", headerArea, juce::Justification::centredLeft);

    // The transport as a pill with a lamp in it. Green is used for this and for
    // nothing else on the panel, so it cannot be mistaken for A, B or a warning.
    const bool running = plugin.isTransportRunning();
    const juce::String label = running ? "PLAYING" : "STOPPED";

    const auto font = uiFont (10.5f, juce::Font::bold).withExtraKerningFactor (0.06f);

    // Fixed width, wide enough for either word: a pill that resized itself
    // whenever the transport started would make the header twitch.
    auto pill = headerArea.toFloat().removeFromRight (96.0f).withSizeKeepingCentre (96.0f, 22.0f);

    g.setColour (card);
    g.fillRoundedRectangle (pill, 11.0f);
    g.setColour (outline);
    g.drawRoundedRectangle (pill.reduced (0.5f), 10.5f, 1.0f);

    auto inside = pill.reduced (11.0f, 0.0f);
    const auto lamp = inside.removeFromLeft (7.0f).withSizeKeepingCentre (7.0f, 7.0f);

    if (running)
    {
        g.setColour (ABLook::transportLamp.withAlpha (0.35f));
        g.fillEllipse (lamp.expanded (2.5f));
    }

    g.setColour (running ? ABLook::transportLamp : dimText.withAlpha (0.6f));
    g.fillEllipse (lamp);

    inside.removeFromLeft (7.0f);
    g.setColour (running ? text : dimText);
    g.setFont (font);
    g.drawText (label, inside, juce::Justification::centredLeft);
}

void ABReferenceEditor::paintCard (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    const auto area = bounds.toFloat();

    g.setColour (card);
    g.fillRoundedRectangle (area, cardRadius);
    g.setColour (outline);
    g.drawRoundedRectangle (area.reduced (0.5f), cardRadius, 1.0f);
}

void ABReferenceEditor::paintMeterTable (juce::Graphics& g)
{
    auto inner = meterArea.reduced (14, 12);

    g.setColour (dimText);
    g.setFont (uiFont (10.5f, juce::Font::bold).withExtraKerningFactor (0.08f));
    g.drawText ("LOUDNESS", inner.removeFromTop (22), juce::Justification::centredLeft);

    inner.removeFromTop (6);

    const int left  = inner.getX();
    const int width = inner.getWidth();

    const int labelColumn = 30;
    const int column = (width - labelColumn) / 3;

    auto columnBounds = [&] (int index, int y, int height)
    {
        return juce::Rectangle<int> (left + labelColumn + index * column, y, column, height);
    };

    g.setColour (dimText);
    g.setFont (uiFont (11.0f));

    const int headerY = inner.getY();
    g.drawText ("LUFS S",  columnBounds (0, headerY, 14), juce::Justification::centredRight);
    g.drawText ("LUFS I",  columnBounds (1, headerY, 14), juce::Justification::centredRight);
    g.drawText ("dBTP",    columnBounds (2, headerY, 14), juce::Justification::centredRight);
    const auto clip = plugin.getSelectedClip();

    struct Row
    {
        const char* name;
        juce::Colour colour;
        float shortTerm, integrated, truePeak;
        bool valid;
    };

    const Row rows[2]
    {
        { "A", accentA,
          plugin.getALufsShortTerm(), plugin.getALufsIntegrated(), plugin.getATruePeakDb(), true },
        { "B", accentB,
          plugin.getBLufsShortTerm(),
          clip != nullptr ? clip->integratedLufs + plugin.getTotalRefDb() : kLoudnessSilence,
          plugin.getBTruePeakDb(),
          clip != nullptr }
    };

    int y = headerY + 18;

    for (const auto& row : rows)
    {
        g.setColour (row.colour);
        g.setFont (uiFont (14.0f, juce::Font::bold));
        g.drawText (row.name, left, y, labelColumn, 26, juce::Justification::centredLeft);

        g.setFont (numberFont (16.0f));

        // The one number that gets its own colour is a reference pushed past
        // 0 dBTP by the level match. That is not an error - it is the plugin
        // telling you the match it was asked for cannot be delivered without
        // the reference clipping, which is itself a fact about the two masters.
        const bool clipping = row.valid && row.truePeak > 0.0f;

        g.setColour (row.valid ? text : dimText);
        g.drawText (row.valid ? formatLufs (row.shortTerm)  : juce::String ("-"), columnBounds (0, y, 26), juce::Justification::centredRight);
        g.drawText (row.valid ? formatLufs (row.integrated) : juce::String ("-"), columnBounds (1, y, 26), juce::Justification::centredRight);

        g.setColour (clipping ? warning : (row.valid ? text : dimText));
        g.drawText (row.valid ? formatDb (row.truePeak) : juce::String ("-"), columnBounds (2, y, 26), juce::Justification::centredRight);

        y += 28;
    }
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

juce::String ABReferenceEditor::describeReference() const
{
    const auto clip = plugin.getSelectedClip();

    if (clip == nullptr)
        return utf8 ("No reference loaded \u2014 choose a file, or drag one onto this window");

    juce::StringArray parts;

    if (clip->wasResampled())
        parts.add (juce::String (clip->sourceSampleRate / 1000.0, 1) + utf8 (" \u2192 ")
                     + juce::String (clip->playbackSampleRate / 1000.0, 1) + " kHz");
    else
        parts.add (juce::String (clip->playbackSampleRate / 1000.0, 1) + " kHz");

    const int seconds = (int) clip->getLengthSeconds();
    parts.add (juce::String (seconds / 60) + ":" + juce::String (seconds % 60).paddedLeft ('0', 2));

    if (clip->sourceChannels == 1)
        parts.add (utf8 ("mono \u2192 stereo"));
    else if (clip->sourceChannels > 2)
        parts.add (juce::String (clip->sourceChannels) + utf8 (" channels \u2192 first 2"));

    parts.add ("file " + formatLufs (clip->integratedLufs) + " LUFS");
    parts.add (formatDb (clip->truePeakDb) + " dBTP");

    return parts.joinIntoString (utf8 ("  ·  "));
}

void ABReferenceEditor::refreshSwitchCaptions()
{
    // Under each letter, what it means at the moment: A is always your own
    // chain; B is whichever reference is selected, by name, so that switching
    // slots changes the switch you are about to press as well.
    const auto clip = plugin.getSelectedClip();
    const juce::String bCaption = clip != nullptr ? clip->file.getFileNameWithoutExtension()
                                                  : juce::String ("No reference");

    auto setCaption = [] (juce::TextButton& button, const juce::String& caption)
    {
        if (button.getProperties()[ABLookAndFeel::caption].toString() != caption)
        {
            button.getProperties().set (ABLookAndFeel::caption, caption);
            button.repaint();
        }
    };

    setCaption (aButton, "Your chain");
    setCaption (bButton, bCaption);
}

//==============================================================================
void ABReferenceEditor::timerCallback()
{
    const bool onB = plugin.apvts.getRawParameterValue (ABParams::ab)->load() > 0.5f;

    aButton.setToggleState (! onB, juce::dontSendNotification);
    bButton.setToggleState (onB, juce::dontSendNotification);

    const auto clip = plugin.getSelectedClip();
    refreshSlots();
    refreshSwitchCaptions();

    if (const bool running = plugin.isTransportRunning(); running != lastTransportRunning)
    {
        lastTransportRunning = running;
        repaint (headerArea);
    }

    waveform.setActive (onB);

    infoLabel.setText (describeReference(), juce::dontSendNotification);

    waveform.setClip (clip);
    waveform.setRegion (plugin.apvts.getRawParameterValue (plugin.getLoopStartParamId())->load(),
                        plugin.apvts.getRawParameterValue (plugin.getLoopEndParamId())->load());
    waveform.setPlayhead (plugin.getReferencePositionSeconds(), plugin.isTransportRunning());

    loopRegionLabel.setText (describeLoopRegion(), juce::dontSendNotification);

    const float match = plugin.getMatchDb();
    const float total = plugin.getTotalRefDb();

    const bool clamped = plugin.isMatchClamped();

    matchLabel.setColour (juce::Label::textColourId, clamped ? warning : text);
    matchLabel.setText (utf8 ("Match ") + formatDb (match, true)
                          + (clamped ? utf8 (" dB (limit) \u2192 B ") : utf8 (" dB \u2192 B "))
                          + formatDb (total, true) + " dB",
                        juce::dontSendNotification);

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

    statusLabel.setColour (juce::Label::textColourId, warn ? warning : dimText);
    statusLabel.setText (status, juce::dontSendNotification);

    repaint (meterArea);
}
