#!/usr/bin/env python3
"""Regenerate data/emojis.txt with Unicode group categories.

Format per line: <emoji>\t<group>\t<short name>

Groups are the Unicode CLDR categories (same tabs used by the Android keyboard):
Smileys & Emotion, People & Body, Animals & Nature, Food & Drink, Travel &
Places, Activities, Objects, Symbols, Flags. The "Component" group (skin tone
modifiers) is skipped.

Within each category, emojis are ordered by Google's own picker ordering
(googlefonts/emoji-metadata, the order Gboard/Android shows). Emojis that
Google does not list as a base (mostly gender/family/directional variants) are
kept at the end of their category in their original CLDR order.
"""
import re
import sys
import urllib.request

EMOJI_TEST_URL = "https://www.unicode.org/Public/emoji/latest/emoji-test.txt"
ORDERING_URL = "https://raw.githubusercontent.com/googlefonts/emoji-metadata/main/emoji_17_0_ordering.json"
SKIP_GROUPS = {"Component"}
SKIN_TONES = set(range(0x1F3FB, 0x1F400))

def fetch(url: str) -> str:
    req = urllib.request.Request(url, headers={"User-Agent": "material3ui/1.0"})
    with urllib.request.urlopen(req, timeout=60) as resp:
        return resp.read().decode("utf-8")

def build_order(text: str):
    """Map emoji -> global picker index from Google's ordering JSON."""
    import json
    order = {}
    idx = 0
    for group in json.loads(text):
        for entry in group["emoji"]:
            emoji = "".join(chr(cp) for cp in entry["base"])
            if emoji not in order:
                order[emoji] = idx
                idx += 1
    return order

def parse(text: str):
    group = ""
    entries = []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith("# group:"):
            group = line.split(":", 1)[1].strip()
            continue
        if line.startswith("# subgroup:"):
            continue
        if line.startswith("#"):
            continue
        m = re.match(r"^([0-9A-F\s]+?)\s*;\s*(\S+)\s*#\s*(.+)$", line)
        if not m:
            continue
        codepoints, status, comment = m.groups()
        if status != "fully-qualified":
            continue
        if group in SKIP_GROUPS:
            continue
        cps = [int(cp, 16) for cp in codepoints.split()]
        # Skip skin-tone variants (Android keyboards keep them out of the grid).
        if any(cp in SKIN_TONES for cp in cps):
            continue
        emoji = "".join(chr(cp) for cp in cps)
        short_name = comment.strip()
        if short_name.startswith(emoji):
            short_name = short_name[len(emoji):]
        short_name = re.sub(r"^E\d+\.\d+\s*", "", short_name.strip())
        entries.append((emoji, group, short_name))
    return entries

def main() -> None:
    out = sys.argv[1] if len(sys.argv) > 1 else (
        "shell/data/emojis.txt"
    )
    order = {}
    try:
        order = build_order(fetch(ORDERING_URL))
    except Exception as exc:  # keep CLDR order if the source is unavailable
        print(f"warning: could not fetch Google ordering ({exc}); keeping CLDR order", file=sys.stderr)

    entries = parse(fetch(EMOJI_TEST_URL))
    by_group = {}
    order_list = []
    for entry in entries:
        if entry[1] not in by_group:
            by_group[entry[1]] = []
            order_list.append(entry[1])
        by_group[entry[1]].append(entry)
    ordered = []
    for group in order_list:
        by_group[group].sort(key=lambda e: order.get(e[0], 10**9))
        ordered.extend(by_group[group])
    with open(out, "w", encoding="utf-8") as fh:
        fh.write("\n".join(f"{e}\t{g}\t{n}" for e, g, n in ordered) + "\n")
    print(f"Wrote {len(ordered)} emojis to {out}")

if __name__ == "__main__":
    main()
