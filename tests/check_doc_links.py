"""Check that every relative link and #anchor in the Markdown docs resolves.

    python -I tests/check_doc_links.py      (from the repo root; exit 1 on problems)

Anchors follow GitHub's slug rule: lower-case, punctuation dropped, spaces to '-'.
"""
import os
import re
import sys


def slug(heading):
    h = heading.strip().lower()
    h = re.sub(r'[^\w\- ]', '', h)
    return h.replace(' ', '-')


def main():
    files = ['README.md', 'BUILD.md', 'CHANGELOG.md']
    files += [os.path.join('docs', f) for f in sorted(os.listdir('docs')) if f.endswith('.md')]
    anchors = {}
    for f in files:
        with open(f, encoding='utf-8') as fh:
            anchors[os.path.normpath(f)] = {slug(m) for m in re.findall(r'^#+ (.+)$', fh.read(), re.M)}
    problems = 0
    for f in files:
        with open(f, encoding='utf-8') as fh:
            text = fh.read()
        for link in re.findall(r'\]\(([^)\s]+)\)', text):
            if link.startswith(('http://', 'https://', 'mailto:')):
                continue
            path, _, anchor = link.partition('#')
            target = os.path.normpath(os.path.join(os.path.dirname(f), path)) if path else os.path.normpath(f)
            if not os.path.exists(target):
                print('missing file:   %s -> %s' % (f, link))
                problems += 1
            elif anchor and target in anchors and anchor not in anchors[target]:
                print('missing anchor: %s -> %s' % (f, link))
                problems += 1
    print('%d docs, %d link problems' % (len(files), problems))
    sys.exit(1 if problems else 0)


if __name__ == '__main__':
    main()
