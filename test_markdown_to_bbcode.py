# Test script: simulates the core logic of the new _markdown_to_bbcode
# to verify the conversion produces clean BBCode for RichTextLabel.
# This is a Python port of the C++ algorithm for quick verification.

import re


def _markdown_to_bbcode(md):
    # Step 1: Extract links ([text](url)) and images (![alt](url)).
    links = []  # list of (url, text) for links, or just alt_text for images
    s = md

    def _replace_link_or_image(pos):
        # Find next '[' and check if it's an image (preceded by '!') or a
        # link, and if there's a matching "(...)" part.
        while True:
            link_start = s.find('[', pos)
            if link_start == -1:
                return pos
            is_image = link_start > 0 and s[link_start - 1] == '!'
            chosen_start = link_start - 1 if is_image else link_start

            text_end = s.find(']', link_start + 1)
            if text_end == -1:
                # Unterminated '[': escape it.
                s2 = s[:chosen_start] + '[lb]' + s[chosen_start + (2 if is_image else 1):]
                return (s2, chosen_start + 4)

            url_open = s.find('(', text_end + 1)
            paren_ok = (url_open != -1 and url_open - text_end <= 2)
            if not paren_ok:
                # Not a link: escape the '['.
                s2 = s[:link_start] + '[lb]' + s[link_start + 1:]
                return (s2, link_start + 4)
            url_close = s.find(')', url_open + 1)
            if url_close == -1:
                s2 = s[:link_start] + '[lb]' + s[link_start + 1:]
                return (s2, link_start + 4)

            text = s[link_start + 1:text_end]
            url = s[url_open + 1:url_close]
            idx = len(links)
            if is_image:
                links.append(('__IMG__', text))
                placeholder = f"@@IMG_{idx}@@"
            else:
                links.append((url, text))
                placeholder = f"@@LINK_{idx}@@"
            s2 = s[:chosen_start] + placeholder + s[url_close + 1:]
            return (s2, chosen_start + len(placeholder))

    pos = 0
    while True:
        result = _replace_link_or_image(pos)
        if isinstance(result, tuple):
            s, new_pos = result
            pos = new_pos
        else:
            break

    # Step 2: Extract code blocks.
    code_blocks = []

    def _replace_code_block(pos):
        fence = s.find('```', pos)
        if fence == -1:
            return pos
        content_start = s.find('\n', fence)
        if content_start == -1:
            return len(s)
        content_start += 1
        fence_end = s.find('```', content_start)
        if fence_end == -1:
            return len(s)
        content = s[content_start:fence_end].strip()
        code_blocks.append(content)
        idx = len(code_blocks) - 1
        placeholder = f"@@CODE_{idx}@@"
        return s[:fence] + placeholder + s[fence_end + 3:], fence + len(placeholder)

    pos = 0
    while True:
        result = _replace_code_block(pos)
        if not isinstance(result, tuple):
            break
        s, pos = result

    # Step 3: Extract inline code.
    inline_codes = []

    def _replace_inline(pos):
        tick = s.find('`', pos)
        if tick == -1:
            return pos
        end = s.find('`', tick + 1)
        if end == -1:
            return len(s)
        code = s[tick + 1:end]
        inline_codes.append(code)
        idx = len(inline_codes) - 1
        placeholder = f"@@INLINE_{idx}@@"
        return s[:tick] + placeholder + s[end + 1:], tick + len(placeholder)

    pos = 0
    while True:
        result = _replace_inline(pos)
        if not isinstance(result, tuple):
            break
        s, pos = result

    # Step 4: inline formatting (bold/italic/~~strike~~).
    # ~~text~~ -> [s]text[/s]
    def _replace_pair(src, open_str, close_str, open_tag, close_tag):
        out = []
        i = 0
        while i < len(src):
            # find open
            o = src.find(open_str, i)
            if o == -1:
                out.append(src[i:])
                break
            c = src.find(close_str, o + len(open_str))
            if c == -1:
                out.append(src[i:])
                break
            content = src[o + len(open_str):c]
            # For italic: check boundary (simplified from C++).
            if open_str == '_':
                start_ok = (o == 0 or not src[o - 1].isalnum())
                end_ok = (c + 1 >= len(src) or not src[c + 1].isalnum())
                if not (start_ok and end_ok):
                    out.append(src[i:o + 1])
                    i = o + 1
                    continue
            out.append(src[i:o] + open_tag + content + close_tag)
            i = c + len(close_str)
        return ''.join(out)

    s = _replace_pair(s, '~~', '~~', '[s]', '[/s]')
    s = _replace_pair(s, '**', '**', '[b]', '[/b]')
    s = _replace_pair(s, '_', '_', '[i]', '[/i]')

    # Step 4.5: escape remaining literal '[' to [lb], but preserve our
    # intentionally emitted BBCode tags.
    whitelist = set(
        "b i u s code url hr table cell ul ol indent font_size font color center right".split()
    )
    i = 0
    out = []
    while i < len(s):
        if s[i] == '[':
            close = s.find(']', i + 1)
            if close != -1:
                tag = s[i + 1:close]
                # Normalize: strip leading '/' and anything after '=' or space.
                tag_name = tag
                for sep in ('=', ' '):
                    idx2 = tag_name.find(sep)
                    if idx2 != -1:
                        tag_name = tag_name[:idx2]
                if tag_name.startswith('/'):
                    tag_name = tag_name[1:]
                if tag_name in whitelist:
                    out.append(s[i:close + 1])
                    i = close + 1
                    continue
                # Unknown tag -> escape the '['.
                out.append('[lb]')
                i += 1
                continue
            # Unterminated '['.
            out.append('[lb]')
            i += 1
            continue
        out.append(s[i])
        i += 1
    s = ''.join(out)

    # Step 5: restore inline codes.
    for idx, code in enumerate(inline_codes):
        s = s.replace(f"@@INLINE_{idx}@@", f"[code]{code}[/code]")

    # Step 6: restore code blocks.
    for idx, code in enumerate(code_blocks):
        s = s.replace(f"@@CODE_{idx}@@", f"[code]{code}[/code]")

    # Step 7: restore links/images.
    for idx, entry in enumerate(links):
        if entry[0] == '__IMG__':
            alt = entry[1]
            s = s.replace(f"@@IMG_{idx}@@", f"[i]{alt}[/i]")
        else:
            url, text = entry
            s = s.replace(f"@@LINK_{idx}@@", f"[url={url}]{text}[/url]")

    return s


# ---- Test cases ----
tests = [
    ("Basic text", "Hello world"),
    ("Bold and italic", "**bold** and _italic_ here"),
    ("Inline code with brackets", "Check `arr[0]` and `obj.key[1]`"),
    ("Literal brackets (the \"b/b\" bug)", "Use array[0] or list[1:3]"),
    ("Markdown link", "See [the docs](https://example.com) for info"),
    ("Link with brackets in URL", "Check [this](https://example.com/page?id=1&ref[test]=ok)"),
    ("Mixed: link then literal brackets", "Read [docs](https://x.com) then use array[0]"),
    ("Image", "See ![screenshot](img.png) above"),
    ("Code block containing brackets", "```\nif (a[0] && b[1]) { return; }\n```"),
    ("Markdown link inside bold (edge case)", "**See [link](https://x.com)**"),
    ("Blockquote", "> This is a quote"),
    ("Headers and bold", "# Hello **world**\n## Subtitle\n\nregular text"),
    ("Table-ish text", "| a | b |\n| - | - |\n| 1 | 2 |"),
]

WHITELIST = set(
    "b i u s code url hr table cell ul ol indent font_size font color center right lb".split()
)

for name, input_text in tests:
    output = _markdown_to_bbcode(input_text)
    print(f"\n=== {name} ===")
    print(f"  IN : {input_text!r}")
    print(f"  OUT: {output!r}")
    # Sanity checks: no stray '[' that isn't part of BBCode.
    raw_brackets = re.findall(r'\[[^\[\]]*\]', output)
    bad = []
    for br in raw_brackets:
        inner = br[1:-1]
        tag_name = inner
        for sep in ('=', ' '):
            idx2 = tag_name.find(sep)
            if idx2 != -1:
                tag_name = tag_name[:idx2]
        if tag_name.startswith('/'):
            tag_name = tag_name[1:]
        if tag_name not in WHITELIST:
            bad.append(br)
    if bad:
        print(f"  !!! WARNING: potential stray brackets: {bad}")
    else:
        print(f"  OK")
