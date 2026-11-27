"""Validate and materialize the restricted CI title input without logging it."""
import base64
import hashlib
import os
from pathlib import Path
import sys
import tomllib

try:
    payload = base64.b64decode(os.environ["BULLETPROOF_TITLE_TOML_B64"], validate=True)
    digest = os.environ["BULLETPROOF_TITLE_TOML_SHA256"].lower()
    if not digest or hashlib.sha256(payload).hexdigest() != digest:
        raise ValueError("SHA-256 mismatch")
    data = tomllib.loads(payload.decode("utf-8"))
    if (data["xbe"]["title"] != 0x56550042 or data["xbe"]["xdk"] != 5849 or data["xbe"]["partitions"] != 64
            or Path(data["xbe"]["path"]).name != "default.xbe"
            or not data["symbols"] or not data["data"]):
        raise ValueError("unexpected title metadata")
    if "includes" in data:
        raise ValueError("metadata must be self-contained")
except (KeyError, ValueError):
    sys.exit("Missing or invalid title TOML input or SHA-256")

path = Path(sys.argv[1])
path.parent.mkdir(parents=True, exist_ok=True)
flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_BINARY", 0)
with os.fdopen(os.open(path, flags, 0o600), "wb") as output:
    output.write(payload)
