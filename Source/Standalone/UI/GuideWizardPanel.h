#pragma once

// [GuideHarmony] 「おまかせ補正」の進み具合パネル。
// 手順の一覧（済み / 実行中 / これから）と、いま何をしているか、終わったら微調整のやり方を表示する。
// ヘッダオンリー（CMake のソース一覧を変えずに追加できるように）。

#include <juce_gui_basics/juce_gui_basics.h>
#include "UIColors.h"
#include <cmath>
#include <functional>
#include <vector>

namespace OpenTune {

class GuideWizardPanel : public juce::Component
{
public:
    enum class StepState { Pending, Running, Done, Skipped, Failed };

    struct Step {
        juce::String label;
        StepState state = StepState::Pending;
    };

    GuideWizardPanel()
    {
        closeButton_.setButtonText(juce::String::fromUTF8("閉じる"));
        closeButton_.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible(closeButton_);
        setInterceptsMouseClicks(true, true);
    }

    std::function<void()> onClose;

    void setTitle(const juce::String& title) { title_ = title; repaint(); }
    void setSteps(std::vector<Step> steps) { steps_ = std::move(steps); repaint(); }
    void setStepState(int index, StepState state)
    {
        if (index >= 0 && index < static_cast<int>(steps_.size())) {
            steps_[static_cast<size_t>(index)].state = state;
            repaint();
        }
    }
    void setDetail(const juce::String& detail)
    {
        if (detail_ != detail) { detail_ = detail; repaint(); }
    }
    void setFooter(const juce::String& footer) { footer_ = footer; repaint(); }

    /** 内容に合わせた高さ（幅 width のとき）。 */
    int getPreferredHeight(int width) const
    {
        const int textWidth = width - 2 * kPad;
        int h = kPad + 24 + 6;                                   // タイトル
        h += static_cast<int>(steps_.size()) * kStepHeight + 6;  // 手順
        h += measure(detail_, textWidth, 14.0f) + 8;             // いまやっていること
        if (footer_.isNotEmpty())
            h += measure(footer_, textWidth, 13.0f) + 8;
        h += 28 + kPad;                                          // 閉じるボタン
        return h;
    }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour(juce::Colour::fromRGBA(14, 20, 32, 236));
        g.fillRoundedRectangle(r, 10.0f);
        g.setColour(juce::Colour::fromRGBA(255, 210, 120, 200));
        g.drawRoundedRectangle(r.reduced(0.5f), 10.0f, 1.5f);

        int y = kPad;
        const int textWidth = getWidth() - 2 * kPad;
        g.setColour(juce::Colour::fromRGB(255, 220, 140));
        g.setFont(UIColors::getUIFontExact(17.0f, juce::Font::bold));
        g.drawText(title_, kPad, y, textWidth, 24, juce::Justification::centredLeft, true);
        y += 24 + 6;

        g.setFont(UIColors::getUIFontExact(14.0f));
        for (const auto& step : steps_) {
            juce::String mark;
            juce::Colour colour;
            switch (step.state) {
                case StepState::Done:    mark = juce::String::fromUTF8("済");   colour = juce::Colour::fromRGB(120, 220, 140); break;
                case StepState::Running: mark = juce::String::fromUTF8("実行中"); colour = juce::Colour::fromRGB(255, 210, 120); break;
                case StepState::Skipped: mark = juce::String::fromUTF8("省略"); colour = juce::Colour::fromRGB(150, 150, 150); break;
                case StepState::Failed:  mark = juce::String::fromUTF8("失敗"); colour = juce::Colour::fromRGB(255, 120, 120); break;
                case StepState::Pending:
                default:                 mark = juce::String::fromUTF8("待ち"); colour = juce::Colour::fromRGB(130, 140, 160); break;
            }
            g.setColour(colour);
            g.drawText(mark, kPad, y, 52, kStepHeight, juce::Justification::centredLeft, false);
            g.setColour(step.state == StepState::Running ? juce::Colours::white : juce::Colour::fromRGB(210, 215, 225));
            g.drawText(step.label, kPad + 56, y, textWidth - 56, kStepHeight, juce::Justification::centredLeft, true);
            y += kStepHeight;
        }
        y += 6;

        g.setColour(juce::Colours::white);
        g.setFont(UIColors::getUIFontExact(14.0f));
        const int dh = measure(detail_, textWidth, 14.0f);
        drawWrapped(g, detail_, kPad, y, textWidth, dh, 14.0f, juce::Colours::white);
        y += dh + 8;

        if (footer_.isNotEmpty()) {
            g.setColour(juce::Colour::fromRGB(200, 210, 225));
            g.setFont(UIColors::getUIFontExact(13.0f));
            const int fh = measure(footer_, textWidth, 13.0f);
            drawWrapped(g, footer_, kPad, y, textWidth, fh, 13.0f, juce::Colour::fromRGB(200, 210, 225));
        }
    }

    void resized() override
    {
        closeButton_.setBounds(getWidth() - kPad - 90, getHeight() - kPad - 28, 90, 28);
    }

private:
    static constexpr int kPad = 14;
    static constexpr int kStepHeight = 22;

    static void drawWrapped(juce::Graphics& g, const juce::String& text, int x, int y, int w, int h,
                            float fontHeight, juce::Colour colour)
    {
        if (text.isEmpty())
            return;
        juce::AttributedString as;
        as.append(text, UIColors::getUIFontExact(fontHeight), colour);
        as.setWordWrap(juce::AttributedString::byChar);
        juce::TextLayout layout;
        layout.createLayout(as, static_cast<float>(w));
        layout.draw(g, juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                              static_cast<float>(w), static_cast<float>(h)));
    }

    static int measure(const juce::String& text, int width, float fontHeight)
    {
        if (text.isEmpty() || width <= 0)
            return 0;
        juce::AttributedString as;
        as.append(text, UIColors::getUIFontExact(fontHeight), juce::Colours::white);
        as.setWordWrap(juce::AttributedString::byChar);
        juce::TextLayout layout;
        layout.createLayout(as, static_cast<float>(width));
        return static_cast<int>(std::ceil(layout.getHeight())) + 4;
    }

    juce::String title_;
    std::vector<Step> steps_;
    juce::String detail_;
    juce::String footer_;
    juce::TextButton closeButton_;
};

} // namespace OpenTune
