#pragma once

// Input strings for 4D Sports Boxing: Chimera's core's movie rows (chimera-core-open4dboxing's waterbox.config), so a
// solution's lines are rows of a Chimera movie:
//
//   "|  160,  100,<67 buttons>|"   (or the 57 before the function keys: a movie of the earlier core)
//
// the mouse's place first (x 0-319, y 0-199, each "%5d,"), then one character a button, its mnemonic when pressed,
// '.' when not, in this order:
//
//   < >                     the mouse's left and right buttons
//   U D L R Q E Z C K O     the numeric keypad's 8 2 4 6 7 9 1 3 5 0 (the human boxer's default controls: 8 forward,
//                           2 back, 4 and 6 sideways, 7 9 1 3 the corners)
//   N _ B X T               Enter (the default controls' punch button), Space, Backspace, Esc, Tab
//   ^ ! % &                 the left Shift, the right Shift (the default controls' other button), Ctrl, Alt
//   a-z 0-9                 the letters and the digits
//   A F G H I J M P S V     the function keys F1-F10 (the ring's cameras and panels)

#include <cstdint>
#include <cstdio>
#include <jaffarCommon/exceptions.hpp>
#include <jaffarCommon/json.hpp>
#include <string>

namespace jaffar
{

struct input_t
{
  __extension__ unsigned __int128 buttons = 0; // bit k: the k-th button above (67: wider than 64 bits)
  int16_t mouseX = 160;
  int16_t mouseY = 100;
};

class InputParser
{
public:

  static constexpr const char *mnemonics = "<>UDLRQEZCKON_BXT^!%&abcdefghijklmnopqrstuvwxyz0123456789AFGHIJMPSV";
  static constexpr size_t buttonCount = 67;
  static constexpr size_t earlierButtonCount = 57; // (the earlier core's rows, without the function keys)

  InputParser(const nlohmann::json &config) {}

  inline input_t parseInputString(const std::string &s) const
  {
    input_t input;
    const size_t count = s.size() - 14;
    if ((count != buttonCount && count != earlierButtonCount) || s[0] != '|' || s[6] != ',' || s[12] != ',' || s.back() != '|') reportBadInputString(s);
    int x, y;
    if (std::sscanf(s.c_str(), "|%d,%d,", &x, &y) != 2 || x < 0 || x > 319 || y < 0 || y > 199) reportBadInputString(s);
    input.mouseX = (int16_t)x;
    input.mouseY = (int16_t)y;
    for (size_t k = 0; k < count; k++)
    {
      const char c = s[13 + k];
      if (c == mnemonics[k]) input.buttons |= (unsigned __int128)1 << k;
      else if (c != '.') reportBadInputString(s);
    }
    return input;
  }

  // The string of an input (the inverse of parseInputString)
  static inline std::string inputString(const input_t &input)
  {
    char head[16];
    std::snprintf(head, sizeof head, "|%5d,%5d,", (int)input.mouseX, (int)input.mouseY);
    std::string s(head);
    for (size_t k = 0; k < buttonCount; k++) s += (input.buttons >> k) & 1 ? mnemonics[k] : '.';
    return s + "|";
  }

private:

  static inline void reportBadInputString(const std::string &s) { JAFFAR_THROW_LOGIC("Could not decode input string: '%s'\n", s.c_str()); }
};

} // namespace jaffar
