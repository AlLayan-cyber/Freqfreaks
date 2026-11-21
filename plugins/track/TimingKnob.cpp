// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TimingKnob.h"
#include "../common/Theme.h"

namespace ff
{

namespace
{
    constexpr float kStart = juce::MathConstants<float>::pi * 1.25f;
    constexpr float kEnd   = juce::MathConstants<float>::pi * 2.75f;

    juce::String msText (float ms)
    {
        if (ms >= 1000.0f)
            return juce::String (ms / 1000.0f, 1) + "s";
        return juce::String (juce::roundToInt (ms));
    }
}

TimingKnob::TimingKnob (TrackProcessor& p) : processor (p)
{
    auto& s = processor.state();
    attack.value   = s.getParameter (trackParams::attack);
    attack.manual  = s.getParameter (trackParams::attackManual);
    release.value  = s.getParameter (trackParams::release);
    release.manual = s.getParameter (trackParams::releaseManual);

    setTooltip (theme::u8 ("Внутри — Release, ободок — Attack. По умолчанию «авто»: следуют ползунку Скорость. "
                           "Потяни — станет ручным. Двойной клик — обратно в авто."));
    setRepaintsOnMouseActivity (true);
}

TimingKnob::Part TimingKnob::partAt (juce::Point<float> pos) const
{
    const auto b = getLocalBounds().toFloat();
    const float outer = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 4.0f;
    const float d = pos.getDistanceFrom (b.getCentre());
    if (d > outer + 4.0f)
        return Part::none;
    return d > outer - 13.0f ? Part::attack : Part::release;
}

bool TimingKnob::isManual (Part p) const
{
    const auto& c = p == Part::attack ? attack : release;
    return c.manual != nullptr && c.manual->getValue() > 0.5f;
}

float TimingKnob::msInUse (Part p) const
{
    return p == Part::attack ? processor.currentAttackMs() : processor.currentReleaseMs();
}

void TimingKnob::beginEdit (Part p)
{
    auto& c = control (p);
    if (c.value == nullptr || c.manual == nullptr)
        return;

    // Grabbing an auto control starts from the time in use, so the sound does not jump.
    if (! isManual (p))
    {
        c.value->beginChangeGesture();
        c.value->setValueNotifyingHost (c.value->convertTo0to1 (msInUse (p)));
        c.value->endChangeGesture();
    }
    dragging = p;
    dragStart = c.value->getValue();
    c.value->beginChangeGesture();
}

void TimingKnob::nudge (Part p, float deltaNormalised)
{
    auto& c = control (p);
    if (c.value == nullptr || c.manual == nullptr)
        return;

    if (! isManual (p))
    {
        c.manual->beginChangeGesture();
        c.manual->setValueNotifyingHost (1.0f);
        c.manual->endChangeGesture();
    }
    c.value->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, c.value->getValue() + deltaNormalised));
    repaint();
}

void TimingKnob::endEdit()
{
    if (dragging != Part::none)
        if (auto* v = control (dragging).value)
            v->endChangeGesture();
    dragging = Part::none;
}

void TimingKnob::mouseMove (const juce::MouseEvent& e)
{
    const auto p = partAt (e.position);
    if (p != hover)
    {
        hover = p;
        repaint();
    }
}

void TimingKnob::mouseExit (const juce::MouseEvent&)
{
    hover = Part::none;
    repaint();
}

void TimingKnob::mouseDown (const juce::MouseEvent& e)
{
    const auto p = partAt (e.position);
    if (p == Part::none)
        return;
    beginEdit (p);
    dragStartY = e.getPosition().y;
}

void TimingKnob::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == Part::none)
        return;
    const float perPixel = e.mods.isShiftDown() ? 0.0015f : 0.006f;
    const float target = juce::jlimit (0.0f, 1.0f, dragStart + (float) (dragStartY - e.getPosition().y) * perPixel);
    auto& c = control (dragging);
    if (c.value != nullptr && std::abs (target - c.value->getValue()) > 1.0e-5f)
        nudge (dragging, target - c.value->getValue());
}

void TimingKnob::mouseUp (const juce::MouseEvent&)
{
    endEdit();
}

void TimingKnob::mouseDoubleClick (const juce::MouseEvent& e)
{
    endEdit();
    const auto p = partAt (e.position);
    if (p == Part::none)
        return;
    if (auto* m = control (p).manual)
    {
        m->beginChangeGesture();
        m->setValueNotifyingHost (0.0f);   // back to auto: follows Speed again
        m->endChangeGesture();
    }
    repaint();
}

void TimingKnob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto p = partAt (e.position);
    if (p == Part::none)
        return;
    auto& c = control (p);
    if (c.value == nullptr)
        return;
    if (! isManual (p))
        c.value->setValueNotifyingHost (c.value->convertTo0to1 (msInUse (p)));
    c.value->beginChangeGesture();
    nudge (p, wheel.deltaY * 0.15f);
    c.value->endChangeGesture();
}

void TimingKnob::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto centre = b.getCentre();
    const float outer = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 4.0f;
    const float ringR = outer - 4.0f;
    const float innerR = outer - 16.0f;

    const bool atkManual = isManual (Part::attack), relManual = isManual (Part::release);
    const float atkPos = attack.value != nullptr ? attack.value->convertTo0to1 (msInUse (Part::attack)) : 0.0f;
    const float relPos = release.value != nullptr ? release.value->convertTo0to1 (msInUse (Part::release)) : 0.0f;

    auto arc = [&] (float radius, float from, float to) {
        juce::Path p;
        p.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, from, to, true);
        return p;
    };

    // Inner body.
    g.setColour (theme::panelDeep.withAlpha (0.92f));
    g.fillEllipse (juce::Rectangle<float> (innerR * 2.0f, innerR * 2.0f).withCentre (centre));
    g.setColour (hover == Part::release ? theme::borderLight.brighter (0.3f) : theme::borderLight);
    g.drawEllipse (juce::Rectangle<float> (innerR * 2.0f, innerR * 2.0f).withCentre (centre), 1.0f);

    // Attack ring: neon with a glow when manual, grey when auto.
    const auto neon = theme::reference;
    g.setColour (theme::track);
    g.strokePath (arc (ringR, kStart, kEnd), juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const auto atkArc = arc (ringR, kStart, kStart + atkPos * (kEnd - kStart));
    if (atkManual)
    {
        for (float glow : { 13.0f, 9.0f })
        {
            g.setColour (neon.withAlpha (glow > 10.0f ? 0.12f : 0.22f));
            g.strokePath (atkArc, juce::PathStrokeType (glow, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (neon);
    }
    else
    {
        g.setColour (theme::textDim.withAlpha (hover == Part::attack ? 0.8f : 0.5f));
    }
    g.strokePath (atkArc, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Release: an arc inside the body.
    const float relR = innerR - 6.0f;
    g.setColour (theme::track);
    g.strokePath (arc (relR, kStart, kEnd), juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (relManual ? theme::space : theme::textDim.withAlpha (hover == Part::release ? 0.8f : 0.5f));
    g.strokePath (arc (relR, kStart, kStart + relPos * (kEnd - kStart)),
                  juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Text: release value in the centre, attack under it.
    g.setColour (relManual ? theme::text : theme::textDim);
    g.setFont (theme::mono (12.5f));
    g.drawText (msText (msInUse (Part::release)), juce::Rectangle<float> (relR * 2.0f, 14.0f).withCentre (centre.translated (0.0f, -6.0f)),
                juce::Justification::centred);
    g.setFont (theme::font (9.5f, true));
    g.drawText (relManual ? "REL" : "REL auto", juce::Rectangle<float> (relR * 2.0f, 12.0f).withCentre (centre.translated (0.0f, 6.0f)),
                juce::Justification::centred);
    g.setColour (atkManual ? neon : theme::textDim);
    g.drawText ((atkManual ? "ATK " : "ATK auto ") + msText (msInUse (Part::attack)),
                juce::Rectangle<float> (outer * 2.0f, 12.0f).withCentre ({ centre.x, centre.y + relR - 1.0f }),
                juce::Justification::centred);
}

} // namespace ff
