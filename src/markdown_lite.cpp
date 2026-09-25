#include "markdown_lite.h"

#include "imgui.h"

namespace {

// Removes Unicode variation selectors (U+FE0E/U+FE0F) and zero-width
// joiners (U+200D): ImGui draws one glyph per code point with no text
// shaping, so these would only ever show up as stray boxes.
void stripInvisibles(std::string &s) {
  static const char *kDrop[] = {"\xEF\xB8\x8F", "\xEF\xB8\x8E", "\xE2\x80\x8D"};
  for (const char *seq : kDrop) {
    size_t pos;
    while ((pos = s.find(seq)) != std::string::npos)
      s.erase(pos, 3);
  }
}

void replaceAll(std::string &s, const std::string &from,
                const std::string &to) {
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
}

// [text](url) -> text
void flattenLinks(std::string &s) {
  size_t open = 0;
  while ((open = s.find('[', open)) != std::string::npos) {
    size_t close = s.find("](", open);
    size_t end = close == std::string::npos ? close : s.find(')', close);
    if (close == std::string::npos || end == std::string::npos) {
      ++open;
      continue;
    }
    s = s.substr(0, open) + s.substr(open + 1, close - open - 1) +
        s.substr(end + 1);
  }
}

// *emphasis* -> emphasis. Only a '*' directly followed by a non-space
// that has a matching '*' directly after a non-space later on the line;
// a lone asterisk ("5 * 3") is left alone.
void flattenSingleAsterisks(std::string &s) {
  size_t open = 0;
  while ((open = s.find('*', open)) != std::string::npos) {
    if (open + 1 >= s.size() || s[open + 1] == ' ') {
      ++open;
      continue;
    }
    size_t close = s.find('*', open + 1);
    if (close == std::string::npos)
      break;
    if (s[close - 1] == ' ') {
      open = close;
      continue;
    }
    s.erase(close, 1);
    s.erase(open, 1);
    open = close - 1;
  }
}

std::string flattenInline(std::string s) {
  stripInvisibles(s);
  flattenLinks(s);
  replaceAll(s, "**", "");
  replaceAll(s, "`", "");
  flattenSingleAsterisks(s);
  // A whole line in _underscores_ is italic; underscores inside words
  // (file_names) are left alone.
  if (s.size() >= 2 && s.front() == '_' && s.back() == '_')
    s = s.substr(1, s.size() - 2);
  return s;
}

std::string trimRight(std::string s) {
  while (!s.empty() &&
         (s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
    s.pop_back();
  return s;
}

} // namespace

std::vector<MarkdownBlock> parseMarkdownLite(const std::string &markdown) {
  std::vector<MarkdownBlock> blocks;
  size_t start = 0;
  while (start <= markdown.size()) {
    size_t nl = markdown.find('\n', start);
    std::string line = trimRight(markdown.substr(
        start, nl == std::string::npos ? std::string::npos : nl - start));
    start = nl == std::string::npos ? markdown.size() + 1 : nl + 1;

    size_t indent = line.find_first_not_of(" \t");
    MarkdownBlock b;
    if (indent == std::string::npos) {
      // Collapse runs of blank lines into one.
      if (!blocks.empty() && blocks.back().kind != MarkdownBlock::Kind::Blank)
        blocks.push_back({MarkdownBlock::Kind::Blank, 0, {}});
      continue;
    }
    std::string body = line.substr(indent);

    if (body == "---" || body == "***" || body == "___") {
      b.kind = MarkdownBlock::Kind::Rule;
    } else if (body[0] == '#') {
      size_t level = body.find_first_not_of('#');
      if (level != std::string::npos && level <= 6 && body[level] == ' ') {
        b.kind = MarkdownBlock::Kind::Heading;
        b.level = (int)level;
        b.text = flattenInline(body.substr(level + 1));
      } else {
        b.text = flattenInline(body);
      }
    } else if ((body[0] == '-' || body[0] == '*' || body[0] == '+') &&
               body.size() > 1 && body[1] == ' ') {
      b.kind = MarkdownBlock::Kind::Bullet;
      b.level = (int)(indent / 2);
      b.text = flattenInline(body.substr(2));
    } else {
      b.text = flattenInline(body);
    }
    blocks.push_back(std::move(b));
  }
  while (!blocks.empty() && blocks.back().kind == MarkdownBlock::Kind::Blank)
    blocks.pop_back();
  return blocks;
}

void renderMarkdownLite(const std::vector<MarkdownBlock> &blocks) {
  const float baseSize = ImGui::GetFontSize();
  const ImVec4 headingColor = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
  for (const auto &b : blocks) {
    switch (b.kind) {
    case MarkdownBlock::Kind::Heading: {
      float scale = b.level == 1 ? 1.45f : b.level == 2 ? 1.25f : 1.1f;
      ImGui::PushFont(nullptr, baseSize * scale);
      ImGui::PushStyleColor(ImGuiCol_Text, headingColor);
      ImGui::TextWrapped("%s", b.text.c_str());
      ImGui::PopStyleColor();
      ImGui::PopFont();
      break;
    }
    case MarkdownBlock::Kind::Bullet:
      ImGui::Indent(b.level * baseSize);
      ImGui::Bullet();
      ImGui::TextWrapped("%s", b.text.c_str());
      ImGui::Unindent(b.level * baseSize);
      break;
    case MarkdownBlock::Kind::Rule:
      ImGui::Separator();
      break;
    case MarkdownBlock::Kind::Blank:
      ImGui::Spacing();
      break;
    case MarkdownBlock::Kind::Paragraph:
      ImGui::TextWrapped("%s", b.text.c_str());
      break;
    }
  }
}
