#include "PluginEditor.h"
#include <BinaryData.h>
#include <cmath>

namespace {
const juce::Colour cream(0xfff4e9ce), neon(0xff26baff), dark(0xff101820);
juce::String noteName(int midi) {
    const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String(names[(midi % 12 + 12) % 12]) + juce::String(midi / 12 - 1);
}
void text(juce::Graphics& g, const juce::String& value, juce::Rectangle<int> bounds,
          float height, juce::Colour colour, juce::Justification align = juce::Justification::centredLeft) {
    g.setColour(colour);
    g.setFont(juce::FontOptions(height));
    g.drawText(value, bounds, align);
}
}

FindNoteLookAndFeel::FindNoteLookAndFeel()
    : cartoonTypeface(juce::Typeface::createSystemTypefaceFor(BinaryData::LuckiestGuyRegular_ttf,
                                                             BinaryData::LuckiestGuyRegular_ttfSize)) {}

juce::Font FindNoteLookAndFeel::displayFont(float height) const {
    return juce::Font(juce::FontOptions(cartoonTypeface).withHeight(height));
}

juce::Font FindNoteLookAndFeel::getTextButtonFont(juce::TextButton&, int height) {
    return displayFont(juce::jmin(15.0f, static_cast<float>(height) * 0.52f));
}

void FindNoteLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                               const juce::Colour&, bool highlighted, bool down) {
    const auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(neon.withAlpha(down ? 0.24f : highlighted ? 0.16f : 0.07f));
    g.fillRoundedRectangle(bounds, 9.0f);
    g.setColour(neon.withAlpha(highlighted ? 1.0f : 0.55f));
    g.drawRoundedRectangle(bounds, 9.0f, 1.2f);
}

void FindNoteLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float, float,
                                          juce::Slider::SliderStyle, juce::Slider& slider) {
    const auto centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    const auto start = static_cast<float>(x);
    const auto trackWidth = static_cast<float>(width);
    const auto activeWidth = juce::jlimit(0.0f, trackWidth, sliderPos - start);
    g.setColour(neon.withAlpha(0.15f));
    g.fillRoundedRectangle(start, centreY - 3.0f, trackWidth, 6.0f, 3.0f);
    g.setColour(neon.withAlpha(0.07f));
    g.fillRoundedRectangle(start - 2.0f, centreY - 7.0f, activeWidth + 4.0f, 14.0f, 7.0f);
    g.setColour(neon.withAlpha(0.14f));
    g.fillRoundedRectangle(start, centreY - 5.0f, activeWidth, 10.0f, 5.0f);
    g.setColour(neon);
    g.fillRoundedRectangle(start, centreY - 3.0f, activeWidth, 6.0f, 3.0f);
    g.setColour(neon.withAlpha(slider.isMouseOverOrDragging() ? 0.23f : 0.12f));
    g.fillEllipse(sliderPos - 13.0f, centreY - 13.0f, 26.0f, 26.0f);
    g.setColour(neon);
    g.fillEllipse(sliderPos - 7.0f, centreY - 7.0f, 14.0f, 14.0f);
    g.setColour(juce::Colour(0xffe1f7ff));
    g.fillEllipse(sliderPos - 3.0f, centreY - 3.0f, 6.0f, 6.0f);
}

FindNoteEditor::FindNoteEditor(FindNoteProcessor& p)
    : AudioProcessorEditor(&p), owner(p), background(juce::ImageCache::getFromMemory(BinaryData::cuphead_jpeg, BinaryData::cuphead_jpegSize)) {
    theme.setColour(juce::Slider::thumbColourId, neon);
    theme.setColour(juce::Slider::trackColourId, neon);
    theme.setColour(juce::Slider::backgroundColourId, neon.withAlpha(0.15f));
    theme.setColour(juce::Slider::textBoxTextColourId, cream);
    theme.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    theme.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    theme.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    theme.setColour(juce::TextButton::buttonColourId, dark.brighter(0.15f));
    theme.setColour(juce::TextButton::textColourOffId, neon);
    setLookAndFeel(&theme);
    const std::array<juce::String, 4> ids { "reference", "low", "high", "gate" };
    const std::array<juce::String, 4> names { "AFINACION LA4", "DESDE", "HASTA", "UMBRAL" };
    for (size_t i = 0; i < sliders.size(); ++i) {
        auto& slider = sliders[i];
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxAbove, false, 110, 25);
        slider.setTextValueSuffix(i == 3 ? " dB" : " Hz");
        slider.setDoubleClickReturnValue(true, i == 0 ? 440.0 : i == 1 ? 35.0 : i == 2 ? 2000.0 : -60.0);
        addAndMakeVisible(slider);
        labels[i].setText(names[i], juce::dontSendNotification);
        labels[i].setFont(theme.displayFont(14.0f));
        labels[i].setColour(juce::Label::textColourId, cream.withAlpha(0.9f));
        labels[i].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(labels[i]);
        attachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner.parameters, ids[i], slider);
    }
    sliders[0].setTooltip("Calibracion musical: La4 = 440 Hz. El pico de referencia se busca automaticamente.");
    sliders[1].setTooltip("Frecuencia minima a buscar. Debe ser menor que Hasta.");
    sliders[2].setTooltip("Limite opcional: el pico mas fuerte se busca automaticamente dentro del rango.");
    sliders[3].setTooltip("Ignorar senales por debajo de este nivel.");
    reset.onClick = [this] {
        for (const auto* id : { "reference", "low", "high", "gate" }) {
            auto* parameter = owner.parameters.getParameter(id);
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->getDefaultValue());
            parameter->endChangeGesture();
        }
        owner.startNewAnalysis();
    };
    addAndMakeVisible(reset);
    newAnalysis.onClick = [this] { owner.startNewAnalysis(); timerCallback(); };
    newAnalysis.setTooltip("Limpia los picos guardados. Pulsalo antes de reproducir otro sample.");
    addAndMakeVisible(newAnalysis);
    resultTable.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    resultTable.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    resultTable.setRowHeight(26);
    resultTable.setWantsKeyboardFocus(false);
    addAndMakeVisible(resultTable);
    timerCallback();
    setSize(760, 520);
    startTimerHz(30);
}

FindNoteEditor::~FindNoteEditor() { stopTimer(); resultTable.setModel(nullptr); setLookAndFeel(nullptr); }

void FindNoteEditor::timerCallback() {
    reading = owner.getReading();
    results = owner.getResults();
    resultRows.clear();
    for (size_t i = 0; i < results.notes.size(); ++i)
        if (results.notes[i].power > 0.0) resultRows.push_back(static_cast<int>(i));
    std::sort(resultRows.begin(), resultRows.end(), [this](int a, int b) {
        if (a == b) return false;
        const auto pa = results.notes[static_cast<size_t>(a)].power;
        const auto pb = results.notes[static_cast<size_t>(b)].power;
        if (pa > pb) return true;
        if (pa < pb) return false;
        if (a == results.principal || b == results.principal) return a == results.principal;
        return a < b;
    });
    resultTable.updateContent();
    resultTable.repaint();
    repaint();
}

void FindNoteEditor::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool) {
    if (row < 0 || row >= static_cast<int>(resultRows.size())) return;
    const auto midi = resultRows[static_cast<size_t>(row)];
    const auto& peak = results.notes[static_cast<size_t>(midi)];
    const auto percent = results.percentage(midi);
    g.setColour(neon.withAlpha(midi == results.principal ? 0.17f : 0.06f));
    g.fillRoundedRectangle(0.0f, 1.0f, static_cast<float>(width) * static_cast<float>(percent / 100.0),
                           static_cast<float>(height - 2), 4.0f);
    const auto colour = midi == results.principal ? neon : cream;
    text(g, noteName(midi), {8, 0, 70, height}, 15.0f, colour);
    text(g, juce::String(peak.frequency, 1), {82, 0, 100, height}, 13.0f, cream, juce::Justification::centredRight);
    const auto percentText = percent < 0.1 ? juce::String("<0.1%") : juce::String(percent, 1) + "%";
    text(g, percentText, {width - 83, 0, 75, height}, 14.0f, colour, juce::Justification::centredRight);
}

void FindNoteEditor::resized() {
    for (size_t i = 0; i < sliders.size(); ++i) {
        const auto x = 26 + static_cast<int>(i) * 151;
        labels[i].setBounds(x, 429, 143, 22);
        sliders[i].setBounds(x, 453, 143, 48);
    }
    reset.setBounds(637, 455, 101, 32);
    newAnalysis.setBounds(570, 24, 166, 32);
    resultTable.setBounds(420, 116, 306, 145);
}

void FindNoteEditor::paint(juce::Graphics& g) {
    g.fillAll(dark);
    g.drawImage(background, juce::Rectangle<float>(0.0f, 0.0f, 760.0f, 434.0f), juce::RectanglePlacement::fillDestination);
    g.setGradientFill(juce::ColourGradient(dark.withAlpha(0.96f), 0.0f, 0.0f,
                                         dark.withAlpha(0.12f), 760.0f, 0.0f, false));
    g.fillRect(0, 0, 760, 420);
    g.setColour(dark.withAlpha(0.92f));
    g.fillRoundedRectangle(24.0f, 296.0f, 712.0f, 112.0f, 12.0f);
    g.setColour(dark);
    g.fillRect(0, 418, 760, 102);
    const auto displayText = [this, &g](const juce::String& value, juce::Rectangle<int> bounds,
                                      float height, juce::Colour colour,
                                      juce::Justification align = juce::Justification::centredLeft) {
        g.setFont(theme.displayFont(height));
        g.setColour(colour);
        g.drawText(value, bounds, align);
    };
    displayText("FINDNOTE", {31, 16, 280, 42}, 35.0f, cream);
    text(g, "NOTA PRINCIPAL / MAYOR PICO GUARDADO", {33, 59, 340, 20}, 11.0f, neon);
    const auto low = owner.parameters.getRawParameterValue("low")->load();
    const auto high = owner.parameters.getRawParameterValue("high")->load();
    const auto validRange = low < high;
    const auto frequency = results.principal >= 0 ? results.notes[static_cast<size_t>(results.principal)].frequency : 0.0;
    const auto valid = frequency > 0.0 && validRange;
    const auto note = findnote::noteForFrequency(frequency, owner.parameters.getRawParameterValue("reference")->load());
    const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const char* latin[] { "Do", "Do sostenido", "Re", "Re sostenido", "Mi", "Fa", "Fa sostenido", "Sol", "Sol sostenido", "La", "La sostenido", "Si" };
    const auto noteText = valid ? juce::String(names[note.pitchClass]) + juce::String(note.octave) : "--";
    displayText(noteText, {32, 92, 360, 110}, 112.0f, juce::Colours::black.withAlpha(0.45f));
    displayText(noteText, {28, 88, 360, 110}, 112.0f, cream);
    text(g, valid ? juce::String(latin[note.pitchClass]) + "  /  " + juce::String(frequency, 2) + " Hz"
                  : validRange ? "Esperando una nota..." : "Desde debe ser menor que Hasta",
         {33, 201, 390, 27}, 18.0f, cream.withAlpha(0.85f));
    text(g, valid ? juce::String(results.percentage(results.principal), 1) + "% del peso de picos guardados"
                  : "Reproduce un sample para acumular resultados",
         {33, 226, 365, 18}, 12.0f, neon);
    g.setColour(dark.withAlpha(0.9f));
    g.fillRoundedRectangle(410.0f, 84.0f, 326.0f, 202.0f, 12.0f);
    text(g, "NOTAS", {428, 92, 70, 20}, 12.0f, neon);
    text(g, "PICO Hz", {510, 92, 90, 20}, 12.0f, neon, juce::Justification::centredRight);
    text(g, "PESO %", {642, 92, 70, 20}, 12.0f, neon, juce::Justification::centredRight);
    if (resultRows.empty()) text(g, "Sin resultados guardados", {428, 128, 275, 24}, 14.0f, cream.withAlpha(0.65f));
    text(g, juce::String(static_cast<int>(resultRows.size())) + " notas / Peso relativo, no probabilidad",
         {422, 266, 306, 16}, 10.0f, cream.withAlpha(0.65f));
    const auto cents = valid ? note.cents : 0.0;
    const auto inTune = valid && std::abs(cents) <= 5.0;
    const auto indicator = inTune ? juce::Colour(0xffb0eaff) : neon;
    g.setColour(neon.withAlpha(0.08f));
    g.fillRoundedRectangle(33.0f, 250.0f, 320.0f, 8.0f, 4.0f);
    g.setColour(neon.withAlpha(0.55f));
    g.fillRoundedRectangle(34.0f, 253.0f, 318.0f, 2.0f, 1.0f);
    for (int tick = -2; tick <= 2; ++tick) {
        const auto x = 193.0f + static_cast<float>(tick) * 79.5f;
        g.drawVerticalLine(static_cast<int>(x), 247.0f, 261.0f);
    }
    if (valid) {
        g.setColour(neon.withAlpha(0.14f));
        g.fillEllipse(181.0f + static_cast<float>(cents) * 3.18f, 242.0f, 24.0f, 24.0f);
        g.setColour(indicator);
        g.fillEllipse(188.0f + static_cast<float>(cents) * 3.18f, 249.0f, 10.0f, 10.0f);
    }
    text(g, "-50", {32, 267, 45, 18}, 11.0f, cream.withAlpha(0.5f));
    text(g, valid ? (cents > 0.0 ? "+" : "") + juce::String(cents, 1) + " cents" : "cents",
         {110, 266, 166, 20}, 13.0f, indicator, juce::Justification::centred);
    text(g, "+50", {321, 267, 40, 18}, 11.0f, cream.withAlpha(0.5f));
    displayText("ESPECTRO / MAXIMOS", {38, 305, 240, 20}, 15.0f, cream);
    text(g, reading.frequency > 0.0 ? "AHORA: " + juce::String(reading.frequency, 1) + " Hz / ACUMULANDO"
                                   : valid ? "SIN SENAL / RESULTADOS CONSERVADOS" : "BUSCANDO PICOS",
         {390, 305, 330, 18}, 10.0f, neon, juce::Justification::centredRight);
    const auto frequencyX = [](double hz) { return 40.0f + static_cast<float>(std::log(hz / 35.0) / std::log(2000.0 / 35.0)) * 678.0f; };
    if (validRange) {
        g.setColour(neon.withAlpha(0.045f));
        g.fillRect(frequencyX(low), 329.0f, frequencyX(high) - frequencyX(low), 51.0f);
    }
    juce::Path spectrum;
    for (size_t i = 0; i < reading.spectrum.size(); ++i) {
        const auto x = 40.0f + static_cast<float>(i) / static_cast<float>(reading.spectrum.size() - 1) * 678.0f;
        const auto y = 380.0f - results.spectrum[i] * 49.0f;
        if (i == 0) spectrum.startNewSubPath(x, y); else spectrum.lineTo(x, y);
    }
    auto fill = spectrum;
    fill.lineTo(718.0f, 382.0f);
    fill.lineTo(40.0f, 382.0f);
    fill.closeSubPath();
    g.setGradientFill(juce::ColourGradient(neon.withAlpha(0.24f), 0.0f, 331.0f,
                                         neon.withAlpha(0.01f), 0.0f, 382.0f, false));
    g.fillPath(fill);
    for (const auto width : {9.0f, 5.0f, 2.0f}) {
        g.setColour(neon.withAlpha(width > 5.0f ? 0.07f : width > 2.0f ? 0.2f : 1.0f));
        g.strokePath(spectrum, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    if (valid) {
        g.setColour(indicator);
        g.drawVerticalLine(static_cast<int>(frequencyX(frequency)), 328.0f, 382.0f);
    }
    text(g, "35 Hz", {40, 385, 70, 16}, 10.0f, cream.withAlpha(0.55f));
    text(g, "MAXIMOS RETENIDOS / NUEVO ANALISIS PARA OTRO SAMPLE", {135, 385, 490, 16}, 10.0f, cream.withAlpha(0.55f), juce::Justification::centred);
    text(g, "2 kHz", {650, 385, 70, 16}, 10.0f, cream.withAlpha(0.55f), juce::Justification::centredRight);
}
