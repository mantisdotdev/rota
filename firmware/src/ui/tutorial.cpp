#include "ui/tutorial.h"

namespace ui {

namespace {

const Prompt kPrompts[kTutorialSteps] = {
    {{"tap the kick"}, 1},
    {{"tap it again", "see it stretch?"}, 2},
    {{"tap the snare"}, 1},
    {{"now turn chance"}, 1},
    {{"hold show to share it"}, 1},
    {{"press show twice", "and tap A A B A", "hold A and press play"}, 3},
};

}  // namespace

const Prompt& tutorial_prompt(int step) {
  if (step < 0) step = 0;
  if (step >= kTutorialSteps) step = kTutorialSteps - 1;
  return kPrompts[step];
}

}  // namespace ui
