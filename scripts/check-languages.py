#!/usr/bin/env python3
"""Validate optional UTF-8 packs against each other and compiled UI messages."""
import ast
from collections import Counter
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
LITERAL = r'"(?:\\.|[^"\\])*"'
GROUP = LITERAL + r'(?:\s*' + LITERAL + r')*'

def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate key: ' + key)
        result[key] = value
    return result

def decode(group):
    return ''.join(ast.literal_eval(token) for token in re.findall(LITERAL, group))

def main():
    files = sorted((ROOT / 'languages').glob('*.json'))
    if not files:
        raise ValueError('no bundled language packs')
    expected = None
    catalogs = {}
    for path in files:
        source = path.read_text(encoding='utf-8')
        if path.stat().st_size > 1024 * 1024:
            raise ValueError(f'{path.name}: exceeds runtime size limit')
        data = json.loads(source, object_pairs_hook=unique)
        assert data['@language'] == path.stem and path.stem != 'en-us', path.name
        assert data['@direction'] in ('ltr', 'rtl') and 0 < len(data['@name']) <= 160, path.name
        strings = {k: v for k, v in data.items() if not k.startswith('@')}
        if expected is None:
            expected = set(strings)
        assert set(strings) == expected, f'{path.name}: missing {expected - set(strings)}; extra {set(strings) - expected}'
        for key, value in strings.items():
            assert isinstance(value, str) and value and '\0' not in value, (path.name, key)
            assert Counter(re.findall(r'\{[A-Za-z_][\w]*\}', key)) == Counter(re.findall(r'\{[A-Za-z_][\w]*\}', value)), (path.name, key, 'placeholders')
            assert key.count('\n') == value.count('\n'), (path.name, key, 'newlines')
            assert re.match(r'^\s*', key)[0] == re.match(r'^\s*', value)[0], (path.name, key, 'leading whitespace')
            assert re.search(r'\s*$', key)[0] == re.search(r'\s*$', value)[0], (path.name, key, 'trailing whitespace')
        catalogs[path.stem] = strings
    required = set()
    for path in (ROOT / 'src/forms').glob('*.cpp'):
        source = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/', '', path.read_text(encoding='utf-8'))
        required.update(decode(m[1]) for m in re.finditer(r'\btr\(\s*(' + GROUP + r')\s*\)', source))
    help_source = (ROOT / 'src/help_content.hpp').read_text(encoding='utf-8').split('help_welcome =', 1)[1]
    required.update(decode(m[0]) for m in re.finditer(GROUP, help_source))
    # Product identifiers and punctuation are deliberately language independent.
    neutral = {'Plan Paint', 'Astra', 'Rainstar', 'RGB', 'RGBA', 'OKLab', 'OKHSL', 'SVG', 'PNG', 'JPEG', 'BMP', 'TIFF', 'TGA', 'ICO', 'CUR'}
    missing = {key for key in required - expected - neutral if re.search(r'[A-Za-z]{2}', key)}
    if missing:
        raise ValueError('Uncatalogued compiled messages: ' + repr(sorted(missing)))
    print(f'{len(files)} language packs: {len(expected)} entries each; UTF-8, metadata, coverage, placeholders and whitespace pass.')

if __name__ == '__main__':
    main()
