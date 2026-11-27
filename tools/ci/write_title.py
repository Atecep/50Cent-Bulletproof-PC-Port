"""Validate and materialize the restricted CI title input without logging it."""
import base64
import binascii
import hashlib
import os
from pathlib import Path
import re
import sys
import tomllib

encoded = os.environ.get("BULLETPROOF_TITLE_TOML_B64", "").strip()
digest = os.environ.get("BULLETPROOF_TITLE_TOML_SHA256", "").strip().lower()

if not encoded:
    sys.exit("Missing BULLETPROOF_TITLE_TOML_B64 environment secret.")
if not digest:
    sys.exit("Missing BULLETPROOF_TITLE_TOML_SHA256 environment variable.")

try:
    payload = base64.b64decode(encoded, validate=True)
except (binascii.Error, ValueError):
    sys.exit("Invalid Base64 in BULLETPROOF_TITLE_TOML_B64.")

if not re.fullmatch(r"[0-9a-f]{64}", digest):
    sys.exit("Invalid SHA-256 format in BULLETPROOF_TITLE_TOML_SHA256. Expected 64 hexadecimal digits.")
if hashlib.sha256(payload).hexdigest() != digest:
    sys.exit("SHA-256 mismatch for the decoded title TOML.")

try:
    data = tomllib.loads(payload.decode("utf-8"))
except (UnicodeDecodeError, tomllib.TOMLDecodeError):
    sys.exit("Invalid title metadata. Expected UTF-8 TOML.")

xbe = data.get("xbe")
if (
    not isinstance(xbe, dict)
    or any(type(xbe.get(key)) is not int or xbe[key] != expected
           for key, expected in (("title", 0x56550042), ("xdk", 5849), ("partitions", 64)))
    or not isinstance(xbe.get("path"), str)
    or "\x00" in xbe["path"]
    or Path(xbe["path"]).name != "default.xbe"
    or any(not isinstance(data.get(key), dict) or not data[key] for key in ("symbols", "data"))
    or "includes" in data
):
    sys.exit("Invalid title metadata. Expected a self-contained Bulletproof configuration.")

path = Path(sys.argv[1])
path.parent.mkdir(parents=True, exist_ok=True)
flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_BINARY", 0)
with os.fdopen(os.open(path, flags, 0o600), "wb") as output:
    output.write(payload)
