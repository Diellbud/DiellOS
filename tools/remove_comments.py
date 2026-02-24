import re
import sys
from pathlib import Path
import tokenize
import io

SRC = Path('src')
EXTS = ['.c', '.h', '.s', '.S', '.py']

def strip_c_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    out_lines = []
    for line in text.splitlines():
        if line.lstrip().startswith('#'):
            out_lines.append(line)
        else:
            line = re.sub(r'//.*', '', line)
            out_lines.append(line)
    return '\n'.join(out_lines) + ("\n" if text.endswith('\n') else '')

def strip_asm_comments(text):
    out_lines = []
    for line in text.splitlines():
        # remove ; comments
        if ';' in line:
            line = line.split(';', 1)[0]
        # remove # comments if not a shebang
        stripped = line.lstrip()
        if stripped.startswith('#') and not stripped.startswith('#!'):
            line = ''
        else:
            if '#' in line:
                line = line.split('#', 1)[0]
        out_lines.append(line)
    return '\n'.join(out_lines) + ("\n" if text.endswith('\n') else '')

def strip_py_comments(text):
    try:
        tokens = tokenize.generate_tokens(io.StringIO(text).readline)
    except Exception:
        return text
    out = []
    for toknum, tokval, start, end, line in tokens:
        if toknum == tokenize.COMMENT:
            continue
        out.append((toknum, tokval))
    try:
        result = tokenize.untokenize(out)
        return result if isinstance(result, str) else result.decode('utf-8')
    except Exception:
        return text

def process_file(p: Path):
    try:
        text = p.read_text(encoding='utf-8')
    except Exception:
        return False, 'read error'
    ext = p.suffix
    if ext in ['.c', '.h']:
        new = strip_c_comments(text)
    elif ext in ['.s', '.S']:
        new = strip_asm_comments(text)
    elif ext == '.py':
        new = strip_py_comments(text)
    else:
        return False, 'unsupported'
    if new != text:
        p.write_text(new, encoding='utf-8')
        return True, 'changed'
    return True, 'nochange'

def main():
    if not SRC.exists():
        print('src/ not found')
        return 2
    changed = 0
    total = 0
    for p in SRC.rglob('*'):
        if p.suffix in EXTS and p.is_file():
            total += 1
            ok, msg = process_file(p)
            print(f'{p}: {msg}')
            if ok and msg == 'changed':
                changed += 1
    print(f'Total files: {total}, Changed: {changed}')
    return 0

if __name__ == '__main__':
    sys.exit(main())
