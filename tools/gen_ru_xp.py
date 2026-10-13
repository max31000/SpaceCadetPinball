#!/usr/bin/env python3
"""Generate the `ru_xp` translation table from the original Russian Windows XP pinball.exe.

The Russian XP build of 3D Pinball localized only a handful of strings (window title,
"game over", dialogs, menus); in-game messages stayed English. This script reads the string
table (RT_STRING), the main menu (RT_MENU) and the high score / key mapper dialogs (RT_DIALOG)
of that exe and emits `SpaceCadetPinball/translations_ru_xp.inc`, an initializer list of
`{Msg::Id, "text"}` pairs used for Lang::RussianXP (see translations.cpp).

Usage:
    pip install pefile
    python tools/gen_ru_xp.py path/to/pinball.exe [-o SpaceCadetPinball/translations_ru_xp.inc]
    python tools/gen_ru_xp.py path/to/pinball.exe --dump   # list all resources found

Only short UI strings are emitted; the exe itself is not part of this repository.
"""
import argparse
import hashlib
import os
import re
import sys

try:
    import pefile
except ImportError:  # pragma: no cover
    sys.exit("pefile is required: pip install pefile")

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
DEFAULT_OUT = os.path.join(REPO, "SpaceCadetPinball", "translations_ru_xp.inc")
TRANSLATIONS_H = os.path.join(REPO, "SpaceCadetPinball", "translations.h")

# Msg ids named STRINGnnn map to string resource id nnn - 101 (upstream convention).
# Named ids that are string resources as well:
NAMED_STRING_IDS = {
    "TextBoxUseBitmapFont": 158,
    "TextBoxColor": 189,
}

# Menu command id -> Msg name (Menu1 enum values in options.h are the original command ids).
MENU_COMMANDS = {
    101: "Menu1_New_Game",
    102: "Menu1_About_Pinball",
    103: "Menu1_High_Scores",
    105: "Menu1_Exit",
    201: "Menu1_Sounds",
    202: "Menu1_Music",
    301: "Menu1_Help_Topics",
    401: "Menu1_Launch_Ball",
    402: "Menu1_Pause_Resume_Game",
    403: "Menu1_Full_Screen",
    404: "Menu1_Demo",
    405: "Menu1_Select_Table",
    406: "Menu1_Player_Controls",
    408: "Menu1_1Player",
    409: "Menu1_2Players",
    410: "Menu1_3Players",
    411: "Menu1_4Players",
}
# Popup menus have no ids: map them by their path of popup indices.
MENU_POPUPS = {
    (0,): "Menu1_Game",
    (1,): "Menu1_Options",
    (1, 0): "Menu1_Select_Players",
    (2,): "Menu1_Help",
}

# Dialog resource name -> {"caption": Msg, control id: Msg}
DIALOGS = {
    "DLG_HIGHSCORES": {
        "caption": "HIGHSCORES_Caption",
        1: "GenericOk",
        2: "GenericCancel",
        112: "HIGHSCORES_Clear",
        701: "HIGHSCORES_Name",
        702: "HIGHSCORES_Score",
        703: "HIGHSCORES_Rank",
    },
    "KEYMAPPER": {
        "caption": "KEYMAPPER_Caption",
        501: "KEYMAPPER_Default",
        901: "KEYMAPPER_FlipperL",
        902: "KEYMAPPER_FlipperR",
        903: "KEYMAPPER_Plunger",
        904: "KEYMAPPER_BumpLeft",
        905: "KEYMAPPER_BumpRight",
        906: "KEYMAPPER_BumpBottom",
        801: "KEYMAPPER_Help1",
        802: "KEYMAPPER_Help2",
        908: "KEYMAPPER_Groupbox1",
        909: "KEYMAPPER_Groupbox2",
    },
}


def res_name(entry):
    return entry.id if entry.name is None else str(entry.name)


def iter_resources(pe, type_id):
    for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        if t.id != type_id:
            continue
        for entry in t.directory.entries:
            for lang in entry.directory.entries:
                data = pe.get_data(lang.data.struct.OffsetToData, lang.data.struct.Size)
                yield res_name(entry), data


def read_strings(pe):
    strings = {}
    for block_id, data in iter_resources(pe, pefile.RESOURCE_TYPE["RT_STRING"]):
        base = (block_id - 1) * 16
        off = 0
        for i in range(16):
            n = int.from_bytes(data[off:off + 2], "little")
            off += 2
            if n:
                strings[base + i] = data[off:off + 2 * n].decode("utf-16-le")
            off += 2 * n
    return strings


def read_sz(data, off):
    end = off
    while data[end:end + 2] != b"\0\0":
        end += 2
    return data[off:end].decode("utf-16-le"), end + 2


def read_sz_or_ord(data, off):
    first = int.from_bytes(data[off:off + 2], "little")
    if first == 0:
        return "", off + 2
    if first == 0xFFFF:
        return int.from_bytes(data[off + 2:off + 4], "little"), off + 4
    return read_sz(data, off)


def read_menu(data):
    """Standard MENUITEMTEMPLATE list -> (commands {id: text}, popups {path: text})."""
    offset = int.from_bytes(data[2:4], "little")
    pos = 4 + offset
    commands, popups = {}, {}

    def level(pos, path):
        index = 0
        while True:
            flags = int.from_bytes(data[pos:pos + 2], "little")
            pos += 2
            item_id = None
            if not flags & 0x10:  # MF_POPUP
                item_id = int.from_bytes(data[pos:pos + 2], "little")
                pos += 2
            text, pos = read_sz(data, pos)
            if flags & 0x10:
                popups[path + (index,)] = text
                pos = level(pos, path + (index,))
                index += 1
            elif text:
                commands[item_id] = text
            if flags & 0x80:  # MF_END
                return pos

    level(pos, ())
    # Popup paths count popups only (separators/commands are not part of the index).
    return commands, popups


def read_dialog(data):
    """Regular DLGTEMPLATE -> (caption, {control id: text})."""
    if data[0:4] == b"\x01\x00\xff\xff":
        raise ValueError("DLGTEMPLATEEX is not supported")
    style = int.from_bytes(data[0:4], "little")
    count = int.from_bytes(data[8:10], "little")
    pos = 18
    _, pos = read_sz_or_ord(data, pos)  # menu
    _, pos = read_sz_or_ord(data, pos)  # class
    caption, pos = read_sz(data, pos)
    if style & 0x40:  # DS_SETFONT
        pos += 2
        _, pos = read_sz(data, pos)
    controls = {}
    for _ in range(count):
        pos = (pos + 3) & ~3
        ctrl_id = int.from_bytes(data[pos + 16:pos + 18], "little")
        pos += 18
        _, pos = read_sz_or_ord(data, pos)  # class
        title, pos = read_sz_or_ord(data, pos)
        extra = int.from_bytes(data[pos:pos + 2], "little")
        pos += 2 + extra
        if isinstance(title, str) and title:
            controls[ctrl_id] = title
    return caption, controls


def ui_text(text):
    """Menu/dialog text -> plain label: drop accelerator marks and the shortcut column."""
    text = text.split("\t", 1)[0]
    text = text.replace("&&", "\0").replace("&", "").replace("\0", "&")
    return text


def c_escape(text):
    text = text.replace("\r\n", "\n")
    out = []
    for ch in text:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif ord(ch) < 0x20:
            out.append("\\%03o" % ord(ch))
        else:
            out.append(ch)
    return "".join(out)


def msg_names():
    src = open(TRANSLATIONS_H, encoding="utf-8").read()
    body = src[src.index("enum class Msg"):]
    body = body[:body.index("};")]
    return set(re.findall(r"^\s*(\w+)\s*(?:=\s*\d+)?\s*,", body, re.M)) - {"Min", "Max"}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("exe", help="path to the original Russian XP pinball.exe")
    ap.add_argument("-o", "--output", default=DEFAULT_OUT)
    ap.add_argument("--dump", action="store_true", help="print the resources and exit")
    args = ap.parse_args()

    raw = open(args.exe, "rb").read()
    pe = pefile.PE(data=raw)
    strings = read_strings(pe)
    menus = list(iter_resources(pe, pefile.RESOURCE_TYPE["RT_MENU"]))
    dialogs = {name: data for name, data in iter_resources(pe, pefile.RESOURCE_TYPE["RT_DIALOG"])}

    if args.dump:
        for sid in sorted(strings):
            print(f"STRING id={sid}: {strings[sid]!r}")
        for name, data in menus:
            commands, popups = read_menu(data)
            for path, text in sorted(popups.items()):
                print(f"MENU {name} popup {path}: {text!r}")
            for cid, text in sorted(commands.items()):
                print(f"MENU {name} cmd {cid}: {text!r}")
        for name, data in dialogs.items():
            try:
                caption, controls = read_dialog(data)
            except ValueError as e:
                print(f"DIALOG {name}: {e}")
                continue
            print(f"DIALOG {name} caption: {caption!r}")
            for cid, text in sorted(controls.items()):
                print(f"DIALOG {name} ctrl {cid}: {text!r}")
        return

    known = msg_names()
    entries = {}

    for name in sorted(known):
        m = re.fullmatch(r"STRING(\d+)", name)
        sid = int(m.group(1)) - 101 if m else NAMED_STRING_IDS.get(name)
        if sid is not None and sid in strings:
            entries[name] = strings[sid]

    if menus:
        commands, popups = read_menu(menus[0][1])
        for cid, name in MENU_COMMANDS.items():
            if cid in commands:
                entries[name] = ui_text(commands[cid])
        for path, name in MENU_POPUPS.items():
            if path in popups:
                entries[name] = ui_text(popups[path])

    for dlg_name, mapping in DIALOGS.items():
        if dlg_name not in dialogs:
            continue
        caption, controls = read_dialog(dialogs[dlg_name])
        for key, name in mapping.items():
            text = caption if key == "caption" else controls.get(key)
            if text:
                entries[name] = ui_text(text)

    unknown = sorted(set(entries) - known)
    if unknown:
        sys.exit(f"generator maps to unknown Msg ids: {unknown}")

    order = {name: i for i, name in enumerate(re.findall(
        r"^\s*(\w+)\s*(?:=\s*\d+)?\s*,", open(TRANSLATIONS_H, encoding="utf-8").read(), re.M))}
    sha = hashlib.sha256(raw).hexdigest()
    lines = [
        "// Generated by tools/gen_ru_xp.py from the Russian Windows XP pinball.exe",
        f"// (sha256 {sha}). Do not edit by hand.",
        "// Lang::RussianXP strings: {Msg id, UTF-8 text}. Missing ids fall back to English.",
    ]
    for name in sorted(entries, key=lambda n: order.get(n, 1 << 30)):
        lines.append(f'{{Msg::{name}, "{c_escape(entries[name])}"}},')
    with open(args.output, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    cyr = sum(1 for t in entries.values() if re.search("[Ѐ-ӿ]", t))
    print(f"{args.output}: {len(entries)} entries ({cyr} with Cyrillic)")


if __name__ == "__main__":
    main()
