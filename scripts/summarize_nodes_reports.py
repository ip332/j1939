#!/usr/bin/env python3
"""Turn can_sandbox -nodes-report JSON files into one Markdown table.

Usage: scripts/summarize_nodes_reports.py REPORT.json... > summary.md
"""
import json
import os
import sys

COLUMNS = ['claims', 'contests', 'reassignments', 'address_changes', 'non_compliant',
           'duplicates', 'pingpongs', 'timeouts']
BRANCHES = ['LowerNameWins', 'HolderDefended', 'HolderSilent', 'ChallengerWithdrew', 'Superseded']


def main(paths):
    print('| capture | frames | NAMEs | ' + ' | '.join(COLUMNS) + ' | ' +
          ' | '.join(BRANCHES) + ' |')
    print('|' + '---|' * (3 + len(COLUMNS) + len(BRANCHES)))
    for path in paths:
        with open(path) as f:
            report = json.load(f)
        stats = report['stats']
        cells = [os.path.basename(path).removesuffix('.json'), str(report['frames']),
                 str(report['names'])]
        cells += [str(stats[c]) for c in COLUMNS]
        cells += [str(stats['resolved'][b]) for b in BRANCHES]
        print('| ' + ' | '.join(cells) + ' |')


if __name__ == '__main__':
    main(sys.argv[1:])
