#pragma once
// A tiny vector "stroke" font for the cockpit screens (no SDL_ttf, no textures): every glyph is a few line segments in a
// unit cell (x 0..1 left->right, y 0..1 top->bottom), which the screen canvas draws as thick quads. Pure logic, unit-tested.
// Supported: A-Z (lower case draws as upper), 0-9 and  + - . , : ; / % ! ? _ = ( ) < > | ' *  and space. Anything else is blank.
#include <string>
#include <vector>

namespace cockpit {

struct Stroke { float x1, y1, x2, y2; };

constexpr float kCellAspect = 0.55f;   // glyph cell width / height
constexpr float kGapRatio = 0.25f;     // extra space between cells, as a fraction of the cell width

const std::vector<Stroke>& glyphStrokes(char c);   // empty for space / unknown characters
bool hasGlyph(char c);                             // true for characters that draw at least one stroke

float textAdvance(float height);                   // distance from one character's start to the next
float textWidth(const std::string& text, float height);   // ink extent of the whole string (no trailing gap)

// Appends the strokes of 'text' with its top-left at (x, y), cell height 'height'. Output is in the caller's units.
void layoutText(const std::string& text, float x, float y, float height, std::vector<Stroke>& out);

} // namespace cockpit
