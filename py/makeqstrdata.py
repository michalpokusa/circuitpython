"""
Process raw qstr file and output qstr data with length, hash and data bytes.

This script works with Python 3.7+

For documentation about the format of compressed translated strings, see
supervisor/shared/translate.h
"""

from __future__ import print_function

import bisect
import re
import sys
import zlib

import collections
import gettext
import os.path
from dataclasses import dataclass, field
from typing import List, Dict
import gzip

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(errors="backslashreplace")

py = os.path.dirname(sys.argv[0])
top = os.path.dirname(py)

# Python 2/3 compatibility:
#   - iterating through bytes is different
#   - codepoint2name lives in a different module
import platform

if platform.python_version_tuple()[0] == "2":
    bytes_cons = lambda val, enc=None: bytearray(val)
    from htmlentitydefs import codepoint2name
elif platform.python_version_tuple()[0] == "3":
    bytes_cons = bytes
    from html.entities import codepoint2name
# end compatibility code

codepoint2name[ord("-")] = "hyphen"

# add some custom names to map characters that aren't in HTML
codepoint2name[ord(" ")] = "space"
codepoint2name[ord("'")] = "squot"
codepoint2name[ord(",")] = "comma"
codepoint2name[ord(".")] = "dot"
codepoint2name[ord(":")] = "colon"
codepoint2name[ord(";")] = "semicolon"
codepoint2name[ord("/")] = "slash"
codepoint2name[ord("%")] = "percent"
codepoint2name[ord("#")] = "hash"
codepoint2name[ord("(")] = "paren_open"
codepoint2name[ord(")")] = "paren_close"
codepoint2name[ord("[")] = "bracket_open"
codepoint2name[ord("]")] = "bracket_close"
codepoint2name[ord("{")] = "brace_open"
codepoint2name[ord("}")] = "brace_close"
codepoint2name[ord("*")] = "star"
codepoint2name[ord("!")] = "bang"
codepoint2name[ord("\\")] = "backslash"
codepoint2name[ord("+")] = "plus"
codepoint2name[ord("$")] = "dollar"
codepoint2name[ord("=")] = "equals"
codepoint2name[ord("?")] = "question"
codepoint2name[ord("@")] = "at_sign"
codepoint2name[ord("^")] = "caret"
codepoint2name[ord("|")] = "pipe"
codepoint2name[ord("~")] = "tilde"

C_ESCAPES = {
    "\a": "\\a",
    "\b": "\\b",
    "\f": "\\f",
    "\n": "\\n",
    "\r": "\\r",
    "\t": "\\t",
    "\v": "\\v",
    "'": "\\'",
    '"': '\\"',
}

# static qstrs, should be sorted
# These are qstrs that are always included and always have the same number. It allows mpy files to omit them.
static_qstr_list = [
    "",
    "__dir__",  # Put __dir__ after empty qstr for builtin dir() to work
    "\n",
    " ",
    "*",
    "/",
    "<module>",
    "_",
    "__call__",
    "__class__",
    "__delitem__",
    "__enter__",
    "__exit__",
    "__getattr__",
    "__getitem__",
    "__hash__",
    "__init__",
    "__int__",
    "__iter__",
    "__len__",
    "__main__",
    "__module__",
    "__name__",
    "__new__",
    "__next__",
    "__qualname__",
    "__repr__",
    "__setitem__",
    "__str__",
    "ArithmeticError",
    "AssertionError",
    "AttributeError",
    "BaseException",
    "EOFError",
    "Ellipsis",
    "Exception",
    "GeneratorExit",
    "ImportError",
    "IndentationError",
    "IndexError",
    "KeyError",
    "KeyboardInterrupt",
    "LookupError",
    "MemoryError",
    "NameError",
    "NoneType",
    "NotImplementedError",
    "OSError",
    "OverflowError",
    "RuntimeError",
    "StopIteration",
    "SyntaxError",
    "SystemExit",
    "TypeError",
    "ValueError",
    "ZeroDivisionError",
    "abs",
    "all",
    "any",
    "append",
    "args",
    "bool",
    "builtins",
    "bytearray",
    "bytecode",
    "bytes",
    "callable",
    "chr",
    "classmethod",
    "clear",
    "close",
    "const",
    "copy",
    "count",
    "dict",
    "dir",
    "divmod",
    "end",
    "endswith",
    "eval",
    "exec",
    "extend",
    "find",
    "format",
    "from_bytes",
    "get",
    "getattr",
    "globals",
    "hasattr",
    "hash",
    "id",
    "index",
    "insert",
    "int",
    "isalpha",
    "isdigit",
    "isinstance",
    "islower",
    "isspace",
    "issubclass",
    "isupper",
    "items",
    "iter",
    "join",
    "key",
    "keys",
    "len",
    "list",
    "little",
    "locals",
    "lower",
    "lstrip",
    "main",
    "map",
    "micropython",
    "next",
    "object",
    "open",
    "ord",
    "pop",
    "popitem",
    "pow",
    "print",
    "range",
    "read",
    "readinto",
    "readline",
    "remove",
    "replace",
    "repr",
    "reverse",
    "rfind",
    "rindex",
    "round",
    "rsplit",
    "rstrip",
    "self",
    "send",
    "sep",
    "set",
    "setattr",
    "setdefault",
    "sort",
    "sorted",
    "split",
    "start",
    "startswith",
    "staticmethod",
    "step",
    "stop",
    "str",
    "strip",
    "sum",
    "super",
    "throw",
    "to_bytes",
    "tuple",
    "type",
    "update",
    "upper",
    "utf-8",
    "value",
    "values",
    "write",
    "zip",
]

# this must match the equivalent function in qstr.c
def compute_hash(qstr, bytes_hash):
    hash = 5381
    for b in qstr:
        hash = (hash * 33) ^ b
    # Make sure that valid hash is never zero, zero means "hash not computed"
    return (hash & ((1 << (8 * bytes_hash)) - 1)) or 1


def translate(translation_file, i18ns):
    with open(translation_file, "rb") as f:
        table = gettext.GNUTranslations(f)

        translations = []
        for original in i18ns:
            unescaped = original
            for s in C_ESCAPES:
                unescaped = unescaped.replace(C_ESCAPES[s], s)
            translation = table.gettext(unescaped)
            # Add in carriage returns to work in terminals
            translation = translation.replace("\n", "\r\n")
            translations.append((original, translation))
        return translations


def qstr_escape(qst):
    def esc_char(m):
        c = ord(m.group(0))
        try:
            name = codepoint2name[c]
        except KeyError:
            name = "0x%02x" % c
        return "_" + name + "_"

    return re.sub(r"[^A-Za-z0-9_]", esc_char, qst)


def parse_input_headers(infiles):
    qcfgs = {}
    qstrs = {}
    i18ns = set()

    # add static qstrs
    for qstr in static_qstr_list:
        # work out the corresponding qstr name
        ident = qstr_escape(qstr)

        # don't add duplicates
        assert ident not in qstrs

        # add the qstr to the list, with order number to retain original order in file
        order = len(qstrs) - 300000
        qstrs[ident] = (order, ident, qstr)

    # read the qstrs in from the input files
    for infile in infiles:
        with open(infile, "rt") as f:
            for line in f:
                line = line.strip()

                # is this a config line?
                match = re.match(r"^QCFG\((.+), (.+)\)", line)
                if match:
                    value = match.group(2)
                    if value[0] == "(" and value[-1] == ")":
                        # strip parenthesis from config value
                        value = value[1:-1]
                    qcfgs[match.group(1)] = value
                    continue

                match = re.match(r'^TRANSLATE\("(.*)"\)$', line)
                if match:
                    i18ns.add(match.group(1))
                    continue

                # is this a QSTR line?
                match = re.match(r"^Q\((.*)\)$", line)
                if not match:
                    continue

                # get the qstr value
                qstr = match.group(1)

                # special cases to specify control characters
                if qstr == "\\n":
                    qstr = "\n"
                elif qstr == "\\r\\n":
                    qstr = "\r\n"

                # work out the corresponding qstr name
                ident = qstr_escape(qstr)

                # don't add duplicates
                if ident in qstrs:
                    continue

                # add the qstr to the list, with order number to retain original order in file
                order = len(qstrs)
                # but put special method names like __add__ at the top of list, so
                # that their id's fit into a byte
                if ident == "":
                    # Sort empty qstr above all still
                    order = -200000
                elif ident == "__dir__":
                    # Put __dir__ after empty qstr for builtin dir() to work
                    order = -190000
                elif ident.startswith("__"):
                    order -= 100000
                qstrs[ident] = (order, ident, qstr)

    if not qcfgs and qstrs:
        sys.stderr.write("ERROR: Empty preprocessor output - check for errors above\n")
        sys.exit(1)

    return qcfgs, qstrs, i18ns


def escape_bytes(qstr):
    if all(32 <= ord(c) <= 126 and c != "\\" and c != '"' for c in qstr):
        # qstr is all printable ASCII so render it as-is (for easier debugging)
        return qstr
    else:
        # qstr contains non-printable codes so render entire thing as hex pairs
        qbytes = bytes_cons(qstr, "utf8")
        return "".join(("\\x%02x" % b) for b in qbytes)


def make_bytes(cfg_bytes_len, cfg_bytes_hash, qstr):
    qbytes = bytes_cons(qstr, "utf8")
    qlen = len(qbytes)
    qhash = compute_hash(qbytes, cfg_bytes_hash)
    if qlen >= (1 << (8 * cfg_bytes_len)):
        print("qstr is too long:", qstr)
        assert False
    qdata = escape_bytes(qstr)
    return '%d, %d, "%s"' % (qhash, qlen, qdata)


def print_qstr_data(qcfgs, qstrs, i18ns):
    # get config variables
    cfg_bytes_len = int(qcfgs["BYTES_IN_LEN"])
    cfg_bytes_hash = int(qcfgs["BYTES_IN_HASH"])

    # print out the starter of the generated C header file
    print("// This file was automatically generated by makeqstrdata.py")
    print("")

    # add NULL qstr with no hash or data
    print('QDEF(MP_QSTRnull, 0, 0, "")')

    total_qstr_size = 0
    total_qstr_compressed_size = 0
    # go through each qstr and print it out
    for order, ident, qstr in sorted(qstrs.values(), key=lambda x: x[0]):
        qbytes = make_bytes(cfg_bytes_len, cfg_bytes_hash, qstr)
        print("QDEF(MP_QSTR_%s, %s)" % (ident, qbytes))

        total_qstr_size += len(qstr)

    best = sys.maxsize
    best_data = None
    best_sort = None

    # Group messages together according to whether they're translated, and then in lexical order
    def alpha(arg):
        original, translation = arg
        return (
            original == translation,
            translation,
        )

    for reverse in (False, True):
        i18ns.sort(key=alpha, reverse=reverse)
        for cutoff in range(3, 8):
            qstr_by_length = sorted(
                (
                    (i + 1, qstr)
                    for i, (order, ident, qstr) in enumerate(qstrs.values())
                    if len(qstr) > cutoff
                ),
                key=lambda x: -len(x[1]),
            )

            def qstrify(s):
                s0 = s
                for num, qstr in qstr_by_length:
                    s = s.replace(qstr, chr(0xE000 + num))
                return s

            all_translated = [translation.encode("utf-8") for original, translation in i18ns]
            all_translated_qstrd = [
                qstrify(translation).encode("utf-8") for original, translation in i18ns
            ]
            all_translated_joined = b"\0".join(all_translated_qstrd)
            compressor = zlib.compressobj(9, wbits=-10)
            compressor.compress(all_translated_joined)
            all_translated_compressed = compressor.flush()

            data = ", ".join(str(c) for c in all_translated_compressed)
            if len(all_translated_compressed) < best:
                best_desc = f"// Compressed to {len(all_translated_compressed)} bytes using cutoff {cutoff} {'reversed ' if reverse else ''}"
                best = len(all_translated_compressed)
                best_data = data
                best_sort = i18ns[:]

    print(best_desc)
    for i, (original, translation) in enumerate(best_sort):
        print('TRANSLATION("{}", {})'.format(original, i))

    print()

    print(
        "TRANSLATION_DATA({}, {{ {} }}\n)".format(max(len(m) for m in all_translated), best_data)
    )


def print_qstr_enums(qstrs):
    # print out the starter of the generated C header file
    print("// This file was automatically generated by makeqstrdata.py")
    print("")

    # add NULL qstr with no hash or data
    print("QENUM(MP_QSTRnull)")

    # go through each qstr and print it out
    for order, ident, qstr in sorted(qstrs.values(), key=lambda x: x[0]):
        print("QENUM(MP_QSTR_%s)" % (ident,))


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(
        description="Process QSTR definitions into headers for compilation"
    )
    parser.add_argument(
        "infiles", metavar="N", type=str, nargs="+", help="an integer for the accumulator"
    )
    parser.add_argument(
        "--translation", default=None, type=str, help="translations for i18n() items"
    )
    parser.add_argument(
        "--compression_filename", default=None, type=str, help="header for compression info"
    )

    args = parser.parse_args()

    qcfgs, qstrs, i18ns = parse_input_headers(args.infiles)
    if args.translation:
        translations = translate(args.translation, i18ns)
        print_qstr_data(qcfgs, qstrs, translations)
        with open(args.compression_filename, "w") as f:
            f.write("#define NUM_MESSAGES {}\n".format(len(i18ns)))
    else:
        print_qstr_enums(qstrs)
