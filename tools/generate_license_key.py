#!/usr/bin/env python3
"""
Issues an offline "VCS-" license key for VocalChop Studio (VIP / giveaway /
no-internet buyers). Gumroad buyers do NOT need this — their keys come from
Gumroad automatically and verify online.

The key is an RSA signature of SHA256(e-mail), checked in the plugin against
the public key embedded in Source/Licensing.h.

Usage:
  python3 tools/generate_license_key.py buyer@example.com \
      --private-key ~/vcs_private_key.json

The private key JSON ({"n": hex, "d": hex}) must NEVER be committed to the
repository (the repo is public). Keep it somewhere safe; whoever holds it can
mint keys.
"""

import argparse
import hashlib
import json
import os
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("email", help="buyer e-mail the key is bound to")
    ap.add_argument("--private-key", default=os.path.expanduser("~/vcs_private_key.json"),
                    help="path to the private key JSON (default: ~/vcs_private_key.json)")
    args = ap.parse_args()

    try:
        with open(args.private_key) as f:
            priv = json.load(f)
    except FileNotFoundError:
        sys.exit(f"private key not found: {args.private_key}")

    n = int(priv["n"], 16)
    d = int(priv["d"], 16)

    email = args.email.strip().lower()
    h = int(hashlib.sha256(email.encode()).hexdigest(), 16)
    sig = format(pow(h, d, n), "x")

    groups = [sig[i:i + 8] for i in range(0, len(sig), 8)]
    print(f"licensed to : {email}")
    print("license key :")
    print("VCS-" + "-".join(groups))


if __name__ == "__main__":
    main()
