#include "ModKnob.h"

#include <cmath>
#include <cstring>

#include "Theme.h"
#include "midipixi/color.h"

namespace {

constexpr const char* kDragPrefix = "mpxsrc:";
constexpr float kHandleSize = 8.0f;
constexpr int kHandleRowH = 12;
constexpr int kMaxHandles = 6;

bool isOctaveDest(mpx::Dest d) {
    using D = mpx::Dest;
    switch (d) {
        case D::Lfo1Rate: case D::Lfo2Rate: case D::Lfo3Rate:
        case D::Env1Attack: case D::Env1Decay: case D::Env1Release:
        case D::Env2Attack: case D::Env2Decay: case D::Env2Release: return true;
        default: return false;
    }
}

juce::String amountText(mpx::Dest d, float amount) {
    if (isOctaveDest(d)) return (amount >= 0 ? "+" : "") + juce::String(amount * 4.0f, 2) + " oct";
    return (amount >= 0 ? "+" : "") + juce::String(juce::roundToInt(amount * 100.0f)) + "%";
}

}  // namespace

// ---------------------------------------------------------------------------------------
// ModKnob

ModKnob::ModKnob(const juce::String& name, double min, double max, double def, Getter get, Setter set, Formatter fmt,
                 double skew, mpx::Dest dest, Context ctx)
    : ParamKnob(name, min, max, def, std::move(get), std::move(set), std::move(fmt), skew),
      dest_(dest), ctx_(std::move(ctx)) {
    slider_.setTooltip("Drop a modulation source here");
}

float ModKnob::applyModulation(mpx::Dest d, float base, float amount) {
    if (isOctaveDest(d)) return base * std::exp2(amount * 4.0f);
    return base + amount;
}

float ModKnob::proportionOf(float v) const {
    if (dest_ == mpx::Dest::Hue) v = mpx::wrap01(v);
    v = juce::jlimit(static_cast<float>(slider_.getMinimum()), static_cast<float>(slider_.getMaximum()), v);
    return static_cast<float>(slider_.valueToProportionOfLength(v));
}

void ModKnob::setLive(const float* values) {
    bool has = values != nullptr;
    float v = has ? values[static_cast<int>(dest_)] : 0.0f;
    if (has != hasLive_ || std::abs(v - live_) > 1e-4f) {
        hasLive_ = has;
        live_ = v;
        repaint();
    }
}

std::vector<ModKnob::Handle> ModKnob::handles() const {
    std::vector<Handle> out;
    mpx::Layer* l = ctx_.layer ? ctx_.layer() : nullptr;
    if (!l) return out;
    std::vector<int> rows;
    for (int i = 0; i < l->modCount; ++i)
        if (l->mods[i].dest == dest_) rows.push_back(i);
    int n = juce::jmin(static_cast<int>(rows.size()), kMaxHandles);
    float total = static_cast<float>(n) * (kHandleSize + 3.0f) - 3.0f;
    float x = static_cast<float>(getWidth()) * 0.5f - total * 0.5f;
    float y = static_cast<float>(getHeight() - kHandleRowH) + (kHandleRowH - kHandleSize) * 0.5f;
    for (int i = 0; i < n; ++i) {
        out.push_back({rows[static_cast<size_t>(i)], {x, y, kHandleSize, kHandleSize}});
        x += kHandleSize + 3.0f;
    }
    return out;
}

int ModKnob::handleAt(juce::Point<int> p) const {
    for (const auto& h : handles())
        if (h.bounds.expanded(2.0f).contains(p.toFloat())) return h.row;
    return -1;
}

void ModKnob::resized() {
    auto r = getLocalBounds();
    r.removeFromTop(14);
    r.removeFromBottom(14 + kHandleRowH);
    int s = juce::jmin(r.getWidth(), r.getHeight());
    slider_.setBounds(r.withSizeKeepingCentre(s, s).reduced(5));
}

void ModKnob::paint(juce::Graphics& g) {
    auto r = getLocalBounds();
    if (dragHover_) {
        g.setColour(theme::accent.withAlpha(0.18f));
        g.fillRoundedRectangle(r.toFloat(), 6.0f);
    }
    g.setColour(theme::textDim);
    g.setFont(juce::Font(juce::FontOptions(11.5f)));
    g.drawText(name_, r.removeFromTop(14), juce::Justification::centred);

    auto handleRow = r.removeFromBottom(kHandleRowH);
    (void)handleRow;
    g.setColour(theme::text);
    float v = static_cast<float>(slider_.getValue());
    g.drawText(fmt_ ? fmt_(v) : fmt::plain(v), r.removeFromBottom(14), juce::Justification::centred);

    mpx::Layer* l = ctx_.layer ? ctx_.layer() : nullptr;
    for (const auto& h : handles()) {
        const mpx::ModRow& row = l->mods[h.row];
        auto c = theme::sourceColour(row.source.type);
        g.setColour(c.withAlpha(h.row == dragRow_ ? 1.0f : 0.85f));
        g.fillEllipse(h.bounds);
        if (row.amount < 0) {  // inverted modulation: hollow centre
            g.setColour(theme::panel);
            g.fillEllipse(h.bounds.reduced(2.5f));
        }
    }
}

void ModKnob::paintOverChildren(juce::Graphics& g) {
    mpx::Layer* l = ctx_.layer ? ctx_.layer() : nullptr;
    if (!l) return;
    auto sb = slider_.getBounds().toFloat();
    auto centre = sb.getCentre();
    const auto rp = slider_.getRotaryParameters();
    auto angleOf = [&](float proportion) {
        return rp.startAngleRadians + proportion * (rp.endAngleRadians - rp.startAngleRadians);
    };
    float radius = juce::jmin(sb.getWidth(), sb.getHeight()) * 0.5f;

    // Modulation ranges, one ring per row, outside the knob's track.
    float base = baseValue();
    int ring = 0;
    for (int i = 0; i < l->modCount && ring < 4; ++i) {
        const mpx::ModRow& row = l->mods[i];
        if (row.dest != dest_ || row.amount == 0.0f) continue;
        float rr = radius + 2.5f + static_cast<float>(ring) * 3.0f;
        bool bipolar = mpx::isBipolarSource(*l, row.source);
        float lo = bipolar ? applyModulation(dest_, base, -row.amount) : base;
        float hi = applyModulation(dest_, base, row.amount);
        float a1 = angleOf(proportionOf(lo)), a2 = angleOf(proportionOf(hi));
        if (dest_ == mpx::Dest::Hue && std::abs(row.amount) >= 1.0f) {
            a1 = rp.startAngleRadians;
            a2 = rp.endAngleRadians;
        }
        if (std::abs(a2 - a1) < 0.02f) a2 = a1 + 0.02f;
        juce::Path p;
        p.addCentredArc(centre.x, centre.y, rr, rr, 0.0f, juce::jmin(a1, a2), juce::jmax(a1, a2), true);
        g.setColour(theme::sourceColour(row.source.type).withAlpha(i == dragRow_ ? 1.0f : 0.8f));
        g.strokePath(p, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        ++ring;
    }

    // Live value of the newest voice.
    if (hasLive_ && ring > 0) {
        float a = angleOf(proportionOf(live_));
        float rr = radius - 4.0f;
        juce::Point<float> pt(centre.x + rr * std::sin(a), centre.y - rr * std::cos(a));
        g.setColour(juce::Colours::white);
        g.fillEllipse(pt.x - 3.0f, pt.y - 3.0f, 6.0f, 6.0f);
        g.setColour(theme::background);
        g.drawEllipse(pt.x - 3.0f, pt.y - 3.0f, 6.0f, 6.0f, 1.0f);
    }
}

void ModKnob::mouseDown(const juce::MouseEvent& e) {
    int row = handleAt(e.getPosition());
    if (row < 0) return;
    mpx::Layer* l = ctx_.layer();
    if (e.mods.isPopupMenu()) {
        juce::PopupMenu m;
        char buf[24];
        m.addSectionHeader(juce::String(mpx::toString(l->mods[row].source, buf, sizeof(buf))) + " -> " +
                           mpx::displayName(dest_));
        m.addItem(1, "Invert");
        m.addItem(2, "Amount 100%");
        m.addItem(3, "Amount 50%");
        m.addSeparator();
        m.addItem(9, "Remove");
        juce::Component::SafePointer<ModKnob> safe(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe, row](int r) {
            if (!safe || r == 0) return;
            mpx::Layer* layer = safe->ctx_.layer();
            if (!layer || row >= layer->modCount) return;
            float& amount = layer->mods[row].amount;
            if (r == 1) amount = -amount;
            if (r == 2) amount = amount < 0 ? -1.0f : 1.0f;
            if (r == 3) amount = amount < 0 ? -0.5f : 0.5f;
            if (r == 9) layer->removeMod(row);
            if (safe->ctx_.modsChanged) safe->ctx_.modsChanged();
        });
        return;
    }
    dragRow_ = row;
    dragStartAmount_ = l->mods[row].amount;
    repaint();
}

void ModKnob::mouseDrag(const juce::MouseEvent& e) {
    if (dragRow_ < 0) return;
    mpx::Layer* l = ctx_.layer();
    if (!l || dragRow_ >= l->modCount) return;
    float sens = e.mods.isShiftDown() ? 0.001f : 0.005f;
    float amount = juce::jlimit(-1.0f, 1.0f, dragStartAmount_ - static_cast<float>(e.getDistanceFromDragStartY()) * sens);
    l->mods[dragRow_].amount = amount;
    if (set_) {
        // Reuse the setter path to notify the processor without changing the base value.
        set_(static_cast<float>(slider_.getValue()));
    }
    repaint();
}

void ModKnob::mouseUp(const juce::MouseEvent&) {
    if (dragRow_ < 0) return;
    dragRow_ = -1;
    if (ctx_.modsChanged) ctx_.modsChanged();
    repaint();
}

void ModKnob::mouseDoubleClick(const juce::MouseEvent& e) {
    int row = handleAt(e.getPosition());
    if (row < 0) return;
    ctx_.layer()->removeMod(row);
    dragRow_ = -1;
    if (ctx_.modsChanged) ctx_.modsChanged();
}

void ModKnob::mouseMove(const juce::MouseEvent& e) {
    int row = handleAt(e.getPosition());
    mpx::Layer* l = ctx_.layer ? ctx_.layer() : nullptr;
    if (row >= 0 && l) {
        char buf[24];
        setTooltip(juce::String(mpx::toString(l->mods[row].source, buf, sizeof(buf))) + " -> " +
                           mpx::displayName(dest_) + ": " + amountText(dest_, l->mods[row].amount) +
                           "  (drag to change, double-click to remove)");
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    } else {
        setTooltip({});
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

bool ModKnob::isInterestedInDragSource(const SourceDetails& d) {
    mpx::ModSource s;
    return ctx_.layer && ctx_.layer() && ModSourceChip::parseDragDescription(d.description, s);
}

void ModKnob::itemDragEnter(const SourceDetails&) {
    dragHover_ = true;
    repaint();
}

void ModKnob::itemDragExit(const SourceDetails&) {
    dragHover_ = false;
    repaint();
}

void ModKnob::itemDropped(const SourceDetails& d) {
    dragHover_ = false;
    mpx::ModSource s;
    mpx::Layer* l = ctx_.layer();
    if (!l || !ModSourceChip::parseDragDescription(d.description, s)) return;
    if (l->findMod(s, dest_) >= 0) {
        repaint();
        return;  // already wired
    }
    // Typical synth wirings get a full-range amount (note -> position, envelope -> brightness).
    bool full = (dest_ == mpx::Dest::Position && s.type == mpx::SourceType::Note) ||
                (dest_ == mpx::Dest::Brightness &&
                 (s.type == mpx::SourceType::Env1 || s.type == mpx::SourceType::Env2));
    float amount = full ? 1.0f : 0.5f;
    // Keep a unipolar modulation inside the parameter range: e.g. dropping an
    // envelope on a brightness of 100% moves the base to 0 so the envelope is visible.
    if (!mpx::isBipolarSource(*l, s) && !isOctaveDest(dest_) && dest_ != mpx::Dest::Hue && set_) {
        float base = baseValue();
        float max = static_cast<float>(slider_.getMaximum());
        float min = static_cast<float>(slider_.getMinimum());
        if (base + amount > max) {
            float nb = juce::jmax(min, max - amount);
            slider_.setValue(nb, juce::dontSendNotification);
            set_(nb);
        }
    }
    if (l->addMod(s, dest_, amount) < 0) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Modulation matrix full",
                                               "This layer already has the maximum number of modulation rows.");
        return;
    }
    if (ctx_.modsChanged) ctx_.modsChanged();
    repaint();
}

// ---------------------------------------------------------------------------------------
// ModSourceChip

ModSourceChip::ModSourceChip(mpx::ModSource s, juce::String label) : source_(s), fixedLabel_(std::move(label)) {
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    if (s.type == mpx::SourceType::CC)
        setTooltip("Drag onto a knob to modulate it. Scroll or click to choose the CC number.");
}

void ModSourceChip::setSource(mpx::ModSource s) {
    source_ = s;
    repaint();
}

juce::String ModSourceChip::label(mpx::ModSource s) {
    using S = mpx::SourceType;
    switch (s.type) {
        case S::Env1: return "ENV 1";
        case S::Env2: return "ENV 2";
        case S::Lfo1: return "LFO 1";
        case S::Lfo2: return "LFO 2";
        case S::Lfo3: return "LFO 3";
        case S::Note: return "NOTE";
        case S::Velocity: return "VEL";
        case S::Aftertouch: return "AFT";
        case S::Gate: return "GATE";
        case S::Random: return "RND";
        case S::Macro: return "M" + juce::String(s.index + 1);
        case S::CC: return "CC " + juce::String(s.index);
        case S::PitchBend: return "BEND";
        case S::Pressure: return "PRESS";
        case S::Beat: return "BEAT";
        default: return "?";
    }
}

juce::String ModSourceChip::dragDescription(mpx::ModSource s) {
    char buf[24];
    return juce::String(kDragPrefix) + mpx::toString(s, buf, sizeof(buf));
}

bool ModSourceChip::parseDragDescription(const juce::var& description, mpx::ModSource& out) {
    juce::String s = description.toString();
    if (!s.startsWith(kDragPrefix)) return false;
    return mpx::parseModSource(s.substring(static_cast<int>(std::strlen(kDragPrefix))).toRawUTF8(), out);
}

void ModSourceChip::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    auto c = theme::sourceColour(source_.type);
    g.setColour(c.withAlpha(isMouseOver() ? 0.35f : 0.22f));
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(c);
    g.drawRoundedRectangle(r, 5.0f, 1.2f);
    g.setColour(theme::text);
    g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
    g.drawText(fixedLabel_.isNotEmpty() ? fixedLabel_ : label(source_), getLocalBounds(), juce::Justification::centred);
}

void ModSourceChip::mouseDown(const juce::MouseEvent& e) {
    dragging_ = false;
    if (source_.type != mpx::SourceType::CC || !(e.mods.isPopupMenu() || e.getNumberOfClicks() > 1)) return;
    juce::PopupMenu menu;
    for (int bank = 0; bank < 8; ++bank) {
        juce::PopupMenu sub;
        for (int n = bank * 16; n < bank * 16 + 16; ++n) {
            juce::String name = "CC " + juce::String(n);
            if (n == 1) name += " (mod wheel)";
            if (n == 11) name += " (expression)";
            if (n == 64) name += " (sustain)";
            sub.addItem(n + 1, name, true, n == source_.index);
        }
        menu.addSubMenu("CC " + juce::String(bank * 16) + "-" + juce::String(bank * 16 + 15), sub);
    }
    juce::Component::SafePointer<ModSourceChip> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe](int r) {
        if (safe && r > 0) safe->setSource({mpx::SourceType::CC, static_cast<uint8_t>(r - 1)});
    });
}

void ModSourceChip::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    if (source_.type != mpx::SourceType::CC) return;
    int n = source_.index + (w.deltaY > 0 ? 1 : -1);
    setSource({mpx::SourceType::CC, static_cast<uint8_t>(juce::jlimit(0, 127, n))});
}

void ModSourceChip::mouseDrag(const juce::MouseEvent& e) {
    if (dragging_ || e.getDistanceFromDragStart() < 4) return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this)) {
        dragging_ = true;
        container->startDragging(dragDescription(source_), this);
    }
}
