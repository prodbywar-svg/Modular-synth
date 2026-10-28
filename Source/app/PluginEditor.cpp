#include "PluginEditor.h"

using namespace ms;

namespace
{
constexpr int kHeaderHeight = 84;
constexpr int kMasterHeight = 250;
constexpr int kStatusHeight = 24;
constexpr int kMargin = 16;
constexpr int kStripHeight = 64;
constexpr int kToolsLabelHeight = 26;

const char* toolTitle (int tool)
{
    switch (toolKind (tool))
    {
        case ToolKind::Drums: return "DRUM MACHINE";
        case ToolKind::Count: break;
    }
    return "TOOL";
}
} // namespace

// =============================================================================
// A tool's window. Closing it only hides it: the tool keeps playing.

class ToolWindow : public juce::DocumentWindow
{
public:
    ToolWindow (const juce::String& name, std::unique_ptr<juce::Component> content)
        : DocumentWindow (name, look::background, juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (content.release(), true);
        setResizable (true, false);
    }

    void closeButtonPressed() override
    {
        setVisible (false);
        notify();
    }

    void moved() override
    {
        DocumentWindow::moved();
        notify();
    }

    void resized() override
    {
        DocumentWindow::resized();
        notify();
    }

    std::function<void()> onTick, onLayoutChange;

private:
    void notify()
    {
        if (onLayoutChange)
            onLayoutChange();
    }
};

// =============================================================================
// One tool's row in the studio window: its output as a scrolling barcode,
// its level, mute / solo, and the buttons that show its window or remove it.

class ToolStrip : public juce::Component
{
public:
    ToolStrip (ModularSynthEditor& e, int t) : editor (e), tool (t)
    {
        for (auto* b : { &addButton, &showButton, &removeButton, &muteButton, &soloButton })
            addAndMakeVisible (*b);
        addAndMakeVisible (level);

        addButton.setText ("+ ADD " + juce::String (toolTitle (tool)));
        addButton.onClick = [this] { editor.addTool (tool); };
        showButton.onClick = [this] { editor.showToolWindow (tool, ! editor.isToolWindowShowing (tool)); };
        removeButton.onClick = [this] { editor.removeTool (tool); };
        muteButton.onClick = [this] {
            editor.send (CommandType::SetToolMute, tool, 0, 0, mix().mute ? 0.0f : 1.0f);
        };
        soloButton.onClick = [this] {
            editor.send (CommandType::SetToolSolo, tool, 0, 0, mix().solo ? 0.0f : 1.0f);
        };

        level.setLabel ("LEVEL");
        level.setDefault (ProjectState().masterLevel);
        level.format = [] (float v) { return formatDb (levelToGain (v)); };
        level.onChange = [this] (float v) { editor.send (CommandType::SetMasterLevel, tool, 0, 0, v); };
    }

    void tick()
    {
        const int active = mix().active ? 1 : 0;
        if (active != shownActive)
        {
            shownActive = active;
            for (juce::Component* c : { (juce::Component*) &showButton, (juce::Component*) &removeButton,
                                        (juce::Component*) &muteButton, (juce::Component*) &soloButton, (juce::Component*) &level })
                c->setVisible (active);
            addButton.setVisible (active == 0);
        }
        const bool showing = editor.isToolWindowShowing (tool);
        showButton.setText (showing ? "HIDE" : "OPEN");
        showButton.setOn (showing);
        muteButton.setOn (mix().mute);
        soloButton.setOn (mix().solo);
        if (toolKind (tool) == ToolKind::Drums)
            level.setValue (editor.processor().getUiState().masterLevel);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const float w = (float) getWidth(), h = (float) getHeight(), mid = h * 0.5f;
        g.setColour (look::faint);
        g.fillRect (0.0f, 0.0f, w, 1.0f);

        const juce::Rectangle<float> tag (4.0f, 6.0f, 30.0f, h - 12.0f);
        const bool active = mix().active;
        g.setColour (active ? look::dim : look::faint);
        g.drawRect (tag, 1.0f);
        g.setFont (look::mono (11.0f, true));
        g.setColour (active ? look::ink : look::dim);
        g.drawText (look::pad (tool + 1, 2), tag, juce::Justification::centred);

        g.setFont (look::mono (13.0f, true));
        g.drawText (toolTitle (tool), juce::Rectangle<float> (48.0f, 4.0f, 220.0f, mid - 2.0f), juce::Justification::bottomLeft);
        g.setFont (look::mono (9.5f));
        g.setColour (look::dim);
        g.drawText (active ? summary() : juce::String ("REMOVED // NOT IN THE PROJECT"),
                    juce::Rectangle<float> (48.0f, mid + 2.0f, 220.0f, mid - 6.0f), juce::Justification::topLeft, true);

        if (! active)
            return;

        // the tool's output, newest on the right: one line per history entry
        const auto& proc = editor.processor();
        const auto box = barcodeArea().toFloat();
        const int columns = juce::jmin (ModularSynthProcessor::kHistoryLength, (int) box.getWidth() / 2);
        const float alpha = mix().mute ? 0.35f : 1.0f;
        for (int c = 0; c < columns; ++c)
        {
            const float v = juce::jlimit (0.0f, 1.0f, std::sqrt (proc.toolHistory (tool, c)) * 1.2f);
            if (v < 0.02f)
                continue;
            const float x = box.getRight() - 2.0f * (float) (c + 1);
            const float lineH = juce::jmax (1.0f, v * box.getHeight());
            g.setColour ((v > 0.95f ? look::red : look::ink).withAlpha (alpha * (0.25f + 0.75f * v)));
            g.fillRect (x, box.getCentreY() - lineH * 0.5f, 1.0f, lineH);
        }
        g.setColour (look::faint);
        g.drawRect (box.expanded (1.0f), 1.0f);
        g.setFont (look::mono (9.0f));
        float peak = 0.0f;
        for (int c = 0; c < 8; ++c)
            peak = juce::jmax (peak, proc.toolHistory (tool, c));
        g.setColour (peak > 1.0f ? look::red : look::dim);
        g.drawText (formatDb (peak), juce::Rectangle<float> (box.getX() - 70.0f, 4.0f, 62.0f, 12.0f), juce::Justification::topRight);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 6);
        auto right = r.removeFromRight (440);
        const int y = r.getCentreY() - 10;
        auto row = juce::Rectangle<int> (right.getX(), y, right.getWidth(), 20);
        level.setBounds (row.removeFromLeft (200));
        row.removeFromLeft (10);
        muteButton.setBounds (row.removeFromLeft (28));
        row.removeFromLeft (4);
        soloButton.setBounds (row.removeFromLeft (28));
        row.removeFromLeft (14);
        showButton.setBounds (row.removeFromLeft (70));
        row.removeFromLeft (6);
        removeButton.setBounds (row.removeFromLeft (84));

        addButton.setBounds (290, y, 220, 20);
    }

private:
    const ToolMix& mix() const { return editor.processor().getStudioState().tools[(size_t) tool]; }

    juce::Rectangle<int> barcodeArea() const
    {
        return { 290, 10, juce::jmax (40, getWidth() - 290 - 460), getHeight() - 20 };
    }

    juce::String summary() const
    {
        const auto& proc = editor.processor();
        if (toolKind (tool) == ToolKind::Drums)
        {
            const auto& d = proc.getUiState();
            int sounding = 0;
            for (const auto& tr : d.tracks)
                for (float v : tr.patterns[(size_t) d.pattern])
                    if (v > 0.0f)
                    {
                        ++sounding;
                        break;
                    }
            return "P" + juce::String (d.pattern + 1) + " // " + look::pad (sounding, 2) + " OF 08 TRACKS PLAYING";
        }
        return {};
    }

    ModularSynthEditor& editor;
    const int tool;
    DataButton addButton { "+ ADD" }, showButton { "OPEN" }, removeButton { "REMOVE" }, muteButton { "M" }, soloButton { "S" };
    DataSlider level;
    int shownActive = -1; // nothing shown yet
};

// =============================================================================
// The studio master: every tool's sum through one more effects rack.

class MasterPanel : public juce::Component
{
public:
    explicit MasterPanel (ModularSynthEditor& e) : editor (e)
    {
        addAndMakeVisible (level);
        level.setLabel ("LEVEL");
        level.setDefault (StudioState().masterLevel);
        level.format = [] (float v) { return formatDb (levelToGain (v)); };
        level.onChange = [this] (float v) { editor.send (CommandType::SetMasterLevel, kStudio, 0, 0, v); };

        fx.attach (*this, [this] { return editor.processor().getStudioState().masterFx; },
                   [this] (int i, float v) { editor.send (CommandType::SetFxParam, kStudio, 0, i, v); });
    }

    void refresh()
    {
        const auto& s = editor.processor().getStudioState();
        level.setValue (s.masterLevel);
        fx.setValues (s.masterFx);
        repaint (meterArea());
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (look::dim);
        g.fillRect (0, 0, getWidth(), 1);
        g.setFont (look::mono (11.0f, true));
        g.setColour (look::ink);
        g.drawText ("MS MASTER // EVERY TOOL + EFFECTS RACK", 0, 6, getWidth() / 2, 18, juce::Justification::centredLeft);

        g.setFont (look::mono (9.5f));
        g.setColour (look::dim);
        for (int c = 0; c < kColumns; ++c)
            g.drawText (c == 0 ? "LEVEL + PEAK" : FxRackControls::groupName (c - 1), columnX (c), 30, columnX (1) - 24, 14,
                        juce::Justification::centredLeft);

        const auto m = meterArea().toFloat();
        paintPeakMeter (g, m.getX(), m.getCentreY(), m.getWidth(), editor.processor().masterPeak(), hold);
        g.setColour (look::dim);
        g.drawFittedText ("THE STUDIO MASTER COMES AFTER EACH TOOL'S OWN BUS. STEMS INCLUDE THEIR SHARE OF IT.",
                          juce::Rectangle<int> (columnX (0), m.toNearestInt().getBottom() + 18, columnX (1) - 24, 40),
                          juce::Justification::topLeft, 3);
    }

    void resized() override
    {
        const int rowH = 20, y0 = 48;
        const int colW = columnX (1) - columnX (0) - 24;
        level.setBounds (columnX (0), y0, colW, rowH);
        fx.layout ([this] (int c) { return columnX (c + 1); }, y0, rowH, colW);
    }

private:
    static constexpr int kColumns = 5;
    int columnX (int c) const { return c * (getWidth() + 24) / kColumns; }
    juce::Rectangle<int> meterArea() const { return { columnX (0), 96, columnX (1) - columnX (0) - 24, 44 }; }

    ModularSynthEditor& editor;
    DataSlider level;
    FxRackControls fx;
    float hold = 0.0f;
};

// =============================================================================
// Export runs off the message thread so the UI keeps drawing.

namespace
{
class ExportThread : public juce::Thread
{
public:
    ExportThread (Take t, ExportOptions o, std::atomic<float>& progressOut,
                  std::function<void (ExportResult)> done)
        : Thread ("Export"), take (std::move (t)), options (std::move (o)), progress (progressOut), onDone (std::move (done))
    {
    }

    void run() override
    {
        auto result = exportTake (take, options, [this] (float p) {
            progress = p;
            return ! threadShouldExit();
        });
        progress = -1.0f;
        juce::MessageManager::callAsync ([cb = onDone, result] { cb (result); });
    }

private:
    Take take;
    ExportOptions options;
    std::atomic<float>& progress;
    std::function<void (ExportResult)> onDone;
};
} // namespace

// =============================================================================

ModularSynthEditor::ModularSynthEditor (ModularSynthProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    juce::LookAndFeel::setDefaultLookAndFeel (&sharedLookAndFeel());
    setLookAndFeel (&sharedLookAndFeel());

    for (int t = 0; t < kNumToolSlots; ++t)
    {
        strips[(size_t) t] = std::make_unique<ToolStrip> (*this, t);
        addAndMakeVisible (*strips[(size_t) t]);
    }
    master = std::make_unique<MasterPanel> (*this);
    addAndMakeVisible (*master);

    for (auto* b : { &playButton, &stopButton, &recButton, &exportButton, &saveButton, &openButton })
        addAndMakeVisible (*b);
    recButton.setAccent (look::red);

    playButton.onClick = [this] { togglePlay(); };
    stopButton.onClick = [this] { stop(); };
    recButton.onClick = [this] { toggleRecord(); };
    exportButton.onClick = [this] { chooseExport(); };
    saveButton.onClick = [this] { saveProject(); };
    openButton.onClick = [this] { openProject(); };

    for (auto* r : { &bpmReadout, &swingReadout, &stepReadout, &posReadout, &rateReadout, &takeReadout })
        addAndMakeVisible (*r);

    bpmReadout.setLabel ("BPM / DRAG");
    bpmReadout.onDrag = [this] (float px, bool fine) {
        send (CommandType::SetBpm, kStudio, 0, 0, proc.getStudioState().bpm + px * (fine ? 0.1f : 0.5f));
    };
    bpmReadout.onDoubleClick = [this] { send (CommandType::SetBpm, kStudio, 0, 0, 132.0f); };

    swingReadout.setLabel ("SWING / DRAG");
    swingReadout.onDrag = [this] (float px, bool fine) {
        send (CommandType::SetSwing, kStudio, 0, 0, proc.getStudioState().swing + px * (fine ? 0.002f : 0.01f));
    };
    swingReadout.onDoubleClick = [this] { send (CommandType::SetSwing, kStudio, 0, 0, 0.0f); };

    stepReadout.setLabel ("BEAT / STEP");
    posReadout.setLabel ("POSITION");
    rateReadout.setLabel ("SAMPLE RATE");
    takeReadout.setLabel ("TAKE");

    lastFolder = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Modular-synth");

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (1000, 560, 2600, 1700);
    setSize (1240, 600);
    startTimerHz (60);
}

ModularSynthEditor::~ModularSynthEditor()
{
    stopTimer();
    if (exportThread != nullptr)
        exportThread->stopThread (15000);
    for (auto& w : windows)
        w.reset();
    setLookAndFeel (nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void ModularSynthEditor::send (CommandType type, int tool, int track, int index, float value)
{
    Command c;
    c.type = type;
    c.tool = tool;
    c.track = track;
    c.index = index;
    c.value = value;
    proc.send (c);
}

void ModularSynthEditor::setStatus (const juce::String& s)
{
    status = s;
    statusTime = juce::Time::getMillisecondCounter();
    repaint (0, getHeight() - kStatusHeight, getWidth(), kStatusHeight);
}

// ---- tool windows -------------------------------------------------------------

bool ModularSynthEditor::isToolWindowShowing (int tool) const
{
    const auto& w = windows[(size_t) tool];
    return w != nullptr && w->isVisible();
}

juce::Rectangle<int> ModularSynthEditor::defaultToolBounds (int tool) const
{
    // a little below and to the right of the studio window, on its screen
    const auto studio = getTopLevelComponent() != nullptr ? getTopLevelComponent()->getScreenBounds() : getScreenBounds();
    juce::Rectangle<int> r (studio.getX() + 48 + 32 * tool, studio.getY() + 72 + 32 * tool, 1320, 820);
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (studio))
        r = r.constrainedWithin (display->userArea);
    return r;
}

void ModularSynthEditor::createToolWindow (int tool, juce::Rectangle<int> bounds)
{
    auto view = std::make_unique<DrumMachineView> (proc);
    auto* raw = view.get();
    view->onUnhandledKey = [this] (const juce::KeyPress& key) { return handleStudioKey (key); };

    auto window = std::make_unique<ToolWindow> ("MODULAR-SYNTH // " + juce::String (look::pad (tool + 1, 2)) + " " + toolTitle (tool), std::move (view));
    window->setResizeLimits (1100, 720, 2600, 1700);
    window->setBounds (bounds);
    window->onTick = [raw] { raw->tick(); };
    window->onLayoutChange = [this] { storeWindowLayout(); };
    windows[(size_t) tool] = std::move (window);
}

void ModularSynthEditor::showToolWindow (int tool, bool show)
{
    if (! validTool (tool) || ! proc.getStudioState().tools[(size_t) tool].active)
        return;
    auto& w = windows[(size_t) tool];
    if (w == nullptr)
        createToolWindow (tool, defaultToolBounds (tool));
    w->setVisible (show);
    if (show)
    {
        w->toFront (true);
        if (auto* content = w->getContentComponent())
            content->grabKeyboardFocus();
    }
    storeWindowLayout();
}

void ModularSynthEditor::addTool (int tool)
{
    send (CommandType::AddTool, tool, 0, 0, 0.0f);
    windows[(size_t) tool].reset();
    showToolWindow (tool, true);
    setStatus ("ADDED THE " + juce::String (toolTitle (tool)) + " // A FRESH ONE, ON THE STUDIO CLOCK");
}

void ModularSynthEditor::removeTool (int tool)
{
    const juce::String name = toolTitle (tool);
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::WarningIcon)
                       .withTitle ("REMOVE THE " + name + "?")
                       .withMessage ("Its patterns, sounds and effects leave the project. Save first if you want to keep them.")
                       .withButton ("REMOVE")
                       .withButton ("CANCEL")
                       .withAssociatedComponent (this);
    juce::Component::SafePointer<ModularSynthEditor> safe (this);
    juce::AlertWindow::showAsync (options, [safe, tool, name] (int result) {
        if (safe == nullptr || result != 1)
            return;
        safe->send (CommandType::RemoveTool, tool, 0, 0, 0.0f);
        safe->windows[(size_t) tool].reset();
        safe->storeWindowLayout();
        safe->setStatus ("REMOVED THE " + name + " // ADD IT AGAIN FROM ITS ROW");
    });
}

void ModularSynthEditor::storeWindowLayout()
{
    auto layout = std::make_unique<juce::DynamicObject>();
    for (int t = 0; t < kNumToolSlots; ++t)
    {
        const auto& w = windows[(size_t) t];
        if (w == nullptr)
            continue;
        auto o = std::make_unique<juce::DynamicObject>();
        const auto b = w->getBounds();
        o->setProperty ("open", w->isVisible());
        o->setProperty ("x", b.getX());
        o->setProperty ("y", b.getY());
        o->setProperty ("w", b.getWidth());
        o->setProperty ("h", b.getHeight());
        layout->setProperty (toolName (t), juce::var (o.release()));
    }
    proc.setWindowLayout (juce::var (layout.release()));
}

void ModularSynthEditor::applyWindowLayout()
{
    appliedLayoutVersion = proc.getWindowLayoutVersion();
    const auto layout = proc.getWindowLayout();
    const auto& displays = juce::Desktop::getInstance().getDisplays();

    for (int t = 0; t < kNumToolSlots; ++t)
    {
        if (! proc.getStudioState().tools[(size_t) t].active)
        {
            windows[(size_t) t].reset();
            continue;
        }

        auto bounds = defaultToolBounds (t);
        bool open = true;
        const auto entry = layout.getProperty (toolName (t), {});
        if (entry.isObject())
        {
            open = (bool) entry.getProperty ("open", true);
            const juce::Rectangle<int> saved ((int) entry.getProperty ("x", 0), (int) entry.getProperty ("y", 0),
                                              (int) entry.getProperty ("w", 1320), (int) entry.getProperty ("h", 820));
            // only where a screen still is (the project may come from another computer)
            if (const auto* display = displays.getDisplayForRect (saved); display != nullptr && display->userArea.intersects (saved))
                bounds = saved;
        }

        if (windows[(size_t) t] == nullptr)
            createToolWindow (t, bounds);
        else
            windows[(size_t) t]->setBounds (bounds);
        windows[(size_t) t]->setVisible (open);
    }
    storeWindowLayout();
}

// ---- transport, export, files -------------------------------------------------

void ModularSynthEditor::togglePlay()
{
    send (proc.isPlaying() ? CommandType::Stop : CommandType::Play, kStudio, 0, 0, 0.0f);
}

void ModularSynthEditor::stop()
{
    send (CommandType::Stop, kStudio, 0, 0, 0.0f);
}

void ModularSynthEditor::toggleRecord()
{
    if (proc.isRecording())
    {
        send (CommandType::RecordStop, kStudio, 0, 0, 0.0f);
    }
    else
    {
        send (CommandType::RecordStart, kStudio, 0, 0, 0.0f);
        setStatus ("RECORDING EVERY TOOL / PLAY PADS, MUTE, MOVE ANYTHING / R OR STOP TO END");
    }
}

void ModularSynthEditor::chooseExport()
{
    if (exportThread != nullptr && exportThread->isThreadRunning())
    {
        setStatus ("AN EXPORT IS ALREADY RUNNING");
        return;
    }

    juce::PopupMenu m;
    m.addSectionHeader ("EXPORT WAV // MASTER + EACH TOOL + STEMS");
    const auto& take = proc.getLastTake();
    if (take.has_value())
        m.addItem (1, "RECORDED TAKE  " + formatTime ((double) take->length / take->sampleRate));
    else
        m.addItem (1, "RECORDED TAKE  (PRESS REC FIRST)", false);
    m.addSeparator();
    const auto patterns = "DRUMS P" + juce::String (viewedPattern (proc.getUiState()) + 1);
    for (int loops : { 1, 4, 8, 16 })
        m.addItem (100 + loops, "PATTERNS ON SCREEN (" + patterns + ") x" + juce::String (loops));

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&exportButton), [this] (int r) {
        if (r <= 0)
            return;

        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
        if (r == 1 && proc.getLastTake().has_value())
            startExport (*proc.getLastTake(), "take-" + stamp);
        else if (r > 100)
            startExport (makePatternTake (proc.getStudioState(), r - 100, proc.getEngineSampleRate()),
                         "p" + juce::String (viewedPattern (proc.getUiState()) + 1) + "-x" + juce::String (r - 100) + "-" + stamp);
    });
}

void ModularSynthEditor::startExport (Take take, const juce::String& baseName)
{
    lastFolder.createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Choose where to save the WAV files", lastFolder);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, take = std::move (take), baseName] (const juce::FileChooser& fc) mutable {
                              const auto dir = fc.getResult();
                              if (dir == juce::File())
                                  return;
                              lastFolder = dir;

                              ExportOptions opt;
                              opt.folder = dir.getChildFile (baseName);
                              opt.baseName = baseName;
                              opt.bitDepth = 24;
                              for (int t = 0; t < kNumTracks; ++t)
                                  opt.trackNames[(size_t) t] = proc.trackName (t);

                              exportProgress = 0.0f;
                              setStatus ("EXPORTING " + baseName.toUpperCase() + " ...");
                              juce::Component::SafePointer<ModularSynthEditor> safe (this);
                              exportThread = std::make_unique<ExportThread> (
                                  std::move (take), opt, exportProgress, [safe] (ExportResult result) {
                                      if (safe == nullptr)
                                          return;
                                      if (! result.ok)
                                      {
                                          safe->setStatus ("EXPORT FAILED: " + result.error.toUpperCase());
                                          return;
                                      }
                                      safe->setStatus ("EXPORTED " + juce::String (result.files.size()) + " FILES ("
                                                       + formatTime ((double) result.lengthSamples / safe->proc.getEngineSampleRate())
                                                       + ") TO " + result.files[0].getParentDirectory().getFullPathName());
                                      result.files[0].revealToUser();
                                  });
                              exportThread->startThread();
                          });
}

void ModularSynthEditor::saveProject()
{
    storeWindowLayout();
    chooser = std::make_unique<juce::FileChooser> ("Save project", lastFolder.getChildFile ("project.msynth"), "*.msynth");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              file = file.withFileExtension ("msynth");
                              setStatus (proc.saveProject (file) ? "SAVED " + file.getFullPathName()
                                                                 : "COULD NOT SAVE " + file.getFullPathName());
                          });
}

void ModularSynthEditor::openProject()
{
    chooser = std::make_unique<juce::FileChooser> ("Open project", lastFolder, "*.msynth");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              juce::String error;
                              if (proc.openProject (file, error))
                                  setStatus (error.isNotEmpty() ? error.toUpperCase() : "OPENED " + file.getFileName().toUpperCase());
                              else
                                  setStatus (error.toUpperCase());
                          });
}

// ---- keys, timer, drawing -----------------------------------------------------

bool ModularSynthEditor::handleStudioKey (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto c = (juce::juce_wchar) juce::CharacterFunctions::toLowerCase (key.getTextCharacter());

    if (mods.isCommandDown())
    {
        if (key.getKeyCode() == 'S') { saveProject(); return true; }
        if (key.getKeyCode() == 'O') { openProject(); return true; }
        return false;
    }

    if (key == juce::KeyPress::spaceKey) { togglePlay(); return true; }
    if (key == juce::KeyPress::escapeKey) { stop(); return true; }
    if (c == 'r') { toggleRecord(); return true; }
    if (c == 'e') { chooseExport(); return true; }
    return false;
}

bool ModularSynthEditor::keyPressed (const juce::KeyPress& key)
{
    return handleStudioKey (key);
}

void ModularSynthEditor::timerCallback()
{
    if (firstShow && isShowing())
    {
        firstShow = false;
        applyWindowLayout();
        grabKeyboardFocus();
        for (auto& w : windows)
            if (w != nullptr && w->isVisible())
            {
                w->toFront (true);
                if (auto* content = w->getContentComponent())
                    content->grabKeyboardFocus();
            }
    }
    else if (! firstShow && proc.getWindowLayoutVersion() != appliedLayoutVersion)
    {
        applyWindowLayout();
    }

    proc.syncPatternFromEngine();
    const auto& studio = proc.getStudioState();
    const double sr = proc.getEngineSampleRate();
    const bool recording = proc.isRecording();

    for (int t = 0; t < kNumToolSlots; ++t)
    {
        auto& w = windows[(size_t) t];
        if (w != nullptr && ! studio.tools[(size_t) t].active)
            w.reset();
        if (w != nullptr && w->isVisible() && w->onTick)
            w->onTick();
        strips[(size_t) t]->tick();
    }
    master->refresh();

    if (auto take = proc.takeFinishedRecording())
        setStatus ("TAKE READY " + formatTime ((double) take->length / take->sampleRate) + " / "
                   + juce::String ((int) take->events.size()) + " EVENTS / PRESS E TO EXPORT MASTER + STEMS");

    playButton.setOn (proc.isPlaying());
    recButton.setOn (recording);

    bpmReadout.setText (juce::String (studio.bpm, 1));
    swingReadout.setText (look::pad (juce::roundToInt (studio.swing * 100.0f), 2) + " %");
    stepReadout.setText (look::pad (proc.currentStep() / 4 + 1, 1) + "." + look::pad (proc.currentStep() % 4 + 1, 1)
                         + "  " + look::pad (proc.currentStep() + 1, 2) + "/" + juce::String (kNumSteps));
    const auto pos = recording ? proc.recordedSamples() : proc.playedSamples();
    posReadout.setText (formatTime ((double) pos / sr) + "  " + juce::String::toHexString ((juce::int64) pos).paddedLeft ('0', 8).toUpperCase());
    posReadout.setColour (recording ? look::red : look::ink);
    rateReadout.setText (juce::String (juce::roundToInt (sr)) + " HZ / 24 BIT");

    if (recording)
    {
        takeReadout.setText ("REC " + formatTime ((double) proc.recordedSamples() / sr) + " / " + juce::String (proc.recordedEvents()) + " EV");
        takeReadout.setColour (look::red);
    }
    else if (const auto& last = proc.getLastTake(); last.has_value())
    {
        takeReadout.setText (formatTime ((double) last->length / last->sampleRate) + " / " + juce::String ((int) last->events.size()) + " EV");
        takeReadout.setColour (look::ink);
    }
    else
    {
        takeReadout.setText ("NONE");
        takeReadout.setColour (look::dim);
    }

    if (exportProgress.load() >= 0.0f || ! status.isEmpty())
        repaint (0, 0, getWidth(), kHeaderHeight);
    repaint (0, getHeight() - kStatusHeight, getWidth(), kStatusHeight);
}

void ModularSynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (look::background);

    // title block
    g.setColour (look::ink);
    g.setFont (look::mono (16.0f, true));
    g.drawText ("MODULAR-SYNTH", kMargin, 12, 260, 20, juce::Justification::centredLeft);
    g.setFont (look::mono (9.5f));
    g.setColour (look::dim);
    g.drawText ("STUDIO // ONE CLOCK, ONE MASTER, A WINDOW PER TOOL // V0.3", kMargin, 32, 380, 14, juce::Justification::centredLeft);

    // header rule, and the export progress as a blue line along it
    g.setColour (look::faint);
    g.fillRect (0, kHeaderHeight - 1, getWidth(), 1);
    const float progress = exportProgress.load();
    if (progress >= 0.0f)
    {
        g.setColour (look::blue);
        g.fillRect (0.0f, (float) kHeaderHeight - 2.0f, (float) getWidth() * progress, 2.0f);
        g.setFont (look::mono (9.5f));
        g.drawText ("EXPORT " + juce::String (juce::roundToInt (progress * 100.0f)) + "%",
                    getWidth() - 160 - kMargin, kHeaderHeight - 16, 160, 12, juce::Justification::centredRight);
    }

    // tools section label, and what comes next under the last tool
    g.setFont (look::mono (11.0f, true));
    g.setColour (look::ink);
    g.drawText ("TOOLS", kMargin, kHeaderHeight + 6, 200, 16, juce::Justification::centredLeft);
    g.setFont (look::mono (9.5f));
    g.setColour (look::dim);
    g.drawText ("OPEN SHOWS A TOOL'S WINDOW // CLOSING THE WINDOW KEEPS IT PLAYING // REMOVE TAKES IT OUT OF THE PROJECT",
                kMargin + 80, kHeaderHeight + 6, getWidth() - 2 * kMargin - 80, 16, juce::Justification::centredRight);
    const int nextY = kHeaderHeight + kToolsLabelHeight + kNumToolSlots * kStripHeight;
    g.setColour (look::faint);
    g.fillRect (kMargin, nextY, getWidth() - 2 * kMargin, 1);
    g.setColour (look::dim);
    g.drawText ("02 BASS SYNTH // 8 SOUNDS, SCALE LOCK, ADSR, CHORUS // NEXT UPDATE", kMargin + 48, nextY + 8, 600, 14,
                juce::Justification::centredLeft);
    g.drawText ("03 LOOPER // AUDIO IN, LOOPS ON THE STUDIO CLOCK // AFTER THAT", kMargin + 48, nextY + 26, 600, 14,
                juce::Justification::centredLeft);

    // status line
    const auto statusArea = juce::Rectangle<int> (kMargin, getHeight() - kStatusHeight, getWidth() - 2 * kMargin, kStatusHeight);
    g.setColour (look::faint);
    g.fillRect (0, statusArea.getY(), getWidth(), 1);
    g.setFont (look::mono (10.0f));
    const bool fresh = status.isNotEmpty() && juce::Time::getMillisecondCounter() - statusTime < 12000;
    g.setColour (proc.isRecording() ? look::red : look::ink);
    g.drawText (fresh ? status : juce::String ("READY"), statusArea, juce::Justification::centredLeft, true);
    g.setColour (look::dim);
    g.drawText ("SPACE PLAY  ESC STOP  R REC  E EXPORT  CTRL+S SAVE  CTRL+O OPEN", statusArea, juce::Justification::centredRight, true);
}

void ModularSynthEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (kHeaderHeight).reduced (kMargin, 0);

    // buttons along the bottom of the header
    auto buttons = header.removeFromBottom (34).withTrimmedBottom (10);
    for (auto* b : { &playButton, &stopButton, &recButton })
    {
        b->setBounds (buttons.removeFromLeft (72));
        buttons.removeFromLeft (6);
    }
    buttons.removeFromLeft (18);
    for (auto* b : { &exportButton, &saveButton, &openButton })
    {
        b->setBounds (buttons.removeFromLeft (80));
        buttons.removeFromLeft (6);
    }

    // readouts across the top right
    auto readouts = header.withTrimmedTop (8).withTrimmedLeft (400);
    const int widths[] = { 90, 100, 110, 210, 140, 180 };
    DataReadout* order[] = { &bpmReadout, &swingReadout, &stepReadout, &posReadout, &rateReadout, &takeReadout };
    for (int i = 0; i < 6; ++i)
    {
        order[i]->setBounds (readouts.removeFromLeft (widths[i]).withHeight (36));
        readouts.removeFromLeft (8);
    }

    area.removeFromBottom (kStatusHeight);
    master->setBounds (area.removeFromBottom (kMasterHeight).reduced (kMargin, 0));

    area.removeFromTop (kToolsLabelHeight);
    for (auto& s : strips)
        s->setBounds (area.removeFromTop (kStripHeight).reduced (kMargin, 0));
}
