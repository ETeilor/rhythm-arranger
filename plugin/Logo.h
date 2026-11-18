#pragma once
#include <BinaryData.h>
#include "Ui.h"

// The Teilormusic wordmark (white), parsed once per editor from the SVG that is built into the plug-in.
inline std::unique_ptr<juce::Drawable> makeLogo() {
    return juce::Drawable::createFromImageData(BinaryData::teilormusic_logo_svg, BinaryData::teilormusic_logo_svgSize);
}
inline void drawLogo(juce::Graphics& g, juce::Drawable* logo, juce::Rectangle<float> box) {
    if (logo) logo->drawWithin(g, box, juce::RectanglePlacement::xRight | juce::RectanglePlacement::yMid, 0.85f);
}
