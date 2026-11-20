"""Checks that every data member of st::Machine (src/machine.h, the names ending in _) is either in the savestate's
field list (src/machine_state.inc, StateFields) or named in its "Left out on purpose" comment. Run after adding a
member to the machine: a member in neither is a state the savestates would lose.
usage: python -P Tools/state_fields.py"""
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(__file__), '..')


def main():
    header = open(os.path.join(ROOT, 'src', 'machine.h'), encoding='utf-8').read()
    inc = open(os.path.join(ROOT, 'src', 'machine_state.inc'), encoding='utf-8').read()
    # Only the class body's own members (not a nested struct's fields: those have no trailing _).
    body = header[header.index('class Machine {'):]
    members = set()
    for line in body.splitlines():
        code = line.split('//')[0]
        if '(' in code and '=' not in code.split('(')[0]:
            continue  # a function
        for m in re.finditer(r'\b([a-z][a-z0-9_]*_)\s*(?:=|;|\{|\[|,)', code):
            members.add(m.group(1))
    fields_at = inc.index('void Machine::StateFields(Io& io) {')
    fields = set(re.findall(r'F\(io, ([a-z0-9_]+_)\)', inc[fields_at:]))
    left_out_at = inc.index('Left out on purpose')
    left_out = set(re.findall(r'\b([a-z][a-z0-9_]*_)\b', inc[left_out_at:fields_at]))
    missing = sorted(members - fields - left_out)
    stale = sorted(fields - members)
    for m in missing:
        print('not in the savestate:', m)
    for m in stale:
        print('in the field list but not a member:', m)
    print('%d members, %d in the state, %d left out' % (len(members), len(members & fields), len(members & left_out)))
    sys.exit(1 if missing or stale else 0)


if __name__ == '__main__':
    main()
