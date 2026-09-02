#pragma once

// The first-run tutorial's prompts (§8.5, D-097), one per step, in Appendix D's
// voice: lowercase, short, no full stops. The steps themselves are the app's
// (app/controller.cpp); the overlay draws the prompt.
namespace ui {

constexpr int kTutorialSteps = 6;
constexpr int kTutorialMaxLines = 3;

struct Prompt {
  const char* lines[kTutorialMaxLines];
  int count;
};

const Prompt& tutorial_prompt(int step);

}  // namespace ui
