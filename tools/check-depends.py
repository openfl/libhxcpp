#!/usr/bin/env python3
# Checks that toolchain/haxe-target.xml's depend lists name every header the
# runtime and generated code include, so editing one rebuilds what uses it.
# hxcpp-depends must hold everything hxcpp.h includes, directly or not, and the
# runtime group, with it, everything the runtime's sources include from this
# repository. A header missing there was never compared, so a change to it
# rebuilt nothing, and the compile cache reused objects built before it
# (libhxcpp #469).
#   python3 tools/check-depends.py   (from the repository root)
import os
import re
import sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
include_dir = os.path.join(root, 'include')
target = open(os.path.join(root, 'toolchain', 'haxe-target.xml'), encoding='utf-8').read()
include_re = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)


def group(name):
    m = re.search(r'<files id="%s"[^>]*>(.*?)</files>' % re.escape(name), target, re.S)
    if not m:
        sys.exit('no files group %s in haxe-target.xml' % name)
    return m.group(1)


def depends(body):
    return {os.path.normpath(os.path.join(root, d.replace('${HXCPP}/', '')))
            for d in re.findall(r'<depend name="(\$\{HXCPP\}/[^"]+)"', body)}


def exists_exact(path):
    # As on a case-sensitive file system: on Windows <memory.h> found hx/Memory.h
    directory, name = os.path.split(path)
    return os.path.isfile(path) and name in os.listdir(directory)


def resolve(name, from_dir):
    for base in (from_dir, include_dir):
        path = os.path.normpath(os.path.join(base, name))
        if path.startswith(root + os.sep) and exists_exact(path):
            return path
    return None


def closure(starts):
    seen = set()
    todo = list(starts)
    while todo:
        path = todo.pop()
        if path in seen:
            continue
        seen.add(path)
        text = open(path, encoding='utf-8', errors='replace').read()
        for name in include_re.findall(text):
            found = resolve(name, os.path.dirname(path))
            # Third-party sources keep their own headers, outside these lists
            if found and 'thirdparty' not in found:
                todo.append(found)
    return seen


rel = lambda p: os.path.relpath(p, root).replace(os.sep, '/')
failed = False

common = depends(group('hxcpp-depends'))
need = closure([os.path.join(include_dir, 'hxcpp.h')])
missing = sorted(rel(p) for p in need - common)
if missing:
    failed = True
    print('hxcpp-depends misses headers hxcpp.h includes:')
    for m in missing:
        print('  ' + m)

runtime = group('runtime')
sources = [os.path.join(root, f) for f in re.findall(r'<file name\s*=\s*"([^"]+)"', runtime)]
sources = [s for s in sources if os.path.isfile(s)]
headers = {p for p in closure(sources) if not p.endswith(('.cpp', '.c', '.cc', '.mm', '.m'))}
missing = sorted(rel(p) for p in headers - common - depends(runtime) if not rel(p).startswith('project/'))
if missing:
    failed = True
    print('the runtime group misses headers its sources include:')
    for m in missing:
        print('  ' + m)

if failed:
    sys.exit(1)
print('depend lists cover %d headers from hxcpp.h and %d from %d runtime sources' % (len(need), len(headers), len(sources)))
