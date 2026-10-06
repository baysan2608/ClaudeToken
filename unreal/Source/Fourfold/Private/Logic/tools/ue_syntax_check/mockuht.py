#!/usr/bin/env python3
"""Mock UHT: writes <Name>.generated.h stand-ins so UE headers + our module parse with clang -fsyntax-only.
For every header that includes "X.generated.h": defines CURRENT_FILE_ID and, per line, the _PROLOG / _GENERATED_BODY /
_GENERATED_BODY_LEGACY / _DELEGATE macros UHT would emit (class bodies via DECLARE_CLASS, struct bodies with
StaticStruct, interfaces, dynamic delegate wrapper declarations). Usage: mockuht.py OUT_DIR ROOT [ROOT...]"""
import os, re, sys

out_dir = sys.argv[1]
roots = sys.argv[2:]
os.makedirs(out_dir, exist_ok=True)

GEN_INC = re.compile(r'#\s*include\s+"([^"]+)\.generated\.h"')
DECL = re.compile(r'^\s*(class|struct)\s+(?:alignas\([^)]*\)\s+)?(?:[A-Z0-9_]+_API\s+)?(?:UE_DEPRECATED\([^)]*\)\s+)?(\w+)\s*(?:final\s*)?(?::\s*(?:public|protected|private)?\s*([\w:<>, ]+?))?\s*(?:\{|$)')

def sanitize(s):
    return re.sub(r'[^A-Za-z0-9_]', '_', s)

def first_base(b):
    if not b:
        return None
    # first base, strip templates commas: take up to first top-level comma
    depth = 0
    for i, ch in enumerate(b):
        if ch == '<': depth += 1
        elif ch == '>': depth -= 1
        elif ch == ',' and depth == 0:
            return b[:i].strip()
    return b.strip()

def macro_args(text, start):
    # text[start] == '(' ; returns list of top-level args and end index
    depth = 0; args = []; cur = ''
    i = start
    while i < len(text):
        ch = text[i]
        if ch == '(':
            depth += 1
            if depth > 1: cur += ch
        elif ch == ')':
            depth -= 1
            if depth == 0:
                args.append(cur.strip()); return args, i
            cur += ch
        elif ch == ',' and depth == 1:
            args.append(cur.strip()); cur = ''
        else:
            cur += ch
        i += 1
    return args, i

def gen_for(path, name):
    try:
        src = open(path, encoding='utf-8', errors='replace').read()
    except OSError:
        return None
    lines = src.split('\n')
    fid = 'FID_' + sanitize(name)
    out = ['#pragma once' if False else '', f'#undef CURRENT_FILE_ID', f'#define CURRENT_FILE_ID {fid}']
    pending = None  # 'class' | 'struct' | 'interface'
    stack = []  # (kind, name, base)
    last_decl = None
    for ln, line in enumerate(lines, start=1):
        s = line.strip()
        if s.startswith('//'):
            continue
        if re.match(r'^\s*UCLASS\s*\(', line):
            out.append(f'#define {fid}_{ln}_PROLOG')
            pending = 'class'
        elif re.match(r'^\s*UINTERFACE\s*\(', line):
            out.append(f'#define {fid}_{ln}_PROLOG')
            pending = 'class'
        elif re.match(r'^\s*USTRUCT\s*\(', line):
            pending = 'struct'
        if re.match(r'^\s*(class|struct)\s+\w', line) and not s.endswith(';'):
            joined = line
            k = ln
            while '{' not in joined and k < len(lines) and k < ln + 6:
                joined += ' ' + lines[k].strip()
                k += 1
            joined = joined.split('{')[0] + '{'
            m = DECL.match(joined)
            if m:
                last_decl = (m.group(1), m.group(2), first_base(m.group(3)), pending)
                pending = None
        gm = re.search(r'\b(GENERATED_BODY|GENERATED_USTRUCT_BODY|GENERATED_UCLASS_BODY|GENERATED_UINTERFACE_BODY|GENERATED_IINTERFACE_BODY|GENERATED_BODY_LEGACY)\s*\(', line)
        if gm and not s.startswith('#'):
            kind = gm.group(1)
            if last_decl is None:
                continue
            ck, cname, cbase, ann = last_decl
            is_struct = (ann == 'struct') or (ann is None and ck == 'struct')
            if kind == 'GENERATED_IINTERFACE_BODY':
                uname = 'U' + cname[1:]
                body = (f'protected: virtual ~{cname}() {{}} public: typedef {uname} UClassType; typedef {cname} ThisClass; '
                        f'virtual UObject* _getUObject() const {{ return nullptr; }} public:')
                out.append(f'#define {fid}_{ln}_GENERATED_BODY_LEGACY {body}')
                out.append(f'#define {fid}_{ln}_GENERATED_BODY {body}')
                continue
            if is_struct:
                body = f'static class UScriptStruct* StaticStruct();'
                if cbase:
                    body += f' typedef {cbase} Super;'
                out.append(f'#define {fid}_{ln}_GENERATED_BODY {body}')
                out.append(f'#define {fid}_{ln}_GENERATED_BODY_LEGACY {body}')
                continue
            base = cbase or 'UObject'
            decl = (f'public: DECLARE_CLASS({cname}, {base}, COMPILED_IN_FLAGS(0), CASTCLASS_None, TEXT("/Script/Mock"), NO_API) '
                    f'DECLARE_SERIALIZER({cname})')
            has_ctor = re.search(r'\b' + re.escape(cname) + r'\s*\(\s*const\s+(class\s+)?FObjectInitializer', src) is not None
            legacy_ctor = '' if has_ctor else f' public: {cname}(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());'
            out.append(f'#define {fid}_{ln}_GENERATED_BODY {decl} private:')
            out.append(f'#define {fid}_{ln}_GENERATED_BODY_LEGACY {decl}{legacy_ctor} public:')
        dm = re.search(r'\bDECLARE_DYNAMIC_(MULTICAST_)?(SPARSE_)?DELEGATE(_RetVal)?\w*\s*\(', line)
        if dm and not s.startswith('#'):
            # collect full macro text from this line onward
            text = '\n'.join(lines[ln - 1: ln + 12])
            idx = text.index('(', text.index(dm.group(0).rstrip('(').strip()))
            args, _ = macro_args(text, idx)
            if dm.group(2):  # sparse: SparseDelegateClass, OwningClass, DelegateName, ... (wrapper named after the class)
                dname = args[0] if args else 'Unknown'
                ret = 'void'
            elif dm.group(3):
                ret = args[0] if args else 'void'
                dname = args[1] if len(args) > 1 else 'Unknown'
            else:
                ret = 'void'
                dname = args[0] if args else 'Unknown'
            out.append(f'#define {fid}_{ln}_DELEGATE template<typename... TFFArgs> static {ret} {dname}_DelegateWrapper(TFFArgs&&...);')
    return '\n'.join(out) + '\n'

# Index of struct / class definitions (unindented, so roughly namespace scope) -> kind.
KIND = {}
DEF_RE = re.compile(r'^(class|struct)\s+(?:alignas\([^)]*\)\s+)?(?:[A-Z0-9_]+_API\s+)?(?:UE_DEPRECATED\([^)]*\)\s+)?([FUAI][A-Z]\w*)\b(?!\s*;)')
for root in roots:
    for dp, dn, fn in os.walk(root):
        for f in fn:
            if f.endswith(('.h', '.inl')):
                try:
                    for line in open(os.path.join(dp, f), encoding='utf-8', errors='replace'):
                        m = DEF_RE.match(line)
                        if m and not line.rstrip().endswith(';'):
                            KIND.setdefault(m.group(2), m.group(1))
                except OSError:
                    pass
ALIAS_RE = re.compile(r'(?:\busing\s+([FUA][A-Z]\w*)\s*=)|(?:\btypedef\b[^;]*?\b([FUA][A-Z]\w*)\s*;)')
for root in roots:
    for dp, dn, fn in os.walk(root):
        for f in fn:
            if f.endswith(('.h', '.inl')):
                try:
                    txt_ = open(os.path.join(dp, f), encoding='utf-8', errors='replace').read()
                except OSError:
                    continue
                for m in ALIAS_RE.finditer(txt_):
                    KIND.pop(m.group(1) or m.group(2), None)
TYPE_TOK = re.compile(r'\b([FUA][A-Z]\w*)\b')

def forward_decls(src):
    names = set()
    lines = src.split('\n')
    for i, line in enumerate(lines):
        if re.match(r'^\s*UFUNCTION\s*\(', line):
            sig = ' '.join(lines[i + 1:i + 4])
            sig = sig.split(';')[0].split('{')[0]
            for t in TYPE_TOK.findall(sig):
                if t in KIND:
                    names.add(t)
    return ''.join(f'{KIND[n]} {n};\n' for n in sorted(names))

count = 0
for root in roots:
    for dp, dn, fn in os.walk(root):
        for f in fn:
            if not f.endswith(('.h', '.inl')):
                continue
            p = os.path.join(dp, f)
            try:
                txt = open(p, encoding='utf-8', errors='replace').read()
            except OSError:
                continue
            for m in GEN_INC.finditer(txt):
                gname = os.path.basename(m.group(1))
                g = gen_for(p, gname)
                if g is not None:
                    g = forward_decls(txt) + g
                    with open(os.path.join(out_dir, gname + '.generated.h'), 'w') as fo:
                        fo.write(g)
                    count += 1
print('generated', count)
