# As in the application: no sys.stderr while the script runs -- the result cache must keep and read back without a sound
import sys, os
sys.stderr = None
sys.__stderr__ = None
os.environ['RUN'] = os.environ.get('RUN', '1')
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 't_rcache.py'), encoding='utf-8').read())
