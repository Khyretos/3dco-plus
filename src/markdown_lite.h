#pragma once
// Just enough Markdown to show release notes in ImGui: headings,
// bullet lists, horizontal rules and paragraphs. Inline markup (**bold**,
// `code`, [links](url), _italic lines_) is flattened to plain text.
// Parse once, draw every frame.

#include <string>
#include <vector>

struct MarkdownBlock {
  enum class Kind { Heading, Bullet, Rule, Paragraph, Blank };
  Kind kind = Kind::Paragraph;
  int level = 0; // heading level (1-6), or bullet nesting depth
  std::string text;
};

std::vector<MarkdownBlock> parseMarkdownLite(const std::string &markdown);

// Draws parsed blocks into the current ImGui window.
void renderMarkdownLite(const std::vector<MarkdownBlock> &blocks);
