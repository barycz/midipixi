#!/usr/bin/env python3
"""Minimal stand-in for JUCE's `juceaide` build helper.

JUCE normally compiles juceaide during configuration and runs it during the
build. On Windows machines with Smart App Control, freshly compiled unsigned
executables are blocked, so midipixi replaces it with this script. It
implements only the sub-commands needed to build the VST3 + standalone on
Windows:

  version                       print the JUCE version
  rcfile <info-file> <out.rc>   Windows version resource (port of juce_ResourceRc.cpp)

The output matches JUCE 8 juceaide.
"""
import os
import sys

JUCE_VERSION = "JUCE v8.0.15"


def parse_project_data(path):
    with open(path, "r", encoding="utf-8") as f:
        contents = f.read()
    result = {}
    for line in contents.split("\x1e"):
        if not line:
            continue
        key, _, value = line.partition("\x1f")
        result.setdefault(key, value)
    return result


def comma_version(version):
    parts = [p.strip() for p in version.replace(",", ".").split(".") if p.strip()]
    while len(parts) < 4:
        parts.append("0")
    return ",".join(parts)


def write_if_changed(path, text):
    data = text.encode("utf-8")
    if os.path.exists(path):
        with open(path, "rb") as f:
            if f.read() == data:
                return
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def rcfile(info_file, out_file):
    d = parse_project_data(info_file)
    version = d.get("VERSION", "")
    lines = [
        "#pragma code_page(65001)",
        "",
        "#ifdef JUCE_USER_DEFINED_RC_FILE",
        " #include JUCE_USER_DEFINED_RC_FILE",
        "#else",
        "",
        "#undef  WIN32_LEAN_AND_MEAN",
        "#define WIN32_LEAN_AND_MEAN",
        "#include <windows.h>",
        "",
        "VS_VERSION_INFO VERSIONINFO",
        "FILEVERSION  " + comma_version(version),
        "PRODUCTVERSION  " + comma_version(version),
        "BEGIN",
        '  BLOCK "StringFileInfo"',
        "  BEGIN",
        '    BLOCK "040904E4"',
        "    BEGIN",
    ]

    def value(name, v):
        if v:
            lines.append('      VALUE "%s",  "%s\\0"' % (name, v.replace('"', '""')))

    value("CompanyName", d.get("COMPANY_NAME", ""))
    value("LegalCopyright", d.get("COMPANY_COPYRIGHT", ""))
    value("FileDescription", d.get("PROJECT_NAME", ""))
    value("FileVersion", version)
    value("ProductName", d.get("PROJECT_NAME", ""))
    value("ProductVersion", version)
    lines += [
        "    END",
        "  END",
        "",
        '  BLOCK "VarFileInfo"',
        "  BEGIN",
        '    VALUE "Translation", 0x409, 1252',
        "  END",
        "END",
        "",
        "#endif",
    ]
    icon = d.get("ICON_FILE", "")
    text = "\r\n".join(lines) + "\r\n"
    if icon and os.path.isfile(icon):
        name = os.path.basename(icon)
        text += '\r\nIDI_ICON1 ICON DISCARDABLE "%s"\r\nIDI_ICON2 ICON DISCARDABLE "%s"' % (name, name)
    write_if_changed(out_file, text)


def main(argv):
    if len(argv) < 2:
        print("juceaide.py: no command", file=sys.stderr)
        return 1
    cmd = argv[1]
    if cmd == "version":
        print(JUCE_VERSION)
        return 0
    if cmd == "rcfile" and len(argv) >= 4:
        rcfile(argv[2], argv[3])
        return 0
    print("juceaide.py: unsupported command '%s' (only version and rcfile are implemented)" % cmd,
          file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
