#!/usr/bin/env python3
"""Export the actual upstream collision layer for the browser renderer."""
import json
import subprocess
import tempfile
from pathlib import Path
root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='teeworlds-map-') as directory:
    result = subprocess.run([
        str(root/'build/teeworlds_bridge'), 'live', str(Path(directory)/'map-tape.bin'),
        str(root/'datasrc/maps/dm1.map'), str(root/'myelin/game.cfg'),
    ], input='9 1 1 4 ', text=True, capture_output=True, timeout=10, check=True)
    line = result.stdout.splitlines()[0]
    data = json.loads(line)
    assert data['type'] == 'map'
    assert len(data['tiles']) == data['width'] * data['height']
    (root/'web/assets/map.json').write_text(line+'\n')
