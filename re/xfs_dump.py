"""Print any XFS file (e.g. rScenario .scn) as an indented tree, using fsm_dump.Xfs.

usage: python xfs_dump.py <file>
"""
import sys
sys.argv, files = sys.argv[:1], sys.argv[1:]
exec(open(__file__.replace('xfs_dump.py', 'fsm_dump.py')).read().split("if __name__")[0])


def walk(o, ind=0):
    pad = '  ' * ind
    if isinstance(o, dict):
        simple = {k: v for k, v in o.items() if not k.startswith('_') and not isinstance(v, (dict, list))}
        print(pad + ' '.join(f'{k}={v}' for k, v in simple.items()))
        for k, v in o.items():
            if isinstance(v, (dict, list)) and v:
                print(pad + f' {k}:')
                walk(v, ind + 2)
    elif isinstance(o, list):
        for v in o:
            walk(v, ind)
    elif o is not None:
        print(pad + str(o))


for f in files:
    x = Xfs(open(f, 'rb').read())
    print('###', f)
    walk(x.obj())
